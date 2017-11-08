/*
* Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "qahw_api"
#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0

#include <utils/Errors.h>
#include <dlfcn.h>
#include <utils/Log.h>
#include <stdlib.h>
#include <cutils/list.h>
#include <assert.h>

#include <hardware/audio.h>
#include <cutils/properties.h>
#include "qahw_api.h"
#include "qahw.h"

#include <mm-audio/qti-audio-server/qti_audio_server.h>
#include <mm-audio/qti-audio-server/qti_audio_server_client.h>

using namespace audiohal;

/* Flag to indicate if QAS is enabled or not */
bool g_binder_enabled = false;
/* QTI audio server handle */
sp<Iqti_audio_server> g_qas = NULL;
/* Handle for client context*/
void* g_ctxt = NULL;
/* Death notification handle */
sp<death_notifier> g_death_notifier = NULL;
/* Client callback handle */
audio_error_callback g_audio_err_cb = NULL;

void death_notifier::binderDied(const wp<IBinder>& who)
{
    if (g_audio_err_cb) {
        ALOGD("%s %d", __func__, __LINE__);
        g_audio_err_cb(g_ctxt);
    }
}

void qahw_register_qas_death_notify_cb(audio_error_callback cb, void* context)
{
    ALOGD("%s %d", __func__, __LINE__);
    g_audio_err_cb = cb;
    g_ctxt = context;
}

death_notifier::death_notifier()
{
    ALOGV("%s %d", __func__, __LINE__);
    sp<ProcessState> proc(ProcessState::self());
    proc->startThreadPool();
}

sp<Iqti_audio_server> get_qti_audio_server() {
    sp<IServiceManager> sm;
    sp<IBinder> binder;
    int retry_cnt = 5;

    if (g_qas == 0) {
        sm = defaultServiceManager();
        if (sm != NULL) {
            do {
                binder = sm->getService(String16(QTI_AUDIO_SERVER));
                if (binder != 0)
                    break;
                else
                    ALOGE("%d:%s: get qas service failed",__LINE__, __func__);

                 ALOGW("qti_audio_server not published, waiting...");
                usleep(500000);
            } while (--retry_cnt);
        } else {
            ALOGE("%d:%s: defaultServiceManager failed",__LINE__, __func__);
        }
        if (binder == NULL)
            return NULL;

        if (g_death_notifier == NULL) {
            g_death_notifier = new death_notifier();
            if(g_death_notifier == NULL) {
                ALOGE("%d: %s() unable to allocate death notifier", __LINE__, __func__);
                return NULL;
            }
        }
        binder->linkToDeath(g_death_notifier);
        g_qas = interface_cast<Iqti_audio_server>(binder);
        assert(g_qas != 0);
    }
    return g_qas;
}

uint32_t qahw_out_get_sample_rate(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_get_sample_rate(out_handle);
    } else {
        return qahw_out_get_sample_rate_l(out_handle);
    }
}

int qahw_out_set_sample_rate(qahw_stream_handle_t *out_handle, uint32_t rate)
{
    ALOGV("%d:%s %d",__LINE__, __func__, rate);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_set_sample_rate(out_handle, rate);
    } else {
        return qahw_out_set_sample_rate_l(out_handle, rate);
    }
}

size_t qahw_out_get_buffer_size(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_get_buffer_size(out_handle);
    } else {
        return qahw_out_get_buffer_size_l(out_handle);
    }
}

audio_channel_mask_t qahw_out_get_channels(const qahw_stream_handle_t
                                              *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return (audio_channel_mask_t)(-ENODEV);
        }
        return qas->qahw_out_get_channels(out_handle);
    } else {
        return qahw_out_get_channels_l(out_handle);
    }
}

audio_format_t qahw_out_get_format(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return AUDIO_FORMAT_INVALID;
        }
        return qas->qahw_out_get_format(out_handle);
    } else {
        return qahw_out_get_format_l(out_handle);
    }
}

int qahw_out_standby(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_standby(out_handle);
    } else {
        return qahw_out_standby_l(out_handle);
    }
}

int qahw_out_set_parameters(qahw_stream_handle_t *out_handle,
                                const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_set_parameters(out_handle, kv_pairs);
    } else {
        return qahw_out_set_parameters_l(out_handle, kv_pairs);
    }
}

char *qahw_out_get_parameters(const qahw_stream_handle_t *out_handle,
                                 const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return NULL;
        }
        return qas->qahw_out_get_parameters(out_handle, keys);
    } else {
        return qahw_out_get_parameters_l(out_handle, keys);
    }
}

int qahw_out_set_param_data(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_set_param_data(out_handle, param_id, payload);
    } else {
        return qahw_out_set_param_data_l(out_handle, param_id, payload);
    }
}

