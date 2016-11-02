/* icc.c
Copyright (c) 2012-2015, 2016, The Linux Foundation. All rights reserved.

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
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

#define LOG_TAG "audio_hw_icc"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include "audio_extn.h"

#ifdef ICC_ENABLED
#define AUDIO_PARAMETER_ICC_ENABLE      "conversation_mode_state"
#define AUDIO_PARAMETER_ICC_SET_SAMPLING_RATE "icc_set_sampling_rate"
#define AUDIO_PARAMETER_KEY_ICC_VOLUME "icc_volume"

#ifdef PLATFORM_MSM8994
#define ICC_RX_VOLUME     "NULL"
#elif defined PLATFORM_MSM8996
#define ICC_RX_VOLUME     "NULL"
#else
#define ICC_RX_VOLUME     "NULL"
#endif

static int32_t start_icc(struct audio_device *adev,
                               struct str_parms *parms);

static int32_t stop_icc(struct audio_device *adev);

struct icc_module {
    struct pcm *icc_pcm_rx;
    struct pcm *icc_pcm_tx;
    bool is_icc_running;
    float icc_volume;
    audio_usecase_t ucid;
};

static struct icc_module iccmod = {
    .icc_pcm_rx = NULL,
    .icc_pcm_tx = NULL,
    .icc_volume = 0,
    .is_icc_running = 0,
    .ucid = USECASE_ICC_CALL,
};
static struct pcm_config pcm_config_icc = {
    .channels = 2,
    .rate = 16000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static int32_t icc_set_volume(struct audio_device *adev, float value)
{
    int32_t vol, ret = 0;

    ALOGV("%s: exit", __func__);
    return ret;
}

static int32_t start_icc(struct audio_device *adev,
                         struct str_parms *parms __unused)
{
    int32_t i, ret = 0;
    struct audio_usecase *uc_info;
    int32_t pcm_dev_rx_id, pcm_dev_tx_id;

    ALOGD("%s: enter", __func__);

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info)
        return -ENOMEM;

    uc_info->id = iccmod.ucid;
    uc_info->type = ICC_CALL;
    uc_info->stream.out = adev->primary_output;
    uc_info->devices = adev->primary_output->devices;
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info->list);

    select_devices(adev, iccmod.ucid);

    pcm_dev_rx_id = platform_get_pcm_device_id(uc_info->id, PCM_PLAYBACK);
    pcm_dev_tx_id = platform_get_pcm_device_id(uc_info->id, PCM_CAPTURE);
    if (pcm_dev_rx_id < 0 || pcm_dev_tx_id < 0 ) {
        ALOGE("%s: Invalid PCM devices (rx: %d tx: %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, uc_info->id);
        ret = -EIO;
        goto exit;
    }

    ALOGV("%s: ICC PCM devices (ICC pcm rx: %d pcm tx: %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, uc_info->id);

    ALOGD("%s: Opening PCM playback device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_rx_id);
    iccmod.icc_pcm_rx = pcm_open(adev->snd_card,
                                   pcm_dev_rx_id,
                                   PCM_OUT, &pcm_config_icc);
    if (iccmod.icc_pcm_rx && !pcm_is_ready(iccmod.icc_pcm_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(iccmod.icc_pcm_rx));
        ret = -EIO;
        goto exit;
    }

    ALOGD("%s: Opening PCM capture device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_tx_id);
    iccmod.icc_pcm_tx = pcm_open(adev->snd_card,
                                   pcm_dev_tx_id,
                                   PCM_IN, &pcm_config_icc);
    if (iccmod.icc_pcm_tx && !pcm_is_ready(iccmod.icc_pcm_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(iccmod.icc_pcm_tx));
        ret = -EIO;
        goto exit;
    }

    if ((uc_info->out_snd_device != SND_DEVICE_NONE) ||
        (uc_info->in_snd_device != SND_DEVICE_NONE)) {
        if (audio_extn_ext_hw_plugin_usecase_start(adev->ext_hw_plugin, uc_info))
            ALOGE("%s: failed to start ext hw plugin", __func__);
    }

    if (pcm_start(iccmod.icc_pcm_rx) < 0) {
        ALOGE("%s: pcm start for icc pcm rx failed", __func__);
        ret = -EINVAL;
        goto exit;
    }
    if (pcm_start(iccmod.icc_pcm_tx) < 0) {
        ALOGE("%s: pcm start for icc pcm tx failed", __func__);
        ret = -EINVAL;
        goto exit;
    }

    iccmod.is_icc_running = true;
    icc_set_volume(adev, iccmod.icc_volume);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return 0;

exit:
    stop_icc(adev);
    ALOGE("%s: Problem in ICC start: status(%d)", __func__, ret);
    return ret;
}

static int32_t stop_icc(struct audio_device *adev)
{
    int32_t i, ret = 0;
    struct audio_usecase *uc_info;

    ALOGD("%s: enter", __func__);
    iccmod.is_icc_running = false;

    /* 1. Close the PCM devices */

    if (iccmod.icc_pcm_rx) {
        pcm_close(iccmod.icc_pcm_rx);
        iccmod.icc_pcm_rx = NULL;
    }
    if (iccmod.icc_pcm_tx) {
        pcm_close(iccmod.icc_pcm_tx);
        iccmod.icc_pcm_tx = NULL;
    }

    uc_info = get_usecase_from_list(adev, iccmod.ucid);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, iccmod.ucid);
        return -EINVAL;
    }

    if ((uc_info->out_snd_device != SND_DEVICE_NONE) ||
        (uc_info->in_snd_device != SND_DEVICE_NONE)) {
        if (audio_extn_ext_hw_plugin_usecase_stop(adev->ext_hw_plugin, uc_info))
            ALOGE("%s: failed to stop ext hw plugin", __func__);
    }

    /* 2. Disable echo reference while stopping icc */
    platform_set_echo_reference(adev, false, uc_info->devices);

    /* 3. Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info);

    /* 4. Disable the rx and tx devices */
    disable_snd_device(adev, uc_info->out_snd_device);
    disable_snd_device(adev, uc_info->in_snd_device);

    list_remove(&uc_info->list);
    free(uc_info);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return ret;
}

