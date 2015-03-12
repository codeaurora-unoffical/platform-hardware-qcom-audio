/*
* Copyright (c) 2015-16, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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
#define LOG_TAG "split_a2dp"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0
#include <errno.h>
#include <cutils/log.h>
#include <dlfcn.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <cutils/properties.h>


#ifdef SPLIT_A2DP_ENABLED


typedef bool (*audio_stream_open_t)(void);
typedef bool (*audio_stream_close_t)(void);
typedef bool (*audio_start_stream_t)(void);
typedef bool (*audio_stop_stream_t)(void);
typedef bool (*audio_suspend_stream_t)(void);
typedef void (*audio_handoff_triggered_t)(void);
typedef void (*clear_a2dpsuspend_flag_t)(void);
typedef void * (*audio_get_codec_config_t)(uint8_t *multicast_status,uint8_t *num_dev,
                               audio_format_t *codec_type);

enum A2DP_STATE {
    A2DP_STATE_CONNECTED,
    A2DP_STATE_STARTED,
    A2DP_STATE_STOPPED,
    A2DP_STATE_DISCONNECTED,
};

struct a2dp_data{
    struct audio_device *adev;
    void *bt_lib_handle;
    audio_stream_open_t audio_stream_open;
    audio_stream_close_t audio_stream_close;
    audio_start_stream_t audio_start_stream;
    audio_stop_stream_t audio_stop_stream;
    audio_suspend_stream_t audio_suspend_stream;
    audio_handoff_triggered_t audio_handoff_triggered;
    clear_a2dpsuspend_flag_t clear_a2dpsuspend_flag;
    audio_get_codec_config_t audio_get_codec_config;
    enum A2DP_STATE bt_state;
    audio_format_t bt_encoder_format;
    void *enc_config_data;
    bool a2dp_started;
    bool a2dp_suspended;
    int  a2dp_total_active_session_request;
    bool is_a2dp_offload_supported;
};

struct a2dp_data a2dp;

#define AUDIO_PARAMETER_A2DP_STARTED "A2dpStarted"
#define BT_IPC_LIB_NAME  "libbthost_if.so"



/*********** START of DSP configurable structures ********************/
#define ASM_MEDIA_FMT_NONE 0

#define ASM_MEDIA_FMT_AAC_V2 0x00010DA6

#define ASM_MEDIA_FMT_AAC_AOT_LC 2
#define ASM_MEDIA_FMT_AAC_AOT_SBR 5
#define ASM_MEDIA_FMT_AAC_AOT_PS 29
#define ASM_MEDIA_FMT_AAC_FORMAT_FLAG_ADTS 0
#define ASM_MEDIA_FMT_AAC_FORMAT_FLAG_RAW 3
#define AFE_MIXER_ENC_FORMAT "AFE_ENC FMT"
#define AFE_MIXER_ENC_CONFIG_BLOCK "AFE_ENC_CONFIG"

/* AAC encoder configuration structure. */
typedef struct asm_aac_enc_cfg_v2_t asm_aac_enc_cfg_v2_t;

struct asm_aac_enc_cfg_v2 {

    /* Encoding rate in bits per second. */
    uint32_t       bit_rate;

    /* Encoding mode.
     * Supported values:
     * - #ASM_MEDIA_FMT_AAC_AOT_LC
     * - #ASM_MEDIA_FMT_AAC_AOT_SBR
     * - #ASM_MEDIA_FMT_AAC_AOT_PS
     */
     uint32_t          enc_mode;

    /* AAC format flag.
     * Supported values:
     * - #ASM_MEDIA_FMT_AAC_FORMAT_FLAG_ADTS
     * - #ASM_MEDIA_FMT_AAC_FORMAT_FLAG_RAW
     */
     uint16_t          aac_fmt_flag;

    /* Number of channels to encode.
     * Supported values:
     * - 0 -- Native mode
     * - 1 -- Mono
     * - 2 -- Stereo
     * - Other values are not supported.
     * @note1hang The eAAC+ encoder mode supports only stereo.
     * Native mode indicates that encoding must be performed with the
     * number of channels at the input.
     * The number of channels must not change during encoding.
     */
     uint32_t          channel_cfg;

