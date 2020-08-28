/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_hw_haptic"
#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0

#include <cutils/properties.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <pthread.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "platform_api.h"
#include <platform.h>
#include <math.h>


#define AUDIO_PARAMETER_KEY_VIBRATE_OPEN "vibrate_open"
#define AUDIO_PARAMETER_KEY_VIBRATE_ENABLED "vibrate_enabled"
#define AUDIO_PARAMETER_KEY_VIBRATE_DURATION "vibrate_duration"

#define DEFAULT_HAPTIC_FREQ_HZ     170  //haptic frequency
#define DEFAULT_HAPTIC_DURATION_MS 500  //duration in msec
#define HAPTIC_PCM_FRAME_TIME_MS   5
#define SEC_TO_MS                  1000
#define DEFAULT_HAPTIC_AMP_PERCENT 100 //haptic percentage amplitude

#define PI 3.14159265358979323846
//#define DUMP_HAPTIC_FILE 1

#ifdef DUMP_HAPTIC_FILE
#define HAPTIC_PCM_FILE "/data/misc/audio/170hz_haptic_pcm"
#endif

#define AUDIO_PARAMETER_KEY_FM_VOLUME "fm_volume"

static struct pcm_config pcm_config_haptic = {
    .channels = 1,
    .rate = 48000,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold =  LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

typedef enum {
    STATE_DEINIT = -1,
    STATE_IDLE,
    STATE_ACTIVE,
    STATE_DISABLED,
} state_t;

typedef enum {
    REQUEST_WRITE,
    REQUEST_QUIT,
} request_t;

struct hap_cmd {
    struct listnode node;
    request_t req;
};

typedef struct {
    pthread_mutex_t lock;
    struct pcm *pcm;
    struct stream_out *out;
    pthread_t thread_id;
    struct listnode cmd_list;
    pthread_cond_t  cond;
    state_t state;
    volatile bool done;
    int duration;
    void * userdata;
} haptic_priv_t;

haptic_priv_t hap;

bool tone_playback_active = false;
bool whs_connected = false;
float fm_vol = 0.0;

static pthread_once_t haptic_init_once_t = PTHREAD_ONCE_INIT;
static pthread_once_t haptic_deinit_once_t = PTHREAD_ONCE_INIT;

static int haptic_cleanup();
void audio_extn_haptic_disable_route(struct audio_device *adev);
void audio_extn_haptic_enable_route(struct audio_device *adev);

static void send_cmd_l(request_t r)
{
    struct hap_cmd *cmd =
        (struct hap_cmd *)calloc(1, sizeof(struct hap_cmd));

    if (cmd == NULL) {
        ALOGE("%s: cmd is NULL", __func__);
        return;
    }

    cmd->req = r;
    list_add_tail(&hap.cmd_list, &cmd->node);
    pthread_cond_signal(&hap.cond);
}

static void gen_pcm (int16_t *buffer)
{
    int i;
    int sample_rate = 48000;
    float amplitude = 1.0f;

    float frequency = (float)property_get_int32("vendor.audio.haptic_freq",
                                                    DEFAULT_HAPTIC_FREQ_HZ);
    int ampl_percent = property_get_int32("vendor.audio.haptic_ampl",
                                                DEFAULT_HAPTIC_AMP_PERCENT);
    ALOGV ("%s ampl_percent = %d", __func__, ampl_percent);
    amplitude = amplitude * (float)ampl_percent / DEFAULT_HAPTIC_AMP_PERCENT;
    ALOGV("%s Generate haptic data with frequency %f, amplitude %f",
                                  __func__, frequency, amplitude);
    for (i = 0; i < sample_rate; i++) {
        buffer[i] = (int16_t)(sin((float) 2 * PI * frequency *
                    ((float)i / (float)sample_rate)) * 32767.0f * amplitude);
    }
}

static void * haptic_thread_loop(void *param __unused)
{
    struct hap_cmd *cmd = NULL;
    struct listnode *item;
    uint8_t *hap_buffer = NULL;
    int rc = 0;

    ALOGV("%s: haptic_thread_loop entry", __func__);
    /* Generate the haptic buffer data once */
    if (hap_buffer == NULL) {
        // Alloc buffer for 1 second
        hap_buffer = (uint8_t *)calloc (1, pcm_config_haptic.rate * sizeof(int16_t));
        if (hap_buffer == NULL) {
            ALOGE("%s: hap.buffer is NULL", __func__);
            goto thrd_exit;
        }
    }
    gen_pcm(hap_buffer);

    while (true) {
        pthread_mutex_lock(&hap.lock);
        if (list_empty(&hap.cmd_list)) {
            pthread_cond_wait(&hap.cond, &hap.lock);
            pthread_mutex_unlock(&hap.lock);
            continue;
        }
        item = list_head(&hap.cmd_list);
        cmd = node_to_item(item, struct hap_cmd, node);
        list_remove(item);
        if (cmd->req == REQUEST_QUIT) {
            ALOGV ("%s: Received cmd REQUEST_QUIT ", __func__);
            free(cmd);
            pthread_mutex_unlock(&hap.lock);
            break;
        }
        // check if start command and pcm is initialized properly
        if (cmd->req == REQUEST_WRITE) {
            struct audio_device * adev = (struct audio_device *)hap.userdata;
            if (hap.pcm == NULL || hap.out == NULL ||
                get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_HAPTIC) == NULL) {
                ALOGE("%s Haptic playback not initialized!", __func__);
                free(cmd);
                pthread_mutex_unlock(&hap.lock);
                continue;
            }
        }
        free(cmd);
        //Change to active state
        hap.state = STATE_ACTIVE;
        ALOGV("%s State changed to ACTIVE", __func__);
        pthread_mutex_unlock(&hap.lock);
        int tot_samples = hap.duration * (pcm_config_haptic.rate / SEC_TO_MS);
        // write 'x' msec pcm buffer
        int num_frames = pcm_config_haptic.channels * HAPTIC_PCM_FRAME_TIME_MS
                                              * (pcm_config_haptic.rate / SEC_TO_MS);
        ALOGV("%s: Total samples = %d, num_frames = %d", __func__,
                                              tot_samples, num_frames);
        int cnt = 0, pos = 0;

#ifdef DUMP_HAPTIC_FILE
        FILE *fp = NULL;
        int rc = 0;
        ALOGD ("%s: Create file %s to dump hap buffer", __func__, HAPTIC_PCM_FILE);
        fp = fopen (HAPTIC_PCM_FILE, "wb");
        if (!fp) {
            ALOGD("Failed to create file %s", HAPTIC_PCM_FILE);
        }
#endif
        while (cnt < tot_samples) {
            pcm_write(hap.pcm, (void *)(hap_buffer + pos), num_frames * sizeof(int16_t));

#ifdef DUMP_HAPTIC_FILE
            if (fp != NULL)
                rc = fwrite(hap_buffer + pos, sizeof(int16_t), num_frames, fp);
#endif
            cnt += num_frames;
            pos += (num_frames * sizeof(int16_t));
            if (pos == pcm_config_haptic.rate * sizeof(int16_t))
                pos = 0;
            // Stop triggered
            if (hap.done && cnt == tot_samples) {
                break;
            }
        }
        // write completed
        ALOGV("%s :Haptic write complete, written = %d, Total Samples = %d",
                                                 __func__, cnt, tot_samples);
#ifdef DUMP_HAPTIC_FILE
        if (fp != NULL) {
            fclose(fp);
            fp = NULL;
        }
#endif
        //Change to idle state
        pthread_mutex_lock(&hap.lock);
        hap.state = STATE_IDLE;
        ALOGV("%s State changed to IDLE", __func__);
        pthread_cond_signal(&hap.cond);
        pthread_mutex_unlock(&hap.lock);
    }
thrd_exit:
    if (hap_buffer != NULL) {
        free(hap_buffer);
        hap_buffer = NULL;
        ALOGV("%s Freed haptic buffer data", __func__);
    }
    ALOGE("haptic_thread_loop exit");
    return 0;
}

