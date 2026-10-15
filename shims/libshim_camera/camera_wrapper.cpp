//
// Copyright (C) 2026 Brian Nahuel Götte
// SPDX-License-Identifier: Apache-2.0
//
// Minimal HAL1 shim for Samsung Exynos7870 (j7xelte) with Front Flash Injection.
//

#define LOG_TAG "ExynosCameraShim_HAL1"

#include <hardware/hardware.h>
#include <hardware/camera.h>

#include <dlfcn.h>
#include <utils/Log.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <map>
#include <mutex>

static camera_module_t* gVendorModule = nullptr;

static char  sCam1Id[] = "1";
static char  sCam0Id[] = "0";
static char* sCam0Conflicts[] = { sCam1Id };
static char* sCam1Conflicts[] = { sCam0Id };

static camera_device_ops_t g_orig_ops[2];
static camera_device_ops_t g_shim_ops[2];

static int (*g_orig_cam1_close)(struct hw_device_t* device) = nullptr;

static std::map<char*, char*> g_param_tracker;
static std::mutex g_param_mutex;

static std::string g_front_flash_mode = "off";

static void set_front_flash_sysfs(int state) {
    int fd = open("/sys/class/camera/flash/front_torch_flash", O_WRONLY);
    if (fd >= 0) {
        char val = state ? '1' : '0';
        write(fd, &val, 1);
        close(fd);
    }
}

static int shim_cam1_close(struct hw_device_t* device) {
    g_front_flash_mode = "off";
    set_front_flash_sysfs(0);

    if (g_orig_cam1_close) {
        return g_orig_cam1_close(device);
    }
    return 0;
}

static int shim_cam1_start_preview(struct camera_device *dev) {
    int ret = g_orig_ops[1].start_preview(dev);
    
    // Vendor HAL heavily resets sensor state during start_preview.
    // We must re-assert our desired flash state here.
    if (g_front_flash_mode == "on" || g_front_flash_mode == "torch") {
        set_front_flash_sysfs(1);
    } else {
        set_front_flash_sysfs(0);
    }
    
    return ret;
}

static char* shim_cam1_get_parameters(struct camera_device *dev) {
    char* orig = g_orig_ops[1].get_parameters(dev);
    if (!orig) return nullptr;

    std::string s(orig);
    s += ";flash-mode=" + g_front_flash_mode;
    s += ";flash-mode-values=off,on,torch,auto";

    char* modified = strdup(s.c_str());

    {
        std::lock_guard<std::mutex> lock(g_param_mutex);
        g_param_tracker[modified] = orig;
    }
    return modified;
}

static void shim_cam1_put_parameters(struct camera_device *dev, char *params) {
    char* orig = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_param_mutex);
        auto it = g_param_tracker.find(params);
        if (it != g_param_tracker.end()) {
            orig = it->second;
            g_param_tracker.erase(it);
        }
    }

    if (orig) {
        g_orig_ops[1].put_parameters(dev, orig);
        free(params);
    } else {
        g_orig_ops[1].put_parameters(dev, params);
    }
}

static int shim_cam1_set_parameters(struct camera_device *dev, const char *params) {
    if (!params) return g_orig_ops[1].set_parameters(dev, params);

    std::string s(params);

    if (s.find("flash-mode=torch") != std::string::npos ||
        s.find("flash-mode=on") != std::string::npos) {
        g_front_flash_mode = (s.find("flash-mode=torch") != std::string::npos) ? "torch" : "on";
        set_front_flash_sysfs(1);
    } else if (s.find("flash-mode=off") != std::string::npos || s.find("flash-mode=auto") != std::string::npos) {
        g_front_flash_mode = "off";
        set_front_flash_sysfs(0);
    }

    std::string clean_params;
    size_t start = 0;
    while (start < s.length()) {
        size_t end = s.find(';', start);
        if (end == std::string::npos) end = s.length();

        std::string token = s.substr(start, end - start);
        if (token.find("flash-mode") == std::string::npos) {
            if (!clean_params.empty()) clean_params += ";";
            clean_params += token;
        }
        start = end + 1;
    }

    return g_orig_ops[1].set_parameters(dev, clean_params.c_str());
}