    /* Number of samples per second.
     * Supported values: - 0 -- Native mode - For other values,
     * Native mode indicates that encoding must be performed with the
     * sampling rate at the input.
     * The sampling rate must not change during encoding.
     */
     uint32_t          sample_rate;
} ;


/* SBC encoder configuration structure. */
#define ASM_MEDIA_FMT_SBC                       0x00010BF2
/** Enumeration for SBC channel Mono mode. */
#define ASM_MEDIA_FMT_SBC_CHANNEL_MODE_MONO                     1
/** Enumeration for SBC channel Stereo mode. */
#define ASM_MEDIA_FMT_SBC_CHANNEL_MODE_STEREO                   2
/** Enumeration for SBC channel Dual Mono mode. */
#define ASM_MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO                8
/** Enumeration for SBC channel Joint Stereo mode. */
#define ASM_MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO             9
/** Enumeration for SBC bit allocation method = loudness. */
#define ASM_MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS            0
/** Enumeration for SBC bit allocation method = SNR.  */
#define ASM_MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR                 1


/* SBC encoder configuration structure. */
typedef struct asm_sbc_enc_cfg_t asm_sbc_enc_cfg_t;
/** @weakgroup weak_asm_sbc_enc_cfg_t@{ */
/* Payload of the SBC encoder configuration parameters in the
#ASM_MEDIA_FMT_SBC media format.*/
struct asm_sbc_enc_cfg_t{

    /** Number of subbands.         @values 4, 8 */
    uint32_t          num_subbands;

    /** Size of the encoded block in samples.         @values 4, 8, 12, 16 */
    uint32_t          blk_len;

    /** Mode used to allocate bits between channels.
        @values         - 0 (Native mode)
         #ASM_MEDIA_FMT_SBC_CHANNEL_MODE_MONO
         #ASM_MEDIA_FMT_SBC_CHANNEL_MODE_STEREO
         #ASM_MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO
         #ASM_MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO */
    uint32_t          channel_mode;

    /** Encoder bit allocation method.
        @values
     #ASM_MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS
     #ASM_MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR @tablebulletend */
    uint32_t          alloc_method;

    /** Number of encoded bits per second.
        @values
         Mono channel -- Maximum of 320 kbps
         Stereo channel -- Maximum of 512 kbps */
     uint32_t          bit_rate;

     /** Number of samples per second.
        @values
            0 (Native mode), 16000, 32000, 44100, 48000Hz */

     uint32_t          sample_rate;

};

#define ASM_MEDIA_FMT_APTX 0x000131ff      // apt-X Classic
#define ASM_MEDIA_FMT_APTX_HD 0x00013200   // apt-X HD
#define PCM_CHANNEL_L 1
#define PCM_CHANNEL_R 2
#define PCM_CHANNEL_C 3

struct asm_custom_enc_cfg_aptx_t
{
    uint32_t sample_rate;
    /* Mono or stereo */
    uint16_t num_channels;
    uint16_t reserved;
     /* num_ch == 1, then PCM_CHANNEL_C,
      * num_ch == 2, then {PCM_CHANNEL_L, PCM_CHANNEL_R}
      */
    uint8_t channel_mapping[8];
    uint32_t custom_size;
};

/*********** END of DSP configurable structures ********************/

static void a2dp_offload_codec_cap_parser(char *value)
{
    char *tok = NULL;

    tok = strtok(value,"-");
    while (tok != NULL)
    {
        if (strcmp(tok,"sbc") == 0)
        {
            ALOGD("%s: SBC offload supported",__func__);
            a2dp.is_a2dp_offload_supported = true;
            break;
        }
        else if (strcmp(tok,"aptx") == 0)
        {
            ALOGD("%s: aptx offload supported",__func__);
            a2dp.is_a2dp_offload_supported= true;
            break;
        }
        tok = strtok(NULL,"-");
    };
}

static void update_offload_codec_capabilities()
{
    char value[PROPERTY_VALUE_MAX] = {'\0'};

    property_get("persist.bt.a2dp_offload_cap", value, "false");
    ALOGD("get_offload_codec_capabilities = %s",value);
    a2dp.is_a2dp_offload_supported = false;
    if (strcmp(value, "false") != 0)
    {
        a2dp_offload_codec_cap_parser(value);
    }
    ALOGD("%s: codec cap = %s",__func__,value);
}

