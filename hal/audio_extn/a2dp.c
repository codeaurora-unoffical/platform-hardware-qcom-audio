/*
* Copyright (c) 2015-17, The Linux Foundation. All rights reserved.
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
#define AUDIO_PARAMETER_A2DP_STARTED "A2dpStarted"
#define BT_IPC_LIB_NAME  "libbthost_if.so"
#define ENC_MEDIA_FMT_NONE                                     0
#define ENC_MEDIA_FMT_AAC                                  0x00010DA6
#define ENC_MEDIA_FMT_APTX                                 0x000131ff
#define ENC_MEDIA_FMT_APTX_HD                              0x00013200
#define ENC_MEDIA_FMT_SBC                                  0x00010BF2
#define MEDIA_FMT_AAC_AOT_LC                               2
#define MEDIA_FMT_AAC_AOT_SBR                              5
#define MEDIA_FMT_AAC_AOT_PS                               29
#define MEDIA_FMT_AAC_FORMAT_FLAG_ADTS                     0
#define MEDIA_FMT_AAC_FORMAT_FLAG_RAW                      3
#define PCM_CHANNEL_L                                      1
#define PCM_CHANNEL_R                                      2
#define PCM_CHANNEL_C                                      3
#define MEDIA_FMT_SBC_CHANNEL_MODE_MONO                    1
#define MEDIA_FMT_SBC_CHANNEL_MODE_STEREO                  2
#define MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO               8
#define MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO            9
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS           0
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR                1
#define MIXER_ENC_CONFIG_BLOCK     "SLIM_7_RX Encoder Config"
#define MIXER_ENC_FMT_SBC          "SBC"
#define MIXER_ENC_FMT_AAC          "AAC"
#define MIXER_ENC_FMT_APTX         "APTX"
#define MIXER_ENC_FMT_APTXHD       "APTXHD"
#define MIXER_ENC_FMT_NONE         "NONE"


typedef int (*audio_stream_open_t)(void);
typedef int (*audio_stream_close_t)(void);
typedef int (*audio_start_stream_t)(void);
typedef int (*audio_stop_stream_t)(void);
typedef int (*audio_suspend_stream_t)(void);
typedef void (*audio_handoff_triggered_t)(void);
typedef void (*clear_a2dpsuspend_flag_t)(void);
typedef void * (*audio_get_codec_config_t)(audio_format_t *codec_type);
typedef int (*audio_check_a2dp_ready_t)(void);

enum A2DP_STATE {
    A2DP_STATE_CONNECTED,
    A2DP_STATE_STARTED,
    A2DP_STATE_STOPPED,
    A2DP_STATE_DISCONNECTED,
};

/* structure used to  update a2dp state machine
 * to communicate IPC library
 * to store DSP encoder configuration information
 */
struct a2dp_data {
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
    audio_check_a2dp_ready_t audio_check_a2dp_ready;
    enum A2DP_STATE bt_state;
    audio_format_t bt_encoder_format;
    void *enc_config_data;
    bool a2dp_started;
    bool a2dp_suspended;
    int  a2dp_total_active_session_request;
    bool is_a2dp_offload_supported;
};

struct a2dp_data a2dp;

/*API to open BT IPC library to start IPC communication */
static void open_a2dp_output()
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
            a2dp.audio_check_a2dp_ready = (audio_check_a2dp_ready_t)
                          dlsym(a2dp.bt_lib_handle,"audio_check_a2dp_ready");
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
        a2dp.bt_state = A2DP_STATE_DISCONNECTED;
        goto init_fail;
    }

init_fail:
    if(ret != 0 && (a2dp.bt_lib_handle != NULL)) {
        dlclose(a2dp.bt_lib_handle);
        a2dp.bt_lib_handle = NULL;
    }
}

static int close_a2dp_output()
{
    ALOGV("%s\n",__func__);
    if (!(a2dp.bt_lib_handle && a2dp.audio_stream_close)) {
        ALOGE("a2dp handle is not identified, Ignoring close request");
        return -ENOSYS;
    }
    if (a2dp.bt_state != A2DP_STATE_DISCONNECTED) {
        ALOGD("calling BT stream close");
        if(a2dp.audio_stream_close() == false)
            ALOGE("failed close a2dp control path from BT library");
    }
    a2dp.a2dp_started = false;
    a2dp.a2dp_total_active_session_request = 0;
    a2dp.a2dp_suspended = false;
    a2dp.bt_encoder_format = AUDIO_FORMAT_INVALID;
    a2dp.bt_state = A2DP_STATE_DISCONNECTED;
    return 0;
}

bool configure_a2dp_format()
{
    void *codec_info = NULL;
    uint8_t multi_cast = 0, num_dev = 1;
    audio_format_t codec_type = AUDIO_FORMAT_INVALID;
    bool is_configured = false;

    if (!a2dp.audio_get_codec_config) {
        ALOGE(" a2dp handle is not identified, ignoring a2dp encoder config");
        return false;
    }
    ALOGD("configure_a2dp_encoder_format start");
    a2dp.bt_encoder_format = AUDIO_FORMAT_PCM_16_BIT;
    is_configured = true;
    return is_configured;
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
           if(configure_a2dp_format() == true) {
                a2dp.a2dp_started = true;
                ret = 0;
                ALOGD("Start playback successful to BT library");
           } else {
                ALOGD(" unable to configure DSP encoder");
                a2dp.a2dp_started = false;
                ret = -ETIMEDOUT;
           }
        }
    }

    if (a2dp.a2dp_started)
        a2dp.a2dp_total_active_session_request++;

    ALOGD("start A2DP playback total active sessions :%d",
          a2dp.a2dp_total_active_session_request);
    return ret;
}

int audio_extn_a2dp_stop_playback()
{
    int ret =0;

    ALOGV("audio_extn_a2dp_stop_playback start");
    if(!(a2dp.bt_lib_handle && a2dp.audio_stop_stream)) {
        ALOGE("a2dp handle is not identified, Ignoring start request");
        return -ENOSYS;
    }

   if (a2dp.a2dp_total_active_session_request > 0)
        a2dp.a2dp_total_active_session_request--;

    if ( a2dp.a2dp_started && !a2dp.a2dp_total_active_session_request) {
        ALOGV("calling BT module stream stop");
        ret = a2dp.audio_stop_stream();
        if (ret < 0)
            ALOGE("stop stream to BT IPC lib failed");
        else
            ALOGV("stop steam to BT IPC lib successful");
        a2dp.bt_encoder_format = ENC_MEDIA_FMT_NONE;
   }
   if(!a2dp.a2dp_total_active_session_request)
       a2dp.a2dp_started = false;
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
         goto param_handled;
     }

     ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                         sizeof(value));

     if( ret >= 0) {
         val = atoi(value);
         if (val & AUDIO_DEVICE_OUT_ALL_A2DP) {
             ALOGV("Received device dis- connect request");
             close_a2dp_output();
         }
         goto param_handled;
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
            } else if (a2dp.a2dp_suspended == true) {
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
        goto param_handled;
     }
param_handled:
     ALOGV("end of a2dp setparam");
}

bool audio_extn_a2dp_is_ready()
{
   bool ret = false;

   if ((a2dp.is_a2dp_offload_supported) &&
       (a2dp.audio_check_a2dp_ready))
       ret = a2dp.audio_check_a2dp_ready();
   return ret;
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
  a2dp.is_a2dp_offload_supported = true;
}
#endif // SPLIT_A2DP_ENABLED