static void haptic_init_once()
{
    hap.state = STATE_IDLE;
    hap.pcm = NULL;

    ALOGV("%s Entry ", __func__);
    if (property_get_bool("vendor.audio.haptic_audio", false) == false) {
        ALOGE("Haptic is disabled");
        hap.state = STATE_DISABLED;
        return;
    }
    ALOGD ("%s: Feature HAPTIC_AUDIO playback enabled", __func__);
    pthread_mutex_init(&hap.lock, (const pthread_mutexattr_t *) NULL);
    pthread_cond_init(&hap.cond, (const pthread_condattr_t *) NULL);
    if (pthread_create(&hap.thread_id,  (const pthread_attr_t *) NULL,
                       haptic_thread_loop, NULL) < 0) {
        ALOGW("Failed to create haptic_thread_loop");
        hap.state = STATE_DEINIT;
    }
    list_init(&hap.cmd_list);
    ALOGV("%s Exit ", __func__);
}

static void haptic_deinit_once()
{
    ALOGV("%s: Entry ", __func__);
    if (hap.state == STATE_DISABLED || hap.state == STATE_DEINIT)
        return;

    hap.done = true;
    pthread_mutex_lock(&hap.lock);
    send_cmd_l(REQUEST_QUIT);
    pthread_mutex_unlock(&hap.lock);
    pthread_join(hap.thread_id, (void **) NULL);
    haptic_cleanup();
    hap.userdata = NULL;
    pthread_mutex_destroy(&hap.lock);
    pthread_cond_destroy(&hap.cond);
    ALOGV("%s: Exit ", __func__);
}