/* API to open BT IPC library to start IPC communication */
static int open_a2dp_output()
{
    int ret = 0;

    ALOGD(" Open A2DP output start ");
    if (a2dp.bt_lib_handle == NULL){
        ALOGD(" Requesting for BT lib handle");
        a2dp.bt_lib_handle = dlopen(BT_IPC_LIB_NAME, RTLD_NOW);

        if (a2dp.bt_lib_handle == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, BT_IPC_LIB_NAME);
            ret = -ENOSYS;
            goto init_fail;
        } else {

            a2dp.audio_stream_open = (audio_stream_open_t)
                                        dlsym(a2dp.bt_lib_handle, "audio_stream_open");
            a2dp.audio_start_stream = (audio_start_stream_t)
                                        dlsym(a2dp.bt_lib_handle, "audio_start_stream");
            a2dp.audio_get_codec_config = (audio_get_codec_config_t)
                                        dlsym(a2dp.bt_lib_handle, "audio_get_codec_config");
            a2dp.audio_suspend_stream = (audio_suspend_stream_t)
                                        dlsym(a2dp.bt_lib_handle, "audio_suspend_stream");
            a2dp.audio_handoff_triggered = (audio_handoff_triggered_t)
                                        dlsym(a2dp.bt_lib_handle, "audio_handoff_triggered");
            a2dp.clear_a2dpsuspend_flag = (clear_a2dpsuspend_flag_t)
                                        dlsym(a2dp.bt_lib_handle, "clear_a2dpsuspend_flag");
            a2dp.audio_stop_stream = (audio_stop_stream_t)
                                        dlsym(a2dp.bt_lib_handle, "audio_stop_stream");
            a2dp.audio_stream_close = (audio_stream_close_t)
                                        dlsym(a2dp.bt_lib_handle, "audio_stream_close");
        }
    }

    if (a2dp.bt_lib_handle && a2dp.audio_stream_open) {
        if (a2dp.bt_state == A2DP_STATE_DISCONNECTED) {
            ALOGD("calling BT stream open");
            ret = a2dp.audio_stream_open();
            if(ret != 0) {
                ALOGE("Failed to open output stream for a2dp: status %d", ret);
                goto init_fail;
            }
            a2dp.bt_state = A2DP_STATE_CONNECTED;
        } else {
            ALOGD("Called a2dp open with improper state, Ignoring request state %d", a2dp.bt_state);
        }
    } else {
        ALOGE("a2dp handle is not identified, Ignoring open request");
        a2dp.bt_state == A2DP_STATE_DISCONNECTED;
        ret = -ENOSYS;
    }

init_fail:

    return ret;
}

static int close_a2dp_output()
{
    ALOGV("close_a2dp_output");
    if (!(a2dp.bt_lib_handle && a2dp.audio_stream_close)) {
        ALOGE("a2dp handle is not identified, Ignoring close request");
        return -ENOSYS;
    }

    if ((a2dp.bt_state == A2DP_STATE_CONNECTED) &&
        (a2dp.bt_state == A2DP_STATE_STARTED) &&
        (a2dp.bt_state == A2DP_STATE_STOPPED)) {
        ALOGD("calling BT stream close");
        if(a2dp.audio_stream_close() == false)
            ALOGE("failed close a2dp control path from BT library");

        a2dp.a2dp_started = false;
        a2dp.a2dp_total_active_session_request = 0;
        a2dp.a2dp_suspended = false;
        a2dp.bt_encoder_format = AUDIO_FORMAT_INVALID;
        a2dp.enc_config_data = NULL;
        a2dp.bt_state = A2DP_STATE_DISCONNECTED;

    } else {
        ALOGD("close a2dp called in improper state");

        a2dp.a2dp_started = false;
        a2dp.a2dp_total_active_session_request = 0;
        a2dp.a2dp_suspended = false;
        a2dp.bt_encoder_format = AUDIO_FORMAT_INVALID;
        a2dp.enc_config_data = NULL;
        a2dp.bt_state = A2DP_STATE_DISCONNECTED;
    }

    return 0;
}

