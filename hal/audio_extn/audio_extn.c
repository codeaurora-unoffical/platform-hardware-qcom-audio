/*
 * Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
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
 *
 * This file was modified by DTS, Inc. The portions of the
 * code modified by DTS, Inc are copyrighted and
 * licensed separately, as follows:
 *
 * (C) 2014 DTS, Inc.
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
 */

#define LOG_TAG "audio_hw_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <log/log.h>
#include <unistd.h>
#include <sched.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "voice_extn.h"
#include "audio_defs.h"
#include "platform.h"
#include "platform_api.h"
#include "edid.h"
#include "audio_feature_manager.h"
#include "voice_extn.h"
#include "adsp_hdlr.h"

#include "sound/compress_params.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_AUDIO_EXTN
#include <log_utils.h>
#endif

#ifndef APTX_DECODER_ENABLED
#define audio_extn_aptx_dec_set_license(adev) (0)
#define audio_extn_set_aptx_dec_bt_addr(adev, parms) (0)
#define audio_extn_parse_aptx_dec_bt_addr(value) (0)
#else
static void audio_extn_aptx_dec_set_license(struct audio_device *adev);
static void audio_extn_set_aptx_dec_bt_addr(struct audio_device *adev, struct str_parms *parms);
static void audio_extn_parse_aptx_dec_bt_addr(char *value);
#endif

#define MAX_SLEEP_RETRY 100
#define WIFI_INIT_WAIT_SLEEP 50
#define MAX_NUM_CHANNELS 8
#define Q14_GAIN_UNITY 0x4000

static bool is_running_on_stock_version = true;
static bool is_compress_meta_data_enabled = false;

struct snd_card_split cur_snd_card_split = {
    .device = {0},
    .snd_card = {0},
    .form_factor = {0},
};

struct snd_card_split *audio_extn_get_snd_card_split()
{
    return &cur_snd_card_split;
}

void fm_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms);
void fm_get_parameters(struct str_parms *query, struct str_parms *reply);

void keep_alive_init(struct audio_device *adev);
void keep_alive_deinit();
void keep_alive_start(ka_mode_t ka_mode);
void keep_alive_stop(ka_mode_t ka_mode);
int keep_alive_set_parameters(struct audio_device *adev,
                                         struct str_parms *parms);

void audio_extn_set_snd_card_split(const char* in_snd_card_name)
{
    /* sound card name follows below mentioned convention
       <target name>-<sound card name>-<form factor>-snd-card
       parse target name, sound card name and form factor
    */
    char *snd_card_name = NULL;
    char *tmp = NULL;
    char *device = NULL;
    char *snd_card = NULL;
    char *form_factor = NULL;

    if (in_snd_card_name == NULL) {
        ALOGE("%s: snd_card_name passed is NULL", __func__);
        goto on_error;
    }
    snd_card_name = strdup(in_snd_card_name);

    device = strtok_r(snd_card_name, "-", &tmp);
    if (device == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.device, device, HW_INFO_ARRAY_MAX_SIZE);

    snd_card = strtok_r(NULL, "-", &tmp);
    if (snd_card == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.snd_card, snd_card, HW_INFO_ARRAY_MAX_SIZE);

    form_factor = strtok_r(NULL, "-", &tmp);
    if (form_factor == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.form_factor, form_factor, HW_INFO_ARRAY_MAX_SIZE);

    ALOGI("%s: snd_card_name(%s) device(%s) snd_card(%s) form_factor(%s)",
               __func__, in_snd_card_name, device, snd_card, form_factor);

on_error:
    if (snd_card_name)
        free(snd_card_name);
}

/* TONE Generation Keys */
/* tone_low_freq and tone_high_freq must be paired */
#define AUDIO_PARAMETER_KEY_TONE_LOW_FREQ "tone_low_freq"
#define AUDIO_PARAMETER_KEY_TONE_HIGH_FREQ "tone_high_freq"
#define AUDIO_PARAMETER_KEY_TONE_DURATION_MS "tone_duration_ms"
#define AUDIO_PARAMETER_KEY_TONE_GAIN "tone_gain"
#define AUDIO_PARAMETER_KEY_TONE_OFF "tone_off"

struct audio_extn_module {
    bool anc_enabled;
    bool aanc_enabled;
    bool custom_stereo_enabled;
    uint32_t proxy_channel_num;
    bool hpx_enabled;
    bool vbat_enabled;
    bool bcl_enabled;
    bool hifi_audio_enabled;
    bool ras_enabled;
    struct aptx_dec_bt_addr addr;
    struct audio_device *adev;
};

static struct audio_extn_module aextnmod;
static bool audio_extn_fm_power_opt_enabled = false;
static bool audio_extn_keep_alive_enabled = false;
static bool audio_extn_hifi_audio_enabled = false;
static bool audio_extn_ras_feature_enabled = false;
static bool audio_extn_kpi_optimize_feature_enabled = false;
static bool audio_extn_display_port_feature_enabled = false;
static bool audio_extn_fluence_feature_enabled = false;
static bool audio_extn_custom_stereo_feature_enabled = false;
static bool audio_extn_anc_headset_feature_enabled = false;
static bool audio_extn_vbat_enabled = false;

#define AUDIO_PARAMETER_KEY_AANC_NOISE_LEVEL "aanc_noise_level"
#define AUDIO_PARAMETER_KEY_ANC        "anc_enabled"
#define AUDIO_PARAMETER_KEY_WFD        "wfd_channel_cap"
#define AUDIO_PARAMETER_CAN_OPEN_PROXY "can_open_proxy"
#define AUDIO_PARAMETER_CUSTOM_STEREO  "stereo_as_dual_mono"
/* Query offload playback instances count */
#define AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE "offload_num_active"
#define AUDIO_PARAMETER_HPX            "HPX"
#define AUDIO_PARAMETER_APTX_DEC_BT_ADDR "bt_addr"

/*
* update sysfs node hdmi_audio_cb to enable notification acknowledge feature
* bit(5) set to 1 to enable this feature
* bit(4) set to 1 to enable acknowledgement
* this is done only once at the first connect event
*
* bit(0) set to 1 when HDMI cable is connected
* bit(0) set to 0 when HDMI cable is disconnected
* this is done when device switch happens by setting audioparamter
*/

#define IS_BIT_SET(NUM, bitno) (NUM & (1 << bitno))

#define EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE 0x30

static ssize_t update_sysfs_node(const char *path, const char *data, size_t len)
{
    ssize_t err = 0;
    int fd = -1;

    err = access(path, W_OK);
    if (!err) {
        fd = open(path, O_WRONLY);
        errno = 0;
        err = write(fd, data, len);
        if (err < 0) {
            err = -errno;
        }
        close(fd);
    } else {
        ALOGE("%s: Failed to access path: %s error: %s",
                __FUNCTION__, path, strerror(errno));
        err = -errno;
    }

    return err;
}

static int get_ext_disp_sysfs_node_index(int ext_disp_type)
{
    int node_index = -1;
    char fbvalue[80] = {0};
    char fbpath[80] = {0};
    int i = 0;
    FILE *ext_disp_fd = NULL;

    while (1) {
        snprintf(fbpath, sizeof(fbpath),
                  "/sys/class/graphics/fb%d/msm_fb_type", i);
        ext_disp_fd = fopen(fbpath, "r");
        if (ext_disp_fd) {
            if (fread(fbvalue, sizeof(char), 80, ext_disp_fd)) {
                if(((strncmp(fbvalue, "dtv panel", strlen("dtv panel")) == 0) &&
                    (ext_disp_type == EXT_DISPLAY_TYPE_HDMI)) ||
                   ((strncmp(fbvalue, "dp panel", strlen("dp panel")) == 0) &&
                    (ext_disp_type == EXT_DISPLAY_TYPE_DP))) {
                    node_index = i;
                    ALOGD("%s: Ext Disp:%d is at fb%d", __func__, ext_disp_type, i);
                    fclose(ext_disp_fd);
                    return node_index;
                }
            }
            fclose(ext_disp_fd);
            i++;
        } else {
            ALOGE("%s: Scanned till end of fbs or Failed to open fb node %d", __func__, i);
            break;
        }
    }

    return -1;
}

static int update_ext_disp_sysfs_node(const struct audio_device *adev, int node_value)
{
    char ext_disp_ack_path[80] = {0};
    char ext_disp_ack_value[3] = {0};
    int index, ret = -1;
    int ext_disp_type = platform_get_ext_disp_type(adev->platform);

    if (ext_disp_type < 0) {
        ALOGE("%s, Unable to get the external display type, err:%d",
              __func__, ext_disp_type);
        return -EINVAL;
    }

    index = get_ext_disp_sysfs_node_index(ext_disp_type);
    if (index >= 0) {
        snprintf(ext_disp_ack_value, sizeof(ext_disp_ack_value), "%d", node_value);
        snprintf(ext_disp_ack_path, sizeof(ext_disp_ack_path),
                  "/sys/class/graphics/fb%d/hdmi_audio_cb", index);

        ret = update_sysfs_node(ext_disp_ack_path, ext_disp_ack_value,
                sizeof(ext_disp_ack_value));

        ALOGI("update hdmi_audio_cb at fb[%d] to:[%d] %s",
            index, node_value, (ret >= 0) ? "success":"fail");
    }

    return ret;
}

static int update_audio_ack_state(const struct audio_device *adev, int node_value)
{
    const char *mixer_ctl_name = "External Display Audio Ack";
    struct mixer_ctl *ctl;
    int ret = 0;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    /* If no mixer command support, fall back to sysfs node approach */
    if (!ctl) {
        ALOGI("%s: could not get ctl for mixer cmd(%s), use sysfs node instead\n",
              __func__, mixer_ctl_name);
        ret = update_ext_disp_sysfs_node(adev, node_value);
    } else {
        char *ack_str = NULL;

        if (node_value == EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE)
            ack_str = "Ack_Enable";
        else if (node_value == 1)
            ack_str = "Connect";
        else if (node_value == 0)
            ack_str = "Disconnect";
        else {
            ALOGE("%s: Invalid input parameter - 0x%x\n",
                  __func__, node_value);
            return -EINVAL;
        }

        ret = mixer_ctl_set_enum_by_string(ctl, ack_str);
        if (ret)
            ALOGE("%s: Could not set ctl for mixer cmd - %s ret %d\n",
                  __func__, mixer_ctl_name, ret);
    }
    return ret;
}

static void audio_extn_ext_disp_set_parameters(const struct audio_device *adev,
                                                     struct str_parms *parms)
{
    char value[32] = {0};
    static bool is_hdmi_sysfs_node_init = false;

    if (str_parms_get_str(parms, "connect", value, sizeof(value)) >= 0
            && (atoi(value) & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        //params = "connect=1024" for external display connection.
        if (is_hdmi_sysfs_node_init == false) {
            //check if this is different for dp and hdmi
            is_hdmi_sysfs_node_init = true;
            update_audio_ack_state(adev, EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE);
        }
        update_audio_ack_state(adev, 1);
    } else if(str_parms_get_str(parms, "disconnect", value, sizeof(value)) >= 0
            && (atoi(value) & AUDIO_DEVICE_OUT_AUX_DIGITAL)){
        //params = "disconnect=1024" for external display disconnection.
        update_audio_ack_state(adev, 0);
        ALOGV("invalidate cached edid");
        platform_invalidate_hdmi_config(adev->platform);
    } else {
        // handle ext disp devices only
        return;
    }
}

static int update_custom_mtmx_coefficients_v2(struct audio_device *adev,
                                              struct audio_custom_mtmx_params *params,
                                              int pcm_device_id)
{
    struct mixer_ctl *ctl = NULL;
    char *mixer_name_prefix = "AudStr";
    char *mixer_name_suffix = "ChMixer Weight Ch";
    char mixer_ctl_name[128] = {0};
    struct audio_custom_mtmx_params_info *pinfo = &params->info;
    int i = 0, err = 0;
    int cust_ch_mixer_cfg[128], len = 0;

    ALOGI("%s: ip_channels %d, op_channels %d, pcm_device_id %d",
          __func__, pinfo->ip_channels, pinfo->op_channels, pcm_device_id);

    if (adev->use_old_pspd_mix_ctrl) {
        /*
         * Below code is to ensure backward compatibilty with older
         * kernel version. Use old mixer control to set mixer coefficients
         */
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
         "Audio Stream %d Channel Mix Cfg", pcm_device_id);

        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }
        cust_ch_mixer_cfg[len++] = pinfo->ip_channels;
        cust_ch_mixer_cfg[len++] = pinfo->op_channels;
        for (i = 0; i < (int) (pinfo->op_channels * pinfo->ip_channels); i++) {
            ALOGV("%s: coeff[%d] %d", __func__, i, params->coeffs[i]);
            cust_ch_mixer_cfg[len++] = params->coeffs[i];
        }
        err = mixer_ctl_set_array(ctl, cust_ch_mixer_cfg, len);
        if (err) {
            ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
            return -EINVAL;
        }
        ALOGD("%s: Mixer ctl set for %s success", __func__, mixer_ctl_name);
    } else {
        for (i = 0; i < (int)pinfo->op_channels; i++) {
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %d %s %d",
                    mixer_name_prefix, pcm_device_id, mixer_name_suffix, i+1);

            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
            if (!ctl) {
                ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                      __func__, mixer_ctl_name);
                 return -EINVAL;
            }
            err = mixer_ctl_set_array(ctl,
                                      &params->coeffs[pinfo->ip_channels * i],
                                      pinfo->ip_channels);
            if (err) {
                ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
                return -EINVAL;
            }
        }
    }
    return 0;
}

static void set_custom_mtmx_params_v2(struct audio_device *adev,
                                      struct audio_custom_mtmx_params_info *pinfo,
                                      int pcm_device_id, bool enable)
{
    struct mixer_ctl *ctl = NULL;
    char *mixer_name_prefix = "AudStr";
    char *mixer_name_suffix = "ChMixer Cfg";
    char mixer_ctl_name[128] = {0};
    int chmixer_cfg[5] = {0}, len = 0;
    int be_id = -1, err = 0;

    be_id = platform_get_snd_device_backend_index(pinfo->snd_device);

    ALOGI("%s: ip_channels %d,op_channels %d,pcm_device_id %d,be_id %d",
          __func__, pinfo->ip_channels, pinfo->op_channels, pcm_device_id, be_id);

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "%s %d %s", mixer_name_prefix, pcm_device_id, mixer_name_suffix);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return;
    }
    chmixer_cfg[len++] = enable ? 1 : 0;
    chmixer_cfg[len++] = 0; /* rule index */
    chmixer_cfg[len++] = pinfo->ip_channels;
    chmixer_cfg[len++] = pinfo->op_channels;
    chmixer_cfg[len++] = be_id + 1;

    err = mixer_ctl_set_array(ctl, chmixer_cfg, len);
    if (err)
        ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
}

static struct audio_custom_mtmx_params *update_channel_weightage_params(audio_channel_mask_t channel_mask,
                                        struct audio_out_channel_map_param *channel_map_param,
                                        struct audio_device_config_param *adev_device_cfg_ptr)
{
    struct audio_custom_mtmx_params *params = NULL;
    uint32_t ip_channels = audio_channel_count_from_out_mask(channel_mask);
    uint8_t *input_channel_map = NULL;
    uint32_t size = sizeof(struct audio_custom_mtmx_params), op_channels = 0;
    int i = 0, j = 0;

    if (!channel_map_param || !adev_device_cfg_ptr) {
        ALOGE("%s: invalid params", __func__);
        return NULL;
    }

    input_channel_map = &channel_map_param->channel_map[0];
    op_channels = adev_device_cfg_ptr->dev_cfg_params.channels;

    /*
     * Allocate memory for coefficients in audio_custom_mtmx_params.
     * Coefficent in audio_custom_mtmx_params is of type uint32_t.
     */
    size += sizeof(uint32_t) * ip_channels * op_channels;
    params = (struct audio_custom_mtmx_params *) calloc(1, size);

    if (!params) {
        ALOGE("%s: failed to alloc mem", __func__);
        return NULL;
    }

    params->info.ip_channels = ip_channels;
    params->info.op_channels = op_channels;

    for (i = 0; i < (int)op_channels; i++) {
        for (j = 0; j < (int)ip_channels; j++) {
            if (adev_device_cfg_ptr->dev_cfg_params.channel_map[i] == input_channel_map[j])
                params->coeffs[ip_channels * i + j] = Q14_GAIN_UNITY;
            ALOGV("%s: op %d ip %d wght %d", __func__, i, j, params->coeffs[ip_channels * i + j]);
        }
    }

    return params;
}

void audio_extn_set_custom_mtmx_params_v2(struct audio_device *adev,
                                        struct audio_usecase *usecase,
                                        bool enable)
{
    struct audio_custom_mtmx_params_info info = {0};
    struct audio_custom_mtmx_params *params = NULL;
    int num_devices = 0, pcm_device_id = -1, i = 0, ret = 0;
    snd_device_t new_snd_devices[SND_DEVICE_OUT_END] = {0};
    struct audio_backend_cfg backend_cfg = {0};
    uint32_t feature_id = 0, idx = 0;
    struct audio_device_config_param *adev_device_cfg_ptr = adev->device_cfg_params;
    int backend_idx = DEFAULT_CODEC_BACKEND;
    struct audio_out_channel_map_param *channel_map_param = NULL;
    audio_channel_mask_t channel_mask = 0;

    switch(usecase->type) {
    case PCM_PLAYBACK:
        if (usecase->stream.out) {
            pcm_device_id =
                platform_get_pcm_device_id(usecase->id, PCM_PLAYBACK);
            if (platform_split_snd_device(adev->platform,
                                          usecase->out_snd_device,
                                          &num_devices, new_snd_devices)) {
                new_snd_devices[0] = usecase->out_snd_device;
                num_devices = 1;
            }
        } else {
            ALOGE("%s: invalid output stream for playback usecase id:%d",
                  __func__, usecase->id);
            return;
        }
        break;
    case PCM_CAPTURE:
        if (usecase->stream.in) {
            pcm_device_id =
                platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
            if (platform_split_snd_device(adev->platform,
                                          usecase->in_snd_device,
                                          &num_devices, new_snd_devices)) {
                new_snd_devices[0] = usecase->in_snd_device;
                num_devices = 1;
            }
        } else {
            ALOGE("%s: invalid input stream for capture usecase id:%d",
                  __func__, usecase->id);
            return;
        }
        break;
    default:
        ALOGV("%s: unsupported usecase id:%d", __func__, usecase->id);
        return;
    }

    /*
     * check and update feature_id before this assignment,
     * if features like dual_mono is enabled and overrides the default(i.e. 0).
     */
    info.id = feature_id;
    info.usecase_id[0] = usecase->id;
    for (i = 0, ret = 0; i < num_devices; i++) {
        backend_idx = platform_get_backend_index(new_snd_devices[i]);
        adev_device_cfg_ptr += backend_idx;
        if (adev_device_cfg_ptr && adev_device_cfg_ptr->use_client_dev_cfg &&
                (usecase->type == PCM_PLAYBACK)) {
            channel_mask = usecase->stream.out->channel_mask;
            channel_map_param = &usecase->stream.out->channel_map_param;
            params = update_channel_weightage_params(channel_mask,
                                     channel_map_param, adev_device_cfg_ptr);
            if (!params)
                return;

            if (enable) {
                ret = update_custom_mtmx_coefficients_v2(adev, params, pcm_device_id);
                if (ret < 0) {
                    ALOGE("%s: error updating mtmx coeffs err:%d", __func__, ret);
                    free(params);
                    return;
                }
            }

            params->info.snd_device = new_snd_devices[i];
            set_custom_mtmx_params_v2(adev, &params->info, pcm_device_id, enable);
            free(params);
        } else {
            info.snd_device = new_snd_devices[i];
            platform_get_codec_backend_cfg(adev, info.snd_device, &backend_cfg);
            if (usecase->type == PCM_PLAYBACK) {
                info.ip_channels = audio_channel_count_from_out_mask(
                                       usecase->stream.out->channel_mask);
                info.op_channels = backend_cfg.channels;
            } else {
                info.ip_channels = backend_cfg.channels;
                info.op_channels = audio_channel_count_from_in_mask(
                                       usecase->stream.in->channel_mask);
            }

            params = platform_get_custom_mtmx_params(adev->platform, &info, &idx);
            if (params) {
                if (enable)
                    ret = update_custom_mtmx_coefficients_v2(adev, params,
                                                          pcm_device_id);
                if (ret < 0)
                    ALOGE("%s: error updating mtmx coeffs err:%d", __func__, ret);
                else
                   set_custom_mtmx_params_v2(adev, &info, pcm_device_id, enable);
            }
        }
    }
}

