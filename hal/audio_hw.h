/*
 * Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 * Not a contribution.
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

#ifndef QCOM_AUDIO_HW_H
#define QCOM_AUDIO_HW_H

#include <stdlib.h>
#include <cutils/list.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>
#include <tinycompress/tinycompress.h>

#include <audio_route/audio_route.h>
#include "audio_defs.h"
#include "voice.h"

#ifdef VHAL_HELPER_ENABLED
#include <vehicle-hal-audio-helper-for-c.h>
#endif

#define VISUALIZER_LIBRARY_PATH "/vendor/lib/soundfx/libqcomvisualizer.so"
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/vendor/lib/soundfx/libqcompostprocbundle.so"
#define ADM_LIBRARY_PATH "/vendor/lib/libadm.so"

/* Flags used to initialize acdb_settings variable that goes to ACDB library */
#define NONE_FLAG            0x00000000
#define ANC_FLAG	     0x00000001
#define DMIC_FLAG            0x00000002
#define QMIC_FLAG            0x00000004
#define TTY_MODE_OFF         0x00000010
#define TTY_MODE_FULL        0x00000020
#define TTY_MODE_VCO         0x00000040
#define TTY_MODE_HCO         0x00000080
#define TTY_MODE_CLEAR       0xFFFFFF0F
#define FLUENCE_MODE_CLEAR   0xFFFFFFF0

#define ACDB_DEV_TYPE_OUT 1
#define ACDB_DEV_TYPE_IN 2
#define ACDB_DEV_TYPE_MAX 2

#define MAX_SUPPORTED_CHANNEL_MASKS 8
#define MAX_SUPPORTED_FORMATS 3
#define DEFAULT_HDMI_OUT_CHANNELS   2

#define SND_CARD_STATE_OFFLINE 0
#define SND_CARD_STATE_ONLINE 1

/* These are the supported use cases by the hardware.
 * Each usecase is mapped to a specific PCM device.
 * Refer to pcm_device_table[].
 */
enum {
    USECASE_INVALID = -1,
    /* Playback usecases */
    USECASE_AUDIO_PLAYBACK_DEEP_BUFFER = 0,
    USECASE_AUDIO_PLAYBACK_LOW_LATENCY,
    USECASE_AUDIO_PLAYBACK_MULTI_CH,
    USECASE_AUDIO_PLAYBACK_OFFLOAD,
#ifdef MULTIPLE_OFFLOAD_ENABLED
    USECASE_AUDIO_PLAYBACK_OFFLOAD2,
    USECASE_AUDIO_PLAYBACK_OFFLOAD3,
    USECASE_AUDIO_PLAYBACK_OFFLOAD4,
    USECASE_AUDIO_PLAYBACK_OFFLOAD5,
    USECASE_AUDIO_PLAYBACK_OFFLOAD6,
    USECASE_AUDIO_PLAYBACK_OFFLOAD7,
    USECASE_AUDIO_PLAYBACK_OFFLOAD8,
    USECASE_AUDIO_PLAYBACK_OFFLOAD9,
#endif
    USECASE_AUDIO_PLAYBACK_ULL,

    USECASE_AUDIO_DIRECT_PCM_OFFLOAD,

    /* FM usecase */
    USECASE_AUDIO_PLAYBACK_FM,

    /* HFP Use case*/
    USECASE_AUDIO_HFP_SCO_UPLINK,
    USECASE_AUDIO_HFP_SCO_DOWNLINK,
    USECASE_AUDIO_HFP_SCO_WB_UPLINK,
    USECASE_AUDIO_HFP_SCO_WB_DOWNLINK,

    /* Capture usecases */
    USECASE_AUDIO_RECORD,
    USECASE_AUDIO_RECORD_COMPRESS,
    USECASE_AUDIO_RECORD_LOW_LATENCY,
    USECASE_AUDIO_RECORD_FM_VIRTUAL,

    /* Voice usecase */
    USECASE_VOICE_CALL,

    /* Voice extension usecases */
    USECASE_VOICE2_CALL,
    USECASE_VOLTE_CALL,
    USECASE_QCHAT_CALL,
    USECASE_VOWLAN_CALL,
    USECASE_VOICEMMODE1_CALL,
    USECASE_VOICEMMODE2_CALL,
    USECASE_COMPRESS_VOIP_CALL,

    USECASE_INCALL_REC_UPLINK,
    USECASE_INCALL_REC_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_COMPRESS,
    USECASE_INCALL_REC_DOWNLINK_COMPRESS,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS,

    USECASE_INCALL_MUSIC_UPLINK,
    USECASE_INCALL_MUSIC_UPLINK2,

    USECASE_AUDIO_SPKR_CALIB_RX,
    USECASE_AUDIO_SPKR_CALIB_TX,

    USECASE_AUDIO_PLAYBACK_AFE_PROXY,
    USECASE_AUDIO_RECORD_AFE_PROXY,
    USECASE_AUDIO_PLAYBACK_DRIVER_SIDE,