int qahw_out_get_param_data(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_get_param_data(out_handle, param_id, payload);
    } else {
        return qahw_out_get_param_data_l(out_handle, param_id, payload);
    }
}

uint32_t qahw_out_get_latency(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_get_latency(out_handle);
    } else {
        return qahw_out_get_latency_l(out_handle);
    }
}

int qahw_out_set_volume(qahw_stream_handle_t *out_handle, float left, float right)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_set_volume(out_handle, left, right);
    } else {
        return qahw_out_set_volume_l(out_handle, left, right);
    }
}

ssize_t qahw_out_write(qahw_stream_handle_t *out_handle,
                        qahw_out_buffer_t *out_buf)
{
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_write(out_handle, out_buf);
    } else {
        return qahw_out_write_l(out_handle, out_buf);
    }
}

int qahw_out_get_render_position(const qahw_stream_handle_t *out_handle,
                                 uint32_t *dsp_frames)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_get_render_position(out_handle, dsp_frames);
    } else {
        return qahw_out_get_render_position_l(out_handle, dsp_frames);
    }
}

int qahw_out_set_callback(qahw_stream_handle_t *out_handle,
                          qahw_stream_callback_t callback,
                          void *cookie)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_set_callback(out_handle, callback, cookie);
    } else {
        return qahw_out_set_callback_l(out_handle, callback, cookie);
    }
}

int qahw_out_pause(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_pause(out_handle);
    } else {
        return qahw_out_pause_l(out_handle);
    }
}

int qahw_out_resume(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_resume(out_handle);
    } else {
        return qahw_out_resume_l(out_handle);
    }
}

int qahw_out_drain(qahw_stream_handle_t *out_handle, qahw_drain_type_t type )
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_drain(out_handle, type);
    } else {
        return qahw_out_drain_l(out_handle, type);
    }
}

int qahw_out_flush(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_flush(out_handle);
    } else {
        return qahw_out_flush_l(out_handle);
    }
}

int qahw_out_get_presentation_position(const qahw_stream_handle_t *out_handle,
                           uint64_t *frames, struct timespec *timestamp)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_out_get_presentation_position(out_handle,
                                             frames, timestamp);
    } else {
        return qahw_out_get_presentation_position_l(out_handle,
                                         frames, timestamp);
    }
}

uint32_t qahw_in_get_sample_rate(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_get_sample_rate(in_handle);
    } else {
        return qahw_in_get_sample_rate_l(in_handle);
    }
}

int qahw_in_set_sample_rate(qahw_stream_handle_t *in_handle, uint32_t rate)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_set_sample_rate(in_handle, rate);
    } else {
        return qahw_in_set_sample_rate_l(in_handle, rate);
    }
}

size_t qahw_in_get_buffer_size(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_get_buffer_size(in_handle);
    } else {
        return qahw_in_get_buffer_size_l(in_handle);
    }
}

audio_channel_mask_t qahw_in_get_channels(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_get_channels(in_handle);
    } else {
        return qahw_in_get_channels_l(in_handle);
    }
}

audio_format_t qahw_in_get_format(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return (audio_format_t)-ENODEV;
        }
        return qas->qahw_in_get_format(in_handle);
    } else {
        return qahw_in_get_format_l(in_handle);
    }
}

int qahw_in_set_format(qahw_stream_handle_t *in_handle, audio_format_t format)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return (audio_format_t)-ENODEV;
        }
        return qas->qahw_in_set_format(in_handle, format);
    } else {
        return qahw_in_set_format_l(in_handle, format);
    }
}

int qahw_in_standby(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_standby(in_handle);
    } else {
        return qahw_in_standby_l(in_handle);
    }
}

int qahw_in_set_parameters(qahw_stream_handle_t *in_handle, const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_set_parameters(in_handle, kv_pairs);
    } else {
        return qahw_in_set_parameters_l(in_handle, kv_pairs);
    }
}

char* qahw_in_get_parameters(const qahw_stream_handle_t *in_handle,
                              const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return NULL;
        }
        return qas->qahw_in_get_parameters(in_handle, keys);
    } else {
        return qahw_in_get_parameters_l(in_handle, keys);
    }
}

ssize_t qahw_in_read(qahw_stream_handle_t *in_handle,
                     qahw_in_buffer_t *in_buf)
{
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_read(in_handle, in_buf);
    } else {
        return qahw_in_read_l(in_handle, in_buf);
    }
}

uint32_t qahw_in_get_input_frames_lost(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_get_input_frames_lost(in_handle);
    } else {
        return qahw_in_get_input_frames_lost_l(in_handle);
    }
}