static int set_custom_mtmx_output_channel_map(struct audio_device *adev,
                                              char *mixer_name_prefix,
                                              uint32_t ch_count,
                                              bool enable)
{
    struct mixer_ctl *ctl = NULL;
    char mixer_ctl_name[128] = {0};
    int ret = 0;
    int channel_map[AUDIO_MAX_DSP_CHANNELS] = {0};

    ALOGV("%s channel_count %d", __func__, ch_count);

    if (!enable) {
        ALOGV("%s: reset output channel map", __func__);
        goto exit;
    }

    switch (ch_count) {
    case 2:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        break;
    case 4:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_LS;
        channel_map[3] = PCM_CHANNEL_RS;
        break;
    case 6:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_LFE;
        channel_map[3] = PCM_CHANNEL_FC;
        channel_map[4] = PCM_CHANNEL_LS;
        channel_map[5] = PCM_CHANNEL_RS;
        break;
    case 8:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_LFE;
        channel_map[3] = PCM_CHANNEL_FC;
        channel_map[4] = PCM_CHANNEL_LS;
        channel_map[5] = PCM_CHANNEL_RS;
        channel_map[6] = PCM_CHANNEL_LB;
        channel_map[7] = PCM_CHANNEL_RB;
        break;
    case 10:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_LFE;
        channel_map[3] = PCM_CHANNEL_FC;
        channel_map[4] = PCM_CHANNEL_LB;
        channel_map[5] = PCM_CHANNEL_RB;
        channel_map[6] = PCM_CHANNEL_LS;
        channel_map[7] = PCM_CHANNEL_RS;
        channel_map[8] = PCM_CHANNEL_TFL;
        channel_map[9] = PCM_CHANNEL_TFR;
        break;
    case 12:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_FC;
        channel_map[3] = PCM_CHANNEL_LFE;
        channel_map[4] = PCM_CHANNEL_LB;
        channel_map[5] = PCM_CHANNEL_RB;
        channel_map[6] = PCM_CHANNEL_LS;
        channel_map[7] = PCM_CHANNEL_RS;
        channel_map[8] = PCM_CHANNEL_TFL;
        channel_map[9] = PCM_CHANNEL_TFR;
        channel_map[10] = PCM_CHANNEL_TSL;
        channel_map[11] = PCM_CHANNEL_TSR;
        break;
    case 14:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_LFE;
        channel_map[3] = PCM_CHANNEL_FC;
        channel_map[4] = PCM_CHANNEL_LB;
        channel_map[5] = PCM_CHANNEL_RB;
        channel_map[6] = PCM_CHANNEL_LS;
        channel_map[7] = PCM_CHANNEL_RS;
        channel_map[8] = PCM_CHANNEL_TFL;
        channel_map[9] = PCM_CHANNEL_TFR;
        channel_map[10] = PCM_CHANNEL_TSL;
        channel_map[11] = PCM_CHANNEL_TSR;
        channel_map[12] = PCM_CHANNEL_FLC;
        channel_map[13] = PCM_CHANNEL_FRC;
        break;
    case 16:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_FC;
        channel_map[3] = PCM_CHANNEL_LFE;
        channel_map[4] = PCM_CHANNEL_LB;
        channel_map[5] = PCM_CHANNEL_RB;
        channel_map[6] = PCM_CHANNEL_LS;
        channel_map[7] = PCM_CHANNEL_RS;
        channel_map[8] = PCM_CHANNEL_TFL;
        channel_map[9] = PCM_CHANNEL_TFR;
        channel_map[10] = PCM_CHANNEL_TSL;
        channel_map[11] = PCM_CHANNEL_TSR;
        channel_map[12] = PCM_CHANNEL_FLC;
        channel_map[13] = PCM_CHANNEL_FRC;
        channel_map[14] = PCM_CHANNEL_RLC;
        channel_map[15] = PCM_CHANNEL_RRC;
        break;
    default:
        ALOGE("%s: unsupported channels(%d) for setting channel map",
               __func__, ch_count);
        return -EINVAL;
    }

exit:
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s",
             mixer_name_prefix, "Output Channel Map");

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ret = mixer_ctl_set_array(ctl, channel_map, ch_count);
    return ret;
}

static int update_custom_mtmx_coefficients_v1(struct audio_device *adev,
                                           struct audio_custom_mtmx_params *params,
                                           struct audio_custom_mtmx_in_params *in_params,
                                           int pcm_device_id,
                                           usecase_type_t type,
                                           bool enable,
                                           uint32_t idx)
{
    struct mixer_ctl *ctl = NULL;
    char mixer_ctl_name[128] = {0};
    struct audio_custom_mtmx_params_info *pinfo = &params->info;
    char mixer_name_prefix[100];
    int i = 0, err = 0, rule = 0;
    uint32_t mtrx_row_cnt = 0, mtrx_column_cnt = 0;
    int reset_coeffs[AUDIO_MAX_DSP_CHANNELS] = {0};

    ALOGI("%s: ip_channels %d, op_channels %d, pcm_device_id %d, usecase type %d, enable %d",
          __func__, pinfo->ip_channels, pinfo->op_channels, pcm_device_id,
          type, enable);

    if (pinfo->fe_id[idx] == 0) {
        ALOGE("%s: Error. no front end defined", __func__);
        return -EINVAL;
    }

    snprintf(mixer_name_prefix, sizeof(mixer_name_prefix), "%s%d",
             "MultiMedia", pinfo->fe_id[idx]);
    /*
     * Enable/Disable channel mixer.
     * If enable, use params and in_params to configure mixer.
     * If disable, reset previously configured mixer.
    */
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s",
             mixer_name_prefix, "Channel Mixer");

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    if (enable)
        err = mixer_ctl_set_enum_by_string(ctl, "Enable");
    else
        err = mixer_ctl_set_enum_by_string(ctl, "Disable");

    if (err) {
        ALOGE("%s: ERROR. %s channel mixer failed", __func__,
                enable ? "Enable" : "Disable");
        return -EINVAL;
    }

    /* Configure output channels of channel mixer */
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s",
             mixer_name_prefix, "Channels");

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    mtrx_row_cnt = pinfo->op_channels;
    mtrx_column_cnt = pinfo->ip_channels;

    if (enable)
        err = mixer_ctl_set_value(ctl, 0, mtrx_row_cnt);
    else
        err = mixer_ctl_set_value(ctl, 0, 0);

    if (err) {
        ALOGE("%s: ERROR. %s mixer output channels failed", __func__,
              enable ? "Set" : "Reset");
        return -EINVAL;
    }

    if (mtrx_row_cnt > AUDIO_MAX_DSP_CHANNELS) {
        ALOGE("%s: unsupported channels(%d) for setting channel map",
              __func__, mtrx_row_cnt);
        return -EINVAL;
    }

    /* To keep output channel map in sync with asm driver channel mapping */
    err  = set_custom_mtmx_output_channel_map(adev, mixer_name_prefix, mtrx_row_cnt,
                                       enable);
    if (err) {
        ALOGE("%s: ERROR. %s mtmx output channel map failed", __func__,
              enable ? "Set" : "Reset");
        return -EINVAL;
    }

    /* Send channel mixer rule */
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s",
             mixer_name_prefix, "Channel Rule");

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    mixer_ctl_set_value(ctl, 0, rule);

    /* Send channel coefficients for each output channel */
    for (i = 0; i < (int)mtrx_row_cnt; i++) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s%d",
                 mixer_name_prefix, "Output Channel", i+1);
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }

        if (enable)
            err = mixer_ctl_set_array(ctl,
                                  &params->coeffs[mtrx_column_cnt * i],
                                  mtrx_column_cnt);
        else
            err = mixer_ctl_set_array(ctl,
                                  reset_coeffs,
                                  mtrx_column_cnt);
        if (err) {
            ALOGE("%s: ERROR. %s coefficients failed for output channel %d",
                   __func__, enable ? "Set" : "Reset", i);
            return -EINVAL;
        }
    }

    /* Configure backend interfaces with information provided in xml */
    i = 0;
    while (in_params->in_ch_info[i].ch_count != 0) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s%d",
                 mixer_name_prefix, "Channel", i+1);
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }
        if (enable) {
            ALOGD("%s: mixer %s, interface %s", __func__, mixer_ctl_name,
                   in_params->in_ch_info[i].hw_interface);
            err = mixer_ctl_set_enum_by_string(ctl,
                      in_params->in_ch_info[i].hw_interface);
        } else {
            err = mixer_ctl_set_enum_by_string(ctl, "ZERO");
        }

        if (err) {
            ALOGE("%s: ERROR. %s channel backend interface failed", __func__,
                   enable ? "Set" : "Reset");
            return -EINVAL;
        }
        i++;
    }

    return 0;
}


void audio_extn_set_custom_mtmx_params_v1(struct audio_device *adev,
                                       struct audio_usecase *usecase,
                                       bool enable)
{
    struct audio_custom_mtmx_params_info info = {0};
    struct audio_custom_mtmx_params *params = NULL;
    struct audio_custom_mtmx_in_params_info in_info = {0};
    struct audio_custom_mtmx_in_params *in_params = NULL;
    int pcm_device_id = -1, ret = 0;
    uint32_t feature_id = 0, idx = 0;

    switch(usecase->type) {
    case PCM_CAPTURE:
        if (usecase->stream.in) {
            pcm_device_id =
                platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
            info.snd_device = usecase->in_snd_device;
        } else {
            ALOGE("%s: invalid input stream for capture usecase id:%d",
                  __func__, usecase->id);
            return;
        }
        break;
    case PCM_PLAYBACK:
    default:
        ALOGV("%s: unsupported usecase id:%d", __func__, usecase->id);
        return;
    }

    ALOGD("%s: snd device %d", __func__, info.snd_device);
    info.id = feature_id;
    info.usecase_id[0] = usecase->id;
    info.op_channels = audio_channel_count_from_in_mask(
                                usecase->stream.in->channel_mask);

    in_info.usecase_id[0] = info.usecase_id[0];
    in_info.op_channels = info.op_channels;
    in_params = platform_get_custom_mtmx_in_params(adev->platform, &in_info);
    if (!in_params) {
        ALOGE("%s: Could not get in params for usecase %d, channels %d",
               __func__, in_info.usecase_id[0], in_info.op_channels);
        return;
    }

    info.ip_channels = in_params->ip_channels;
    ALOGD("%s: ip channels %d, op channels %d", __func__, info.ip_channels, info.op_channels);

    params = platform_get_custom_mtmx_params(adev->platform, &info, &idx);
    if (params) {
        ret = update_custom_mtmx_coefficients_v1(adev, params, in_params,
                             pcm_device_id, usecase->type, enable, idx);
        if (ret < 0)
            ALOGE("%s: error updating mtmx coeffs err:%d", __func__, ret);
    }
}

snd_device_t audio_extn_get_loopback_snd_device(struct audio_device *adev,
                                                struct audio_usecase *usecase,
                                                int channel_count)
{
    snd_device_t snd_device = SND_DEVICE_NONE;
    struct audio_custom_mtmx_in_params_info in_info = {0};
    struct audio_custom_mtmx_in_params *in_params = NULL;

    if (!adev || !usecase) {
        ALOGE("%s: Invalid params", __func__);
        return snd_device;
    }

    in_info.usecase_id[0] = usecase->id;
    in_info.op_channels = channel_count;
    in_params = platform_get_custom_mtmx_in_params(adev->platform, &in_info);
    if (!in_params) {
        ALOGE("%s: Could not get in params for usecase %d, channels %d",
               __func__, in_info.usecase_id[0], in_info.op_channels);
        return snd_device;
    }

    switch(in_params->mic_ch) {
    case 2:
        snd_device = SND_DEVICE_IN_HANDSET_DMIC_AND_EC_REF_LOOPBACK;
        break;
    case 4:
        snd_device = SND_DEVICE_IN_HANDSET_QMIC_AND_EC_REF_LOOPBACK;
        break;
    case 6:
        snd_device = SND_DEVICE_IN_HANDSET_6MIC_AND_EC_REF_LOOPBACK;
        break;
    case 8:
        snd_device = SND_DEVICE_IN_HANDSET_8MIC_AND_EC_REF_LOOPBACK;
        break;
    default:
        ALOGE("%s: Unsupported mic channels %d",
               __func__, in_params->mic_ch);
        break;
    }

    ALOGD("%s: return snd device %d", __func__, snd_device);
    return snd_device;
}

#ifndef DTS_EAGLE
#define audio_extn_hpx_set_parameters(adev, parms)         (0)
#define audio_extn_hpx_get_parameters(query, reply)  (0)
#define audio_extn_check_and_set_dts_hpx_state(adev)       (0)
#else
void audio_extn_hpx_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};
    char prop[PROPERTY_VALUE_MAX] = "false";
    bool hpx_state = false;
    const char *mixer_ctl_name = "Set HPX OnOff";
    struct mixer_ctl *ctl = NULL;
    ALOGV("%s", __func__);

    property_get("vendor.audio.use.dts_eagle", prop, "0");
    if (strncmp("true", prop, sizeof("true")))
        return;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HPX, value,
                            sizeof(value));
    if (ret >= 0) {
        if (!strncmp("ON", value, sizeof("ON")))
            hpx_state = true;

        if (hpx_state == aextnmod.hpx_enabled)
            return;

        aextnmod.hpx_enabled = hpx_state;
        /* set HPX state on stream pp */
        if (adev->offload_effects_set_hpx_state != NULL)
            adev->offload_effects_set_hpx_state(hpx_state);

        audio_extn_dts_eagle_fade(adev, aextnmod.hpx_enabled, NULL);
        /* set HPX state on device pp */
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (ctl)
            mixer_ctl_set_value(ctl, 0, aextnmod.hpx_enabled);
    }
}

static int audio_extn_hpx_get_parameters(struct str_parms *query,
                                       struct str_parms *reply)
{
    int ret;
    char value[32]={0};

    ALOGV("%s: hpx %d", __func__, aextnmod.hpx_enabled);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_HPX, value,
                            sizeof(value));
    if (ret >= 0) {
        if (aextnmod.hpx_enabled)
            str_parms_add_str(reply, AUDIO_PARAMETER_HPX, "ON");
        else
            str_parms_add_str(reply, AUDIO_PARAMETER_HPX, "OFF");
    }
    return ret;
}

void audio_extn_check_and_set_dts_hpx_state(const struct audio_device *adev)
{
    char prop[PROPERTY_VALUE_MAX];
    property_get("vendor.audio.use.dts_eagle", prop, "0");
    if (strncmp("true", prop, sizeof("true")))
        return;
    if (adev->offload_effects_set_hpx_state)
        adev->offload_effects_set_hpx_state(aextnmod.hpx_enabled);
}
#endif

/* Affine AHAL thread to CPU core */
void audio_extn_set_cpu_affinity()
{
    cpu_set_t cpuset;
    struct sched_param sched_param;
    int policy = SCHED_FIFO, rc = 0;

    ALOGV("%s: Set CPU affinity for read thread", __func__);
    CPU_ZERO(&cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
        ALOGE("%s: CPU Affinity allocation failed for Capture thread",
               __func__);

    sched_param.sched_priority = sched_get_priority_min(policy);
    rc = sched_setscheduler(0, policy, &sched_param);
    if (rc != 0)
         ALOGE("%s: Failed to set realtime priority", __func__);
}

// START: VBAT =============================================================
void vbat_feature_init(bool is_feature_enabled)
{
    audio_extn_vbat_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature VBAT is %s ----",
                  __func__, is_feature_enabled ? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_vbat_enabled(void)
{
    if (!audio_extn_vbat_enabled)
        return false;

    ALOGD("%s: status: %d", __func__, aextnmod.vbat_enabled);
    return (aextnmod.vbat_enabled ? true: false);
}

bool audio_extn_can_use_vbat(void)
{
    if (!audio_extn_vbat_enabled)
        return false;

    char prop_vbat_enabled[PROPERTY_VALUE_MAX] = "false";

    property_get("persist.vendor.audio.vbat.enabled", prop_vbat_enabled, "0");
    if (!strncmp("true", prop_vbat_enabled, 4)) {
        aextnmod.vbat_enabled = 1;
    }

    ALOGD("%s: vbat.enabled property is set to %s", __func__, prop_vbat_enabled);
    return (aextnmod.vbat_enabled ? true: false);
}

bool audio_extn_is_bcl_enabled(void)
{
    if (!audio_extn_vbat_enabled)
        return false;

    ALOGD("%s: status: %d", __func__, aextnmod.bcl_enabled);
    return (aextnmod.bcl_enabled ? true: false);
}

bool audio_extn_can_use_bcl(void)
{
    if (!audio_extn_vbat_enabled)
        return false;

    char prop_bcl_enabled[PROPERTY_VALUE_MAX] = "false";

    property_get("persist.vendor.audio.bcl.enabled", prop_bcl_enabled, "0");
    if (!strncmp("true", prop_bcl_enabled, 4)) {
        aextnmod.bcl_enabled = 1;
    }

    ALOGD("%s: bcl.enabled property is set to %s", __func__, prop_bcl_enabled);
    return (aextnmod.bcl_enabled ? true: false);
}

// START: ANC_HEADSET -------------------------------------------------------
void anc_headset_feature_init(bool is_feature_enabled)
{
    audio_extn_anc_headset_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature FM_POWER_OPT is %s----", __func__,
                                    is_feature_enabled? "ENABLED": "NOT ENABLED");

}

bool audio_extn_get_anc_enabled(void)
{
    ALOGD("%s: anc_enabled:%d", __func__, aextnmod.anc_enabled);
    return (aextnmod.anc_enabled ? true: false);
}

bool audio_extn_should_use_handset_anc(int in_channels)
{
    if (audio_extn_anc_headset_feature_enabled) {
        char prop_aanc[PROPERTY_VALUE_MAX] = "false";

        property_get("persist.vendor.audio.aanc.enable", prop_aanc, "0");
        if (!strncmp("true", prop_aanc, 4)) {
            ALOGD("%s: AANC enabled in the property", __func__);
            aextnmod.aanc_enabled = 1;
        }

        return (aextnmod.aanc_enabled && aextnmod.anc_enabled
                && (in_channels == 1));
    }
    return false;
}

bool audio_extn_should_use_fb_anc(void)
{
  char prop_anc[PROPERTY_VALUE_MAX] = "feedforward";

  if (audio_extn_anc_headset_feature_enabled) {
      property_get("persist.vendor.audio.headset.anc.type", prop_anc, "0");
      if (!strncmp("feedback", prop_anc, sizeof("feedback"))) {
        ALOGD("%s: FB ANC headset type enabled\n", __func__);
        return true;
      }
  }
  return false;
}

void audio_extn_set_aanc_noise_level(struct audio_device *adev,
                                     struct str_parms *parms)
{
    int ret;
    char value[32] = {0};
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "AANC Noise Level";

    if(audio_extn_anc_headset_feature_enabled) {
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_AANC_NOISE_LEVEL, value,
                            sizeof(value));
        if (ret >= 0) {
            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
            if (ctl)
                mixer_ctl_set_value(ctl, 0, atoi(value));
            else
                ALOGW("%s: Not able to get mixer ctl: %s",
                      __func__, mixer_ctl_name);
        }
    }
}