void configure_a2dp_encoder_format()
{
    void *codec_info = NULL;
    uint8_t multi_cast = 0, num_dev = 1;
    audio_format_t codec_type = AUDIO_FORMAT_INVALID;
    struct mixer_ctl *ctl_enc_format, *ctl_enc_data;
    int ret = 0;

    if (!a2dp.audio_get_codec_config) {
        ALOGE(" a2dp handle is not identified, ignoring a2dp encoder config");
        return;
    }
    ALOGD("configure_a2dp_encoder_format start");
    ctl_enc_format = mixer_get_ctl_by_name(a2dp.adev->mixer, AFE_MIXER_ENC_FORMAT);
    if (!ctl_enc_format) {
        ALOGE(" ERROR  a2dp encoder format mixer control not identifed");
        return;
    }

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, AFE_MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        return;
    }

    ALOGD("Requesting codec config from BT module");
    codec_info = a2dp.audio_get_codec_config(&multi_cast, &num_dev,
                               &codec_type);

    if (codec_type == AUDIO_FORMAT_SBC) {
        ALOGE(" Received SBC encoder BT device");
        audio_sbc_encoder_config *sbc_bt_cfg = (audio_sbc_encoder_config *)codec_info;
        struct asm_sbc_enc_cfg_t sbc_dsp_cfg;

        if(mixer_ctl_set_value(ctl_enc_format, 0, ASM_MEDIA_FMT_SBC) < 0) {
            ALOGE("%s: Couldn't set SBC encoder format to AFE", __func__);
            return;
        } else {
            ALOGD(" Successfully updated SBC format to AFE");
            a2dp.bt_encoder_format = AUDIO_FORMAT_SBC;
        }

        memset(&sbc_dsp_cfg, 0x0, sizeof(struct asm_sbc_enc_cfg_t));
        sbc_dsp_cfg.num_subbands = sbc_bt_cfg->subband;
        sbc_dsp_cfg.blk_len = sbc_bt_cfg->blk_len;
        switch(sbc_bt_cfg->channels) {
            case 0:
                sbc_dsp_cfg.channel_mode = ASM_MEDIA_FMT_SBC_CHANNEL_MODE_MONO;
            break;

            case 1:
                sbc_dsp_cfg.channel_mode = ASM_MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO;
            break;

            case 3:
                sbc_dsp_cfg.channel_mode = ASM_MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO;
            break;

            case 2:
            default:
                sbc_dsp_cfg.channel_mode = ASM_MEDIA_FMT_SBC_CHANNEL_MODE_STEREO;
            break;
        }
        if (sbc_bt_cfg->alloc)
            sbc_dsp_cfg.alloc_method = ASM_MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS;
        else
            sbc_dsp_cfg.alloc_method = ASM_MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR;

        sbc_dsp_cfg.bit_rate = sbc_bt_cfg->bitrate;
        sbc_dsp_cfg.sample_rate = sbc_bt_cfg->sampling_rate;

        ret = mixer_ctl_set_array(ctl_enc_data, (void *)&sbc_dsp_cfg, sizeof(struct asm_sbc_enc_cfg_t));
        if (ret != 0) {
            ALOGE("%s: mixer_ctl_set_array() failed to set SBC encoder config", __func__);
        }
    } else if ((codec_type == AUDIO_FORMAT_APTX) ||
        (codec_type == AUDIO_FORMAT_APTX_HD)) {
            audio_aptx_encoder_config *aptx_bt_cfg = (audio_aptx_encoder_config *)codec_info;
            struct asm_custom_enc_cfg_aptx_t aptx_dsp_cfg;

            if(codec_type == AUDIO_FORMAT_APTX) {
                ALOGD(" Received APTX encoder BT device");
                if(mixer_ctl_set_value(ctl_enc_format, 0, ASM_MEDIA_FMT_APTX) < 0) {
                    ALOGE("%s: Couldn't set APTX encoder format to AFE", __func__);
                    return;
                } else {
                    ALOGD(" Successfully updated APTX format to AFE");
                    a2dp.bt_encoder_format = AUDIO_FORMAT_APTX;
                }
            } else if(codec_type == AUDIO_FORMAT_APTX_HD) {
                    ALOGD(" Received APTX HD encoder BT device");
                    if(mixer_ctl_set_value(ctl_enc_format, 0, ASM_MEDIA_FMT_APTX_HD) < 0) {
                        ALOGE("%s: Couldn't set APTX-HD encoder format to AFE", __func__);
                        return;
                    } else {
                        ALOGD(" Successfully updated APTX-HD format to AFE");
                        a2dp.bt_encoder_format = AUDIO_FORMAT_APTX_HD;
                    }
            }
            memset(&aptx_dsp_cfg, 0x0, sizeof(struct asm_custom_enc_cfg_aptx_t));
            aptx_dsp_cfg.sample_rate = aptx_bt_cfg->sampling_rate;
            aptx_dsp_cfg.num_channels = aptx_bt_cfg->chnl;
            switch(aptx_dsp_cfg.num_channels) {
                case 1:
                    aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_C;
                break;

                case 2:
                default:
                    aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_L;
                    aptx_dsp_cfg.channel_mapping[1] = PCM_CHANNEL_R;
                break;
            }
            ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg, sizeof(struct asm_custom_enc_cfg_aptx_t));
            if (ret != 0) {
                ALOGE("%s: mixer_ctl_set_array() failed to set APTX encoder config", __func__);
            }
    }
}