int qahw_in_get_capture_position(const qahw_stream_handle_t *in_handle,
                                 int64_t *frames, int64_t *time)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_in_get_capture_position(in_handle, frames, time);
    } else {
        return qahw_in_get_capture_position_l(in_handle, frames, time);
    }
}

int qahw_init_check(const qahw_module_handle_t *hw_module)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_init_check(hw_module);
    } else {
        return qahw_init_check_l(hw_module);
    }
}

int qahw_set_voice_volume(qahw_module_handle_t *hw_module, float volume)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_set_voice_volume(hw_module, volume);
    } else {
        return qahw_set_voice_volume_l(hw_module, volume);
    }
}

int qahw_set_mode(qahw_module_handle_t *hw_module, audio_mode_t mode)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_set_mode(hw_module, mode);;
    } else {
        return qahw_set_mode_l(hw_module, mode);
    }
}

int qahw_set_mic_mute(qahw_module_handle_t *hw_module, bool state)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_set_mic_mute(hw_module, state);
    } else {
        return qahw_set_mic_mute_l(hw_module, state);
    }
}

int qahw_get_mic_mute(qahw_module_handle_t *hw_module, bool *state)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_get_mic_mute(hw_module, state);
    } else {
        return qahw_get_mic_mute_l(hw_module, state);
    }
}

int qahw_set_parameters(qahw_module_handle_t *hw_module, const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_set_parameters(hw_module, kv_pairs);
    } else {
        return qahw_set_parameters_l(hw_module, kv_pairs);
    }
}

char* qahw_get_parameters(const qahw_module_handle_t *hw_module,
                           const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return NULL;
        }
        return qas->qahw_get_parameters(hw_module, keys);;
    } else {
        return qahw_get_parameters_l(hw_module, keys);
    }
}

int qahw_get_param_data(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_get_param_data(hw_module, param_id, payload);
    } else {
        return qahw_get_param_data_l(hw_module, param_id, payload);
    }
}

int qahw_set_param_data(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_set_param_data(hw_module, param_id, payload);
    } else {
        return qahw_set_param_data_l(hw_module, param_id, payload);
    }
}

size_t qahw_get_input_buffer_size(const qahw_module_handle_t *hw_module,
                                  const struct audio_config *config)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_get_input_buffer_size(hw_module, config);
    } else {
        return qahw_get_input_buffer_size_l(hw_module, config);
    }
}

int qahw_open_output_stream(qahw_module_handle_t *hw_module,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            qahw_stream_handle_t **out_handle,
                            const char *address)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_open_output_stream(hw_module, handle, devices,
                                             flags, config, out_handle,
                                             address);
    } else {
        return qahw_open_output_stream_l(hw_module, handle, devices,
                                           flags, config, out_handle,
                                           address);
    }
}

int qahw_close_output_stream(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    int status;
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_close_output_stream(out_handle);
    } else {
        return qahw_close_output_stream_l(out_handle);
    }
}

int qahw_open_input_stream(qahw_module_handle_t *hw_module,
                           audio_io_handle_t handle,
                           audio_devices_t devices,
                           struct audio_config *config,
                           qahw_stream_handle_t **in_handle,
                           audio_input_flags_t flags,
                           const char *address,
                           audio_source_t source)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_open_input_stream(hw_module, handle, devices,
                                       config, in_handle, flags,
                                       address, source);
    } else {
        return qahw_open_input_stream_l(hw_module, handle, devices,
                                       config, in_handle, flags,
                                       address, source);
    }
}

int qahw_close_input_stream(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_close_input_stream(in_handle);
    } else {
        return qahw_close_input_stream_l(in_handle);
    }
}

int qahw_get_version()
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_get_version();
    } else {
        return qahw_get_version_l();
    }
}

int qahw_unload_module(qahw_module_handle_t *hw_module)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return -ENODEV;
        }
        return qas->qahw_unload_module(hw_module);
    } else {
        return qahw_unload_module_l(hw_module);
    }
}

qahw_module_handle_t *qahw_load_module(const char *hw_module_id)
{
    char value[PROPERTY_VALUE_MAX];

    ALOGV("%d:%s",__LINE__, __func__);
    if (property_get("persist.qas.enabled", value, NULL))
        g_binder_enabled = atoi(value) || !strncmp("true", value, 4);
    ALOGV("%d:%s: g_binder_enabled %d",__LINE__, __func__, g_binder_enabled);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas == 0) {
           ALOGE("%d:%s: invalid HAL handle %d",__LINE__, __func__);
           return (void*)(-ENODEV);
        }
        return qas->qahw_load_module(hw_module_id);
    } else {
        return qahw_load_module_l(hw_module_id);
    }
}