void audio_extn_haptic_init(struct audio_device *adev)
{
    int rc = pthread_once(&haptic_init_once_t, haptic_init_once);
    if (rc) {
        ALOGE("%s Failed ", __func__);
    }
    hap.userdata = adev;
}

void audio_extn_haptic_deinit()
{
    ALOGV("%s: Entry ", __func__);
    if (pthread_once(&haptic_deinit_once_t, haptic_deinit_once)) {
        ALOGE("%s Failed ", __func__);
    }
    ALOGV("%s: Exit", __func__);
}

void haptic_stop()
{
    ALOGV("%s: Entry ", __func__);
    pthread_mutex_lock(&hap.lock);
    hap.done = true;

    //Wait for haptic playback to complete
    while (hap.state != STATE_IDLE) {
        pthread_cond_wait(&hap.cond, &hap.lock);
    }
    // If haptic playback stopped & IDLE , call haptic_cleanup
    if (hap.state == STATE_IDLE) {
        ALOGV("%s Haptic state is IDLE, cleanup!", __func__);
        haptic_cleanup();
    }
exit:
    pthread_mutex_unlock(&hap.lock);
    ALOGV("%s: Exit ", __func__);
}

void audio_extn_haptic_stop (struct audio_device *adev)
{
    state_t state = STATE_DEINIT;
    ALOGV("%s: Entry ", __func__);

    pthread_mutex_lock(&hap.lock);
    state = hap.state;
    if (state == STATE_DISABLED || state == STATE_DEINIT) {
        ALOGV("%s :Haptic playback feature disabled", __func__);
        goto exit;
    }
    if (hap.state != STATE_ACTIVE && hap.pcm == NULL &&
        get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_HAPTIC) == NULL) {
        ALOGV("%s: Haptic playback already stopped", __func__);
        goto exit;
    }
    else {
        pthread_mutex_unlock(&hap.lock);
        haptic_stop();
    }
exit:
    pthread_mutex_unlock(&hap.lock);
    ALOGV("%s: Exit ", __func__);
}