bool audio_extn_icc_is_active(struct audio_device *adev)
{
    struct audio_usecase *icc_usecase = NULL;
    icc_usecase = get_usecase_from_list(adev, iccmod.ucid);

    if (icc_usecase != NULL)
        return true;
    else
        return false;
}

audio_usecase_t audio_extn_icc_get_usecase()
{
    return iccmod.ucid;
}

void audio_extn_icc_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    int ret;
    int rate;
    int val;
    float vol;
    char value[32]={0};

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_ICC_ENABLE, value,
                            sizeof(value));
    if (ret >= 0) {
           if (!strncmp(value,"true",sizeof(value)))
               ret = start_icc(adev,parms);
           else
               stop_icc(adev);
    }
    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms,AUDIO_PARAMETER_ICC_SET_SAMPLING_RATE, value,
                            sizeof(value));
    if (ret >= 0) {
           rate = atoi(value);
           if (rate == 16000){
               iccmod.ucid = USECASE_ICC_CALL;
               pcm_config_icc.rate = rate;
           } else
               ALOGE("Unsupported rate..");
    }

    if (iccmod.is_icc_running) {
        memset(value, 0, sizeof(value));
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                                value, sizeof(value));
        if (ret >= 0) {
            val = atoi(value);
            if (val > 0)
                select_devices(adev, iccmod.ucid);
        }
    }

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_ICC_VOLUME,
                            value, sizeof(value));
    if (ret >= 0) {
        if (sscanf(value, "%f", &vol) != 1){
            ALOGE("%s: error in retrieving icc volume", __func__);
            ret = -EIO;
            goto exit;
        }
        ALOGD("%s: icc_set_volume usecase, Vol: [%f]", __func__, vol);
        icc_set_volume(adev, vol);
    }
exit:
    ALOGV("%s Exit",__func__);
}
#endif /*ICC_ENABLED*/
