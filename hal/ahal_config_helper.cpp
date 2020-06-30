/*
 * Copyright (c) 2019, 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "ahal_config_helper"

#include "ahal_config_helper.h"
#include <cutils/properties.h>
#include <log/log.h>

struct AHalConfigHelper {
    static AHalConfigHelper* mConfigHelper;

    AHalConfigHelper() : isRemote(false) { };
    static AHalConfigHelper* getAHalConfInstance() {
        if (!mConfigHelper)
            mConfigHelper = new AHalConfigHelper();
        return mConfigHelper;
    }
    void initDefaultConfig(bool isVendorEnhancedFwk);
    AHalValues* getAHalValues();
    inline void retrieveConfigs();

    AHalValues mConfigs;
    bool isRemote; // configs specified from remote
};

AHalConfigHelper* AHalConfigHelper::mConfigHelper;

void AHalConfigHelper::initDefaultConfig(bool isVendorEnhancedFwk)
{
    ALOGV("%s: enter", __FUNCTION__);
    if (isVendorEnhancedFwk) {
        mConfigs = {
            true,        /* SND_MONITOR */
            false,       /* COMPRESS_CAPTURE */
            false,       /* SOURCE_TRACK */
            false,       /* SSREC */
            false,       /* AUDIOSPHERE */
            true,        /* AFE_PROXY */
            false,       /* USE_DEEP_AS_PRIMARY_OUTPUT */
            false,       /* HDMI_EDID */
            false,       /* KEEP_ALIVE */
            false,       /* HIFI_AUDIO */
            false,       /* RECEIVER_AIDED_STEREO */
            false,       /* KPI_OPTIMIZE */
            false,       /* DISPLAY_PORT */
            true,        /* FLUENCE */
            false,       /* CUSTOM_STEREO */
            false,       /* ANC_HEADSET */
            false,       /* DSM_FEEDBACK */
            false,       /* USB_OFFLOAD */
            false,       /* USB_OFFLOAD_BURST_MODE */
            false,       /* USB_OFFLOAD_SIDETONE_VOLM */
            false,       /* A2DP_OFFLOAD */
            false,       /* VBAT */
            true,        /* COMPRESS_METADATA_NEEDED */
            false,       /* COMPRESS_VOIP */
            false,       /* DYNAMIC_ECNS */
            true,        /* SPKR_PROT */
        };
    } else {
        mConfigs = {
#if SND_MONITOR_ENABLED
            true,        /* SND_MONITOR */
#else
            false,        /* SND_MONITOR */
#endif
#if COMPRESS_CAPTURE_ENABLED
            true,       /* COMPRESS_CAPTURE */
#else
            false,       /* COMPRESS_CAPTURE */
#endif
#if SOURCE_TRACKING_ENABLED
            false,      /* SOURCE_TRACK */
#else
            false,       /* SOURCE_TRACK */
#endif
#if SSREC_ENABLED
            false,       /* SSREC */
#else
            false,       /* SSREC */
#endif
#if AUDIOSPHERE_ENABLED
            false,       /* AUDIOSPHERE */
#else
            false,       /* AUDIOSPHERE */
#endif
#if AFE_PROXY_ENABLED
            true,       /* AFE_PROXY */
#else
            false,       /* AFE_PROXY */
#endif
#if USE_DEEP_AS_PRIMARY_OUTPUT_ENABLED
            false,       /* USE_DEEP_AS_PRIMARY_OUTPUT */
#else
            false,       /* USE_DEEP_AS_PRIMARY_OUTPUT */
#endif
#if HDMI_EDID_ENABLED
            false,       /* HDMI_EDID */
#else
            false,       /* HDMI_EDID */
#endif
#if KEEP_ALIVE_ENABLED
            true,       /* KEEP_ALIVE */
#else
            false,       /* KEEP_ALIVE */
#endif
#if HIFI_AUDIO_ENABLED
            false,       /* HIFI_AUDIO */
#else
            false,       /* HIFI_AUDIO */
#endif
#if RECEIVER_AIDED_STEREO_ENABLED
            false,       /* RECEIVER_AIDED_STEREO */
#else
            false,       /* RECEIVER_AIDED_STEREO */
#endif
#if KPI_OPTIMIZE_ENABLED
            false,       /* KPI_OPTIMIZE */
#else
            false,       /* KPI_OPTIMIZE */
#endif
#if DISPLAY_PORT_ENABLED
            false,       /* DISPLAY_PORT */
#else
            false,       /* DISPLAY_PORT */
#endif
#if FLUENCE_ENABLED
            true,       /* FLUENCE */
#else
            false,       /* FLUENCE */
#endif
#if CUSTOM_STEREO_ENABLED
            false,       /* CUSTOM_STEREO */
#else
            false,       /* CUSTOM_STEREO */
#endif
#if ANC_HEADSET_ENABLED
            false,       /* ANC_HEADSET */
#else
            false,       /* ANC_HEADSET */
#endif
#if DSM_FEEDBACK_ENABLED
            false,       /* DSM_FEEDBACK */
#else
            false,       /* DSM_FEEDBACK */
#endif
#if USB_OFFLOAD_ENABLED
            false,        /* USB_OFFLOAD */
#else
            false,        /* USB_OFFLOAD */
#endif
#if USB_OFFLOAD_BURST_MODE_ENABLED
            false,       /* USB_OFFLOAD_BURST_MODE */
#else
            false,       /* USB_OFFLOAD_BURST_MODE */
#endif
#if USB_OFFLOAD_SIDETONE_VOLM_ENABLED
            false,       /* USB_OFFLOAD_SIDETONE_VOLM */
#else
            false,       /* USB_OFFLOAD_SIDETONE_VOLM */
#endif
#if A2DP_OFFLOAD_ENABLED
            false,        /* A2DP_OFFLOAD */
#else
            false,        /* A2DP_OFFLOAD */
#endif
#if VBAT_MONITOR_ENABLED
            false,       /* VBAT */
#else
            false,       /* VBAT */
#endif
#if COMPRESS_METADATA_ENABLED
            true,       /* COMPRESS_METADATA_NEEDED */
#else
            false,       /* COMPRESS_METADATA_NEEDED */
#endif
#ifdef COMPRESS_VOIP_ENABLED
            false,       /* COMPRESS_VOIP */
#else
            false,       /* COMPRESS_VOIP */
#endif
#ifdef DYNAMIC_ECNS_ENABLED
            true,       /* DYNAMIC_ECNS */
#else
            false,       /* DYNAMIC_ECNS */
#endif
#ifdef SPKR_PROT_ENABLED
            true,        /* SPKR_PROT */
#else
            false,        /* SPKR_PROT */
#endif
        };
    }
}

AHalValues* AHalConfigHelper::getAHalValues()
{
    ALOGV("%s: enter", __FUNCTION__);
    retrieveConfigs();
    return &mConfigs;
}

void AHalConfigHelper::retrieveConfigs()
{
    ALOGV("%s: enter", __FUNCTION__);
    // ToDo: Add logic to query AHalValues from config store
    // once support is added to it
    return;
}

extern "C" {

AHalValues* confValues = nullptr;

void audio_extn_ahal_config_helper_init(bool is_vendor_enhanced_fwk)
{
    AHalConfigHelper* confInstance = AHalConfigHelper::getAHalConfInstance();
    if (confInstance)
        confInstance->initDefaultConfig(is_vendor_enhanced_fwk);
}

AHalValues* audio_extn_get_feature_values()
{
    AHalConfigHelper* confInstance = AHalConfigHelper::getAHalConfInstance();
    if (confInstance)
        confValues = confInstance->getAHalValues();
    return confValues;
}

bool audio_extn_is_config_from_remote()
{
    AHalConfigHelper* confInstance = AHalConfigHelper::getAHalConfInstance();
    if (confInstance)
        return confInstance->isRemote;
    return false;
}

} // extern C
