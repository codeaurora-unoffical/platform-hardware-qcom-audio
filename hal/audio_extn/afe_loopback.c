/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_afe_loopback"
/* #define LOG_NDEBUG 0 */
/* #define VERY_VERY_VERBOSE_LOGGING */
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#define MAX_NUM_PATCHES 1
#define PATCH_HANDLE_INVALID 0xFFFF
#define MAX_SOURCE_PORTS_PER_PATCH 1
#define MAX_SINK_PORTS_PER_PATCH 1
#define AFE_LOOPBACK_RX_UNITY_GAIN 0x2000

#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include "audio_utils/primitives.h"
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>
#include <system/thread_defs.h>
#include <cutils/sched_policy.h>
#include "audio_extn.h"
#include <system/audio.h>

/*
* Unique patch handle ID = (unique_patch_handle_type << 8 | patch_handle_num)
* Eg : HDMI_IN_SPKR_OUT handles can be 0x1000, 0x1001 and so on..
*/
typedef enum patch_handle_type {
    AUDIO_PATCH_MIC_IN_SPKR_OUT = 0x10,
    AUDIO_PATCH_DTMF_IN_SPKR_OUT,
} patch_handle_type_t;

typedef enum patch_state {
    PATCH_INACTIVE,// Patch is not created yet
    PATCH_CREATED, // Patch created but not in running state yet, probably due
                   // to lack of proper port config
    PATCH_RUNNING, // Patch in running state, moves to this state when patch
                   // created and proper port config is available
} patch_state_t;

typedef struct loopback_patch {
    audio_patch_handle_t patch_handle_id;            /* patch unique ID */
    struct audio_port_config loopback_source;        /* Source port config */
    struct audio_port_config loopback_sink;          /* Source port config */
    struct pcm *source_stream;                  /* Source stream */
    struct pcm *sink_stream;                    /* Source stream */
    struct stream_inout patch_stream;                /* InOut type stream */
    patch_state_t patch_state;                       /* Patch operation state */
} loopback_patch_t;

typedef struct patch_db_struct {
    int32_t num_patches;
    loopback_patch_t loopback_patch[MAX_NUM_PATCHES];
} patch_db_t;

typedef struct audio_loopback {
    struct audio_device *adev;
    patch_db_t patch_db;
    audio_usecase_t uc_id;
    usecase_type_t  uc_type;
    pthread_mutex_t lock;
} audio_loopback_t;

typedef struct port_info {
    audio_port_handle_t      id;                /* port unique ID */
    audio_port_role_t        role;              /* sink or source */
    audio_port_type_t        type;              /* device, mix ... */
} port_info_t;

/* Audio loopback module struct */
static audio_loopback_t *audio_loopback_mod = NULL;

static struct pcm_config pcm_config = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};


uint32_t format_to_bitwidth(audio_format_t format)
{
    switch (format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            return 16;
        case AUDIO_FORMAT_PCM_8_BIT:
            return 8;
        case AUDIO_FORMAT_PCM_32_BIT:
            return 32;
        case AUDIO_FORMAT_PCM_8_24_BIT:
            return 32;
        case AUDIO_FORMAT_PCM_24_BIT_PACKED:
            return 24;
        default:
            return 16;
    }
}

/* Initialize patch database */
int init_patch_database(patch_db_t* patch_db)
{
    int patch_init_rc = 0, patch_num=0;
    patch_db->num_patches = 0;
    for (patch_num=0;patch_num < MAX_NUM_PATCHES;patch_num++) {
        patch_db->loopback_patch[patch_num].patch_handle_id = (int32_t)
        PATCH_HANDLE_INVALID;
    }
    return patch_init_rc;
}

bool is_supported_source_device(audio_devices_t source_device_mask)
{
    if((source_device_mask & AUDIO_DEVICE_IN_BUILTIN_MIC) ||
       (source_device_mask & AUDIO_DEVICE_IN_BACK_MIC) ||
       (source_device_mask & AUDIO_DEVICE_IN_WIRED_HEADSET)) {
           ALOGD("%s, source_device_mask (%08x)", __func__, source_device_mask);
           return true;
    }
    return false;
}