int audio_extn_a2dp_start_playback()
{
    int ret = 0;

    ALOGD("audio_extn_a2dp_start_playback start");

    if(!(a2dp.bt_lib_handle && a2dp.audio_start_stream
       && a2dp.audio_get_codec_config)) {
        ALOGE("a2dp handle is not identified, Ignoring start request");
        return -ENOSYS;
    }

    if(a2dp.a2dp_suspended == true) {
        //session will be restarted after suspend completion
        ALOGD("a2dp start requested during suspend state");
        a2dp.a2dp_total_active_session_request++;
        return 0;
    }

    if (!a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        ALOGD("calling BT module stream start");
        /* This call indicates BT IPC lib to start playback */
        ret =  a2dp.audio_start_stream();
        ALOGE("BT controller start return = %d",ret);
        if (ret != 0 ) {
           ALOGE("BT controller start failed");
           a2dp.a2dp_started = false;
        } else {
           a2dp.a2dp_started = true;

           configure_a2dp_encoder_format();
           ALOGD("Start playback successful to BT library");

           //query codec specific info and update to AFE driver
        }
    }

    if (a2dp.a2dp_started)
        a2dp.a2dp_total_active_session_request++;

    ALOGD("start A2DP playback total active sessions :%d",
          a2dp.a2dp_total_active_session_request);
    return 0;
}

int audio_extn_a2dp_stop_playback()
{
    int ret =0;

    ALOGD("audio_extn_a2dp_stop_playback start");
    if(!(a2dp.bt_lib_handle && a2dp.audio_stop_stream)) {
        ALOGE("a2dp handle is not identified, Ignoring start request");
        return -ENOSYS;
    }

    if(a2dp.a2dp_suspended == true) {
        ALOGD("STOP playback is called during suspend state");

        // sessions are already closed during suspend, just update active sessions counts
         if(a2dp.a2dp_total_active_session_request > 0)
            a2dp.a2dp_total_active_session_request--;
         return 0;
    }
    if ( a2dp.a2dp_started && (a2dp.a2dp_total_active_session_request > 0))
        a2dp.a2dp_total_active_session_request--;

    if ( a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        struct mixer_ctl *ctl_enc_format;

        ALOGD("calling BT module stream stop");
        ret = a2dp.audio_stop_stream();
        if (ret < 0)
            ALOGE("out_standby to BT HAL failed");
        else
            ALOGV("out_standby to BT HAL successful");

        ctl_enc_format = mixer_get_ctl_by_name(a2dp.adev->mixer, AFE_MIXER_ENC_FORMAT);
        if (!ctl_enc_format) {
            ALOGE(" ERROR  a2dp encoder format mixer control not identifed");
        } else {
            if(mixer_ctl_set_value(ctl_enc_format, 0, ASM_MEDIA_FMT_NONE) < 0) {
                ALOGE("%s: Couldn't reset media format to AFE", __func__);
            } else {
                ALOGD(" Successfully reset media format");
                a2dp.bt_encoder_format = ASM_MEDIA_FMT_NONE;
            }
        }

    }
    if(!a2dp.a2dp_total_active_session_request) {
       a2dp.a2dp_started = false;
    }

    ALOGD("Stop A2DP playback total active sessions :%d",
          a2dp.a2dp_total_active_session_request);
    return 0;
}

