/*
 * Copyright (C) 2018 TheKit <nekit1000@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HWC2_COMPATIBILITY_LAYER_H_
#define HWC2_COMPATIBILITY_LAYER_H_

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <system/graphics.h>

#ifdef __cplusplus
extern "C" {
#endif
	typedef void (*on_vsync_received_callback)(int32_t sequenceId, hwc2_display_t display,
                    int64_t timestamp);
	typedef void (*on_hotplug_received_callback)(int32_t sequenceId, hwc2_display_t display,
                    bool connected, bool primaryDisplay);
    typedef void (*on_refresh_received_callback)(int32_t sequenceId, hwc2_display_t display);

    typedef struct HWC2EventListener
    {
        on_vsync_received_callback on_vsync_received;
        on_hotplug_received_callback on_hotplug_received;
        on_refresh_received_callback on_refresh_received;
    } HWC2EventListener;

    typedef struct HWC2DisplayConfig {
        hwc2_config_t id;
        hwc2_display_t display;
        int32_t width;
        int32_t height;
        nsecs_t vsyncPeriod;
        float dpiX;
        float dpiY;
    } HWC2DisplayConfig;

    struct hwc2_compat_device;
    typedef struct hwc2_compat_device hwc2_compat_device_t;

    struct hwc2_compat_display;
    typedef struct hwc2_compat_display hwc2_compat_display_t;

    struct hwc2_compat_layer;
    typedef struct hwc2_compat_layer hwc2_compat_layer_t;

    struct hwc2_compat_out_fences;
    typedef struct hwc2_compat_out_fences hwc2_compat_out_fences_t;

    hwc2_compat_device_t* hwc2_compat_device_new(bool);
    void hwc2_compat_device_register_callback(HWC2EventListener* listener,
                                              int composerSequenceId);
    hwc2_compat_display_t* hwc2_compat_device_get_display_by_id(
                                hwc2_compat_device_t* device,
                                hwc2_display_t id);

    HWC2DisplayConfig* hwc2_compat_display_get_active_config(
                                hwc2_compat_display_t* display);

    bool hwc2_compat_display_accept_changes(hwc2_compat_display_t* display);
    hwc2_compat_layer_t* hwc2_compat_display_create_layer(hwc2_compat_display_t*
                                                          display);
    void hwc2_compat_display_destroy_layer(hwc2_compat_display_t* display,
                                           hwc2_compat_layer_t* layer);

    bool hwc2_compat_display_get_release_fences(hwc2_compat_display_t* display,
                                                hwc2_compat_out_fences_t**
                                                outFences);
    bool hwc2_compat_display_present(hwc2_compat_display_t* display,
                                     int32_t* outPresentFence);

    bool hwc2_compat_display_set_client_target(hwc2_compat_display_t* display,
                                               uint32_t slot,
                                               ANativeWindowBuffer* buffer,
                                               const int32_t acquireFenceFd,
                                               android_dataspace_t dataspace);

    bool hwc2_compat_display_set_power_mode(hwc2_compat_display_t* display,
                                            int mode);
    bool hwc2_compat_display_set_vsync_enabled(hwc2_compat_display_t* display,
                                               int enabled);

    int32_t hwc2_compat_display_validate(hwc2_compat_display_t* display,
                                         uint32_t* outNumTypes,
                                         uint32_t* outNumRequests);

    bool hwc2_compat_display_present_or_validate(hwc2_compat_display_t* display,
                                                 uint32_t* outNumTypes,
                                                 uint32_t* outNumRequests,
                                                 int32_t* outPresentFence,
                                                 uint32_t* state);

    bool hwc2_compat_layer_set_blend_mode(hwc2_compat_layer_t* layer, int mode);
    bool hwc2_compat_layer_set_color(hwc2_compat_layer_t* layer,
                                     hwc_color_t color);
    bool hwc2_compat_layer_set_composition_type(hwc2_compat_layer_t* layer,
                                                int type);
    bool hwc2_compat_layer_set_dataspace(hwc2_compat_layer_t* layer,
                                         android_dataspace_t dataspace);
    bool hwc2_compat_layer_set_display_frame(hwc2_compat_layer_t* layer,
                                             int32_t left, int32_t top,
                                             int32_t right, int32_t bottom);
    bool hwc2_compat_layer_set_plane_alpha(hwc2_compat_layer_t* layer,
                                           float alpha);
    bool hwc2_compat_layer_set_sideband_stream(hwc2_compat_layer_t* layer,
                                               const native_handle_t* stream);
    bool hwc2_compat_layer_set_source_crop(hwc2_compat_layer_t* layer,
                                           float left, float top,
                                           float right, float bottom);
    bool hwc2_compat_layer_set_transform(hwc2_compat_layer_t* layer,
                                         int transform);
    bool hwc2_compat_layer_set_visible_region(hwc2_compat_layer_t* layer,
                                              int32_t left, int32_t top,
                                              int32_t right, int32_t bottom);

#ifdef __cplusplus
}
#endif

#endif // UI_COMPATIBILITY_LAYER_H_