bool is_supported_sink_device(audio_devices_t sink_device_mask)
{
    if((sink_device_mask & AUDIO_DEVICE_OUT_SPEAKER) ||
       (sink_device_mask & AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
       (sink_device_mask & AUDIO_DEVICE_OUT_EARPIECE)) {
           ALOGD("%s, sink_device_mask (%08x)", __func__, sink_device_mask);
           return true;
       }
    return false;
}

/* Set loopback volume : for mute implementation */
static int afe_loopback_set_volume(struct audio_device *adev,
              uint16_t low_freq, uint16_t high_freq,
              uint16_t duration_ms, uint16_t gain)
{
    int32_t ret = 0;
    struct mixer_ctl *ctl;
    char mixer_ctl_name[MAX_LENGTH_MIXER_CONTROL_IN_INT];
    long mixer_values[4];

    mixer_values[0] = low_freq;
    mixer_values[1] = high_freq;
    mixer_values[2] = duration_ms;
    mixer_values[3] = gain;
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "DTMF_Generate Rx Low High Duration Gain");

    ALOGD("%s: (%d)\n", __func__, mixer_values[0]);
    ALOGD("%s: (%d)\n", __func__, mixer_values[1]);
    ALOGD("%s: (%d)\n", __func__, mixer_values[2]);
    ALOGD("%s: (%d)\n", __func__, mixer_values[3]);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    if (mixer_ctl_set_array(ctl, mixer_values, ARRAY_SIZE(mixer_values)) < 0) {
        ALOGE("%s: Couldn't set HW Loopback Volume: [%d]", __func__, mixer_values[3]);
        return -EINVAL;
    }

    ALOGV("%s: exit", __func__);
    return ret;
}

/* Get patch type based on source and sink ports configuration */
/* Only ports of type 'DEVICE' are supported */
patch_handle_type_t get_loopback_patch_type(loopback_patch_t*  loopback_patch)
{
    bool is_source_supported=false, is_sink_supported=false;
    struct audio_port_config *source_patch_config = &loopback_patch->
                                                    loopback_source;

    if (loopback_patch->patch_handle_id != PATCH_HANDLE_INVALID) {
        ALOGE("%s, Patch handle already exists", __func__);
        return loopback_patch->patch_handle_id;
    }

    if (loopback_patch->loopback_source.role == AUDIO_PORT_ROLE_SOURCE) {
        switch (loopback_patch->loopback_source.type) {
        case AUDIO_PORT_TYPE_DEVICE :
            if ((loopback_patch->loopback_source.config_mask &
                AUDIO_PORT_CONFIG_FORMAT) &&
                (is_supported_source_device(loopback_patch->loopback_source.ext.device.type))) {
                    switch (loopback_patch->loopback_source.format) {
                    case AUDIO_FORMAT_PCM:
                    case AUDIO_FORMAT_PCM_16_BIT:
                        is_source_supported = true;
                    default:
                    break;
                    }
            } else {
                ALOGE("%s, Unsupported source port device %d", __func__,loopback_patch->loopback_sink.ext.device.type);
            }
            break;
        default :
            break;
            //Unsupported as of now, need to extend for other source types
        }
    }
    if (loopback_patch->loopback_sink.role == AUDIO_PORT_ROLE_SINK) {
        switch (loopback_patch->loopback_sink.type) {
        case AUDIO_PORT_TYPE_DEVICE :
            if ((loopback_patch->loopback_sink.config_mask &
                AUDIO_PORT_CONFIG_FORMAT) &&
                (is_supported_sink_device(loopback_patch->loopback_sink.ext.device.type))) {
                    switch (loopback_patch->loopback_sink.format) {
                    case AUDIO_FORMAT_PCM:
                    case AUDIO_FORMAT_PCM_16_BIT:
                        is_sink_supported = true;
                        break;
                    default:
                        break;
                    }
            } else {
                ALOGE("%s, Unsupported sink port device %d", __func__,loopback_patch->loopback_sink.ext.device.type);
            }
            break;
        default :
            break;
            //Unsupported as of now, need to extend for other sink types
        }
    }
    if (is_source_supported && is_sink_supported) {
        if (source_patch_config->id) {
            ALOGD("%s, is_source_supported (%d) is sink_supported(%d) patch id (%08x)",
                 __func__, is_source_supported, is_sink_supported, AUDIO_PATCH_DTMF_IN_SPKR_OUT);
            return AUDIO_PATCH_DTMF_IN_SPKR_OUT;
        } else {
            ALOGD("%s, is_source_supported (%d) is sink_supported(%d) patch id (%08x)",
                 __func__, is_source_supported, is_sink_supported, AUDIO_PATCH_MIC_IN_SPKR_OUT);
            return AUDIO_PATCH_MIC_IN_SPKR_OUT;
        }
    }
    ALOGE("%s, Unsupported source or sink port config", __func__);
    return loopback_patch->patch_handle_id;
}

