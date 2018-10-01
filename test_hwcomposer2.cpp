/*
 * Copyright (c) 2012 Carsten Munk <carsten.munk@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <malloc.h>
#include <thread>
#include <memory>
#include <mutex>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <ui/GraphicBuffer.h>
#include <ui/Fence.h>
#include <ui/FloatRect.h>
#include <ui/Region.h>

#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <sync/sync.h>

#include "hybris-gralloc.h"
#include "HWC2.h"

#include <hwcomposer_window.h>

const char vertex_src [] =
"                                        \
   attribute vec4        position;       \
   varying mediump vec2  pos;            \
   uniform vec4          offset;         \
                                         \
   void main()                           \
   {                                     \
      gl_Position = position + offset;   \
      pos = position.xy;                 \
   }                                     \
";


const char fragment_src [] =
"                                                      \
   varying mediump vec2    pos;                        \
   uniform mediump float   phase;                      \
                                                       \
   void  main()                                        \
   {                                                   \
      gl_FragColor  =  vec4( 1., 0.9, 0.7, 1.0 ) *     \
        cos( 30.*sqrt(pos.x*pos.x + 1.5*pos.y*pos.y)   \
             + atan(pos.y,pos.x) - phase );            \
   }                                                   \
";

GLuint load_shader(const char *shader_source, GLenum type)
{
	GLuint  shader = glCreateShader(type);

	glShaderSource(shader, 1, &shader_source, NULL);
	glCompileShader(shader);

	return shader;
}


GLfloat norm_x    =  0.0;
GLfloat norm_y    =  0.0;
GLfloat offset_x  =  0.0;
GLfloat offset_y  =  0.0;
GLfloat p1_pos_x  =  0.0;
GLfloat p1_pos_y  =  0.0;

GLint phase_loc;
GLint offset_loc;
GLint position_loc;

const float vertexArray[] = {
	0.0,  1.0,  0.0,
	-1.,  0.0,  0.0,
	0.0, -1.0,  0.0,
	1.,  0.0,  0.0,
	0.0,  1.,  0.0
};

std::mutex hotplugMutex;
std::condition_variable hotplugCv;

class HWComposer : public HWComposerNativeWindow
{
	private:
		HWC2::Layer *layer;
		HWC2::Display *hwcDisplay;
	protected:
		void present(HWComposerNativeWindowBuffer *buffer);

	public:

		HWComposer(unsigned int width, unsigned int height, unsigned int format, HWC2::Display *display, HWC2::Layer *layer);
		void set();	
};

HWComposer::HWComposer(unsigned int width, unsigned int height, unsigned int format, HWC2::Display *display, HWC2::Layer *layer) : HWComposerNativeWindow(width, height, format)
{
	this->layer = layer;
	this->hwcDisplay = display;
}

void HWComposer::present(HWComposerNativeWindowBuffer *buffer)
{
	uint32_t numTypes = 0;
	uint32_t numRequests = 0;
	HWC2::Error error = HWC2::Error::None;
	error = hwcDisplay->validate(&numTypes, &numRequests);

	if (error != HWC2::Error::None && error != HWC2::Error::HasChanges) {
		ALOGE("prepare: validate failed for display %lu: %s (%d)", hwcDisplay->getId(),
				to_string(error).c_str(), static_cast<int32_t>(error));
		return;
	}

	if (numTypes || numRequests) {
		ALOGE("prepare: validate required changes for display %lu: %s (%d)", hwcDisplay->getId(),
				to_string(error).c_str(), static_cast<int32_t>(error));
		return;
	}

	error = hwcDisplay->acceptChanges();
	if (error != HWC2::Error::None) {
		ALOGE("prepare: acceptChanges failed: %s", to_string(error).c_str());
		return;
	}

	android::sp<android::GraphicBuffer> target(
		new android::GraphicBuffer(buffer->handle, android::GraphicBuffer::WRAP_HANDLE,
			buffer->width, buffer->height,
			buffer->format, /* layerCount */ 1,
			buffer->usage, buffer->stride));

	android::sp<android::Fence> acquireFenceFd(
			new android::Fence(getFenceBufferFd(buffer)));
	hwcDisplay->setClientTarget(0, target, acquireFenceFd, HAL_DATASPACE_UNKNOWN);

	android::sp<android::Fence> lastPresentFence;
	error = hwcDisplay->present(&lastPresentFence);
	if (error != HWC2::Error::None) {
		ALOGE("presentAndGetReleaseFences: failed for display %lu: %s (%d)",
			hwcDisplay->getId(), to_string(error).c_str(), static_cast<int32_t>(error));
		return;
	}

	std::unordered_map<HWC2::Layer*, android::sp<android::Fence>> releaseFences;
	error = hwcDisplay->getReleaseFences(&releaseFences);
	if (error != HWC2::Error::None) {
		ALOGE("presentAndGetReleaseFences: Failed to get release fences "
			"for display %lu: %s (%d)",
				hwcDisplay->getId(), to_string(error).c_str(),
				static_cast<int32_t>(error));
		return;
	}

	setFenceBufferFd(buffer, releaseFences[layer]->dup());

	if (lastPresentFence.get())
		lastPresentFence->wait(android::Fence::TIMEOUT_NEVER);
} 