    /*Audio FM Tuner usecase*/
    USECASE_AUDIO_FM_TUNER_EXT,

    /* In Car Communication usecase*/
    USECASE_ICC_CALL,

    /* Active Noise Cancellation usecase*/
    USECASE_ANC_LOOPBACK,
#ifdef BUS_ADDRESS_ENABLED
    /* bus address streams for car support */
    USECASE_AUDIO_PLAYBACK_MEDIA,
    USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION,
#endif

    AUDIO_USECASE_MAX
};

const char * const use_case_table[AUDIO_USECASE_MAX];

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/*
 * tinyAlsa library interprets period size as number of frames
 * one frame = channel_count * sizeof (pcm sample)
 * so if format = 16-bit PCM and channels = Stereo, frame size = 2 ch * 2 = 4 bytes
 * DEEP_BUFFER_OUTPUT_PERIOD_SIZE = 1024 means 1024 * 4 = 4096 bytes
 * We should take care of returning proper size when AudioFlinger queries for
 * the buffer size of an input/output stream
 */

enum {
    OFFLOAD_CMD_EXIT,               /* exit compress offload thread loop*/
    OFFLOAD_CMD_DRAIN,              /* send a full drain request to DSP */
    OFFLOAD_CMD_PARTIAL_DRAIN,      /* send a partial drain request to DSP */
    OFFLOAD_CMD_WAIT_FOR_BUFFER,    /* wait for buffer released by DSP */
};

enum {
    OFFLOAD_STATE_IDLE,
    OFFLOAD_STATE_PLAYING,
    OFFLOAD_STATE_PAUSED,
};

struct offload_cmd {
    struct listnode node;
    int cmd;
    int data[];
};

struct stream_app_type_cfg {
    int sample_rate;
    uint32_t bit_width;
    int app_type;
};

struct stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
    pthread_cond_t  cond;
    struct pcm_config config;
    struct compr_config compr_config;
    struct pcm *pcm;
    struct compress *compr;
    int standby;
    int pcm_device_id;
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    audio_devices_t devices;
    audio_output_flags_t flags;
    audio_usecase_t usecase;
    /* Array of supported channel mask configurations. +1 so that the last entry is always 0 */
    audio_channel_mask_t supported_channel_masks[MAX_SUPPORTED_CHANNEL_MASKS + 1];
    audio_format_t supported_formats[MAX_SUPPORTED_FORMATS+1];
    bool muted;
    uint64_t written; /* total frames written, not cleared when entering standby */
    audio_io_handle_t handle;
    struct stream_app_type_cfg app_type_cfg;

    int non_blocking;
    int playback_started;
    int offload_state;
    pthread_cond_t offload_cond;
    pthread_t offload_thread;
    struct listnode offload_cmd_list;
    bool offload_thread_blocked;

    stream_callback_t offload_callback;
    void *offload_cookie;
    struct compr_gapless_mdata gapless_mdata;
    int send_new_metadata;
    bool send_next_track_params;
    bool is_compr_metadata_avail;
    unsigned int bit_width;

#ifdef BUS_ADDRESS_ENABLED
    char address[AUDIO_DEVICE_MAX_ADDRESS_LEN];
    int car_audio_stream;
#endif

#ifdef VHAL_HELPER_ENABLED
    vehicle_hal_audio_helper_t *vhal_audio_helper;
#endif

    struct audio_device *dev;
};

struct stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
    struct pcm_config config;
    struct pcm *pcm;
    int standby;
    int source;
    int pcm_device_id;
    audio_devices_t device;
    audio_channel_mask_t channel_mask;
    audio_usecase_t usecase;
    bool enable_aec;
    bool enable_ns;
    audio_format_t format;
    audio_io_handle_t capture_handle;
    audio_input_flags_t flags;
    bool is_st_session;
    bool is_st_session_active;
    unsigned int sample_rate;
    struct stream_app_type_cfg app_type_cfg;
    unsigned int bit_width;

    struct audio_device *dev;
};

typedef enum {
    PCM_PLAYBACK,
    PCM_CAPTURE,
    VOICE_CALL,
    VOIP_CALL,
    PCM_HFP_CALL,
    PCM_PASSTHROUGH,
    ICC_CALL,
    ANC_LOOPBACK
} usecase_type_t;

union stream_ptr {
    struct stream_in *in;
    struct stream_out *out;
};

struct audio_usecase {
    struct listnode list;
    audio_usecase_t id;
    usecase_type_t  type;
    audio_devices_t devices;
    snd_device_t out_snd_device;
    snd_device_t in_snd_device;
    struct stream_app_type_cfg out_app_type_cfg;
    struct stream_app_type_cfg in_app_type_cfg;
    union stream_ptr stream;
};

struct sound_card_status {
    pthread_mutex_t lock;
    int state;
};

struct stream_format {
    struct listnode list;
    audio_format_t format;
};

struct stream_sample_rate {
    struct listnode list;
    uint32_t sample_rate;
};