/* Releases an existing loopback session */
/* Conditions : Session setup goes bad or actual session teardown */
int32_t release_loopback_session(loopback_patch_t *active_loopback_patch)
{
    int32_t ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = audio_loopback_mod->adev;
    struct stream_inout *inout =  &active_loopback_patch->patch_stream;
    struct audio_port_config *source_patch_config = &active_loopback_patch->
                                                    loopback_source;

    /* 1. Close the PCM devices */
    if (!source_patch_config->id) {
        if (active_loopback_patch->source_stream) {
            pcm_close(active_loopback_patch->source_stream);
            active_loopback_patch->source_stream = NULL;
        } else {
            ALOGE("%s: Failed to close loopback stream in capture path",
                __func__);
        }
    }

    /* 2. Close the PCM devices */
    if (active_loopback_patch->sink_stream) {
        pcm_close(active_loopback_patch->sink_stream);
        active_loopback_patch->sink_stream = NULL;
    } else {
        ALOGE("%s: Failed to close loopback stream in capture path",
            __func__);
    }

    uc_info = get_usecase_from_list(adev, audio_loopback_mod->uc_id);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the loopback usecase (%d) in the list",
            __func__, audio_loopback_mod->uc_id);
        return -EINVAL;
    }

    active_loopback_patch->patch_state = PATCH_INACTIVE;

    /* 2. Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info);

    /* 3. Disable the rx and tx devices */
    disable_snd_device(adev, uc_info->out_snd_device);
    if (!source_patch_config->id)
        disable_snd_device(adev, uc_info->in_snd_device);

    /* 4. Reset backend device to default state */
    platform_invalidate_backend_config(adev->platform,uc_info->in_snd_device);

    list_remove(&uc_info->list);
    free(uc_info);

    adev->active_input = get_next_active_input(adev);

    ALOGD("%s: Release loopback session exit: status(%d)", __func__, ret);
    return ret;
}

/* Callback funtion called in the case of failures */
int loopback_stream_cb(stream_callback_event_t event, void *param, void *cookie)
{
    if (event == AUDIO_EXTN_STREAM_CBK_EVENT_ERROR) {
        pthread_mutex_lock(&audio_loopback_mod->lock);
        release_loopback_session(cookie);
        audio_loopback_mod->patch_db.num_patches--;
        pthread_mutex_unlock(&audio_loopback_mod->lock);
    }
    return 0;
}