void haptic_start(struct audio_device *adev)
{
    struct pcm *pcm;
    struct audio_usecase *usecase;
    int rc = 0;
    unsigned int flags = PCM_OUT|PCM_MONOTONIC;

    ALOGV("%s Entry ", __func__);

    int hap_pcm_dev_id =
            platform_get_pcm_device_id(USECASE_AUDIO_PLAYBACK_HAPTIC,
                                       PCM_PLAYBACK);

    pthread_mutex_lock(&hap.lock);
    if (hap.state == STATE_DISABLED || hap.state == STATE_DEINIT) {
        ALOGE("%s : Haptic feature not initialized, cannot start", __func__);
        goto exit;
    }
    if (hap.state == STATE_ACTIVE) {
        ALOGD("%s: Haptic playback is already ACTIVE", __func__);
        goto exit;
    }
    if (hap.state == STATE_IDLE && hap.pcm != NULL &&
        get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_HAPTIC) != NULL) {
        ALOGD("%s: Haptic State IDLE, re-start vibrate playback", __func__);
        goto start_write;
    }

    if (hap.out == NULL) {
        hap.out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
        if (hap.out == NULL) {
            ALOGE("%s: hap.out is NULL", __func__);
            rc = -ENOMEM;
            goto exit;
        }
    }

    /* Populate haptic out stream,  usecase and add to usecase
       list to select speaker always.*/
    hap.out->flags = 0;
    //Speaker is only supported device for haptic stream
    hap.out->devices = AUDIO_DEVICE_OUT_SPEAKER;
    hap.out->dev = adev;
    hap.out->format = AUDIO_FORMAT_PCM_16_BIT;
    hap.out->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    hap.out->channel_mask = AUDIO_CHANNEL_OUT_MONO;
    hap.out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_MONO;
    hap.out->config = pcm_config_haptic;


    usecase = calloc(1, sizeof(struct audio_usecase));
    if (usecase == NULL) {
        ALOGE("%s: usecase is NULL", __func__);
        rc = -ENOMEM;
        // Free Out Stream
        free(hap.out);
        hap.out = NULL;
        goto exit;
    }
    usecase->stream.out = hap.out;
    usecase->type = PCM_PLAYBACK;
    usecase->id = USECASE_AUDIO_PLAYBACK_HAPTIC;
    usecase->out_snd_device = SND_DEVICE_NONE;
    usecase->in_snd_device = SND_DEVICE_NONE;
    list_add_tail(&adev->usecase_list, &usecase->list);
    select_devices(adev, USECASE_AUDIO_PLAYBACK_HAPTIC);

    ALOGD("%s :Opening PCM device %d for haptic playback",
                                     __func__, hap_pcm_dev_id);
    hap.pcm = pcm_open(adev->snd_card, hap_pcm_dev_id,
                      flags, &pcm_config_haptic);

    if (hap.pcm == NULL || !pcm_is_ready(hap.pcm)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hap.pcm));
        if (hap.pcm != NULL) {
            pcm_close(hap.pcm);
            hap.pcm = NULL;
        }
        goto exit;
    }
start_write:
    send_cmd_l(REQUEST_WRITE);
exit:
    //cleanup
    pthread_mutex_unlock(&hap.lock);
    ALOGV("%s Exit ", __func__);
}

void audio_extn_haptic_start (struct audio_device *adev) {
    ALOGV("%s: Enter", __func__);
    haptic_start(adev);
    ALOGV("%s: Exit", __func__);
}

/* must be called with adev lock held */
static int haptic_cleanup()
{
    struct audio_device * adev = (struct audio_device *)hap.userdata;
    struct audio_usecase *uc_info = NULL;

    ALOGV("%s: Enter", __func__);

    if (hap.out != NULL) {
        free(hap.out);
        hap.out = NULL;
    }

    uc_info = get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_HAPTIC);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find haptic usecase in the list", __func__);
    } else {
        disable_audio_route(adev, uc_info);
        disable_snd_device(adev, uc_info->out_snd_device);
        list_remove(&uc_info->list);
        free(uc_info);
    }
    if (hap.pcm != NULL) {
        ALOGD("%s: Closing Haptic PCM device", __func__);
        pcm_close(hap.pcm);
    }
    hap.done = false;
    hap.pcm = NULL;
    ALOGV("%s: Exit ", __func__);
    return 0;
}