inline static uint32_t interpreted_version(hw_device_t *hwc_device)
{
	uint32_t version = hwc_device->version;

	if ((version & 0xffff0000) == 0) {
		// Assume header version is always 1
		uint32_t header_version = 1;

		// Legacy version encoding
		version = (version << 16) | header_version;
	}
	return version;
}

class HWComposerCallback : public HWC2::ComposerCallback
{
	private:
		HWC2::Device* hwcDevice;
	public:
		HWComposerCallback(HWC2::Device* device);

		void onVsyncReceived(int32_t sequenceId, hwc2_display_t display,
							int64_t timestamp) override;
		void onHotplugReceived(int32_t sequenceId, hwc2_display_t display,
							HWC2::Connection connection,
							bool primaryDisplay) override;
		void onRefreshReceived(int32_t sequenceId, hwc2_display_t display) override;
};

HWComposerCallback::HWComposerCallback(HWC2::Device* device)
{
	hwcDevice = device;
}

void HWComposerCallback::onVsyncReceived(int32_t sequenceId, hwc2_display_t display,
							int64_t timestamp)
{
}

void HWComposerCallback::onHotplugReceived(int32_t sequenceId, hwc2_display_t display,
							HWC2::Connection connection,
							bool primaryDisplay)
{
	ALOGI("onHotplugReceived(%d, %" PRIu64 ", %s, %s)",
		sequenceId, display,
		connection == HWC2::Connection::Connected ?
				"connected" : "disconnected",
		primaryDisplay ? "primary" : "external");

	{
		std::lock_guard<std::mutex> lock(hotplugMutex);
		hwcDevice->onHotplug(display, connection);
	}

	hotplugCv.notify_all();
}

void HWComposerCallback::onRefreshReceived(int32_t sequenceId, hwc2_display_t display)
{
}

std::shared_ptr<const HWC2::Display::Config> getActiveConfig(HWC2::Display* hwcDisplay, int32_t displayId) {
	std::shared_ptr<const HWC2::Display::Config> config;
	auto error = hwcDisplay->getActiveConfig(&config);
	if (error == HWC2::Error::BadConfig) {
		fprintf(stderr, "getActiveConfig: No config active, returning null");
		return nullptr;
	} else if (error != HWC2::Error::None) {
		fprintf(stderr, "getActiveConfig failed for display %d: %s (%d)", displayId,
				to_string(error).c_str(), static_cast<int32_t>(error));
		return nullptr;
	} else if (!config) {
		fprintf(stderr, "getActiveConfig returned an unknown config for display %d",
				displayId);
		return nullptr;
	}

	return config;
}