void audio_extn_set_anc_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int ret;
    char value[32] ={0};
    struct listnode *node;
    struct audio_usecase *usecase;
    struct str_parms *query_44_1;
    struct str_parms *reply_44_1;
    struct str_parms *parms_disable_44_1;

    if(!audio_extn_anc_headset_feature_enabled)
        return;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_ANC, value,
                            sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, "true") == 0)
            aextnmod.anc_enabled = true;
        else
            aextnmod.anc_enabled = false;

        /* Store current 44.1 configuration and disable it temporarily before
         * changing ANC state.
         * Since 44.1 playback is not allowed with anc on.
         * If ANC switch is done when 44.1 is active three devices would need
         * sequencing 1. "headphones-44.1", 2. "headphones-anc" and
         * 3. "headphones".
         * Note: Enable/diable of anc would affect other two device's state.
         */
        query_44_1 = str_parms_create_str(AUDIO_PARAMETER_KEY_NATIVE_AUDIO);
        reply_44_1 = str_parms_create();
        if (!query_44_1 || !reply_44_1) {
            if (query_44_1) {
                str_parms_destroy(query_44_1);
            }
            if (reply_44_1) {
                str_parms_destroy(reply_44_1);
            }

            ALOGE("%s: param creation failed", __func__);
            return;
        }

        platform_get_parameters(adev->platform, query_44_1, reply_44_1);

        parms_disable_44_1 = str_parms_create();
        if (!parms_disable_44_1) {
            str_parms_destroy(query_44_1);
            str_parms_destroy(reply_44_1);
            ALOGE("%s: param creation failed for parms_disable_44_1", __func__);
            return;
        }

        str_parms_add_str(parms_disable_44_1, AUDIO_PARAMETER_KEY_NATIVE_AUDIO, "false");
        platform_set_parameters(adev->platform, parms_disable_44_1);
        str_parms_destroy(parms_disable_44_1);

        // Refresh device selection for anc playback
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->type != PCM_CAPTURE) {
                if (usecase->stream.out->devices == \
                    AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
                    usecase->stream.out->devices ==  \
                    AUDIO_DEVICE_OUT_WIRED_HEADSET ||
                    usecase->stream.out->devices ==  \
                    AUDIO_DEVICE_OUT_EARPIECE) {
                        select_devices(adev, usecase->id);
                        ALOGV("%s: switching device completed", __func__);
                        break;
                }
            }
        }

        // Restore 44.1 configuration on top of updated anc state
        platform_set_parameters(adev->platform, reply_44_1);
        str_parms_destroy(query_44_1);
        str_parms_destroy(reply_44_1);
    }

    ALOGD("%s: anc_enabled:%d", __func__, aextnmod.anc_enabled);
}
// END: ANC_HEADSET -------------------------------------------------------

static int32_t afe_proxy_set_channel_mapping(struct audio_device *adev,
                                                     int channel_count)
{
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "Playback Device Channel Map";
    long set_values[8] = {0};
    int ret;
    ALOGV("%s channel_count:%d",__func__, channel_count);

    switch (channel_count) {
    case 2:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        break;
    case 6:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        set_values[2] = PCM_CHANNEL_FC;
        set_values[3] = PCM_CHANNEL_LFE;
        set_values[4] = PCM_CHANNEL_LS;
        set_values[5] = PCM_CHANNEL_RS;
        break;
    case 8:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        set_values[2] = PCM_CHANNEL_FC;
        set_values[3] = PCM_CHANNEL_LFE;
        set_values[4] = PCM_CHANNEL_LS;
        set_values[5] = PCM_CHANNEL_RS;
        set_values[6] = PCM_CHANNEL_LB;
        set_values[7] = PCM_CHANNEL_RB;
        break;
    default:
        ALOGE("unsupported channels(%d) for setting channel map",
                                                    channel_count);
        return -EINVAL;
    }

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    ALOGV("AFE: set mapping(%ld %ld %ld %ld %ld %ld %ld %ld) for channel:%d",
        set_values[0], set_values[1], set_values[2], set_values[3], set_values[4],
        set_values[5], set_values[6], set_values[7], channel_count);
    ret = mixer_ctl_set_array(ctl, set_values, channel_count);
    return ret;
}

int32_t audio_extn_set_afe_proxy_channel_mixer(struct audio_device *adev,
                                    int channel_count)
{
    int32_t ret = 0;
    const char *channel_cnt_str = NULL;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "PROXY_RX Channels";

    if (!audio_feature_manager_is_feature_enabled(AFE_PROXY)) {
        ALOGW("%s: AFE_PROXY is disabled", __func__);
        return -ENOSYS;
    }

    ALOGD("%s: entry", __func__);
    /* use the existing channel count set by hardware params to
    configure the back end for stereo as usb/a2dp would be
    stereo by default */
    ALOGD("%s: channels = %d", __func__, channel_count);
    switch (channel_count) {
    case 8: channel_cnt_str = "Eight"; break;
    case 7: channel_cnt_str = "Seven"; break;
    case 6: channel_cnt_str = "Six"; break;
    case 5: channel_cnt_str = "Five"; break;
    case 4: channel_cnt_str = "Four"; break;
    case 3: channel_cnt_str = "Three"; break;
    default: channel_cnt_str = "Two"; break;
    }

    if(channel_count >= 2 && channel_count <= 8) {
       ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
       if (!ctl) {
            ALOGE("%s: could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
        return -EINVAL;
       }
    }
    mixer_ctl_set_enum_by_string(ctl, channel_cnt_str);

    if (channel_count == 6 || channel_count == 8 || channel_count == 2) {
        ret = afe_proxy_set_channel_mapping(adev, channel_count);
    } else {
        ALOGE("%s: set unsupported channel count(%d)",  __func__, channel_count);
        ret = -EINVAL;
    }

    ALOGD("%s: exit", __func__);
    return ret;
}

void audio_extn_set_afe_proxy_parameters(struct audio_device *adev,
                                         struct str_parms *parms)
{
    int ret, val;
    char value[32]={0};

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_WFD, value,
                            sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        aextnmod.proxy_channel_num = val;
        adev->cur_wfd_channels = val;
        ALOGD("%s: channel capability set to: %d", __func__,
               aextnmod.proxy_channel_num);
    }
}

int audio_extn_get_afe_proxy_parameters(const struct audio_device *adev,
                                        struct str_parms *query,
                                        struct str_parms *reply)
{
    int ret, val = 0;
    char value[32]={0};

    ret = str_parms_get_str(query, AUDIO_PARAMETER_CAN_OPEN_PROXY, value,
                            sizeof(value));
    if (ret >= 0) {
        val = (adev->allow_afe_proxy_usage ? 1: 0);
        str_parms_add_int(reply, AUDIO_PARAMETER_CAN_OPEN_PROXY, val);
    }
    ALOGV("%s: called ... can_use_proxy %d", __func__, val);
    return 0;
}

/* must be called with hw device mutex locked */
int32_t audio_extn_read_afe_proxy_channel_masks(struct stream_out *out)
{
    int ret = 0;
    int channels = aextnmod.proxy_channel_num;

    if (!audio_feature_manager_is_feature_enabled(AFE_PROXY)) {
        ALOGW("%s: AFE_PROXY is disabled", __func__);
        return -ENOSYS;
    }

    switch (channels) {
        /*
         * Do not handle stereo output in Multi-channel cases
         * Stereo case is handled in normal playback path
         */
    case 6:
        ALOGV("%s: AFE PROXY supports 5.1", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        break;
    case 8:
        ALOGV("%s: AFE PROXY supports 5.1 and 7.1 channels", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        out->supported_channel_masks[1] = AUDIO_CHANNEL_OUT_7POINT1;
        break;
    default:
        ALOGE("AFE PROXY does not support multi channel playback");
        ret = -ENOSYS;
        break;
    }
    return ret;
}

int32_t audio_extn_get_afe_proxy_channel_count()
{

    if (!audio_feature_manager_is_feature_enabled(AFE_PROXY)) {
        ALOGW("%s: AFE_PROXY is disabled", __func__);
        return -ENOSYS;
    }

    return aextnmod.proxy_channel_num;
}

static int get_active_offload_usecases(const struct audio_device *adev,
                                       struct str_parms *query,
                                       struct str_parms *reply)
{
    int ret, count = 0;
    char value[32]={0};
    struct listnode *node;
    struct audio_usecase *usecase;

    ALOGV("%s", __func__);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE, value,
                            sizeof(value));
    if (ret >= 0) {
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (is_offload_usecase(usecase->id))
                count++;
        }
        ALOGV("%s, number of active offload usecases: %d", __func__, count);
        str_parms_add_int(reply, AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE, count);
    }
    return ret;
}

void compress_meta_data_feature_init(bool is_featuer_enabled)
{
    is_compress_meta_data_enabled = is_featuer_enabled;
}

bool if_compress_meta_data_feature_enabled()
{
    return is_compress_meta_data_enabled;
}

//START: USB_OFFLOAD ==========================================================
// LIB is part of hal lib, so no need for dlopen and getting function pointer
// rather have function declared here

void usb_init(void *adev);
void usb_deinit();
void usb_add_device(audio_devices_t device, int card);
void usb_remove_device(audio_devices_t device, int card);
bool usb_is_config_supported(unsigned int *bit_width,
                                        unsigned int *sample_rate,
                                        unsigned int *ch,
                                        bool is_playback);
int usb_enable_sidetone(int device, bool enable);
void usb_set_sidetone_gain(struct str_parms *parms,
                                     char *value, int len);
bool usb_is_capture_supported();
int usb_get_max_channels(bool playback);
int usb_get_max_bit_width(bool playback);
int usb_get_sup_sample_rates(bool type, uint32_t *sr, uint32_t l);
bool usb_is_tunnel_supported();
bool usb_alive(int card);
bool usb_connected(struct str_parms *parms);
unsigned long usb_find_service_interval(bool min, bool playback);
int usb_altset_for_service_interval(bool is_playback,
                                               unsigned long service_interval,
                                               uint32_t *bit_width,
                                               uint32_t *sample_rate,
                                               uint32_t *channel_count);
int usb_set_service_interval(bool playback,
                                        unsigned long service_interval,
                                        bool *reconfig);
int usb_get_service_interval(bool playback,
                                        unsigned long *service_interval);
int usb_check_and_set_svc_int(struct audio_usecase *uc_info,
                                         bool starting_output_stream);
bool usb_is_reconfig_req();
void usb_set_reconfig(bool is_required);

static bool is_usb_offload_enabled = false;
static bool is_usb_burst_mode_enabled = false;
static bool is_usb_sidetone_vol_enabled = false;

void usb_offload_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    is_usb_offload_enabled = is_feature_enabled;
}

void usb_offload_burst_mode_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    is_usb_burst_mode_enabled = is_feature_enabled;
}

void usb_offload_sidetone_volume_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    is_usb_sidetone_vol_enabled = is_feature_enabled;
}

bool audio_extn_usb_is_sidetone_volume_enabled()
{
    return is_usb_sidetone_vol_enabled;
}

void audio_extn_usb_init(void *adev)
{
    if (is_usb_offload_enabled)
        usb_init(adev);
}


void audio_extn_usb_deinit()
{
    if (is_usb_offload_enabled)
        usb_deinit();
}

void audio_extn_usb_add_device(audio_devices_t device, int card)
{
    if (is_usb_offload_enabled)
        usb_add_device(device, card);
}

void audio_extn_usb_remove_device(audio_devices_t device, int card)
{
    if (is_usb_offload_enabled)
        usb_remove_device(device, card);
}

bool audio_extn_usb_is_config_supported(unsigned int *bit_width,
                                        unsigned int *sample_rate,
                                        unsigned int *ch,
                                        bool is_playback)
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_is_config_supported(bit_width, sample_rate, ch, is_playback);

    return ret_val;
}

int audio_extn_usb_enable_sidetone(int device, bool enable)
{
    int ret_val = 0;
    if (is_usb_offload_enabled)
        ret_val = usb_enable_sidetone(device, enable);

    return ret_val;
}

void audio_extn_usb_set_sidetone_gain(struct str_parms *parms,
                                      char *value, int len)
{
    if (is_usb_offload_enabled)
        usb_set_sidetone_gain(parms, value, len);
}

bool audio_extn_usb_is_capture_supported()
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_is_capture_supported();

    return ret_val;
}

int audio_extn_usb_get_max_channels(bool playback)
{
    int ret_val = 0;
    if (is_usb_offload_enabled)
        ret_val = usb_get_max_channels(playback);

    return ret_val;
}

int audio_extn_usb_get_max_bit_width(bool playback)
{
    int ret_val = 0;
    if (is_usb_offload_enabled)
        ret_val = usb_get_max_bit_width(playback);

    return ret_val;
}


int audio_extn_usb_get_sup_sample_rates(bool type, uint32_t *sr, uint32_t l)
{
    int ret_val = 0;
    if (is_usb_offload_enabled)
        ret_val = usb_get_sup_sample_rates(type, sr, l);

    return ret_val;
}

bool audio_extn_usb_is_tunnel_supported()
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_is_tunnel_supported();

    return ret_val;
}

bool audio_extn_usb_alive(int card)
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_alive(card);

    return ret_val;
}

bool audio_extn_usb_connected(struct str_parms *parms)
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_connected(parms);

    return ret_val;
}

unsigned long audio_extn_usb_find_service_interval(bool min, bool playback)
{
    unsigned long ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_find_service_interval(min, playback);

    return ret_val;
}

int audio_extn_usb_altset_for_service_interval(bool is_playback,
                                               unsigned long service_interval,
                                               uint32_t *bit_width,
                                               uint32_t *sample_rate,
                                               uint32_t *channel_count)
{
    int ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_altset_for_service_interval(is_playback, service_interval,
                                         bit_width, sample_rate, channel_count);

    return ret_val;
}

int audio_extn_usb_set_service_interval(bool playback,
                                        unsigned long service_interval,
                                        bool *reconfig)
{
    int ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_set_service_interval(playback, service_interval, reconfig);

    return ret_val;
}

int audio_extn_usb_get_service_interval(bool playback,
                                        unsigned long *service_interval)
{
    int ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_get_service_interval(playback, service_interval);

    return ret_val;
}

int audio_extn_usb_check_and_set_svc_int(struct audio_usecase *uc_info,
                                         bool starting_output_stream)
{
    int ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_check_and_set_svc_int(uc_info, starting_output_stream);

    return ret_val;
}

bool audio_extn_usb_is_reconfig_req()
{
    bool ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_is_reconfig_req();

    return ret_val;
}

void audio_extn_usb_set_reconfig(bool is_required)
{
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        usb_set_reconfig(is_required);
}

//END: USB_OFFLOAD ===========================================================

//START: SPEAKER_PROTECTION ==========================================================
#ifdef __LP64__
#if LINUX_ENABLED
#define SPKR_PROT_LIB_PATH  "/usr/lib64/audio.spkr.prot.so"
#define CIRRUS_SPKR_PROT_LIB_PATH  "/usr/lib64/audio.external.spkr.prot.so"
#else
#define SPKR_PROT_LIB_PATH  "/vendor/lib64/libspkrprot.so"
#define CIRRUS_SPKR_PROT_LIB_PATH  "/vendor/lib64/libcirrusspkrprot.so"
#endif
#else
#if LINUX_ENABLED
#define SPKR_PROT_LIB_PATH  "/usr/lib/audio.spkr.prot.so"
#define CIRRUS_SPKR_PROT_LIB_PATH  "/usr/lib/audio.external.spkr.prot.so"
#else
#define SPKR_PROT_LIB_PATH  "/vendor/lib/libspkrprot.so"
#define CIRRUS_SPKR_PROT_LIB_PATH  "/vendor/lib/libcirrusspkrprot.so"
#endif
#endif

static void *spkr_prot_lib_handle = NULL;

typedef void (*spkr_prot_init_t)(void *, spkr_prot_init_config_t);
static spkr_prot_init_t spkr_prot_init;

typedef int (*spkr_prot_deinit_t)();
static spkr_prot_deinit_t spkr_prot_deinit;

typedef int (*spkr_prot_start_processing_t)(snd_device_t);
static spkr_prot_start_processing_t spkr_prot_start_processing;

typedef void (*spkr_prot_stop_processing_t)(snd_device_t);
static spkr_prot_stop_processing_t spkr_prot_stop_processing;

typedef bool (*spkr_prot_is_enabled_t)();
static spkr_prot_is_enabled_t spkr_prot_is_enabled;

typedef void (*spkr_prot_calib_cancel_t)(void *);
static spkr_prot_calib_cancel_t spkr_prot_calib_cancel;

typedef void (*spkr_prot_set_parameters_t)(struct str_parms *,
                                           char *, int);
static spkr_prot_set_parameters_t spkr_prot_set_parameters;

typedef int (*fbsp_set_parameters_t)(struct str_parms *);
static fbsp_set_parameters_t fbsp_set_parameters;

typedef int (*fbsp_get_parameters_t)(struct str_parms *,
                                   struct str_parms *);
static fbsp_get_parameters_t fbsp_get_parameters;

typedef int (*get_spkr_prot_snd_device_t)(snd_device_t);
static get_spkr_prot_snd_device_t get_spkr_prot_snd_device;

void spkr_prot_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
#if LINUX_ENABLED
        spkr_prot_lib_handle = dlopen(SPKR_PROT_LIB_PATH, RTLD_NOW);
#else
        if (is_running_on_stock_version)
            spkr_prot_lib_handle = dlopen(CIRRUS_SPKR_PROT_LIB_PATH, RTLD_NOW);
        else
            spkr_prot_lib_handle = dlopen(SPKR_PROT_LIB_PATH, RTLD_NOW);