void audio_extn_haptic_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms)
{
    int ret, duration;
    char value[32]={0};
    bool hap_start = false;

    //ALOGV("%s: enter", __func__);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FM_VOLUME,
                                           value, sizeof(value));
    if (ret >= 0) {
        if (sscanf(value, "%f", &fm_vol) != 1){
            ALOGE("%s: error in retrieving fm volume", __func__);
        }
        ALOGV("%s: fm_volume = %f", __func__, fm_vol);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value, sizeof(value));
    if (ret >= 0) {
        int val = atoi(value);
        if ((val & AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
            (val & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
            whs_connected = true;
            ALOGV("%s: *********** Wired HEADSET CONNECTED ************", __func__);
        }
    }
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value, sizeof(value));
    if (ret >= 0) {
        int val = atoi(value);
        if ((val & AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
            (val & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
            whs_connected = false;
            ALOGV("%s: ********* Wired HEADSET DISCONNECT***********", __func__);
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VIBRATE_ENABLED,
                value, sizeof(value));
    if (ret >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_VIBRATE_ENABLED);
        ALOGD("%s: key <%s> = %s", __func__, AUDIO_PARAMETER_KEY_VIBRATE_ENABLED,
                                                                         value);
        if (!strncmp("true", value, sizeof("true"))) {
            hap_start = true;
        } else {
            hap_start = false;
            ALOGD("%s Stop Haptic Audio Playback", __func__);
            audio_extn_haptic_stop(adev);
            audio_extn_haptic_enable_route(adev);
            return;
        }
    }
    // Parse duration value
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_VIBRATE_DURATION,
                                                             &duration);
    if (ret >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_VIBRATE_DURATION);
        hap.duration = duration;
        ALOGD("%s: key <%s> = %d", __func__, AUDIO_PARAMETER_KEY_VIBRATE_DURATION,
                                                                 hap.duration);
        if (hap.duration <= 0) {
            ALOGE("%s: Invalid haptic duration %d, default to %d", __func__,
                    hap.duration, DEFAULT_HAPTIC_DURATION_MS);
            hap.duration = DEFAULT_HAPTIC_DURATION_MS;
        }
    }

    if (hap_start) {
        ALOGD("%s Start Haptic Audio Playback", __func__);
        audio_extn_haptic_start(adev);
        if (get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_HAPTIC) != NULL) {
            audio_extn_haptic_disable_route(adev);
        }
    }
}

bool audio_extn_is_haptic_started(struct audio_device *adev, int usecase_id,
                                  int out_device, bool start)
{
    bool is_hap_started = false;
    struct listnode *node = NULL;
    struct audio_usecase *usecase = NULL;

    if (get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_HAPTIC) != NULL &&
        (hap.state == STATE_ACTIVE || hap.state == STATE_IDLE) &&
        hap.pcm != NULL && (out_device & AUDIO_DEVICE_OUT_SPEAKER) &&
        (out_device > AUDIO_DEVICE_OUT_SPEAKER)) {
        is_hap_started = true;
    }
    if (!start && tone_playback_active && ((usecase_id == USECASE_AUDIO_PLAYBACK_LOW_LATENCY) ||
        (usecase_id == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER)) &&
        (out_device & AUDIO_DEVICE_OUT_SPEAKER)) {
        tone_playback_active = false;
        ALOGV("Tone playback stopped ", __func__);
    }
    if (start && ((usecase_id == USECASE_AUDIO_PLAYBACK_LOW_LATENCY) ||
        (usecase_id == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER)) &&
        (out_device & AUDIO_DEVICE_OUT_SPEAKER) &&
        (out_device > AUDIO_DEVICE_OUT_SPEAKER)) {
        ALOGV("Tone playback started ", __func__);
        tone_playback_active = true;
        if (is_hap_started) {
            list_for_each(node, &adev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                if (usecase->id == usecase_id)
                    enable_audio_route(adev, usecase);
            }
        }
    }
    return is_hap_started;
}