struct streams_output_cfg {
    struct listnode list;
    audio_output_flags_t flags;
    struct listnode format_list;
    struct listnode sample_rate_list;
    struct stream_app_type_cfg app_type_cfg;
};

struct streams_input_cfg {
    struct listnode list;
    audio_input_flags_t flags;
    struct listnode format_list;
    struct listnode sample_rate_list;
    struct stream_app_type_cfg app_type_cfg;
};

typedef struct streams_input_ctxt {
    struct listnode list;
    struct stream_in *input;
} streams_input_ctxt_t;

typedef struct streams_output_ctxt {
    struct listnode list;
    struct stream_out *output;
} streams_output_ctxt_t;

typedef void* (*adm_init_t)();
typedef void (*adm_deinit_t)(void *);
typedef void (*adm_register_output_stream_t)(void *, audio_io_handle_t, audio_output_flags_t);
typedef void (*adm_register_input_stream_t)(void *, audio_io_handle_t, audio_input_flags_t);
typedef void (*adm_deregister_stream_t)(void *, audio_io_handle_t);
typedef void (*adm_request_focus_t)(void *, audio_io_handle_t);
typedef void (*adm_abandon_focus_t)(void *, audio_io_handle_t);

struct audio_device {
    struct audio_hw_device device;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    struct mixer *mixer;
    audio_mode_t mode;
    audio_devices_t out_device;
    struct stream_in *active_input;
    struct stream_out *primary_output;
    struct stream_out *voice_tx_output;
    struct stream_out *current_call_output;
    bool bluetooth_nrec;
    bool screen_off;
    int *snd_dev_ref_cnt;
    struct listnode usecase_list;
    struct listnode streams_output_cfg_list;
    struct listnode streams_input_cfg_list;
    struct audio_route *audio_route;
    int acdb_settings;
    bool speaker_lr_swap;
    struct voice voice;
    unsigned int cur_hdmi_channels;
    unsigned int cur_wfd_channels;
    bool bt_wb_speech_enabled;
    bool voice_barge_in_enabled;
    bool vad_stream_running;
    struct stream_in *vad_stream;

    int snd_card;
    unsigned int cur_codec_backend_samplerate;
    unsigned int cur_codec_backend_bit_width;
    bool is_channel_status_set;
    void *platform;
    unsigned int offload_usecases_state;
    void *visualizer_lib;
    int (*visualizer_start_output)(audio_io_handle_t, int);
    int (*visualizer_stop_output)(audio_io_handle_t, int);
    void *offload_effects_lib;
    int (*offload_effects_start_output)(audio_io_handle_t, int, struct mixer *);
    int (*offload_effects_stop_output)(audio_io_handle_t, int);

    struct sound_card_status snd_card_status;
    int (*offload_effects_set_hpx_state)(bool);

    void *adm_data;
    void *adm_lib;
    adm_init_t adm_init;
    adm_deinit_t adm_deinit;
    adm_register_input_stream_t adm_register_input_stream;
    adm_register_output_stream_t adm_register_output_stream;
    adm_deregister_stream_t adm_deregister_stream;
    adm_request_focus_t adm_request_focus;
    adm_abandon_focus_t adm_abandon_focus;

    void (*offload_effects_get_parameters)(struct str_parms *,
                                           struct str_parms *);
    void (*offload_effects_set_parameters)(struct str_parms *);
    void *ext_hw_plugin;
    struct listnode audio_patch_record_list;
    unsigned int audio_patch_index;
    struct listnode active_inputs_list;
    struct listnode active_outputs_list;
};

struct audio_patch_record {
    struct listnode list;
    audio_patch_handle_t handle;
    audio_usecase_t usecase;
    audio_io_handle_t input_io_handle;
    audio_io_handle_t output_io_handle;
};

int select_devices(struct audio_device *adev,
                          audio_usecase_t uc_id);
int disable_audio_route(struct audio_device *adev,
                        struct audio_usecase *usecase);
int disable_snd_device(struct audio_device *adev,
                       snd_device_t snd_device);
int enable_snd_device(struct audio_device *adev,
                      snd_device_t snd_device);

int enable_audio_route(struct audio_device *adev,
                       struct audio_usecase *usecase);

struct audio_usecase *get_usecase_from_list(struct audio_device *adev,
                                                   audio_usecase_t uc_id);

bool is_offload_usecase(audio_usecase_t uc_id);

int pcm_ioctl(struct pcm *pcm, int request, ...);

int get_snd_card_state(struct audio_device *adev);
audio_usecase_t get_usecase_id_from_usecase_type(struct audio_device *adev,
                                                 usecase_type_t type);

#define LITERAL_TO_STRING(x) #x
#define CHECK(condition) LOG_ALWAYS_FATAL_IF(!(condition), "%s",\
            __FILE__ ":" LITERAL_TO_STRING(__LINE__)\
            " ASSERT_FATAL(" #condition ") failed.")

/*
 * NOTE: when multiple mutexes have to be acquired, always take the
 * stream_in or stream_out mutex first, followed by the audio_device mutex.
 */

#endif // QCOM_AUDIO_HW_H