/* Create a loopback session based on active loopback patch selected */
int create_loopback_session(loopback_patch_t *active_loopback_patch)
{
    int32_t ret = 0, bits_per_sample;
    struct audio_usecase *uc_info;
    int32_t pcm_dev_rx_id, pcm_dev_tx_id;
    struct audio_device *adev = audio_loopback_mod->adev;
    struct audio_port_config *source_patch_config = &active_loopback_patch->
                                                    loopback_source;
    struct audio_port_config *sink_patch_config = &active_loopback_patch->
                                                    loopback_sink;
    struct stream_inout *inout =  &active_loopback_patch->patch_stream;
    struct stream_in loopback_source_stream;

    ALOGD("%s: Create loopback session begin", __func__);

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info) {
        ALOGE("%s: Failure to open loopback session", __func__);
        return -ENOMEM;
    }
    if (source_patch_config->id) {
        audio_loopback_mod->uc_id = USECASE_AUDIO_DTMF;
        uc_info->id = USECASE_AUDIO_DTMF;
        uc_info->type = DTMF_PLAYBACK;
        loopback_source_stream.source = AUDIO_SOURCE_UNPROCESSED;
    } else {
        audio_loopback_mod->uc_id = USECASE_AUDIO_AFE_LOOPBACK;
        uc_info->id = USECASE_AUDIO_AFE_LOOPBACK;
        uc_info->type = AFE_LOOPBACK;
        loopback_source_stream.source = AUDIO_SOURCE_MIC;
    }
    uc_info->stream.inout = &active_loopback_patch->patch_stream;
    uc_info->devices = active_loopback_patch->patch_stream.out_config.devices;
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info->list);

    loopback_source_stream.device = inout->in_config.devices;
    loopback_source_stream.channel_mask = inout->in_config.channel_mask;
    loopback_source_stream.bit_width = inout->in_config.bit_width;
    loopback_source_stream.sample_rate = inout->in_config.sample_rate;
    loopback_source_stream.format = inout->in_config.format;

    pcm_config.channels = 2;
    pcm_config.rate = 48000;
    pcm_config.period_size = 256;
    pcm_config.period_count = 4;
    pcm_config.format = PCM_FORMAT_S16_LE;
    pcm_config.start_threshold = 0;
    pcm_config.stop_threshold = INT_MAX;
    pcm_config.avail_min = 0;

    memcpy(&loopback_source_stream.usecase, uc_info,
           sizeof(struct audio_usecase));
    adev->active_input = &loopback_source_stream;
    select_devices(adev, uc_info->id);

    pcm_dev_rx_id = platform_get_pcm_device_id(uc_info->id, PCM_PLAYBACK);
    pcm_dev_tx_id = platform_get_pcm_device_id(uc_info->id, PCM_CAPTURE);

    if (pcm_dev_rx_id < 0) {
        ALOGE("%s: Invalid PCM devices (asm: rx %d) for the usecase(%d)",
            __func__, pcm_dev_rx_id, uc_info->id);
        ret = -EIO;
        goto exit;
    }
    if (pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid PCM devices (asm: tx %d) for the usecase(%d)",
            __func__, pcm_dev_tx_id, uc_info->id);
        ret = -EIO;
        goto exit;
    }

    ALOGD("%s: LOOPBACK PCM devices (rx: %d) (tx: %d) usecase(%d)",
        __func__, pcm_dev_rx_id, pcm_dev_tx_id, uc_info->id);

    /* setup a channel for client <--> adsp communication for stream events */
    inout->dev = adev;

    /* Open stream in playback path */
    ALOGV("%s: Opening PCM playback device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_rx_id);
    active_loopback_patch->sink_stream = pcm_open(adev->snd_card,
                               pcm_dev_rx_id,
                               PCM_OUT, &pcm_config);
    if (active_loopback_patch->sink_stream &&
            !pcm_is_ready(active_loopback_patch->sink_stream)) {
        ALOGE("%s: %s", __func__, pcm_get_error(active_loopback_patch->sink_stream));
        ret = -EIO;
        goto exit;
    }

    ALOGV("%s: Opening PCM capture device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_tx_id);
    if (!source_patch_config->id) {
        active_loopback_patch->source_stream = pcm_open(adev->snd_card,
                               pcm_dev_tx_id,
                               PCM_IN, &pcm_config);
        if (active_loopback_patch->source_stream &&
                !pcm_is_ready(active_loopback_patch->source_stream)) {
            ALOGE("%s: %s", __func__, pcm_get_error(active_loopback_patch->source_stream));
            ret = -EIO;
            goto exit;
        }
    } else
        active_loopback_patch->source_stream = active_loopback_patch->sink_stream;

    active_loopback_patch->patch_state = PATCH_CREATED;

    if (!source_patch_config->id) {
        if (pcm_start(active_loopback_patch->source_stream) < 0) {
            ALOGE("%s: Failure to start loopback stream in capture path",
            __func__);
            ret = -EINVAL;
            goto exit;
        }
    }

    if (pcm_start(active_loopback_patch->sink_stream) < 0) {
        ALOGE("%s: Cannot start loopback stream in playback path",
                __func__);
        ret = -EINVAL;
        goto exit;
    }
    /* Move patch state to running, now that session is set up */
    active_loopback_patch->patch_state = PATCH_RUNNING;
    ALOGD("%s: Create loopback session end: status(%d)", __func__, ret);
    return ret;

exit:
    ALOGE("%s: Problem in Loopback session creation: \
            status(%d), releasing session ", __func__, ret);
    release_loopback_session(active_loopback_patch);
    return ret;
}

void update_patch_stream_config(struct stream_config *stream_cfg ,
                                struct audio_port_config *port_cfg)
{
    stream_cfg->sample_rate = port_cfg->sample_rate;
    stream_cfg->channel_mask = port_cfg->channel_mask;
    stream_cfg->format = port_cfg->format;
    stream_cfg->devices = port_cfg->ext.device.type;
    stream_cfg->bit_width = format_to_bitwidth(port_cfg->format);
}

/* API to create audio patch */
int audio_extn_afe_loopback_create_audio_patch(struct audio_hw_device *dev,
                                     unsigned int num_sources,
                                     const struct audio_port_config *sources,
                                     unsigned int num_sinks,
                                     const struct audio_port_config *sinks,
                                     audio_patch_handle_t *handle)
{
    int status = 0;
    patch_handle_type_t loopback_patch_type=0x0;
    loopback_patch_t loopback_patch, *active_loopback_patch = NULL;

    ALOGV("%s : Create audio patch begin", __func__);

    if ((audio_loopback_mod == NULL) || (dev == NULL)) {
        ALOGE("%s, Loopback module not initialized orInvalid device", __func__);
        status = -EINVAL;
        return status;
    }

    pthread_mutex_lock(&audio_loopback_mod->lock);
    if (audio_loopback_mod->patch_db.num_patches >= MAX_NUM_PATCHES ) {
        ALOGE("%s, Exhausted maximum possible patches per device", __func__);
        status = -EINVAL;
        goto exit_create_patch;
    }

    /* Port configuration check & validation */
    if (num_sources > MAX_SOURCE_PORTS_PER_PATCH ||
        num_sinks > MAX_SINK_PORTS_PER_PATCH) {
        ALOGE("%s, Unsupported patch configuration, sources %d sinks %d ",
                __func__, num_sources, num_sources);
        status = -EINVAL;
        goto exit_create_patch;
    }

    /* Use an empty patch from patch database and initialze */
    active_loopback_patch = &(audio_loopback_mod->patch_db.loopback_patch[
                                audio_loopback_mod->patch_db.num_patches]);
    active_loopback_patch->patch_handle_id = PATCH_HANDLE_INVALID;
    active_loopback_patch->patch_state = PATCH_INACTIVE;
    active_loopback_patch->patch_stream.ip_hdlr_handle = NULL;
    active_loopback_patch->patch_stream.adsp_hdlr_stream_handle = NULL;
    memcpy(&active_loopback_patch->loopback_source, &sources[0], sizeof(struct
    audio_port_config));
    memcpy(&active_loopback_patch->loopback_sink, &sinks[0], sizeof(struct
    audio_port_config));

    /* Get loopback patch type based on source and sink ports configuration */
    loopback_patch_type = get_loopback_patch_type(active_loopback_patch);

    if (loopback_patch_type == PATCH_HANDLE_INVALID) {
        ALOGE("%s, Unsupported patch type", __func__);
        status = -EINVAL;
        goto exit_create_patch;
    }

    update_patch_stream_config(&active_loopback_patch->patch_stream.in_config,
                                &active_loopback_patch->loopback_source);
    update_patch_stream_config(&active_loopback_patch->patch_stream.out_config,
                                &active_loopback_patch->loopback_sink);
    // Lock patch database, create patch handle and add patch handle to the list

    active_loopback_patch->patch_handle_id = (loopback_patch_type << 8 |
                                audio_loopback_mod->patch_db.num_patches);

    /* Is usecase afe loopback? If yes, invoke loopback driver */
    if ((active_loopback_patch->loopback_source.type == AUDIO_PORT_TYPE_DEVICE)
       &&
       (active_loopback_patch->loopback_sink.type == AUDIO_PORT_TYPE_DEVICE)) {
        status = create_loopback_session(active_loopback_patch);
        if (status != 0)
            goto exit_create_patch;
    }

    // Create callback thread to listen to events from HW data path

    /* Fill unique handle ID generated based on active loopback patch */
    *handle = audio_loopback_mod->patch_db.loopback_patch[audio_loopback_mod->
                                        patch_db.num_patches].patch_handle_id;
    audio_loopback_mod->patch_db.num_patches++;

exit_create_patch :
    ALOGV("%s : Create audio patch end, status(%d)", __func__, status);
    pthread_mutex_unlock(&audio_loopback_mod->lock);
    return status;
}

/* API to release audio patch */
int audio_extn_afe_loopback_release_audio_patch(struct audio_hw_device *dev,
                                             audio_patch_handle_t handle)
{
    int status = 0, n=0, patch_index=-1;
    bool patch_found = false;
    loopback_patch_t *active_loopback_patch = NULL;
    ALOGV("%s audio_extn_afe_loopback_release_audio_patch begin %d", __func__, __LINE__);

    if ((audio_loopback_mod == NULL) || (dev == NULL)) {
        ALOGE("%s, Invalid device", __func__);
        status = -1;
        return status;
    }

    pthread_mutex_lock(&audio_loopback_mod->lock);

    for (n=0;n < MAX_NUM_PATCHES;n++) {
        if (audio_loopback_mod->patch_db.loopback_patch[n].patch_handle_id ==
           handle) {
            patch_found = true;
            patch_index = n;
            break;
        }
    }

    if (patch_found && (audio_loopback_mod->patch_db.num_patches > 0)) {
        active_loopback_patch = &(audio_loopback_mod->patch_db.loopback_patch[
                                patch_index]);
        status = release_loopback_session(active_loopback_patch);
        audio_loopback_mod->patch_db.num_patches--;
    } else {
        ALOGE("%s, Requested Patch handle does not exist", __func__);
        status = -1;
    }
    pthread_mutex_unlock(&audio_loopback_mod->lock);

    ALOGV("%s audio_extn_afe_loopback_release_audio_patch done, status(%d)", __func__,
    status);
    return status;
}

/* Find port config from patch database based on port info */
static struct audio_port_config* get_port_from_patch_db(port_info_t *port,
                               patch_db_t *audio_patch_db, int *patch_num)
{
    int n=0, patch_index=-1;
    struct audio_port_config *cur_port=NULL;

    if (port->role == AUDIO_PORT_ROLE_SOURCE) {
        for (n=0;n < audio_patch_db->num_patches;n++) {
            cur_port = &(audio_patch_db->loopback_patch[n].loopback_source);
            if ((cur_port->id == port->id) && (cur_port->type == port->type) && (
               cur_port->role == port->role)) {
                patch_index = n;
                break;
            }
        }
    } else if (port->role == AUDIO_PORT_ROLE_SINK) {
        for (n=0;n < audio_patch_db->num_patches;n++) {
            cur_port = &(audio_patch_db->loopback_patch[n].loopback_sink);
            if ((cur_port->id == port->id) && (cur_port->type == port->type) && (
               cur_port->role == port->role)) {
                patch_index = n;
                break;
            }
        }
    }
    *patch_num = patch_index;
    return cur_port;
}

int audio_extn_afe_loopback_set_audio_port_config(struct audio_hw_device *dev,
                                        const struct audio_port_config *config)
{
    int status = 0, n=0, patch_num=-1;
    port_info_t port_info;
    struct audio_port_config *port_out=NULL;
    struct audio_device *adev = audio_loopback_mod->adev;

    ALOGV("%s %d", __func__, __LINE__);

    if ((audio_loopback_mod == NULL) || (dev == NULL)) {
        ALOGE("%s, Invalid device", __func__);
        status = -EINVAL;
        return status;
    }

    pthread_mutex_lock(&audio_loopback_mod->lock);

    port_info.id = config->id;
    port_info.role = config->role;              /* sink or source */
    port_info.type = config->type;              /* device, mix  */
    port_out = get_port_from_patch_db(&port_info, &audio_loopback_mod->patch_db
                                    , &patch_num);

    if (port_out == NULL) {
        ALOGE("%s, Unable to find a valid matching port in patch \
        database,exiting", __func__);
        status = -EINVAL;
        goto exit_set_port_config;
    }

    port_out->config_mask |= config->config_mask;
    if(config->config_mask & AUDIO_PORT_CONFIG_GAIN)
        port_out->gain = config->gain;

    if((port_out->config_mask & AUDIO_PORT_CONFIG_GAIN) &&
        port_out->gain.mode == AUDIO_GAIN_MODE_JOINT ) {
        status = afe_loopback_set_volume(adev,
                    port_out->gain.values[0],
                    port_out->gain.values[1],
                    port_out->gain.values[2],
                    port_out->gain.values[3]);
        if (status) {
            ALOGE("%s, Error setting loopback gain config: status %d",
                  __func__, status);
        }
    } else {
        ALOGE("%s, Unsupported port config ,exiting", __func__);
        status = -EINVAL;
    }

    /* Currently, port config is not used for anything,
    need to restart session    */
exit_set_port_config:
    pthread_mutex_unlock(&audio_loopback_mod->lock);
    return status;
}

/* Loopback extension initialization, part of hal init sequence */
int audio_extn_afe_loopback_init(struct audio_device *adev)
{
    ALOGV("%s Audio loopback extension initializing", __func__);
    int ret = 0, size = 0;

    if (audio_loopback_mod != NULL) {
        pthread_mutex_lock(&audio_loopback_mod->lock);
        if (audio_loopback_mod->adev == adev) {
            ALOGV("%s %d : Audio loopback module already exists", __func__,
                    __LINE__);
        } else {
            ALOGV("%s %d : Audio loopback module called for invalid device",
                    __func__, __LINE__);
            ret = -EINVAL;
        }
        goto loopback_done;
    }
    audio_loopback_mod = malloc(sizeof(struct audio_loopback));
    if (audio_loopback_mod == NULL) {
        ALOGE("%s, out of memory", __func__);
        ret = -ENOMEM;
        goto loopback_done;
    }

    pthread_mutex_init(&audio_loopback_mod->lock,
                        (const pthread_mutexattr_t *)NULL);
    pthread_mutex_lock(&audio_loopback_mod->lock);
    audio_loopback_mod->adev = adev;

    ret = init_patch_database(&audio_loopback_mod->patch_db);

    audio_loopback_mod->uc_id = USECASE_AUDIO_AFE_LOOPBACK;
    audio_loopback_mod->uc_type = AFE_LOOPBACK;

loopback_done:
    if (ret != 0) {
        if (audio_loopback_mod != NULL) {
            pthread_mutex_unlock(&audio_loopback_mod->lock);
            pthread_mutex_destroy(&audio_loopback_mod->lock);
            free(audio_loopback_mod);
            audio_loopback_mod = NULL;
        }
    } else {
        pthread_mutex_unlock(&audio_loopback_mod->lock);
    }
    ALOGV("%s Audio loopback extension initialized", __func__);
    return ret;
}

void audio_extn_afe_loopback_deinit(struct audio_device *adev)
{
    ALOGV("%s Audio loopback extension de-initializing", __func__);

    if (audio_loopback_mod == NULL) {
        ALOGE("%s, loopback module NULL, cannot deinitialize", __func__);
        return;
    }
    pthread_mutex_lock(&audio_loopback_mod->lock);

    if (audio_loopback_mod->adev == adev) {
        if (audio_loopback_mod != NULL) {
            pthread_mutex_unlock(&audio_loopback_mod->lock);
            pthread_mutex_destroy(&audio_loopback_mod->lock);
            free(audio_loopback_mod);
            audio_loopback_mod = NULL;
        }
        return;
    } else {
        ALOGE("%s, loopback module not valid, cannot deinitialize", __func__);
    }
    pthread_mutex_unlock(&audio_loopback_mod->lock);
    return;
}