#endif

        if (spkr_prot_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //if mandatoy functions are not found, disble feature

        // Mandatory functions
        if (((spkr_prot_init =
             (spkr_prot_init_t)dlsym(spkr_prot_lib_handle, "spkr_prot_init")) == NULL) ||
            ((spkr_prot_deinit =
             (spkr_prot_deinit_t)dlsym(spkr_prot_lib_handle, "spkr_prot_deinit")) == NULL) ||
            ((spkr_prot_start_processing =
             (spkr_prot_start_processing_t)dlsym(spkr_prot_lib_handle, "spkr_prot_start_processing")) == NULL) ||
            ((spkr_prot_stop_processing =
             (spkr_prot_stop_processing_t)dlsym(spkr_prot_lib_handle, "spkr_prot_stop_processing")) == NULL) ||
            ((spkr_prot_is_enabled =
             (spkr_prot_is_enabled_t)dlsym(spkr_prot_lib_handle, "spkr_prot_is_enabled")) == NULL) ||
            ((spkr_prot_calib_cancel =
             (spkr_prot_calib_cancel_t)dlsym(spkr_prot_lib_handle, "spkr_prot_calib_cancel")) == NULL) ||
            ((get_spkr_prot_snd_device =
             (get_spkr_prot_snd_device_t)dlsym(spkr_prot_lib_handle, "get_spkr_prot_snd_device")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        // options functions, can be NULL

        spkr_prot_set_parameters = NULL;
        fbsp_set_parameters = NULL;
        fbsp_get_parameters = NULL;
        if ((spkr_prot_set_parameters =
             (spkr_prot_set_parameters_t)dlsym(spkr_prot_lib_handle, "spkr_prot_set_parameters")) == NULL) {
            ALOGW("%s: dlsym failed for spkr_prot_set_parameters", __func__);
        }

        if ((fbsp_set_parameters =
             (fbsp_set_parameters_t)dlsym(spkr_prot_lib_handle, "fbsp_set_parameters")) == NULL) {
            ALOGW("%s: dlsym failed for fbsp_set_parameters", __func__);
        }

        if ((fbsp_get_parameters =
             (fbsp_get_parameters_t)dlsym(spkr_prot_lib_handle, "fbsp_get_parameters")) == NULL) {
            ALOGW("%s: dlsym failed for fbsp_get_parameters", __func__);
        }

        ALOGD("%s:: ---- Feature SPKR_PROT is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (spkr_prot_lib_handle) {
        dlclose(spkr_prot_lib_handle);
        spkr_prot_lib_handle = NULL;
    }

    spkr_prot_init = NULL;
    spkr_prot_deinit = NULL;
    spkr_prot_start_processing = NULL;
    spkr_prot_stop_processing = NULL;
    spkr_prot_is_enabled = NULL;
    spkr_prot_calib_cancel = NULL;
    spkr_prot_set_parameters = NULL;
    fbsp_set_parameters = NULL;
    fbsp_get_parameters = NULL;
    get_spkr_prot_snd_device = NULL;

    ALOGW(":: %s: ---- Feature SPKR_PROT is disabled ----", __func__);
    return;
}

void audio_extn_spkr_prot_init(void *adev) {
    if (spkr_prot_init != NULL) {
        // init function pointers
        spkr_prot_init_config_t spkr_prot_config_val;
        spkr_prot_config_val.fp_read_line_from_file = read_line_from_file;
        spkr_prot_config_val.fp_get_usecase_from_list = get_usecase_from_list;
        spkr_prot_config_val.fp_disable_snd_device  = disable_snd_device;
        spkr_prot_config_val.fp_enable_snd_device = enable_snd_device;
        spkr_prot_config_val.fp_disable_audio_route = disable_audio_route;
        spkr_prot_config_val.fp_enable_audio_route = enable_audio_route;
        spkr_prot_config_val.fp_platform_set_snd_device_backend = platform_set_snd_device_backend;
        spkr_prot_config_val.fp_platform_get_snd_device_name_extn = platform_get_snd_device_name_extn;
        spkr_prot_config_val.fp_platform_get_default_app_type_v2 = platform_get_default_app_type_v2;
        spkr_prot_config_val.fp_platform_send_audio_calibration = platform_send_audio_calibration;
        spkr_prot_config_val.fp_platform_get_pcm_device_id = platform_get_pcm_device_id;
        spkr_prot_config_val.fp_platform_get_snd_device_name = platform_get_snd_device_name;
        spkr_prot_config_val.fp_platform_spkr_prot_is_wsa_analog_mode = platform_spkr_prot_is_wsa_analog_mode;
        spkr_prot_config_val.fp_platform_get_vi_feedback_snd_device = platform_get_vi_feedback_snd_device;
        spkr_prot_config_val.fp_platform_get_spkr_prot_snd_device = platform_get_spkr_prot_snd_device;
        spkr_prot_config_val.fp_platform_check_and_set_codec_backend_cfg = platform_check_and_set_codec_backend_cfg;
        spkr_prot_config_val.fp_audio_extn_get_snd_card_split = audio_extn_get_snd_card_split;
        spkr_prot_config_val.fp_audio_extn_is_vbat_enabled = audio_extn_is_vbat_enabled;

        spkr_prot_init(adev, spkr_prot_config_val);
    }

    return;
}

int audio_extn_spkr_prot_deinit() {
    int ret_val = 0;

    if (spkr_prot_deinit != NULL)
        ret_val = spkr_prot_deinit();

    return ret_val;
}

int audio_extn_spkr_prot_start_processing(snd_device_t snd_device) {
    int ret_val = 0;

    if (spkr_prot_start_processing != NULL)
        ret_val = spkr_prot_start_processing(snd_device);

    return ret_val;
}

void audio_extn_spkr_prot_stop_processing(snd_device_t snd_device)
{
    if (spkr_prot_stop_processing != NULL)
        spkr_prot_stop_processing(snd_device);

    return;
}

bool audio_extn_spkr_prot_is_enabled()
{
    bool ret_val = false;

    if (spkr_prot_is_enabled != NULL)
        ret_val = spkr_prot_is_enabled();

    return ret_val;
}

void audio_extn_spkr_prot_calib_cancel(void *adev)
{
    if (spkr_prot_calib_cancel != NULL)
        spkr_prot_calib_cancel(adev);

    return;
}

void audio_extn_spkr_prot_set_parameters(struct str_parms *parms,
                                         char *value, int len)
{
    if (spkr_prot_set_parameters != NULL)
        spkr_prot_set_parameters(parms, value, len);

    return;
}

int audio_extn_fbsp_set_parameters(struct str_parms *parms)
{
    int ret_val = 0;

    if (fbsp_set_parameters != NULL)
        ret_val = fbsp_set_parameters(parms);

    return ret_val;
}

int audio_extn_fbsp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply)
{
    int ret_val = 0;

    if (fbsp_get_parameters != NULL)
        ret_val = fbsp_get_parameters(query, reply);

    return ret_val;
}

int audio_extn_get_spkr_prot_snd_device(snd_device_t snd_device)
{
    int ret_val = snd_device;

    if (get_spkr_prot_snd_device != NULL)
        ret_val = get_spkr_prot_snd_device(snd_device);

    return ret_val;
}

//END: SPEAKER_PROTECTION ==========================================================

//START: EXTERNAL_QDSP ================================================================
#ifdef __LP64__
#define EXTERNAL_QDSP_LIB_PATH  "/vendor/lib64/libextqdsp.so"
#else
#define EXTERNAL_QDSP_LIB_PATH  "/vendor/lib/libextqdsp.so"
#endif

static void *external_qdsp_lib_handle = NULL;

typedef void (*external_qdsp_init_t)(void *);
static external_qdsp_init_t external_qdsp_init;

typedef void (*external_qdsp_deinit_t)(void);
static external_qdsp_deinit_t external_qdsp_deinit;

typedef bool (*external_qdsp_set_state_t)(struct audio_device *,
                                        int , float , bool);
static external_qdsp_set_state_t external_qdsp_set_state;

typedef void (*external_qdsp_set_device_t)(struct audio_usecase *);
static external_qdsp_set_device_t external_qdsp_set_device;

typedef void (*external_qdsp_set_parameter_t)(struct audio_device *,
                                              struct str_parms *);
static external_qdsp_set_parameter_t external_qdsp_set_parameter;

typedef bool (*external_qdsp_supported_usb_t)(void);
static external_qdsp_supported_usb_t external_qdsp_supported_usb;

void external_qdsp_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        external_qdsp_lib_handle = dlopen(EXTERNAL_QDSP_LIB_PATH, RTLD_NOW);
        if (external_qdsp_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((external_qdsp_init =
             (external_qdsp_init_t)dlsym(external_qdsp_lib_handle, "ma_init")) == NULL) ||
            ((external_qdsp_deinit =
             (external_qdsp_deinit_t)dlsym(external_qdsp_lib_handle, "ma_deinit")) == NULL) ||
            ((external_qdsp_set_state =
             (external_qdsp_set_state_t)dlsym(external_qdsp_lib_handle, "set_state")) == NULL) ||
            ((external_qdsp_set_device =
             (external_qdsp_set_device_t)dlsym(external_qdsp_lib_handle, "set_device")) == NULL) ||
            ((external_qdsp_set_parameter =
             (external_qdsp_set_parameter_t)dlsym(external_qdsp_lib_handle, "set_parameters")) == NULL) ||
            ((external_qdsp_supported_usb =
             (external_qdsp_supported_usb_t)dlsym(external_qdsp_lib_handle, "supported_usb")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature EXTERNAL_QDSP is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (external_qdsp_lib_handle) {
        dlclose(external_qdsp_lib_handle);
        external_qdsp_lib_handle = NULL;
    }

    external_qdsp_init = NULL;
    external_qdsp_deinit = NULL;
    external_qdsp_set_state = NULL;
    external_qdsp_set_device = NULL;
    external_qdsp_set_parameter = NULL;
    external_qdsp_supported_usb = NULL;

    ALOGW(":: %s: ---- Feature EXTERNAL_QDSP is disabled ----", __func__);
    return;
}

void audio_extn_qdsp_init(void *platform) {
    if (external_qdsp_init != NULL)
        external_qdsp_init(platform);

    return;
}

void audio_extn_qdsp_deinit() {
    if (external_qdsp_deinit != NULL)
        external_qdsp_deinit();

    return;
}

bool audio_extn_qdsp_set_state(struct audio_device *adev, int stream_type,
                             float vol, bool active) {
    bool ret_val = false;

    if (external_qdsp_set_state != NULL)
        ret_val = external_qdsp_set_state(adev, stream_type, vol, active);

    return ret_val;
}

void audio_extn_qdsp_set_device(struct audio_usecase *usecase) {
    if (external_qdsp_set_device != NULL)
        external_qdsp_set_device(usecase);

    return;
}

void audio_extn_qdsp_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms) {
    if (external_qdsp_set_parameter != NULL)
        external_qdsp_set_parameter(adev, parms);

    return;
}

bool audio_extn_qdsp_supported_usb() {
    bool ret_val = false;

    if (external_qdsp_supported_usb != NULL)
        ret_val = external_qdsp_supported_usb();

    return ret_val;
}


//END: EXTERNAL_QDSP ================================================================

//START: EXTERNAL_SPEAKER ================================================================
#ifdef __LP64__
#define EXTERNAL_SPKR_LIB_PATH  "/vendor/lib64/libextspkr.so"
#else
#define EXTERNAL_SPKR_LIB_PATH  "/vendor/lib/libextspkr.so"
#endif

static void *external_speaker_lib_handle = NULL;

typedef void* (*external_speaker_init_t)(struct audio_device *);
static external_speaker_init_t external_speaker_init;

typedef void (*external_speaker_deinit_t)(void *);
static external_speaker_deinit_t external_speaker_deinit;

typedef void (*external_speaker_update_t)(void *);
static external_speaker_update_t external_speaker_update;

typedef void (*external_speaker_set_mode_t)(void *, audio_mode_t);
static external_speaker_set_mode_t external_speaker_set_mode;

typedef void (*external_speaker_set_voice_vol_t)(void *, float);
static external_speaker_set_voice_vol_t external_speaker_set_voice_vol;


void external_speaker_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        external_speaker_lib_handle = dlopen(EXTERNAL_SPKR_LIB_PATH, RTLD_NOW);
        if (external_speaker_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((external_speaker_init =
             (external_speaker_init_t)dlsym(external_speaker_lib_handle, "extspk_init")) == NULL) ||
            ((external_speaker_deinit =
             (external_speaker_deinit_t)dlsym(external_speaker_lib_handle, "extspk_deinit")) == NULL) ||
            ((external_speaker_update =
             (external_speaker_update_t)dlsym(external_speaker_lib_handle, "extspk_update")) == NULL) ||
            ((external_speaker_set_mode =
             (external_speaker_set_mode_t)dlsym(external_speaker_lib_handle, "extspk_set_mode")) == NULL) ||
            ((external_speaker_set_voice_vol =
             (external_speaker_set_voice_vol_t)dlsym(external_speaker_lib_handle, "extspk_set_voice_vol")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature EXTERNAL_SPKR is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (external_speaker_lib_handle) {
        dlclose(external_speaker_lib_handle);
        external_speaker_lib_handle = NULL;
    }

    external_speaker_init = NULL;
    external_speaker_deinit = NULL;
    external_speaker_update = NULL;
    external_speaker_set_mode = NULL;
    external_speaker_set_voice_vol = NULL;

    ALOGW(":: %s: ---- Feature EXTERNAL_SPKR is disabled ----", __func__);
    return;
}

void *audio_extn_extspk_init(struct audio_device *adev) {
    void* ret_val = NULL;

    if (external_speaker_init != NULL)
        ret_val = external_speaker_init(adev);

    return ret_val;
}

void audio_extn_extspk_deinit(void *extn) {
    if (external_speaker_deinit != NULL)
        external_speaker_deinit(extn);

    return;
}

void audio_extn_extspk_update(void* extn) {
    if (external_speaker_update != NULL)
        external_speaker_update(extn);

    return;
}

void audio_extn_extspk_set_mode(void* extn, audio_mode_t mode) {
    if (external_speaker_set_mode != NULL)
        external_speaker_set_mode(extn, mode);

    return;
}

void audio_extn_extspk_set_voice_vol(void* extn, float vol) {
    if (external_speaker_set_voice_vol != NULL)
        external_speaker_set_voice_vol(extn, vol);

    return;
}

//END: EXTERNAL_SPEAKER ================================================================


//START: EXTERNAL_SPEAKER_TFA ================================================================
#ifdef __LP64__
#define EXTERNAL_SPKR_TFA_LIB_PATH  "/vendor/lib64/libextspkr_tfa.so"
#else
#define EXTERNAL_SPKR_TFA_LIB_PATH  "/vendor/lib/libextspkr_tfa.so"
#endif

static void *external_speaker_tfa_lib_handle = NULL;

typedef int (*external_speaker_tfa_enable_t)(void);
static external_speaker_tfa_enable_t external_speaker_tfa_enable;

typedef void (*external_speaker_tfa_disable_t)(snd_device_t);
static external_speaker_tfa_disable_t external_speaker_tfa_disable;

typedef void (*external_speaker_tfa_set_mode_t)();
static external_speaker_tfa_set_mode_t external_speaker_tfa_set_mode;

typedef void (*external_speaker_tfa_set_mode_bt_t)();
static external_speaker_tfa_set_mode_bt_t external_speaker_tfa_set_mode_bt;

typedef void (*external_speaker_tfa_update_t)(void);
static external_speaker_tfa_update_t external_speaker_tfa_update;

typedef void (*external_speaker_tfa_set_voice_vol_t)(float);
static external_speaker_tfa_set_voice_vol_t external_speaker_tfa_set_voice_vol;

typedef int (*external_speaker_tfa_init_t)(struct audio_device *);
static external_speaker_tfa_init_t external_speaker_tfa_init;

typedef void (*external_speaker_tfa_deinit_t)(void);
static external_speaker_tfa_deinit_t external_speaker_tfa_deinit;

typedef bool (*external_speaker_tfa_is_supported_t)(void);
static external_speaker_tfa_is_supported_t external_speaker_tfa_is_supported;

void external_speaker_tfa_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        external_speaker_tfa_lib_handle = dlopen(EXTERNAL_SPKR_TFA_LIB_PATH, RTLD_NOW);
        if (external_speaker_tfa_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((external_speaker_tfa_enable =
             (external_speaker_tfa_enable_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_enable_speaker")) == NULL) ||
            ((external_speaker_tfa_disable =
             (external_speaker_tfa_disable_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_disable_speaker")) == NULL) ||
            ((external_speaker_tfa_set_mode =
             (external_speaker_tfa_set_mode_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_set_mode")) == NULL) ||
            ((external_speaker_tfa_set_mode_bt =
             (external_speaker_tfa_set_mode_bt_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_set_mode_bt")) == NULL) ||
            ((external_speaker_tfa_update =
             (external_speaker_tfa_update_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_update")) == NULL) ||
            ((external_speaker_tfa_set_voice_vol =
             (external_speaker_tfa_set_voice_vol_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_set_voice_vol")) == NULL) ||
            ((external_speaker_tfa_init =
             (external_speaker_tfa_init_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_init")) == NULL) ||
            ((external_speaker_tfa_deinit =
             (external_speaker_tfa_deinit_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_deinit")) == NULL) ||
            ((external_speaker_tfa_is_supported =
             (external_speaker_tfa_is_supported_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_is_supported")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature EXTERNAL_SPKR is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (external_speaker_tfa_lib_handle) {
        dlclose(external_speaker_tfa_lib_handle);
        external_speaker_tfa_lib_handle = NULL;
    }

    external_speaker_tfa_enable = NULL;
    external_speaker_tfa_disable = NULL;
    external_speaker_tfa_set_mode = NULL;
    external_speaker_tfa_update = NULL;
    external_speaker_tfa_set_voice_vol = NULL;
    external_speaker_tfa_init = NULL;
    external_speaker_tfa_deinit = NULL;
    external_speaker_tfa_is_supported = NULL;

    ALOGW(":: %s: ---- Feature EXTERNAL_SPKR_TFA is disabled ----", __func__);
    return;
}

int audio_extn_external_speaker_tfa_enable_speaker() {
    int ret_val = 0;

    if (external_speaker_tfa_enable != NULL)
        ret_val = external_speaker_tfa_enable();

    return ret_val;
}

void audio_extn_external_speaker_tfa_disable_speaker(snd_device_t snd_device) {
    if (external_speaker_tfa_disable != NULL)
        external_speaker_tfa_disable(snd_device);

    return;
}

void audio_extn_external_speaker_tfa_set_mode(bool is_mode_bt) {
    if (is_mode_bt && (external_speaker_tfa_set_mode_bt != NULL))
        external_speaker_tfa_set_mode_bt();
    else if (external_speaker_tfa_set_mode != NULL)
        external_speaker_tfa_set_mode();

    return;
}

void audio_extn_external_speaker_tfa_update() {
    if (external_speaker_tfa_update != NULL)
        external_speaker_tfa_update();

    return;
}

void audio_extn_external_speaker_tfa_set_voice_vol(float vol) {
    if (external_speaker_tfa_set_voice_vol != NULL)
        external_speaker_tfa_set_voice_vol(vol);

    return;
}

int  audio_extn_external_tfa_speaker_init(struct audio_device *adev) {
    int ret_val = 0;

    if (external_speaker_tfa_init != NULL)
        ret_val = external_speaker_tfa_init(adev);

    return ret_val;
}

void audio_extn_external_speaker_tfa_deinit() {
    if (external_speaker_tfa_deinit != NULL)
        external_speaker_tfa_deinit();

    return;
}

bool audio_extn_external_speaker_tfa_is_supported() {
    bool ret_val = false;

    if (external_speaker_tfa_is_supported != NULL)
        ret_val = external_speaker_tfa_is_supported;

    return ret_val;
}


//END: EXTERNAL_SPEAKER_TFA ================================================================


//START: HWDEP_CAL ================================================================
#ifdef __LP64__
#define HWDEP_CAL_LIB_PATH  "/vendor/lib64/libhwdepcal.so"
#else
#define HWDEP_CAL_LIB_PATH  "/vendor/lib/libhwdepcal.so"
#endif

static void *hwdep_cal_lib_handle = NULL;

typedef void (*hwdep_cal_send_t)(int, void*);
static hwdep_cal_send_t hwdep_cal_send;

void hwdep_cal_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        hwdep_cal_lib_handle = dlopen(HWDEP_CAL_LIB_PATH, RTLD_NOW);
        if (hwdep_cal_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if ((hwdep_cal_send =
            (hwdep_cal_send_t)dlsym(hwdep_cal_lib_handle, "hwdep_cal_send")) == NULL) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature HWDEP_CAL is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (hwdep_cal_lib_handle) {
        dlclose(hwdep_cal_lib_handle);
        hwdep_cal_lib_handle = NULL;
    }

    hwdep_cal_send = NULL;

    ALOGW(":: %s: ---- Feature HWDEP_CAL is disabled ----", __func__);
    return;
}

void audio_extn_hwdep_cal_send(int snd_card, void *acdb_handle)
{
    if (hwdep_cal_send != NULL)
        hwdep_cal_send(snd_card, acdb_handle);

    return;
}


//END: HWDEP_CAL =====================================================================


//START: DSM_FEEDBACK ================================================================
#ifdef __LP64__
#define DSM_FEEDBACK_LIB_PATH  "/vendor/lib64/libdsmfeedback.so"
#else
#define DSM_FEEDBACK_LIB_PATH  "/vendor/lib/libdsmfeedback.so"
#endif

static void *dsm_feedback_lib_handle = NULL;

typedef void (*dsm_feedback_enable_t)(struct audio_device*, snd_device_t, bool);
static dsm_feedback_enable_t dsm_feedback_enable;

void dsm_feedback_feature_init (bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        dsm_feedback_lib_handle = dlopen(DSM_FEEDBACK_LIB_PATH, RTLD_NOW);
        if (dsm_feedback_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if ((dsm_feedback_enable =
            (dsm_feedback_enable_t)dlsym(dsm_feedback_lib_handle, "dsm_feedback_enable")) == NULL) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature DSM_FEEDBACK is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (dsm_feedback_lib_handle) {
        dlclose(dsm_feedback_lib_handle);
        dsm_feedback_lib_handle = NULL;
    }

    dsm_feedback_enable = NULL;

    ALOGW(":: %s: ---- Feature DSM_FEEDBACK is disabled ----", __func__);
    return;
}

void audio_extn_dsm_feedback_enable(struct audio_device *adev, snd_device_t snd_device, bool benable)
{
    if (dsm_feedback_enable != NULL)
        dsm_feedback_enable(adev, snd_device, benable);

    return;
}

//END:   DSM_FEEDBACK ================================================================

//START: SND_MONITOR_FEATURE ================================================================
#ifdef __LP64__
#if LINUX_ENABLED
#define SND_MONITOR_PATH  "/usr/lib64/audio.snd.monitor.so"
#else
#define SND_MONITOR_PATH  "/vendor/lib64/libsndmonitor.so"
#endif
#else
#if LINUX_ENABLED
#define SND_MONITOR_PATH  "/usr/lib/audio.snd.monitor.so"
#else
#define SND_MONITOR_PATH  "/vendor/lib/libsndmonitor.so"
#endif
#endif

static void *snd_mnt_lib_handle = NULL;

typedef int (*snd_mon_init_t)();
static snd_mon_init_t snd_mon_init;
typedef int (*snd_mon_deinit_t)();
static snd_mon_deinit_t snd_mon_deinit;
typedef int (*snd_mon_register_listener_t)(void *, snd_mon_cb);
static snd_mon_register_listener_t snd_mon_register_listener;
typedef int (*snd_mon_unregister_listener_t)(void *);
static snd_mon_unregister_listener_t snd_mon_unregister_listener;

void snd_mon_feature_init (bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        snd_mnt_lib_handle = dlopen(SND_MONITOR_PATH, RTLD_NOW);
        if (snd_mnt_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((snd_mon_init = (snd_mon_init_t)dlsym(snd_mnt_lib_handle,"snd_mon_init")) == NULL) ||
            ((snd_mon_deinit = (snd_mon_deinit_t)dlsym(snd_mnt_lib_handle,"snd_mon_deinit")) == NULL) ||
            ((snd_mon_register_listener = (snd_mon_register_listener_t)dlsym(snd_mnt_lib_handle,"snd_mon_register_listener")) == NULL) ||
            ((snd_mon_unregister_listener = (snd_mon_unregister_listener_t)dlsym(snd_mnt_lib_handle,"snd_mon_unregister_listener")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }
        ALOGD("%s:: ---- Feature SND_MONITOR is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (snd_mnt_lib_handle) {
        dlclose(snd_mnt_lib_handle);
        snd_mnt_lib_handle = NULL;
    }

    snd_mon_init                = NULL;
    snd_mon_deinit              = NULL;
    snd_mon_register_listener   = NULL;
    snd_mon_unregister_listener = NULL;
    ALOGW(":: %s: ---- Feature SND_MONITOR is disabled ----", __func__);
    return;
}

int audio_extn_snd_mon_init()
{
    int ret = 0;
    if (snd_mon_init != NULL)
        ret = snd_mon_init();

    return ret;
}

int audio_extn_snd_mon_deinit()
{
    int ret = 0;
    if (snd_mon_deinit != NULL)
        ret = snd_mon_deinit();

    return ret;
}

int audio_extn_snd_mon_register_listener(void *stream, snd_mon_cb cb)
{
    int ret = 0;
    if (snd_mon_register_listener != NULL)
        ret = snd_mon_register_listener(stream, cb);

    return ret;
}

int audio_extn_snd_mon_unregister_listener(void *stream)
{
    int ret = 0;
    if (snd_mon_unregister_listener != NULL)
        ret = snd_mon_unregister_listener(stream);

    return ret;
}

//END: SND_MONITOR_FEATURE ================================================================

//START: SOURCE_TRACKING_FEATURE ==============================================
int get_soundfocus_data(const struct audio_device *adev,
                                   struct sound_focus_param *payload);
int get_sourcetrack_data(const struct audio_device *adev,
                                    struct source_tracking_param *payload);
int set_soundfocus_data(struct audio_device *adev,
                                   struct sound_focus_param *payload);
void source_track_set_parameters(struct audio_device *adev,
                                            struct str_parms *parms);
void source_track_get_parameters(const struct audio_device *adev,
                                            struct str_parms *query,
                                            struct str_parms *reply);

static bool is_src_trkn_enabled = false;

void src_trkn_feature_init(bool is_feature_enabled) {
    is_src_trkn_enabled = is_feature_enabled;

    if (is_src_trkn_enabled) {
        ALOGD("%s:: ---- Feature SOURCE_TRACKING is Enabled ----", __func__);
        return;
    }

    ALOGW(":: %s: ---- Feature SOURCE_TRACKING is disabled ----", __func__);
}

int audio_extn_get_soundfocus_data(const struct audio_device *adev,
                                   struct sound_focus_param *payload) {
    int ret = 0;

    if (is_src_trkn_enabled)
        ret = get_soundfocus_data(adev, payload);

    return ret;
}

int audio_extn_get_sourcetrack_data(const struct audio_device *adev,
                                    struct source_tracking_param *payload) {
    int ret = 0;

    if (is_src_trkn_enabled)
        ret = get_sourcetrack_data(adev, payload);

    return ret;
}

int audio_extn_set_soundfocus_data(struct audio_device *adev,
                                   struct sound_focus_param *payload) {
    int ret = 0;

    if (is_src_trkn_enabled)
        ret = set_soundfocus_data(adev, payload);

    return ret;
}

void audio_extn_set_clock_mixer(struct audio_device *adev,
                                snd_device_t snd_device) {
    struct listnode *node = NULL;
    audio_clock_data_t *cdata = NULL;
    int be_id = -1;

    ALOGV("%s", __func__);

    be_id = platform_get_snd_device_backend_index(snd_device);

    list_for_each(node, &adev->clock_switch_list) {
        cdata = node_to_item(node, audio_clock_data_t, list);
        if (cdata->be_id == be_id) {
            if (cdata->clock_switch) {
                struct mixer_ctl *ctl = NULL;
                const char *mixer_ctl_name = "MCLK_SRC CFG";
                long ctl_data[3];
                int status = 0;

                ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
                if (!ctl) {
                    ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                            __func__, mixer_ctl_name);
                }

                ctl_data[0] = (long)cdata->be_id;
                ctl_data[1] = (long)cdata->clock_type;
                ctl_data[2] = (long)cdata->clock_frequency;

                /* trigger mixer control to change clock type/frequency */
                status = mixer_ctl_set_array(ctl, ctl_data,
                                          sizeof(ctl_data)/sizeof(ctl_data[0]));
                if (status < 0) {
                    ALOGE("%s: Could not set ctl for mixer cmd - %s, ret %d",
                                              __func__, mixer_ctl_name, status);
                }
                cdata->clock_switch = false;
            }
            break;
        }
    }
}

void audio_extn_update_clock_data_with_backend(struct audio_device *adev,
                                               struct audio_usecase *usecase) {
    struct listnode *node = NULL;
    audio_clock_data_t *cdata = NULL;
    snd_device_t snd_device = SND_DEVICE_NONE;

    ALOGV("%s", __func__);

    list_for_each(node, &adev->clock_switch_list) {
        cdata = node_to_item(node, audio_clock_data_t, list);
        if (usecase->devices == cdata->device) {
            snd_device = platform_get_output_snd_device(adev->platform,
                                                        usecase->stream.out);
            cdata->be_id = platform_get_snd_device_backend_index(snd_device);
            break;
        }
    }
}

void audio_extn_set_clock_switch_params(struct audio_device *adev,
                                        struct str_parms *parms)
{
    audio_clock_data_t *cdata = NULL;
    audio_clock_data_t *clock_data = NULL;
    bool entry_found = false;
    struct listnode *node = NULL;
    int ret = 0;
    char value[32];
    struct audio_usecase *uc = NULL;

    ALOGD("%s", __func__);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_CLOCK, value, sizeof(value));
    if (ret >= 0) {
        clock_data = (audio_clock_data_t *)malloc(sizeof(audio_clock_data_t));
        clock_data->clock_type = (audio_clock_type) atoi(value);

        if (str_parms_get_str(parms,AUDIO_PARAMETER_CLOCK_FREQUENCY,value,
                                                          sizeof(value)) >= 0) {
            clock_data->clock_frequency = atol(value);
        } else {
            ALOGE("%s: Invalid params for clock frequency\n", __func__);
            free(clock_data);
            clock_data = NULL;
            return;
        }

        if (str_parms_get_str(parms,"device",value,sizeof(value)) >= 0) {
            clock_data->device = (audio_devices_t) atoi(value);
        } else {
            ALOGE("%s: Invalid params for device\n", __func__);
            free(clock_data);
            clock_data = NULL;
            return;
        }

        clock_data->clock_switch = true;
        clock_data->be_id = -1;
    } else {
        ALOGE("%s: Invalid params for device\n", __func__);
        return;
    }

    /* If clock list is empty, add entry to the list, else search for the
       clock data of the device in the list. If found, update the entry if
       there is a change in clock data. Else, add the entry to the list */
    if (list_empty(&adev->clock_switch_list)) {
        list_add_tail(&adev->clock_switch_list, &clock_data->list);
        cdata = clock_data;
    } else {
        list_for_each(node, &adev->clock_switch_list) {
            cdata = node_to_item(node, audio_clock_data_t, list);

            if (cdata->device == clock_data->device) {
                if (cdata->clock_frequency != clock_data->clock_frequency ||
                                 cdata->clock_type != clock_data->clock_type) {
                    cdata->clock_frequency = clock_data->clock_frequency;
                    cdata->clock_type = clock_data->clock_type;
                }
                cdata->clock_switch = true;
                free(clock_data);
                clock_data = NULL;
                entry_found = true;
                break;
            }
        }

        if (!entry_found) {
            cdata = clock_data;
            list_add_tail(&adev->clock_switch_list, &clock_data->list);
        }
    }

    if (!list_empty(&adev->usecase_list)) {
        /* Teardown all the usecases having same backend, requested
           for clock switch */
        list_for_each(node, &adev->usecase_list) {
            uc = node_to_item(node, struct audio_usecase, list);
            if (uc->devices == cdata->device) {
                if (uc->out_snd_device != SND_DEVICE_NONE)
                    cdata->be_id = platform_get_snd_device_backend_index
                                                       (uc->out_snd_device);

                if (uc->in_snd_device != SND_DEVICE_NONE)
                    cdata->be_id = platform_get_snd_device_backend_index
                                                        (uc->in_snd_device);

                disable_audio_route(adev, uc);

                if (uc->out_snd_device != SND_DEVICE_NONE)
                    ret = disable_snd_device(adev, uc->out_snd_device);

                if (uc->in_snd_device != SND_DEVICE_NONE)
                    ret = disable_snd_device(adev, uc->in_snd_device);
            }
        }

        /* Restart all the usecases that are tore down during clock switch */
        list_for_each(node, &adev->usecase_list) {
            uc = node_to_item(node, struct audio_usecase, list);
            if (uc->devices == cdata->device) {
                audio_extn_update_clock_data_with_backend(adev, uc);

                if (uc->out_snd_device != SND_DEVICE_NONE)
                    ret = enable_snd_device(adev, uc->out_snd_device);

                if (uc->in_snd_device != SND_DEVICE_NONE)
                    ret = enable_snd_device(adev, uc->in_snd_device);

                ret = enable_audio_route(adev, uc);
            }
        }
    }
}

void audio_extn_source_track_set_parameters(struct audio_device *adev,
                                            struct str_parms *parms) {
    if (is_src_trkn_enabled)
        source_track_set_parameters(adev, parms);
}

void audio_extn_source_track_get_parameters(const struct audio_device *adev,
                                            struct str_parms *query,
                                            struct str_parms *reply) {
    if (is_src_trkn_enabled)
        source_track_get_parameters(adev, query, reply);
}
//END: SOURCE_TRACKING_FEATURE ================================================

//START: SSREC_FEATURE ==========================================================
#ifdef __LP64__
#if LINUX_ENABLED
#define SSREC_LIB_PATH  "/usr/lib64/audio.ssrec.so"
#else
#define SSREC_LIB_PATH  "/vendor/lib64/libssrec.so"
#endif
#else
#if LINUX_ENABLED
#define SSREC_LIB_PATH  "/usr/lib/audio.ssrec.so"
#else
#define SSREC_LIB_PATH  "/vendor/lib/libssrec.so"
#endif
#endif

static void *ssrec_lib_handle = NULL;

typedef bool (*ssr_check_usecase_t)(struct stream_in *);
static ssr_check_usecase_t ssr_check_usecase;

typedef int (*ssr_set_usecase_t)(struct stream_in *,
                                 struct audio_config *,
                                 bool *);
static ssr_set_usecase_t ssr_set_usecase;

typedef int32_t (*ssr_init_t)(struct stream_in *,
                              int num_out_chan);
static ssr_init_t ssr_init;

typedef int32_t (*ssr_deinit_t)();
static ssr_deinit_t ssr_deinit;

typedef void (*ssr_update_enabled_t)();
static ssr_update_enabled_t ssr_update_enabled;

typedef bool (*ssr_get_enabled_t)();
static ssr_get_enabled_t ssr_get_enabled;

typedef int32_t (*ssr_read_t)(struct audio_stream_in *,
                              void *,
                              size_t);
static ssr_read_t ssr_read;

typedef void (*ssr_set_parameters_t)(struct audio_device *,
                                     struct str_parms *);
static ssr_set_parameters_t ssr_set_parameters;

typedef void (*ssr_get_parameters_t)(const struct audio_device *,
                                     struct str_parms *,
                                     struct str_parms *);
static ssr_get_parameters_t ssr_get_parameters;

typedef struct stream_in *(*ssr_get_stream_t)();
static ssr_get_stream_t ssr_get_stream;

void ssrec_feature_init(bool is_feature_enabled) {

    if (is_feature_enabled) {
        ssrec_lib_handle = dlopen(SSREC_LIB_PATH, RTLD_NOW);
        if (ssrec_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }

        if (((ssr_check_usecase = (ssr_check_usecase_t)dlsym(ssrec_lib_handle, "ssr_check_usecase")) == NULL) ||
            ((ssr_set_usecase = (ssr_set_usecase_t)dlsym(ssrec_lib_handle, "ssr_set_usecase")) == NULL) ||
            ((ssr_init = (ssr_init_t)dlsym(ssrec_lib_handle, "ssr_init")) == NULL) ||
            ((ssr_deinit = (ssr_deinit_t)dlsym(ssrec_lib_handle, "ssr_deinit")) == NULL) ||
            ((ssr_update_enabled = (ssr_update_enabled_t)dlsym(ssrec_lib_handle, "ssr_update_enabled")) == NULL) ||
            ((ssr_get_enabled = (ssr_get_enabled_t)dlsym(ssrec_lib_handle, "ssr_get_enabled")) == NULL) ||
            ((ssr_read = (ssr_read_t)dlsym(ssrec_lib_handle, "ssr_read")) == NULL) ||
            ((ssr_set_parameters = (ssr_set_parameters_t)dlsym(ssrec_lib_handle, "ssr_set_parameters")) == NULL) ||
            ((ssr_get_parameters = (ssr_get_parameters_t)dlsym(ssrec_lib_handle, "ssr_get_parameters")) == NULL) ||
            ((ssr_get_stream = (ssr_get_stream_t)dlsym(ssrec_lib_handle, "ssr_get_stream")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature SSREC is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if(ssrec_lib_handle) {
        dlclose(ssrec_lib_handle);
        ssrec_lib_handle = NULL;
    }

    ssr_check_usecase = NULL;
    ssr_set_usecase = NULL;
    ssr_init = NULL;
    ssr_deinit = NULL;
    ssr_update_enabled = NULL;
    ssr_get_enabled = NULL;
    ssr_read = NULL;
    ssr_set_parameters = NULL;
    ssr_get_parameters = NULL;
    ssr_get_stream = NULL;

    ALOGW(":: %s: ---- Feature SSREC is disabled ----", __func__);
}

bool audio_extn_ssr_check_usecase(struct stream_in *in) {
    bool ret = false;

    if (ssrec_lib_handle != NULL)
        ret = ssr_check_usecase(in);

    return ret;
}

int audio_extn_ssr_set_usecase(struct stream_in *in,
                               struct audio_config *config,
                               bool *channel_mask_updated) {
    int ret = 0;

    if (ssrec_lib_handle != NULL)
        ret = ssr_set_usecase(in, config, channel_mask_updated);

    return ret;
}

int32_t audio_extn_ssr_init(struct stream_in *in,
                            int num_out_chan) {
    int32_t ret = 0;

    if (ssrec_lib_handle != NULL)
        ret = ssr_init(in, num_out_chan);

    return ret;
}

int32_t audio_extn_ssr_deinit() {
    int32_t ret = 0;

    if (ssrec_lib_handle != NULL)
        ret = ssr_deinit();

    return ret;
}

void audio_extn_ssr_update_enabled() {

    if (ssrec_lib_handle)
        ssr_update_enabled();
}

bool audio_extn_ssr_get_enabled() {
    bool ret = false;

    if (ssrec_lib_handle)
        ret = ssr_get_enabled();

    return ret;
}

int32_t audio_extn_ssr_read(struct audio_stream_in *stream,
                            void *buffer,
                            size_t bytes) {
    int32_t ret = 0;

    if (ssrec_lib_handle)
        ret = ssr_read(stream, buffer, bytes);

    return ret;
}

void audio_extn_ssr_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms) {

    if (ssrec_lib_handle)
        ssr_set_parameters(adev, parms);
}

void audio_extn_ssr_get_parameters(const struct audio_device *adev,
                                   struct str_parms *query,
                                   struct str_parms *reply) {

    if (ssrec_lib_handle)
        ssr_get_parameters(adev, query, reply);
}

struct stream_in *audio_extn_ssr_get_stream() {
    struct stream_in *ret = NULL;

    if (ssrec_lib_handle)
        ret = ssr_get_stream();

    return ret;
}
//END: SSREC_FEATURE ============================================================

//START: COMPRESS_CAPTURE_FEATURE ================================================================
#ifdef __LP64__
#if LINUX_ENABLED
#define COMPRESS_CAPTURE_PATH  "/usr/lib64/audio.compress.capture.so"
#else
#define COMPRESS_CAPTURE_PATH  "/vendor/lib64/libcomprcapture.so"
#endif
#else
#if LINUX_ENABLED
#define COMPRESS_CAPTURE_PATH  "/usr/lib/audio.compress.capture.so"
#else
#define COMPRESS_CAPTURE_PATH  "/vendor/lib/libcomprcapture.so"
#endif
#endif

static void *compr_cap_lib_handle = NULL;

typedef void (*compr_cap_init_t)(struct stream_in*);
static compr_cap_init_t compr_cap_init;

typedef void (*compr_cap_deinit_t)();
static compr_cap_deinit_t compr_cap_deinit;

typedef bool (*compr_cap_enabled_t)();
static compr_cap_enabled_t compr_cap_enabled;

typedef bool (*compr_cap_format_supported_t)(audio_format_t);
static compr_cap_format_supported_t compr_cap_format_supported;

typedef bool (*compr_cap_usecase_supported_t)(audio_usecase_t);
static compr_cap_usecase_supported_t compr_cap_usecase_supported;

typedef size_t (*compr_cap_get_buffer_size_t)(audio_usecase_t);
static compr_cap_get_buffer_size_t compr_cap_get_buffer_size;

typedef int (*compr_cap_read_t)(struct stream_in*, void*, size_t);
static compr_cap_read_t compr_cap_read;

void compr_cap_feature_init(bool is_feature_enabled)
{
    if(is_feature_enabled) {
        //dlopen lib
        compr_cap_lib_handle = dlopen(COMPRESS_CAPTURE_PATH, RTLD_NOW);
        if (compr_cap_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((compr_cap_init = (compr_cap_init_t)dlsym(compr_cap_lib_handle,"compr_cap_init")) == NULL) ||
            ((compr_cap_deinit = (compr_cap_deinit_t)dlsym(compr_cap_lib_handle,"compr_cap_deinit")) == NULL) ||
            ((compr_cap_enabled = (compr_cap_enabled_t)dlsym(compr_cap_lib_handle,"compr_cap_enabled")) == NULL) ||
            ((compr_cap_format_supported = (compr_cap_format_supported_t)dlsym(compr_cap_lib_handle,"compr_cap_format_supported")) == NULL) ||
            ((compr_cap_usecase_supported = (compr_cap_usecase_supported_t)dlsym(compr_cap_lib_handle,"compr_cap_usecase_supported")) == NULL) ||
            ((compr_cap_get_buffer_size = (compr_cap_get_buffer_size_t)dlsym(compr_cap_lib_handle,"compr_cap_get_buffer_size")) == NULL) ||
            ((compr_cap_read = (compr_cap_read_t)dlsym(compr_cap_lib_handle,"compr_cap_read")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature COMPRESS_CAPTURE is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (compr_cap_lib_handle) {
        dlclose(compr_cap_lib_handle);
        compr_cap_lib_handle = NULL;
    }

    compr_cap_init                 = NULL;
    compr_cap_deinit               = NULL;
    compr_cap_enabled              = NULL;
    compr_cap_format_supported     = NULL;
    compr_cap_usecase_supported    = NULL;
    compr_cap_get_buffer_size      = NULL;
    compr_cap_read                 = NULL;

    ALOGW(":: %s: ---- Feature COMPRESS_CAPTURE is disabled ----", __func__);
    return;
}

void audio_extn_compr_cap_init(struct stream_in* instream)
{
    if (compr_cap_init != NULL)
        compr_cap_init(instream);

    return;
}

void audio_extn_compr_cap_deinit()
{
    if(compr_cap_deinit)
        compr_cap_deinit();

    return;
}

bool audio_extn_compr_cap_enabled()
{
    bool ret_val = false;

    if (compr_cap_enabled)
        ret_val = compr_cap_enabled();

    return ret_val;
}

bool audio_extn_compr_cap_format_supported(audio_format_t format)
{
    bool ret_val = false;

    if (compr_cap_format_supported != NULL)
        ret_val =  compr_cap_format_supported(format);

    return ret_val;
}

bool audio_extn_compr_cap_usecase_supported(audio_usecase_t usecase)
{
    bool ret_val = false;

    if (compr_cap_usecase_supported != NULL)
        ret_val =  compr_cap_usecase_supported(usecase);

    return ret_val;
}

size_t audio_extn_compr_cap_get_buffer_size(audio_format_t format)
{
    size_t ret_val = 0;

    if (compr_cap_get_buffer_size != NULL)
        ret_val =  compr_cap_get_buffer_size(format);

    return ret_val;
}

size_t audio_extn_compr_cap_read(struct stream_in *in,
                                        void *buffer, size_t bytes)
{
    size_t ret_val = 0;

    if (compr_cap_read != NULL)
        ret_val =  compr_cap_read(in, buffer, bytes);

    return ret_val;
}


void audio_extn_init(struct audio_device *adev)
{
    //fix-me: check running on vendor enhanced build
    //is_running_on_stock_version = !isRunningWithVendorEnhancedFramework();
    aextnmod.anc_enabled = 0;
    aextnmod.aanc_enabled = 0;
    aextnmod.custom_stereo_enabled = 0;
    aextnmod.proxy_channel_num = 2;
    aextnmod.hpx_enabled = 0;
    aextnmod.vbat_enabled = 0;
    aextnmod.bcl_enabled = 0;
    aextnmod.hifi_audio_enabled = 0;
    aextnmod.addr.nap = 0;
    aextnmod.addr.uap = 0;
    aextnmod.addr.lap = 0;
    aextnmod.adev = adev;

    audio_extn_dolby_set_license(adev);
    audio_extn_aptx_dec_set_license(adev);
}

int audio_extn_parse_compress_metadata(struct stream_out *out,
                                       struct str_parms *parms)
{
    int ret = 0;
    char value[32];

    if (!if_compress_meta_data_feature_enabled()) {
        return ret;
    }

    if (out->format == AUDIO_FORMAT_FLAC) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.min_blk_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.max_blk_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.min_frame_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.max_frame_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("FLAC metadata: min_blk_size %d, max_blk_size %d min_frame_size %d max_frame_size %d",
              out->compr_config.codec->options.flac_dec.min_blk_size,
              out->compr_config.codec->options.flac_dec.max_blk_size,
              out->compr_config.codec->options.flac_dec.min_frame_size,
              out->compr_config.codec->options.flac_dec.max_frame_size);
    }

    else if (out->format == AUDIO_FORMAT_ALAC) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_FRAME_LENGTH, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.frame_length = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.compatible_version = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_BIT_DEPTH, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.bit_depth = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_PB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.pb = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.mb = atoi(value);
            out->is_compr_metadata_avail = true;
        }

        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_KB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.kb = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_NUM_CHANNELS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.num_channels = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_RUN, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.max_run = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.max_frame_bytes = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.avg_bit_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_SAMPLING_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.sample_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.channel_layout_tag = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("ALAC CSD values: frameLength %d bitDepth %d numChannels %d"
                " maxFrameBytes %d, avgBitRate %d, sampleRate %d",
                out->compr_config.codec->options.alac.frame_length,
                out->compr_config.codec->options.alac.bit_depth,
                out->compr_config.codec->options.alac.num_channels,
                out->compr_config.codec->options.alac.max_frame_bytes,
                out->compr_config.codec->options.alac.avg_bit_rate,
                out->compr_config.codec->options.alac.sample_rate);
    }

    else if (out->format == AUDIO_FORMAT_APE) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.compatible_version = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.compression_level = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FORMAT_FLAGS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.format_flags = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.blocks_per_frame = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.final_frame_blocks = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_TOTAL_FRAMES, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.total_frames = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.bits_per_sample = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_NUM_CHANNELS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.num_channels = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SAMPLE_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.sample_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.seek_table_present = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("APE CSD values: compatibleVersion %d compressionLevel %d"
                " formatFlags %d blocksPerFrame %d finalFrameBlocks %d"
                " totalFrames %d bitsPerSample %d numChannels %d"
                " sampleRate %d seekTablePresent %d",
                out->compr_config.codec->options.ape.compatible_version,
                out->compr_config.codec->options.ape.compression_level,
                out->compr_config.codec->options.ape.format_flags,
                out->compr_config.codec->options.ape.blocks_per_frame,
                out->compr_config.codec->options.ape.final_frame_blocks,
                out->compr_config.codec->options.ape.total_frames,
                out->compr_config.codec->options.ape.bits_per_sample,
                out->compr_config.codec->options.ape.num_channels,
                out->compr_config.codec->options.ape.sample_rate,
                out->compr_config.codec->options.ape.seek_table_present);
    }

    else if (out->format == AUDIO_FORMAT_VORBIS) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_VORBIS_BITSTREAM_FMT, value, sizeof(value));
        if (ret >= 0) {
        // transcoded bitstream mode
            out->compr_config.codec->options.vorbis_dec.bit_stream_fmt = (atoi(value) > 0) ? 1 : 0;
            out->is_compr_metadata_avail = true;
        }
    }
#ifdef AMR_OFFLOAD_ENABLED
    else if (out->format == AUDIO_FORMAT_AMR_WB_PLUS) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_AMR_WB_PLUS_BITSTREAM_FMT, value, sizeof(value));
        if (ret >= 0) {
        // transcoded bitstream mode
            out->compr_config.codec->options.amrwbplus.bit_stream_fmt = atoi(value);
            out->is_compr_metadata_avail = true;
        }
    }
#endif

    else if (out->format == AUDIO_FORMAT_WMA || out->format == AUDIO_FORMAT_WMA_PRO) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_FORMAT_TAG, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->format = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_AVG_BIT_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.avg_bit_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BLOCK_ALIGN, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.super_block_align = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.bits_per_sample = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_CHANNEL_MASK, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.channelmask = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION1, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt1 = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION2, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.wma.encodeopt2 = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("WMA params: fmt %x, bit rate %x, balgn %x, sr %d, chmsk %x"
                " encop %x, op1 %x, op2 %x",
                out->compr_config.codec->format,
                out->compr_config.codec->options.wma.avg_bit_rate,
                out->compr_config.codec->options.wma.super_block_align,
                out->compr_config.codec->options.wma.bits_per_sample,
                out->compr_config.codec->options.wma.channelmask,
                out->compr_config.codec->options.wma.encodeopt,
                out->compr_config.codec->options.wma.encodeopt1,
                out->compr_config.codec->options.wma.encodeopt2);
    }

    return ret;
}

#ifdef AUXPCM_BT_ENABLED
int32_t audio_extn_read_xml(struct audio_device *adev, uint32_t mixer_card,
                            const char* mixer_xml_path,
                            const char* mixer_xml_path_auxpcm)
{
    char bt_soc[128];
    bool wifi_init_complete = false;
    int sleep_retry = 0;

    while (!wifi_init_complete && sleep_retry < MAX_SLEEP_RETRY) {
        property_get("qcom.bluetooth.soc", bt_soc, NULL);
        if (strncmp(bt_soc, "unknown", sizeof("unknown"))) {
            wifi_init_complete = true;
        } else {
            usleep(WIFI_INIT_WAIT_SLEEP*1000);
            sleep_retry++;
        }
    }

    if (!strncmp(bt_soc, "ath3k", sizeof("ath3k")))
        adev->audio_route = audio_route_init(mixer_card, mixer_xml_path_auxpcm);
    else
        adev->audio_route = audio_route_init(mixer_card, mixer_xml_path);

    return 0;
}
#endif /* AUXPCM_BT_ENABLED */

static int audio_extn_set_multichannel_mask(struct audio_device *adev,
                                            struct stream_in *in,
                                            struct audio_config *config,
                                            bool *channel_mask_updated)
{
    int ret = -EINVAL;
    int channel_count = audio_channel_count_from_in_mask(in->channel_mask);
    *channel_mask_updated = false;

    int max_mic_count = platform_get_max_mic_count(adev->platform);
    /* validate input params. Avoid updated channel mask if HDMI or loopback device */
    if ((channel_count == 6) &&
        (in->format == AUDIO_FORMAT_PCM_16_BIT) &&
        !((in->device & AUDIO_DEVICE_IN_HDMI) & ~(AUDIO_DEVICE_BIT_IN)) &&
        (!is_loopback_input_device(in->device))) {
        switch (max_mic_count) {
            case 4:
                config->channel_mask = AUDIO_CHANNEL_INDEX_MASK_4;
                break;
            case 3:
                config->channel_mask = AUDIO_CHANNEL_INDEX_MASK_3;
                break;
            case 2:
                config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
                break;
            default:
                config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
                break;
        }
        ret = 0;
        *channel_mask_updated = true;
    }

    return ret;
}

int audio_extn_check_and_set_multichannel_usecase(struct audio_device *adev,
                                                  struct stream_in *in,
                                                  struct audio_config *config,
                                                  bool *update_params)
{
    bool ssr_supported = false;
    in->config.rate = config->sample_rate;
    in->sample_rate = config->sample_rate;
    ssr_supported = audio_extn_ssr_check_usecase(in);
    if (ssr_supported) {
        return audio_extn_ssr_set_usecase(in, config, update_params);
    } else if (audio_extn_ffv_check_usecase(in)) {
        char ffv_lic[LICENSE_STR_MAX_LEN + 1] = {0};
        int ffv_key = 0;
        if(platform_get_license_by_product(adev->platform, PRODUCT_FFV, &ffv_key, ffv_lic))
        {
            ALOGD("%s: Valid licence not availble for %s ", __func__, PRODUCT_FFV);
            return -EINVAL;
        }
        ALOGD("%s: KEY: %d LICENSE: %s ", __func__, ffv_key, ffv_lic);
        return audio_extn_ffv_set_usecase(in, ffv_key, ffv_lic);
    } else {
        return audio_extn_set_multichannel_mask(adev, in, config,
                                                update_params);
    }
}

#ifdef APTX_DECODER_ENABLED
static void audio_extn_aptx_dec_set_license(struct audio_device *adev)
{
    int ret, key = 0;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "APTX Dec License";

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return;
    }
    key = platform_get_meta_info_key_from_list(adev->platform, "aptx");

    ALOGD("%s Setting APTX License with key:0x%x",__func__, key);
    ret = mixer_ctl_set_value(ctl, 0, key);
    if (ret)
        ALOGE("%s: cannot set license, error:%d",__func__, ret);
}

static void audio_extn_set_aptx_dec_bt_addr(struct audio_device *adev __unused, struct str_parms *parms)
{
    int ret = 0;
    char value[256];

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_APTX_DEC_BT_ADDR, value,
                            sizeof(value));
    if (ret >= 0) {
        audio_extn_parse_aptx_dec_bt_addr(value);
    }
}

int audio_extn_set_aptx_dec_params(struct aptx_dec_param *payload)
{
    struct aptx_dec_param *aptx_cfg = payload;

    aextnmod.addr.nap = aptx_cfg->bt_addr.nap;
    aextnmod.addr.uap = aptx_cfg->bt_addr.uap;
    aextnmod.addr.lap = aptx_cfg->bt_addr.lap;
    return 0;
}

static void audio_extn_parse_aptx_dec_bt_addr(char *value)
{
    int ba[6];
    char *str, *tok;
    uint32_t addr[3];
    int i = 0;

    ALOGV("%s: value %s", __func__, value);
    tok = strtok_r(value, ":", &str);
    while (tok != NULL) {
        ba[i] = strtol(tok, NULL, 16);
        i++;
        tok = strtok_r(NULL, ":", &str);
    }
    addr[0] = (ba[0] << 8) | ba[1];
    addr[1] = ba[2];
    addr[2] = (ba[3] << 16) | (ba[4] << 8) | ba[5];

    aextnmod.addr.nap = addr[0];
    aextnmod.addr.uap = addr[1];
    aextnmod.addr.lap = addr[2];
}

void audio_extn_send_aptx_dec_bt_addr_to_dsp(struct stream_out *out)
{
    ALOGV("%s", __func__);
    out->compr_config.codec->options.aptx_dec.nap = aextnmod.addr.nap;
    out->compr_config.codec->options.aptx_dec.uap = aextnmod.addr.uap;
    out->compr_config.codec->options.aptx_dec.lap = aextnmod.addr.lap;
}

#endif //APTX_DECODER_ENABLED

void audio_extn_set_dsd_dec_params(struct stream_out *out, int blk_size)
{
    ALOGV("%s", __func__);
    out->compr_config.codec->options.dsd_dec.blk_size = blk_size;
}

int audio_extn_out_set_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload) {
    int ret = -EINVAL;

    if (!out || !payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    ALOGD("%s: enter: stream (%p) usecase(%d: %s) param_id %d", __func__,
            out, out->usecase, use_case_table[out->usecase], param_id);

    switch (param_id) {
        case AUDIO_EXTN_PARAM_OUT_RENDER_WINDOW:
            ret = audio_extn_utils_compress_set_render_window(out,
                    (struct audio_out_render_window_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_START_DELAY:
            ret = audio_extn_utils_compress_set_start_delay(out,
                    (struct audio_out_start_delay_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_ENABLE_DRIFT_CORRECTION:
            ret = audio_extn_utils_compress_enable_drift_correction(out,
                    (struct audio_out_enable_drift_correction *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_CORRECT_DRIFT:
            ret = audio_extn_utils_compress_correct_drift(out,
                    (struct audio_out_correct_drift *)(payload));
            break;
        case AUDIO_EXTN_PARAM_ADSP_STREAM_CMD:
            ret = audio_extn_adsp_hdlr_stream_set_param(out->adsp_hdlr_stream_handle,
                    ADSP_HDLR_STREAM_CMD_REGISTER_EVENT,
                    (void *)&payload->adsp_event_params);
            break;
        case AUDIO_EXTN_PARAM_OUT_CHANNEL_MAP:
            ret = audio_extn_utils_set_channel_map(out,
                    (struct audio_out_channel_map_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_MIX_MATRIX_PARAMS:
            ret = audio_extn_utils_set_pan_scale_params(out,
                    (struct mix_matrix_params *)(payload));
            break;
        case AUDIO_EXTN_PARAM_CH_MIX_MATRIX_PARAMS:
            ret = audio_extn_utils_set_downmix_params(out,
                    (struct mix_matrix_params *)(payload));
            break;
        default:
            ALOGE("%s:: unsupported param_id %d", __func__, param_id);
            break;
    }
    return ret;
}

#ifdef AUDIO_HW_LOOPBACK_ENABLED
int audio_extn_hw_loopback_set_param_data(audio_patch_handle_t handle,
                                          audio_extn_loopback_param_id param_id,
                                          audio_extn_loopback_param_payload *payload) {
    int ret = -EINVAL;

    if (!payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    ALOGD("%d: %s: param id is %d\n", __LINE__, __func__, param_id);

    switch(param_id) {
        case AUDIO_EXTN_PARAM_LOOPBACK_RENDER_WINDOW:
            ret = audio_extn_hw_loopback_set_render_window(handle, payload);
            break;
        case AUDIO_EXTN_PARAM_LOOPBACK_SET_CALLBACK:
            ret = audio_extn_hw_loopback_set_callback(handle, payload);
            break;
        default:
            ALOGE("%s: unsupported param id %d", __func__, param_id);
            break;
    }

    return ret;
}
#endif


/* API to get playback stream specific config parameters */
int audio_extn_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload)
{
    int ret = -EINVAL;
    struct audio_usecase *uc_info;

    if (!out || !payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    switch (param_id) {
        case AUDIO_EXTN_PARAM_AVT_DEVICE_DRIFT:
            uc_info = get_usecase_from_list(out->dev, out->usecase);
            if (uc_info == NULL) {
                ALOGE("%s: Could not find the usecase (%d) in the list",
                       __func__, out->usecase);
                ret = -EINVAL;
            } else {
                ret = audio_extn_utils_get_avt_device_drift(uc_info,
                        (struct audio_avt_device_drift_param *)payload);
                if(ret)
                    ALOGE("%s:: avdrift query failed error %d", __func__, ret);
            }
            break;
        case AUDIO_EXTN_PARAM_OUT_PRESENTATION_POSITION:
            ret = audio_ext_get_presentation_position(out,
                      (struct audio_out_presentation_position_param *)payload);
                if (ret < 0)
                    ALOGE("%s:: presentation position query failed error %d",
                           __func__, ret);
            break;
        default:
            ALOGE("%s:: unsupported param_id %d", __func__, param_id);
            break;
    }

    return ret;
}

/* API to set capture stream specific config parameters */
int audio_extn_in_set_param_data(struct stream_in *in,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload) {
    int ret = -EINVAL;

    if (!in || !payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    ALOGD("%s: enter: stream (%p) usecase(%d: %s) param_id %d", __func__,
            in, in->usecase, use_case_table[in->usecase], param_id);

    switch (param_id) {
        case AUDIO_EXTN_PARAM_IN_TTP_OFFSET:
            ret = audio_extn_compress_in_set_ttp_offset(in,
                    (struct audio_in_ttp_offset_param *)(payload));
            break;
        default:
            ALOGE("%s:: unsupported param_id %d", __func__, param_id);
            break;
    }
    return ret;
}

int audio_extn_set_device_cfg_params(struct audio_device *adev,
                                     struct audio_device_cfg_param *payload)
{
    struct audio_device_cfg_param *device_cfg_params = payload;
    int ret = -EINVAL;
    struct stream_out out;
    uint32_t snd_device = 0, backend_idx = 0;
    struct audio_device_config_param *adev_device_cfg_ptr;

    ALOGV("%s", __func__);

    if (!device_cfg_params || !adev || !adev->device_cfg_params) {
        ALOGE("%s:: Invalid Param", __func__);
        return ret;
    }

    /* Config is not supported for combo devices */
    if (popcount(device_cfg_params->device) != 1) {
        ALOGE("%s:: Invalid Device (%#x) - Config is ignored", __func__, device_cfg_params->device);
        return ret;
    }

    adev_device_cfg_ptr = adev->device_cfg_params;
    /* Create an out stream to get snd device from audio device */
    out.devices = device_cfg_params->device;
    out.sample_rate = device_cfg_params->sample_rate;
    snd_device = platform_get_output_snd_device(adev->platform, &out);
    backend_idx = platform_get_backend_index(snd_device);

    ALOGV("%s:: device %d sample_rate %d snd_device %d backend_idx %d",
                __func__, out.devices, out.sample_rate, snd_device, backend_idx);

    ALOGV("%s:: Device Config Params from Client samplerate %d  channels %d"
          " bit_width %d  format %d  device %d  channel_map[0] %d channel_map[1] %d"
          " channel_map[2] %d channel_map[3] %d channel_map[4] %d channel_map[5] %d"
          " channel_allocation %d\n", __func__, device_cfg_params->sample_rate,
          device_cfg_params->channels, device_cfg_params->bit_width,
          device_cfg_params->format, device_cfg_params->device,
          device_cfg_params->channel_map[0], device_cfg_params->channel_map[1],
          device_cfg_params->channel_map[2], device_cfg_params->channel_map[3],
          device_cfg_params->channel_map[4], device_cfg_params->channel_map[5],
          device_cfg_params->channel_allocation);

    /* Copy the config values into adev structure variable */
    adev_device_cfg_ptr += backend_idx;
    adev_device_cfg_ptr->use_client_dev_cfg = true;
    memcpy(&adev_device_cfg_ptr->dev_cfg_params, device_cfg_params, sizeof(struct audio_device_cfg_param));

    return 0;
}

int audio_extn_set_pll_device_cfg_params(struct audio_device *adev,
                                     struct audio_pll_device_cfg_param *payload)
{
    int ret = 0;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "PLL config data";
    uint32_t snd_device = 0, backend_idx = 0;
    struct stream_out out;
    struct audio_pll_device_cfg_param *dev_cfg_params = payload;
    struct pll_device_config_params pll_device_cfg_params;

    ALOGV("%s\n", __func__);

    if (!dev_cfg_params || !adev) {
        ALOGE("Invalid param\n");
        ret = -EINVAL;
        goto err;
    }

    /* Config is not supported for combo devices */
    if (popcount(dev_cfg_params->device) != 1) {
        ALOGE("%s:: Invalid Device (%#x) - Config is ignored\n",
              __func__, dev_cfg_params->device);
        ret = -EINVAL;
        goto err;
    }

    memset(&out, 0, sizeof(struct stream_out));

    out.devices = dev_cfg_params->device;
    snd_device = platform_get_output_snd_device(adev->platform, &out);
    if (snd_device < SND_DEVICE_MIN || snd_device >= SND_DEVICE_MAX) {
        ALOGE("%s: Invalid sound device %d", __func__, snd_device);
        ret = -EINVAL;
        goto err;
    }

    backend_idx = platform_get_snd_device_backend_index(snd_device);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        ret = -EINVAL;
        goto err;
    }

    memset(&pll_device_cfg_params, 0, sizeof(pll_device_cfg_params));

    pll_device_cfg_params.be_idx = backend_idx;
    pll_device_cfg_params.drift = dev_cfg_params->drift;
    pll_device_cfg_params.reset = (uint32_t)dev_cfg_params->reset;

    /* trigger mixer control to send clock drift value */
    ret = mixer_ctl_set_array(ctl, &pll_device_cfg_params,
                   sizeof(struct pll_device_config_params));
    if (ret < 0)
        ALOGE("%s:[%d] Could not set ctl for mixer cmd - %s, ret %d",
              __func__, pll_device_cfg_params.drift, mixer_ctl_name, ret);
err:
    return ret;
}

//START: FM_POWER_OPT_FEATURE ================================================================
void fm_feature_init(bool is_feature_enabled)
{
    audio_extn_fm_power_opt_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature FM_POWER_OPT is %s----", __func__, is_feature_enabled? "ENABLED": "NOT ENABLED");
}


void audio_extn_fm_get_parameters(struct str_parms *query, struct str_parms *reply)
{
    if(audio_extn_fm_power_opt_enabled)
        fm_get_parameters(query, reply);
}

void audio_extn_fm_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms)
{
    if(audio_extn_fm_power_opt_enabled)
        fm_set_parameters(adev, parms);
}
//END: FM_POWER_OPT_FEATURE ================================================================

//START: HDMI_EDID =========================================================================
#ifdef __LP64__
#if LINUX_ENABLED
#define HDMI_EDID_LIB_PATH  "/usr/lib64/audio.hdmi.edid.so"
#else
#define HDMI_EDID_LIB_PATH  "/vendor/lib64/libhdmiedid.so"
#endif
#else
#if LINUX_ENABLED
#define HDMI_EDID_LIB_PATH  "/usr/lib/audio.hdmi.edid.so"
#else
#define HDMI_EDID_LIB_PATH  "/vendor/lib/libhdmiedid.so"
#endif
#endif

static void *hdmi_edid_lib_handle = NULL;

typedef bool (*hdmi_edid_is_supported_sr_t)(edid_audio_info*, int);
static hdmi_edid_is_supported_sr_t hdmi_edid_is_supported_sr;

typedef bool (*hdmi_edid_is_supported_bps_t)(edid_audio_info*, int);
static hdmi_edid_is_supported_bps_t hdmi_edid_is_supported_bps;

typedef int (*hdmi_edid_get_highest_supported_sr_t)(edid_audio_info*);
static hdmi_edid_get_highest_supported_sr_t hdmi_edid_get_highest_supported_sr;

typedef bool (*hdmi_edid_get_sink_caps_t)(edid_audio_info*, char*);
static hdmi_edid_get_sink_caps_t hdmi_edid_get_sink_caps;

void hdmi_edid_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: HDMI_EDID feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        hdmi_edid_lib_handle = dlopen(HDMI_EDID_LIB_PATH, RTLD_NOW);
        if (hdmi_edid_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((hdmi_edid_is_supported_sr =
             (hdmi_edid_is_supported_sr_t)dlsym(hdmi_edid_lib_handle,
                                                "edid_is_supported_sr")) == NULL) ||
            ((hdmi_edid_is_supported_bps =
             (hdmi_edid_is_supported_bps_t)dlsym(hdmi_edid_lib_handle,
                                                "edid_is_supported_bps")) == NULL) ||
            ((hdmi_edid_get_highest_supported_sr =
             (hdmi_edid_get_highest_supported_sr_t)dlsym(hdmi_edid_lib_handle,
                                                "edid_get_highest_supported_sr")) == NULL) ||
            ((hdmi_edid_get_sink_caps =
             (hdmi_edid_get_sink_caps_t)dlsym(hdmi_edid_lib_handle,
                                                "edid_get_sink_caps")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature HDMI_EDID is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (hdmi_edid_lib_handle) {
        dlclose(hdmi_edid_lib_handle);
        hdmi_edid_lib_handle = NULL;
    }

    hdmi_edid_is_supported_sr = NULL;
    hdmi_edid_is_supported_bps = NULL;
    hdmi_edid_get_highest_supported_sr = NULL;
    hdmi_edid_get_sink_caps = NULL;
    ALOGW(":: %s: ---- Feature HDMI_EDID is disabled ----", __func__);
    return;
}

bool audio_extn_edid_is_supported_sr(edid_audio_info* info, int sr)
{
    bool ret = false;

    if(hdmi_edid_is_supported_sr != NULL)
        ret = hdmi_edid_is_supported_sr(info, sr);
    return ret;
}

bool audio_extn_edid_is_supported_bps(edid_audio_info* info, int bps)
{
    bool ret = false;

    if(hdmi_edid_is_supported_bps != NULL)
        ret = hdmi_edid_is_supported_bps(info, bps);
    return ret;
}
int audio_extn_edid_get_highest_supported_sr(edid_audio_info* info)
{
    int ret = -1;

    if(hdmi_edid_get_highest_supported_sr != NULL)
        ret = hdmi_edid_get_highest_supported_sr(info);
    return ret;
}

bool audio_extn_edid_get_sink_caps(edid_audio_info* info, char *edid_data)
{
    bool ret = false;

    if(hdmi_edid_get_sink_caps != NULL)
        ret = hdmi_edid_get_sink_caps(info, edid_data);
    return ret;
}

//END: HDMI_EDID =========================================================================


//START: KEEP_ALIVE =========================================================================

void keep_alive_feature_init(bool is_feature_enabled)
{
    audio_extn_keep_alive_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature KEEP_ALIVE is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

void audio_extn_keep_alive_init(struct audio_device *adev)
{
    if(audio_extn_keep_alive_enabled)
        keep_alive_init(adev);
}

void audio_extn_keep_alive_deinit()
{
    if(audio_extn_keep_alive_enabled)
        keep_alive_deinit();
}

void audio_extn_keep_alive_start(ka_mode_t ka_mode)
{
    if(audio_extn_keep_alive_enabled)
        keep_alive_start(ka_mode);
}

void audio_extn_keep_alive_stop(ka_mode_t ka_mode)
{
    if(audio_extn_keep_alive_enabled)
        keep_alive_stop(ka_mode);
}

bool audio_extn_keep_alive_is_active()
{
    bool ret = false;
    return ret;
}

int audio_extn_keep_alive_set_parameters(struct audio_device *adev,
                                         struct str_parms *parms)
{
    int ret = -1;
    if(audio_extn_keep_alive_enabled)
        return keep_alive_set_parameters(adev, parms);
    return ret;
}
//END: KEEP_ALIVE =========================================================================

//START: HIFI_AUDIO =========================================================================
void hifi_audio_feature_init(bool is_feature_enabled)
{
    audio_extn_hifi_audio_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature HIFI_AUDIO is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_hifi_audio_enabled(void)
{
    bool ret = false;
    if(audio_extn_hifi_audio_enabled)
    {
        ALOGV("%s: status: %d", __func__, aextnmod.hifi_audio_enabled);
        return (aextnmod.hifi_audio_enabled ? true: false);
    }
    return ret;
}

bool audio_extn_is_hifi_audio_supported(void)
{
    bool ret = false;

    if(audio_extn_hifi_audio_enabled)
    {
        /*
         * for internal codec, check for hifiaudio property to enable hifi audio
         */
        if (property_get_bool("persist.vendor.audio.hifi.int_codec", false))
        {
            ALOGD("%s: hifi audio supported on internal codec", __func__);
            aextnmod.hifi_audio_enabled = 1;
        }
        return (aextnmod.hifi_audio_enabled ? true: false);
    }
    return ret;
}

//END: HIFI_AUDIO =========================================================================

//START: RAS =============================================================================
void ras_feature_init(bool is_feature_enabled)
{
    audio_extn_ras_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature RAS_FEATURE is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_ras_enabled(void)
{
    bool ret = false;

    if(audio_extn_ras_feature_enabled)
    {
        ALOGD("%s: status: %d", __func__, aextnmod.ras_enabled);
        return (aextnmod.ras_enabled ? true: false);
    }
    return ret;
}

bool audio_extn_can_use_ras(void)
{
    bool ret = false;

    if(audio_extn_ras_feature_enabled)
    {
        if (property_get_bool("persist.vendor.audio.ras.enabled", false))
            aextnmod.ras_enabled = 1;
        ALOGD("%s: ras.enabled property is set to %d", __func__, aextnmod.ras_enabled);
        return (aextnmod.ras_enabled ? true: false);
    }
    return ret;
}

//END: RAS ===============================================================================

//START: KPI_OPTIMIZE =============================================================================
void kpi_optimize_feature_init(bool is_feature_enabled)
{
    audio_extn_kpi_optimize_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature KPI_OPTIMIZE is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

typedef int (*perf_lock_acquire_t)(int, int, int*, int);
typedef int (*perf_lock_release_t)(int);

static void *qcopt_handle;
static perf_lock_acquire_t perf_lock_acq;
static perf_lock_release_t perf_lock_rel;

char opt_lib_path[512] = {0};

int audio_extn_perf_lock_init(void)
{
    int ret = 0;

    //if feature is disabled, exit immediately
    if(!audio_extn_kpi_optimize_feature_enabled)
        goto err;

    if (qcopt_handle == NULL) {
        if (property_get("ro.vendor.extension_library",
                         opt_lib_path, NULL) <= 0) {
            ALOGE("%s: Failed getting perf property \n", __func__);
            ret = -EINVAL;
            goto err;
        }
        if ((qcopt_handle = dlopen(opt_lib_path, RTLD_NOW)) == NULL) {
            ALOGE("%s: Failed to open perf handle \n", __func__);
            ret = -EINVAL;
            goto err;
        } else {
            perf_lock_acq = (perf_lock_acquire_t)dlsym(qcopt_handle,
                                                       "perf_lock_acq");
            if (perf_lock_acq == NULL) {
                ALOGE("%s: Perf lock Acquire NULL \n", __func__);
                dlclose(qcopt_handle);
                ret = -EINVAL;
                goto err;
            }
            perf_lock_rel = (perf_lock_release_t)dlsym(qcopt_handle,
                                                       "perf_lock_rel");
            if (perf_lock_rel == NULL) {
                ALOGE("%s: Perf lock Release NULL \n", __func__);
                dlclose(qcopt_handle);
                ret = -EINVAL;
                goto err;
            }
            ALOGE("%s: Perf lock handles Success \n", __func__);
        }
    }
err:
    return ret;
}

void audio_extn_perf_lock_acquire(int *handle, int duration,
                                 int *perf_lock_opts, int size)
{
    if (audio_extn_kpi_optimize_feature_enabled)
    {
        if (!perf_lock_opts || !size || !perf_lock_acq || !handle) {
            ALOGE("%s: Incorrect params, Failed to acquire perf lock, err ",
                  __func__);
            return;
        }
        /*
         * Acquire performance lock for 1 sec during device path bringup.
         * Lock will be released either after 1 sec or when perf_lock_release
         * function is executed.
         */
        *handle = perf_lock_acq(*handle, duration, perf_lock_opts, size);
        if (*handle <= 0)
            ALOGE("%s: Failed to acquire perf lock, err: %d\n",
                  __func__, *handle);
    }
}

void audio_extn_perf_lock_release(int *handle)
{
    if (audio_extn_kpi_optimize_feature_enabled) {
         if (perf_lock_rel && handle && (*handle > 0)) {
            perf_lock_rel(*handle);
            *handle = 0;
        } else
            ALOGE("%s: Perf lock release error \n", __func__);
    }
}

//END: KPI_OPTIMIZE =============================================================================

//START: DISPLAY_PORT =============================================================================
void display_port_feature_init(bool is_feature_enabled)
{
    audio_extn_display_port_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature DISPLAY_PORT is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_display_port_enabled()
{
    return audio_extn_display_port_feature_enabled;
}
//END: DISPLAY_PORT ===============================================================================
//START: FLUENCE =============================================================================
void fluence_feature_init(bool is_feature_enabled)
{
    audio_extn_fluence_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature FLUENCE is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_fluence_enabled()
{
    return audio_extn_fluence_feature_enabled;
}

void audio_extn_set_fluence_parameters(struct audio_device *adev,
                                            struct str_parms *parms)
{
    int ret = 0, err;
    char value[32];
    struct listnode *node;
    struct audio_usecase *usecase;

    if (audio_extn_is_fluence_enabled()) {
        err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE,
                                 value, sizeof(value));
        ALOGV_IF(err >= 0, "%s: Set Fluence Type to %s", __func__, value);
        if (err >= 0) {
            ret = platform_set_fluence_type(adev->platform, value);
            if (ret != 0) {
                ALOGE("platform_set_fluence_type returned error: %d", ret);
            } else {
                /*
                 *If the fluence is manually set/reset, devices
                 *need to get updated for all the usecases
                 *i.e. audio and voice.
                 */
                 list_for_each(node, &adev->usecase_list) {
                     usecase = node_to_item(node, struct audio_usecase, list);
                     select_devices(adev, usecase->id);
                 }
            }
        }

    }
}

int audio_extn_get_fluence_parameters(const struct audio_device *adev,
                       struct str_parms *query, struct str_parms *reply)
{
    int ret = -1, err;
    char value[256] = {0};

    if (audio_extn_is_fluence_enabled()) {
        err = str_parms_get_str(query, AUDIO_PARAMETER_KEY_FLUENCE, value,
                                                          sizeof(value));
        if (err >= 0) {
            ret = platform_get_fluence_type(adev->platform, value, sizeof(value));
            if (ret >= 0) {
                ALOGV("%s: Fluence Type is %s", __func__, value);
                str_parms_add_str(reply, AUDIO_PARAMETER_KEY_FLUENCE, value);
            } else
                goto done;
        }
    }
done:
    return ret;
}
//END: FLUENCE ===============================================================================
//START: CUSTOM_STEREO =============================================================================
void custom_stereo_feature_init(bool is_feature_enabled)
{
    audio_extn_custom_stereo_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature CUSTOM_STEREO is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_custom_stereo_enabled()
{
    return audio_extn_custom_stereo_feature_enabled;
}

void audio_extn_customstereo_set_parameters(struct audio_device *adev,
                                           struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};
    bool custom_stereo_state = false;
    const char *mixer_ctl_name = "Set Custom Stereo OnOff";
    struct mixer_ctl *ctl;

    ALOGV("%s", __func__);

    if (audio_extn_custom_stereo_feature_enabled) {
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_CUSTOM_STEREO, value,
                            sizeof(value));
        if (ret >= 0) {
            if (!strncmp("true", value, sizeof("true")) || atoi(value))
                custom_stereo_state = true;

            if (custom_stereo_state == aextnmod.custom_stereo_enabled)
                return;

            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
            if (!ctl) {
                ALOGE("%s: Could not get ctl for mixer cmd - %s",
                      __func__, mixer_ctl_name);
                return;
            }
            if (mixer_ctl_set_value(ctl, 0, custom_stereo_state) < 0) {
                ALOGE("%s: Could not set custom stereo state %d",
                      __func__, custom_stereo_state);
                return;
            }
            aextnmod.custom_stereo_enabled = custom_stereo_state;
            ALOGV("%s: Setting custom stereo state success", __func__);
        }
    }
}

void audio_extn_send_dual_mono_mixing_coefficients(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl;
    char mixer_ctl_name[128];
    int cust_ch_mixer_cfg[128], len = 0;
    int ip_channel_cnt = audio_channel_count_from_out_mask(out->channel_mask);
    int pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
    int op_channel_cnt= 2;
    int i, j, err;

    ALOGV("%s", __func__);

    if (audio_extn_custom_stereo_feature_enabled) {
        if (!out->started) {
        out->set_dual_mono = true;
        goto exit;
        }

        ALOGD("%s: i/p channel count %d, o/p channel count %d, pcm id %d", __func__,
               ip_channel_cnt, op_channel_cnt, pcm_device_id);

        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                 "Audio Stream %d Channel Mix Cfg", pcm_device_id);
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
            __func__, mixer_ctl_name);
            goto exit;
        }

        /* Output channel count corresponds to backend configuration channels.
         * Input channel count corresponds to ASM session channels.
         * Set params is called with channels that need to be selected from
         * input to generate output.
         * ex: "8,2" to downmix from 8 to 2 i.e. to downmix from 8 to 2,
         *
         * This mixer control takes values in the following sequence:
         * - input channel count(m)
         * - output channel count(n)
         * - weight coeff for [out ch#1, in ch#1]
         * ....
         * - weight coeff for [out ch#1, in ch#m]
         *
         * - weight coeff for [out ch#2, in ch#1]
         * ....
         * - weight coeff for [out ch#2, in ch#m]
         *
         * - weight coeff for [out ch#n, in ch#1]
         * ....
         * - weight coeff for [out ch#n, in ch#m]
         *
         * To get dualmono ouptu weightage coeff is calculated as Unity gain
         * divided by number of input channels.
         */
        cust_ch_mixer_cfg[len++] = ip_channel_cnt;
        cust_ch_mixer_cfg[len++] = op_channel_cnt;
        for (i = 0; i < op_channel_cnt; i++) {
             for (j = 0; j < ip_channel_cnt; j++) {
                  cust_ch_mixer_cfg[len++] = Q14_GAIN_UNITY/ip_channel_cnt;
             }
        }

        err = mixer_ctl_set_array(ctl, cust_ch_mixer_cfg, len);
        if (err)
            ALOGE("%s: ERROR. Mixer ctl set failed", __func__);

    }
exit:
    return;
}
//END: CUSTOM_STEREO =============================================================================
// START: A2DP_OFFLOAD ===================================================================
#ifdef __LP64__
#if LINUX_ENABLED
#define A2DP_OFFLOAD_LIB_PATH "/usr/lib64/audio.a2dp.offload.so"
#else
#define A2DP_OFFLOAD_LIB_PATH "/vendor/lib64/liba2dpoffload.so"
#endif
#else
#if LINUX_ENABLED
#define A2DP_OFFLOAD_LIB_PATH "/usr/lib/audio.a2dp.offload.so"
#else
#define A2DP_OFFLOAD_LIB_PATH "/vendor/lib/liba2dpoffload.so"
#endif
#endif

static void *a2dp_lib_handle = NULL;

typedef void (*a2dp_init_t)(void *, a2dp_offload_init_config_t);
static a2dp_init_t a2dp_init;

typedef int (*a2dp_start_playback_t)();
static a2dp_start_playback_t a2dp_start_playback;

typedef int (*a2dp_stop_playback_t)();
static a2dp_stop_playback_t a2dp_stop_playback;

typedef int (*a2dp_set_parameters_t)(struct str_parms *,
                                     bool *);
static a2dp_set_parameters_t a2dp_set_parameters;

typedef int (*a2dp_get_parameters_t)(struct str_parms *,
                                   struct str_parms *);
static a2dp_get_parameters_t a2dp_get_parameters;

typedef bool (*a2dp_is_force_device_switch_t)();
static a2dp_is_force_device_switch_t a2dp_is_force_device_switch;

typedef void (*a2dp_set_handoff_mode_t)(bool);
static a2dp_set_handoff_mode_t a2dp_set_handoff_mode;

typedef void (*a2dp_get_enc_sample_rate_t)(int *);
static a2dp_get_enc_sample_rate_t a2dp_get_enc_sample_rate;

typedef void (*a2dp_get_dec_sample_rate_t)(int *);
static a2dp_get_dec_sample_rate_t a2dp_get_dec_sample_rate;

typedef uint32_t (*a2dp_get_encoder_latency_t)();
static a2dp_get_encoder_latency_t a2dp_get_encoder_latency;

typedef bool (*a2dp_sink_is_ready_t)();
static a2dp_sink_is_ready_t a2dp_sink_is_ready;

typedef bool (*a2dp_source_is_ready_t)();
static a2dp_source_is_ready_t a2dp_source_is_ready;

typedef bool (*a2dp_source_is_suspended_t)();
static a2dp_source_is_suspended_t a2dp_source_is_suspended;

typedef int (*a2dp_start_capture_t)();
static a2dp_start_capture_t a2dp_start_capture;

typedef int (*a2dp_stop_capture_t)();
static a2dp_stop_capture_t a2dp_stop_capture;

int a2dp_offload_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__,
                  is_feature_enabled ? "Enabled" : "NOT Enabled");
    if (is_feature_enabled) {
        // dlopen lib
        a2dp_lib_handle = dlopen(A2DP_OFFLOAD_LIB_PATH, RTLD_NOW);

        if (!a2dp_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }

        if (!(a2dp_init = (a2dp_init_t)dlsym(a2dp_lib_handle, "a2dp_init")) ||
            !(a2dp_start_playback =
                 (a2dp_start_playback_t)dlsym(a2dp_lib_handle, "a2dp_start_playback")) ||
            !(a2dp_stop_playback =
                 (a2dp_stop_playback_t)dlsym(a2dp_lib_handle, "a2dp_stop_playback")) ||
            !(a2dp_set_parameters =
                 (a2dp_set_parameters_t)dlsym(a2dp_lib_handle, "a2dp_set_parameters")) ||
            !(a2dp_get_parameters =
                 (a2dp_get_parameters_t)dlsym(a2dp_lib_handle, "a2dp_get_parameters")) ||
            !(a2dp_is_force_device_switch =
                 (a2dp_is_force_device_switch_t)dlsym(
                                    a2dp_lib_handle, "a2dp_is_force_device_switch")) ||
            !(a2dp_set_handoff_mode =
                 (a2dp_set_handoff_mode_t)dlsym(
                                          a2dp_lib_handle, "a2dp_set_handoff_mode")) ||
            !(a2dp_get_enc_sample_rate =
                 (a2dp_get_enc_sample_rate_t)dlsym(
                                       a2dp_lib_handle, "a2dp_get_enc_sample_rate")) ||
            !(a2dp_get_encoder_latency =
                 (a2dp_get_encoder_latency_t)dlsym(
                                       a2dp_lib_handle, "a2dp_get_encoder_latency")) ||
            !(a2dp_source_is_ready =
                 (a2dp_source_is_ready_t)dlsym(a2dp_lib_handle, "a2dp_source_is_ready")) ||
            !(a2dp_source_is_suspended =
                 (a2dp_source_is_suspended_t)dlsym(
                                       a2dp_lib_handle, "a2dp_source_is_suspended"))) {
            ALOGE("%s: dlsym failed", __func__);

            goto feature_disabled;
        }
        ALOGD("%s:: ---- Feature A2DP_OFFLOAD is Enabled ----", __func__);
        return 0;
    }

feature_disabled:
    if (a2dp_lib_handle) {
        dlclose(a2dp_lib_handle);
        a2dp_lib_handle = NULL;
    }

    a2dp_init = NULL;
    a2dp_start_playback= NULL;
    a2dp_stop_playback = NULL;
    a2dp_set_parameters = NULL;
    a2dp_get_parameters = NULL;
    a2dp_is_force_device_switch = NULL;
    a2dp_set_handoff_mode = NULL;
    a2dp_get_enc_sample_rate = NULL;
    a2dp_get_dec_sample_rate = NULL;
    a2dp_get_encoder_latency = NULL;
    a2dp_sink_is_ready = NULL;
    a2dp_source_is_ready = NULL;
    a2dp_source_is_suspended = NULL;
    a2dp_start_capture = NULL;
    a2dp_stop_capture = NULL;

    ALOGW(":: %s: ---- Feature A2DP_OFFLOAD is disabled ----", __func__);
    return -ENOSYS;
}

void audio_extn_a2dp_init(void *adev)
{
    if (a2dp_init) {
        a2dp_offload_init_config_t a2dp_init_config;
        a2dp_init_config.fp_platform_get_pcm_device_id = platform_get_pcm_device_id;
        a2dp_init_config.fp_check_a2dp_restore = check_a2dp_restore;

        a2dp_init(adev, a2dp_init_config);
    }
}

int audio_extn_a2dp_start_playback()
{
    return (a2dp_start_playback ? a2dp_start_playback() : 0);
}

int audio_extn_a2dp_stop_playback()
{
    return (a2dp_stop_playback ? a2dp_stop_playback() : 0);
}

int audio_extn_a2dp_set_parameters(struct str_parms *parms,
                                   bool *reconfig)
{
    return (a2dp_set_parameters ?
                    a2dp_set_parameters(parms, reconfig) : 0);
}

int audio_extn_a2dp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply)
{
    return (a2dp_get_parameters ?
                    a2dp_get_parameters(query, reply) : 0);
}

bool audio_extn_a2dp_is_force_device_switch()
{
    return (a2dp_is_force_device_switch ?
                a2dp_is_force_device_switch() : false);
}

void audio_extn_a2dp_set_handoff_mode(bool is_on)
{
    if (a2dp_set_handoff_mode)
        a2dp_set_handoff_mode(is_on);
}

void audio_extn_a2dp_get_enc_sample_rate(int *sample_rate)
{
    if (a2dp_get_enc_sample_rate)
        a2dp_get_enc_sample_rate(sample_rate);
}

uint32_t audio_extn_a2dp_get_encoder_latency()
{
    return (a2dp_get_encoder_latency ?
                a2dp_get_encoder_latency() : 0);
}


bool audio_extn_a2dp_source_is_ready()
{
    return (a2dp_source_is_ready ?
                a2dp_source_is_ready() : false);
}

bool audio_extn_a2dp_source_is_suspended()
{
    return (a2dp_source_is_suspended ?
                a2dp_source_is_suspended() : false);
}


// END: A2DP_OFFLOAD =====================================================================
void audio_extn_feature_init()
{
    for(int index = 0; index < MAX_SUPPORTED_FEATURE; index++)
    {
        bool enable = audio_feature_manager_is_feature_enabled(index);
        switch (index) {
            case SND_MONITOR:
                snd_mon_feature_init(enable);
                break;
            case COMPRESS_CAPTURE:
                compr_cap_feature_init(enable);
                break;
            case DSM_FEEDBACK:
                dsm_feedback_feature_init(enable);
                break;
            case SSREC:
                ssrec_feature_init(enable);
                break;
            case SOURCE_TRACK:
                src_trkn_feature_init(enable);
            case HDMI_EDID:
                hdmi_edid_feature_init(enable);
                break;
            case KEEP_ALIVE:
                keep_alive_feature_init(enable);
                break;
            case HIFI_AUDIO:
                hifi_audio_feature_init(enable);
                break;
            case RECEIVER_AIDED_STEREO:
                ras_feature_init(enable);
                break;
            case KPI_OPTIMIZE:
                kpi_optimize_feature_init(enable);
            case USB_OFFLOAD:
                usb_offload_feature_init(enable);
                break;
            case USB_OFFLOAD_BURST_MODE:
                usb_offload_burst_mode_feature_init(enable);
                break;
            case USB_OFFLOAD_SIDETONE_VOLM:
                usb_offload_sidetone_volume_feature_init(enable);
                break;
            case A2DP_OFFLOAD:
                a2dp_offload_feature_init(enable);
                break;
            case COMPRESS_METADATA_NEEDED:
                compress_meta_data_feature_init(enable);
                break;
            case VBAT:
                vbat_feature_init(enable);
                break;
            case DISPLAY_PORT:
                display_port_feature_init(enable);
                break;
            case FLUENCE:
                fluence_feature_init(enable);
                break;
             case CUSTOM_STEREO:
                custom_stereo_feature_init(enable);
                break;
            case ANC_HEADSET:
                anc_headset_feature_init(enable);
                break;
            case SPKR_PROT:
                spkr_prot_feature_init(enable);
                break;
            default:
                break;
        }
    }
}

void audio_extn_set_parameters(struct audio_device *adev,
                               struct str_parms *parms)
{
   bool a2dp_reconfig = false;

   audio_extn_set_aanc_noise_level(adev, parms);
   audio_extn_set_anc_parameters(adev, parms);
   audio_extn_set_fluence_parameters(adev, parms);
   audio_extn_set_afe_proxy_parameters(adev, parms);
   audio_extn_fm_set_parameters(adev, parms);
   audio_extn_sound_trigger_set_parameters(adev, parms);
   audio_extn_listen_set_parameters(adev, parms);
   audio_extn_ssr_set_parameters(adev, parms);
   audio_extn_hfp_set_parameters(adev, parms);
   audio_extn_dts_eagle_set_parameters(adev, parms);
   audio_extn_a2dp_set_parameters(parms, &a2dp_reconfig);
   audio_extn_ddp_set_parameters(adev, parms);
   audio_extn_ds2_set_parameters(adev, parms);
   audio_extn_customstereo_set_parameters(adev, parms);
   audio_extn_hpx_set_parameters(adev, parms);
   audio_extn_pm_set_parameters(parms);
   audio_extn_source_track_set_parameters(adev, parms);
   audio_extn_fbsp_set_parameters(parms);
   audio_extn_keep_alive_set_parameters(adev, parms);
   audio_extn_passthru_set_parameters(adev, parms);
   audio_extn_ext_disp_set_parameters(adev, parms);
   audio_extn_qaf_set_parameters(adev, parms);
   if (adev->offload_effects_set_parameters != NULL)
       adev->offload_effects_set_parameters(parms);
   audio_extn_set_aptx_dec_bt_addr(adev, parms);
   audio_extn_ffv_set_parameters(adev, parms);
   audio_extn_ext_hw_plugin_set_parameters(adev->ext_hw_plugin, parms);
   audio_extn_set_clock_switch_params(adev, parms);
}

void audio_extn_get_parameters(const struct audio_device *adev,
                              struct str_parms *query,
                              struct str_parms *reply)
{
    char *kv_pairs = NULL;
    audio_extn_get_afe_proxy_parameters(adev, query, reply);
    audio_extn_get_fluence_parameters(adev, query, reply);
    audio_extn_ssr_get_parameters(adev, query, reply);
    get_active_offload_usecases(adev, query, reply);
    audio_extn_dts_eagle_get_parameters(adev, query, reply);
    audio_extn_hpx_get_parameters(query, reply);
    audio_extn_source_track_get_parameters(adev, query, reply);
    audio_extn_fbsp_get_parameters(query, reply);
    audio_extn_sound_trigger_get_parameters(adev, query, reply);
    audio_extn_fm_get_parameters(query, reply);
    if (adev->offload_effects_get_parameters != NULL)
        adev->offload_effects_get_parameters(query, reply);
    audio_extn_ext_hw_plugin_get_parameters(adev->ext_hw_plugin, query, reply);

    kv_pairs = str_parms_to_str(reply);
    ALOGD_IF(kv_pairs != NULL, "%s: returns %s", __func__, kv_pairs);
    free(kv_pairs);
}

int audio_ext_get_presentation_position(struct stream_out *out,
                           struct audio_out_presentation_position_param *pos_param)
{
    int ret = -ENODATA;

    if (!out) {
        ALOGE("%s:: Invalid stream",__func__);
        return ret;
    }

    if (is_offload_usecase(out->usecase)) {
        if (out->compr != NULL)
            ret = audio_extn_utils_compress_get_dsp_presentation_pos(out,
                                  &pos_param->frames, &pos_param->timestamp, pos_param->clock_id);
    } else {
        if (out->pcm)
            ret = audio_extn_utils_pcm_get_dsp_presentation_pos(out,
                                  &pos_param->frames, &pos_param->timestamp, pos_param->clock_id);
    }

    ALOGV("%s frames %lld timestamp %lld", __func__, (long long int)pos_param->frames,
           pos_param->timestamp.tv_sec*1000000000LL + pos_param->timestamp.tv_nsec);

    return ret;
}

#ifdef TONE_ENABLED
int audio_extn_set_tone_parameters(struct stream_out *out,
                                  struct str_parms *parms)
{
    int value = 0;
    int ret = 0, err = 0;
    char *kv_pairs = str_parms_to_str(parms);
    char str_value[256] = {0};

    ALOGV_IF(kv_pairs != NULL, "%s: enter: %s", __func__, kv_pairs);

    err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_TONE_GAIN, &value);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_TONE_GAIN);
        int32_t tone_gain = value;

        voice_extn_dtmf_set_rx_tone_gain(out, tone_gain);
    }
    err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_TONE_LOW_FREQ, &value);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_TONE_LOW_FREQ);
        uint32_t tone_low_freq = value;
        uint32_t tone_high_freq = 0;
        uint32_t tone_duration_ms = 0;
        err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_TONE_HIGH_FREQ, &value);
        if (err >= 0) {
            tone_high_freq = value;
            str_parms_del(parms, AUDIO_PARAMETER_KEY_TONE_HIGH_FREQ);
        } else {
            ALOGE("%s: tone_high_freq key not found", __func__);
            ret = -EINVAL;
            goto done;
        }
        err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_TONE_DURATION_MS, &value);
        if (err >= 0) {
            tone_duration_ms = value;
            str_parms_del(parms, AUDIO_PARAMETER_KEY_TONE_DURATION_MS);
        } else {
            ALOGE("%s: tone duration key not found, setting to default infinity",
                  __func__);
            tone_duration_ms = 0xFFFF;
        }
        voice_extn_dtmf_generate_rx_tone(out, tone_low_freq, tone_high_freq,
                                         tone_duration_ms);
    }
    err = str_parms_has_key(parms, AUDIO_PARAMETER_KEY_TONE_OFF);
    if (err > 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_TONE_OFF);
        voice_extn_dtmf_set_rx_tone_off(out);
    }

done:
    ALOGV("%s: exit with code(%d)", __func__, ret);
    free(kv_pairs);
    return ret;
}

#endif