static int load_vendor_module() {
    if (gVendorModule) return 0;
    dlopen("/vendor/lib/libexynoscamera_shim.so", RTLD_NOW | RTLD_GLOBAL);
    void* handle = dlopen("/vendor/lib/hw/camera.exynos5_vendor.so", RTLD_NOW);
    if (!handle) {
        ALOGE("Failed to dlopen /vendor/lib/hw/camera.exynos5_vendor.so: %s", dlerror());
        return -EINVAL;
    }
    gVendorModule = (camera_module_t*) dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR);
    return gVendorModule ? 0 : -EINVAL;
}

static int shim_get_camera_info(int camera_id, struct camera_info *info) {
    if (load_vendor_module() != 0) return -EINVAL;

    memset(info, 0, sizeof(struct camera_info));
    int ret = gVendorModule->get_camera_info(camera_id, info);

    if (ret == 0) {
        info->device_version = CAMERA_DEVICE_API_VERSION_1_0;
        info->resource_cost = 100;
        if (camera_id == 0) {
            info->conflicting_devices = sCam0Conflicts;
            info->conflicting_devices_length = 1;
        } else if (camera_id == 1) {
            info->conflicting_devices = sCam1Conflicts;
            info->conflicting_devices_length = 1;
        }
    }
    return ret;
}

static int shim_device_open(const hw_module_t* /*module*/, const char* name, hw_device_t** device) {
    if (load_vendor_module() != 0) return -EINVAL;

    int ret;
    if (gVendorModule->open_legacy != nullptr) {
        ret = gVendorModule->open_legacy(
            (const hw_module_t*)gVendorModule, name,
            CAMERA_DEVICE_API_VERSION_1_0, device);
    } else {
        ret = gVendorModule->common.methods->open(
            (const hw_module_t*)gVendorModule, name, device);
    }

    if (ret == 0) {
        int cam_id = atoi(name);
        if (cam_id == 1) {
            camera_device_t* cam = (camera_device_t*)*device;

            memcpy(&g_orig_ops[1], cam->ops, sizeof(camera_device_ops_t));
            memcpy(&g_shim_ops[1], cam->ops, sizeof(camera_device_ops_t));

            g_shim_ops[1].get_parameters = shim_cam1_get_parameters;
            g_shim_ops[1].put_parameters = shim_cam1_put_parameters;
            g_shim_ops[1].set_parameters = shim_cam1_set_parameters;
            
            // Re-inyectamos el start_preview
            g_shim_ops[1].start_preview = shim_cam1_start_preview;

            cam->ops = &g_shim_ops[1];

            g_orig_cam1_close = cam->common.close;
            cam->common.close = shim_cam1_close;
        }
    }
    return ret;
}

static struct hw_module_methods_t shim_module_methods = { .open = shim_device_open };

static int shim_set_callbacks(const camera_module_callbacks_t *c) {
    if (load_vendor_module() != 0) return -EINVAL;
    if (gVendorModule->set_callbacks) return gVendorModule->set_callbacks(c);
    return 0;
}

static int shim_set_torch_mode(const char* id, bool enabled) {
    if (load_vendor_module() != 0) return -EINVAL;

    if (strcmp(id, "1") == 0) {
        set_front_flash_sysfs(enabled ? 1 : 0);
        return 0;
    }

    if (gVendorModule->set_torch_mode) return gVendorModule->set_torch_mode(id, enabled);
    return -ENOSYS;
}

static int shim_init() {
    if (load_vendor_module() != 0) return -EINVAL;
    if (gVendorModule->init) return gVendorModule->init();
    return 0;
}

static void shim_get_vendor_tag_ops(vendor_tag_ops_t* ops) {
    if (load_vendor_module() != 0) return;
    if (gVendorModule->get_vendor_tag_ops) gVendorModule->get_vendor_tag_ops(ops);
}

camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = CAMERA_HARDWARE_MODULE_ID,
        .name = "Exynos7870 HAL1 Shim",
        .author = "Brian Nahuel Götte",
        .methods = &shim_module_methods,
        .dso = nullptr,
        .reserved = {0},
    },
    .get_number_of_cameras = []() -> int {
        if (load_vendor_module() != 0) return 0;
        return gVendorModule->get_number_of_cameras();
    },
    .get_camera_info = shim_get_camera_info,
    .set_callbacks = shim_set_callbacks,
    .get_vendor_tag_ops = shim_get_vendor_tag_ops,
    .open_legacy = nullptr,
    .set_torch_mode = shim_set_torch_mode,
    .init = shim_init,
    .reserved = {0}
};