int main()
{
	EGLDisplay display;
	EGLConfig ecfg;
	EGLint num_config;
	EGLint attr[] = {       // some attributes to set up our egl-interface
		EGL_BUFFER_SIZE, 32,
		EGL_RENDERABLE_TYPE,
		EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLSurface surface;
	EGLint ctxattr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLContext context;

	EGLBoolean rv;

	int err;
	int composerSequenceId = 0;

	auto hwcDevice = new HWC2::Device(false);
	assert(hwcDevice);
	hwcDevice->registerCallback(new HWComposerCallback(hwcDevice), composerSequenceId);

	std::unique_lock<std::mutex> lock(hotplugMutex);
	HWC2::Display *hwcDisplay;
	while (!(hwcDisplay = hwcDevice->getDisplayById(0))) {
		/* Wait at most 5s for hotplug events */
		hotplugCv.wait_for(lock, std::chrono::seconds(5));
	}
	hotplugMutex.unlock();
	assert(hwcDisplay);

	hwcDisplay->setPowerMode(HWC2::PowerMode::On);

	std::shared_ptr<const HWC2::Display::Config> config;
	config = getActiveConfig(hwcDisplay, 0);

 	printf("width: %i height: %i\n", config->getWidth(), config->getHeight());

	HWC2::Layer* layer;
	hwcDisplay->createLayer(&layer);

	android::Rect r = {0, 0, config->getWidth(), config->getHeight()};
	layer->setCompositionType(HWC2::Composition::Client);
	layer->setBlendMode(HWC2::BlendMode::None);
	layer->setSourceCrop(android::FloatRect(0.0f, 0.0f, config->getWidth(), config->getHeight()));
	layer->setDisplayFrame(r);
	layer->setVisibleRegion(android::Region(r));

 	HWComposer *win = new HWComposer(config->getWidth(), config->getHeight(), HAL_PIXEL_FORMAT_RGBA_8888, hwcDisplay, layer);
 	printf("created native window\n");
 	hybris_gralloc_initialize(0);

	display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	assert(eglGetError() == EGL_SUCCESS);
	assert(display != EGL_NO_DISPLAY);

	rv = eglInitialize(display, 0, 0);
	assert(eglGetError() == EGL_SUCCESS);
	assert(rv == EGL_TRUE);

	eglChooseConfig((EGLDisplay) display, attr, &ecfg, 1, &num_config);
	assert(eglGetError() == EGL_SUCCESS);
	assert(rv == EGL_TRUE);

	surface = eglCreateWindowSurface((EGLDisplay) display, ecfg, (EGLNativeWindowType) static_cast<ANativeWindow *> (win), NULL);
	assert(eglGetError() == EGL_SUCCESS);
	assert(surface != EGL_NO_SURFACE);

	context = eglCreateContext((EGLDisplay) display, ecfg, EGL_NO_CONTEXT, ctxattr);
	assert(eglGetError() == EGL_SUCCESS);
	assert(context != EGL_NO_CONTEXT);

	assert(eglMakeCurrent((EGLDisplay) display, surface, surface, context) == EGL_TRUE);
	printf("selected current context\n");

	const char *version = (const char *)glGetString(GL_VERSION);
	assert(version);
	printf("%s\n",version);

	GLuint vertexShader   = load_shader ( vertex_src , GL_VERTEX_SHADER  );     // load vertex shader
	GLuint fragmentShader = load_shader ( fragment_src , GL_FRAGMENT_SHADER );  // load fragment shader

	GLuint shaderProgram  = glCreateProgram ();                 // create program object
	glAttachShader ( shaderProgram, vertexShader );             // and attach both...
	glAttachShader ( shaderProgram, fragmentShader );           // ... shaders to it

	glLinkProgram ( shaderProgram );    // link the program
	glUseProgram  ( shaderProgram );    // and select it for usage

	//// now get the locations (kind of handle) of the shaders variables
	position_loc  = glGetAttribLocation  ( shaderProgram , "position" );
	phase_loc     = glGetUniformLocation ( shaderProgram , "phase"    );
	offset_loc    = glGetUniformLocation ( shaderProgram , "offset"   );
	if ( position_loc < 0  ||  phase_loc < 0  ||  offset_loc < 0 ) {
		return 1;
	}

	//glViewport ( 0 , 0 , 800, 600); // commented out so it uses the initial window dimensions
	glClearColor ( 1. , 1. , 1. , 1.);    // background color
	float phase = 0;
	int i, oldretire = -1, oldrelease = -1, oldrelease2 = -1;
	for (i=0; i<60*60; ++i) {
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f ( phase_loc , phase );  // write the value of phase to the shaders phase
		phase  =  fmodf ( phase + 0.5f , 2.f * 3.141f );    // and update the local variable

		glUniform4f ( offset_loc  ,  offset_x , offset_y , 0.0 , 0.0 );

		glVertexAttribPointer ( position_loc, 3, GL_FLOAT, GL_FALSE, 0, vertexArray );
		glEnableVertexAttribArray ( position_loc );
		glDrawArrays ( GL_TRIANGLE_STRIP, 0, 5 );

		eglSwapBuffers ( (EGLDisplay) display, surface );  // get the rendered buffer to the screen
	}

	printf("stop\n");
//
// #if 0
// 	(*egldestroycontext)((EGLDisplay) display, context);
// 	printf("destroyed context\n");
//
// 	(*egldestroysurface)((EGLDisplay) display, surface);
// 	printf("destroyed surface\n");
// 	(*eglterminate)((EGLDisplay) display);
// 	printf("terminated\n");
// 	android_dlclose(baz);
// #endif

	return 0;
}

// vim:ts=4:sw=4:noexpandtab