void audio_extn_a2dp_set_parameters(struct str_parms *parms)
{
     int ret, val;
     char value[32]={0};

     if(a2dp.is_a2dp_offload_supported == false) {
        ALOGV("no supported encoders identified,ignoring a2dp setparam");
        return;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value,
                            sizeof(value));
     if( ret >= 0) {
         val = atoi(value);
         if (val & AUDIO_DEVICE_OUT_ALL_A2DP) {
             ALOGV("Received device connect request for A2DP");
             open_a2dp_output();
         }
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                         sizeof(value));

     if( ret >= 0) {
         val = atoi(value);
         if (val & AUDIO_DEVICE_OUT_ALL_A2DP) {
             ALOGV("Received device dis- connect request");
             close_a2dp_output();
         }
     }

     ret = str_parms_get_str(parms, "A2dpSuspended", value, sizeof(value));
     if (ret >= 0) {
         if (a2dp.bt_lib_handle && (a2dp.bt_state != A2DP_STATE_DISCONNECTED) ) {
             if ((!strncmp(value,"true",sizeof(value)))) {
                ALOGD("Setting a2dp to suspend state");
                int active_sessions = a2dp.a2dp_total_active_session_request, count = 0;
                //Force close all active sessions on suspend (if any)
                for(count  = 0; count< active_sessions; count ++)
                 audio_extn_a2dp_stop_playback();
                a2dp.a2dp_total_active_session_request = active_sessions;
                a2dp.a2dp_suspended = true;

                if(a2dp.audio_suspend_stream)
                   a2dp.audio_suspend_stream();
            }
            else if (a2dp.a2dp_suspended == true) {
                ALOGD("Resetting a2dp suspend state");
                if(a2dp.clear_a2dpsuspend_flag)
                    a2dp.clear_a2dpsuspend_flag();

                a2dp.a2dp_suspended = false;
                //Force restart all active sessions post suspend (if any)
                if(a2dp.a2dp_total_active_session_request > 0){
                    int active_sessions = a2dp.a2dp_total_active_session_request;
                    a2dp.a2dp_total_active_session_request = 0;
                    audio_extn_a2dp_start_playback();
                    a2dp.a2dp_total_active_session_request = active_sessions;
                }
            }
        }
     }
#if 0
     ret = str_parms_get_str(parms,"reconfigA2dp", value, sizeof(value));
     if (ret >= 0) {
         if (a2dp.bt_lib_handle && (a2dp.bt_state != A2DP_STATE_DISCONNECTED)) {
             if ((!strncmp(value,"true",sizeof(value)))) {
                 ALOGD("handoff trigger notified");
                 if (a2dp.a2dp_total_active_session_request > 0) {
                    int active_sessions = a2dp.a2dp_total_active_session_request;
                    a2dp.a2dp_total_active_session_request = 0;
                    a2dp.a2dp_started = 0;
                    a2dp.audio_handoff_triggered();
                    //usleep(500000);
                    audio_extn_a2dp_start_playback();
                    a2dp.a2dp_total_active_session_request = active_sessions;
                    ALOGD("handoff done successfully");
                 }
             }
         }
     }
#endif
}

void audio_extn_a2dp_init (void *adev)
{
  a2dp.adev = (struct audio_device*)adev;
  a2dp.bt_lib_handle = NULL;
  a2dp.a2dp_started = false;
  a2dp.bt_state = A2DP_STATE_DISCONNECTED;
  a2dp.a2dp_total_active_session_request = 0;
  a2dp.a2dp_suspended = false;
  a2dp.bt_encoder_format = AUDIO_FORMAT_INVALID;
  a2dp.enc_config_data = NULL;
  a2dp.is_a2dp_offload_supported = false;
  update_offload_codec_capabilities();
}
#endif // SPLIT_A2DP_ENABLED
