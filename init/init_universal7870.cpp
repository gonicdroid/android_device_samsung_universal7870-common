/*
   Copyright (c) 2016, The Linux Foundation. All rights reserved.
   Copyright (c) 2017-2026, The LineageOS Project. All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
   ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
   BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
   OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
   IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/properties.h>

using android::base::GetProperty;

// copied from build/tools/releasetools/ota_from_target_files.py
// but with "." at the end and empty entry
std::vector<std::string> ro_product_props_default_source_order = {
    "",
    "product.",
    "odm.",
    "odm_dlkm.",
    "vendor.",
    "vendor_dlkm.",
    "system.",
    "system_ext.",
};

void property_override(char const prop[], char const value[], bool add)
{
    auto pi = (prop_info *) __system_property_find(prop);

    if (pi != nullptr) {
        __system_property_update(pi, value, strlen(value));
    } else if (add) {
        __system_property_add(prop, strlen(prop), value, strlen(value));
    }
}

void set_ro_product_prop(char const prop[], char const value[])
{
    for (const auto &source : ro_product_props_default_source_order) {
        auto prop_name = "ro.product." + source + prop;
        property_override(prop_name.c_str(), value, false);
    }
}

void set_ro_build_prop(char const prop[], char const value[])
{
    for (const auto &source : ro_product_props_default_source_order) {
        auto prop_name = "ro." + source + "build." + prop;
        property_override(prop_name.c_str(), value, false);
    }
};

void vendor_load_properties()
{
    std::string bootloader = GetProperty("ro.bootloader", "");
    if (bootloader.find("J710FQ") == 0) {
        /* SM-J710FQ */
        property_override("ro.build.description", "j7xeltetur-user 8.1.0 M1AJQ J710FQXXU3CSG1 release-keys", false);
        set_ro_product_prop("model", "SM-J710FQ");
        set_ro_product_prop("device", "j7xelte");
        set_ro_product_prop("name", "j7xeltetur");
    } else if (bootloader.find("J710F") == 0) {
        /* SM-J710F */
        property_override("ro.build.description", "j7xeltexx-user 8.1.0 M1AJQ J710FXXS6CTC1 release-keys", false);
        set_ro_product_prop("model", "SM-J710F");
        set_ro_product_prop("device", "j7xelte");
        set_ro_product_prop("name", "j7xeltexx");
    } else if (bootloader.find("J710GN") == 0) {
        /* SM-J710GN */
        property_override("ro.build.description", "j7xeltedx-user 8.1.0 M1AJQ J710GNDXS4CTJ1 release-keys", false);
        set_ro_product_prop("model", "SM-J710GN");
        set_ro_product_prop("device", "j7xelte");
        set_ro_product_prop("name", "j7xeltedx");
    } else if (bootloader.find("J710K") == 0) {
        /* SM-J710K */
        property_override("ro.build.description", "j7xeltektt-user 8.1.0 M1AJQ J710KKKS1CTJ1 release-keys", false);
        set_ro_product_prop("model", "SM-J710K");
        set_ro_product_prop("device", "j7xeltektt");
        set_ro_product_prop("name", "j7xeltektt");
    } else if (bootloader.find("J710MN") == 0) {
        /* SM-J710MN */
        property_override("ro.build.description", "j7xelteub-user 8.1.0 M1AJQ J710MNUBS4CTF2 release-keys", false);
        set_ro_product_prop("model", "SM-J710MN");
        set_ro_product_prop("device", "j7xelte");
        set_ro_product_prop("name", "j7xelteub");
    }  else if (bootloader.find("J701MT") == 0) {
        /* SM-J701MT */
        property_override("ro.build.description", "j7veltedtvvj-user 8.1.0 M1AJQ J701MTVJU6BSD1 release-keys", false);
        set_ro_product_prop("model", "SM-J701MT");
        set_ro_product_prop("device", "j7velte");
        set_ro_product_prop("name", "j7veltedtvvj");
    } else if (bootloader.find("J701M") == 0) {
        /* SM-J701M */
        property_override("ro.build.description", "j7velteub-user 8.1.0 M1AJQ J701MUBU6BSD3 release-keys", false);
        set_ro_product_prop("model", "SM-J701M");
        set_ro_product_prop("device", "j7velte");
        set_ro_product_prop("name", "j7velteub");
    } else if (bootloader.find("J701F") == 0) {
        /* SM-J701F */
        property_override("ro.build.description", "j7veltedx-user 8.1.0 M1AJQ J701FXXU6BSD1 release-keys", false);
        set_ro_product_prop("model", "SM-J701F");
        set_ro_product_prop("device", "j7velte");
        set_ro_product_prop("name", "j7veltedx");
    }
    else {
        /* J710F if not found*/
        property_override("ro.build.description", "j7xeltexx-user 8.1.0 M1AJQ J710FXXS6CTC1 release-keys", false);
        set_ro_product_prop("model", "SM-J710F");
        set_ro_product_prop("device", "j7xelte");
        set_ro_product_prop("name", "j7xeltexx");
    }

    LOG(INFO) << "Found bootloader id " << bootloader <<  ", setting build properties" << std::endl;
}