void audio_extn_haptic_enable_route(struct audio_device *adev)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    struct str_parms *parms = NULL;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->id != USECASE_AUDIO_PLAYBACK_HAPTIC &&
            usecase->stream.out->devices & AUDIO_DEVICE_OUT_SPEAKER) {
            ALOGV("%s usecase %d active on device %d,ignore ", __func__,
                                       usecase->id, usecase->stream.out->devices);
            return;
        }
    }
    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (whs_connected &&
            ((usecase->stream.out->devices & AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
            (usecase->stream.out->devices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE))) {
            disable_snd_device(adev, SND_DEVICE_OUT_SPEAKER);
            ALOGV("%s Routing usecase id: %d to out device %d", __func__,  usecase->id,
                                        usecase->stream.out->devices);
            select_devices(adev, usecase->id);

            switch (usecase->id) {
                case USECASE_AUDIO_PLAYBACK_OFFLOAD:
                    //resume media
                    ALOGV("%s RESUME USECASE_AUDIO_PLAYBACK_OFFLOAD",__func__);
                    if (usecase->stream.out->stream.resume)
                        usecase->stream.out->stream.resume(&(usecase->stream.out->stream));
                    break;
                case USECASE_AUDIO_PLAYBACK_FM:
                    /* FM use-case, volume has to be set*/
                    if (fm_vol > 0.0) {
                        parms = str_parms_create();
                        if (parms != NULL) {
                            str_parms_add_float(parms, AUDIO_PARAMETER_KEY_FM_VOLUME, fm_vol);
                            audio_extn_fm_set_parameters(adev, parms);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

void audio_extn_haptic_disable_route(struct audio_device *adev)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    struct str_parms *parms = NULL;
    int num_usecases = 0;
    bool ignore_disable = false;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        num_usecases+=1;
        if (usecase->id != USECASE_AUDIO_PLAYBACK_HAPTIC &&
            usecase->stream.out->devices & AUDIO_DEVICE_OUT_SPEAKER) {
            //ALOGD("%s Usecase %d active on device %d, so ignore",__func__,
            //                     usecase->id, usecase->stream.out->devices);
            ignore_disable = true;
        }
    }
    ALOGV("%s Num usecases = %d, ignore_disable = %d",__func__,
                                         num_usecases, ignore_disable);
    if (ignore_disable)
       return;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (whs_connected && (usecase->type == PCM_PLAYBACK) &&
            usecase->id != USECASE_AUDIO_PLAYBACK_HAPTIC &&
            !(usecase->stream.out->devices & AUDIO_DEVICE_OUT_SPEAKER) &&
            ((usecase->stream.out->devices & AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
            (usecase->stream.out->devices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE))) {

            ALOGV("%s PCM Playback usecase %d active on device %d",__func__,
                                  usecase->id, usecase->stream.out->devices);
            switch (usecase->id) {
                case USECASE_AUDIO_PLAYBACK_OFFLOAD:
                    //pause media
                    ALOGV("%s Pause USECASE_AUDIO_PLAYBACK_OFFLOAD",__func__);
                    if (usecase->stream.out->stream.pause)
                        usecase->stream.out->stream.pause(&(usecase->stream.out->stream));
                    break;
                case USECASE_AUDIO_PLAYBACK_LOW_LATENCY:
                case USECASE_AUDIO_PLAYBACK_DEEP_BUFFER:
                    if (!tone_playback_active) {
                        disable_audio_route(adev, usecase);
                    }
                    else {
                        ALOGV("Tone playback active, not disabling",__func__);
                        tone_playback_active = false;
                    }
                    break;
                 case USECASE_AUDIO_PLAYBACK_FM:
                    /* FM use-case, set volume to 0*/
                    parms = str_parms_create();
                    if (parms != NULL) {
                        float vol = 0.0f;
                        str_parms_add_float(parms, AUDIO_PARAMETER_KEY_FM_VOLUME, vol);
                        audio_extn_fm_set_parameters(adev, parms);
                    }
                    break;
                default :
                    disable_audio_route(adev, usecase);
                    break;
            }
        }
    }
}
