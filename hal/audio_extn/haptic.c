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
#include "audio_hw.h"
#include "audio_extn.h"
#include "platform_api.h"
#include <platform.h>
#include<math.h>


#define AUDIO_PARAMETER_KEY_VIBRATE_OPEN "vibrate_open"

#define PI 3.14159265358979323846

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
    REQUEST_STOP,
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
} haptic_priv_t;

haptic_priv_t hap;


static void send_cmd_l(request_t r)
{
    if (hap.state == STATE_DEINIT || hap.state == STATE_DISABLED)
        return;

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

static void gen_pcm(short buffer[], int length){

    short i;
    float frequency = 170.0f;
    float sampling_ratio = 48000.0f;
    float amplitude = 0.5f;

    for (i = 0; i < length; i++)
    {
        float theta = ((float)i / sampling_ratio) * PI;
        buffer[i] = (short)(sin(theta * frequency) * 32767.0f * amplitude);
    }

}

static void * haptic_thread_loop(void *param __unused)
{
    struct hap_cmd *cmd = NULL;
    struct listnode *item;
    ALOGE("haptic_thread_loop entry");
    while (true) {
        ALOGE("Ramjee line no = %d", __LINE__);
        pthread_mutex_lock(&hap.lock);
        ALOGE("Ramjee line no = %d", __LINE__);
        if (list_empty(&hap.cmd_list)) {
            ALOGE("Ramjee line no = %d", __LINE__);
            pthread_cond_wait(&hap.cond, &hap.lock);
            pthread_mutex_unlock(&hap.lock);
            continue;
        }
        item = list_head(&hap.cmd_list);
        cmd = node_to_item(item, struct hap_cmd, node);
        list_remove(item);
        ALOGE("Ramjee line no = %d", __LINE__);
        if (cmd->req == REQUEST_QUIT) {
            free(cmd);
            pthread_mutex_unlock(&hap.lock);
            break;
        } 
        ALOGE("Ramjee line no = %d", __LINE__);

        free(cmd);
        hap.state = STATE_ACTIVE;
        ALOGV("State changed to Active");
        ALOGE("Ramjee line no = %d", __LINE__);
        pthread_cond_signal(&hap.cond);
        pthread_mutex_unlock(&hap.lock);
        ALOGE("Ramjee line no = %d", __LINE__);
//TODO start pcm_write for X ms lock and unlock mutex
        short durationSec = 10;
        short size = 10 * 48000; 
        short buffer[size];
        int cnt = 0, frame_size = 480;
        gen_pcm(buffer, size);
        ALOGE("Ramjee line no = %d", __LINE__);
        while (cnt < size) {
	        pcm_write(hap.pcm, (void *)buffer, frame_size);
                cnt += frame_size;
        } 
        ALOGE("Ramjee line no = %d", __LINE__);
        pthread_mutex_lock(&hap.lock);
        hap.state = STATE_IDLE;
        pthread_cond_signal(&hap.cond);
        pthread_mutex_unlock(&hap.lock);
   }
    ALOGE("haptic_thread_loop exit");
    return 0;
}

void audio_extn_haptic_init(struct audio_device *adev)
{
    ALOGE("audio_extn_haptic_init entry");
    if (property_get_bool("vendor.audio.haptic_audio", false) == false) {
        ALOGE("Haptic is disabled");
        hap.state = STATE_DISABLED;
        return;
    }
    pthread_mutex_init(&hap.lock, (const pthread_mutexattr_t *) NULL);
    pthread_cond_init(&hap.cond, (const pthread_condattr_t *) NULL);
    if (pthread_create(&hap.thread_id,  (const pthread_attr_t *) NULL,
                       haptic_thread_loop, NULL) < 0) {
        ALOGW("Failed to create haptic_thread_loop");
        hap.state = STATE_DEINIT; 
    }
    hap.state = STATE_IDLE;
    list_init(&hap.cmd_list);
    ALOGE("audio_extn_haptic_init exit");
}

void audio_extn_haptic_deinit()
{
    ALOGE("audio_extn_haptic_deinit entry");
    if (hap.state == STATE_DISABLED || hap.state == STATE_DEINIT)
        return;
    pthread_mutex_lock(&hap.lock);
    send_cmd_l(REQUEST_QUIT);
    pthread_mutex_unlock(&hap.lock);
    pthread_join(hap.thread_id, (void **) NULL);
    pthread_mutex_destroy(&hap.lock);
    pthread_cond_destroy(&hap.cond);
    ALOGE("audio_extn_haptic_deinit entry");
}


void haptic_stop(struct audio_device *adev)
{
    ALOGE("haptic_stop entry");
    if ( hap.state == STATE_DISABLED || hap.state == STATE_DEINIT )
        return;
    pthread_mutex_lock(&hap.lock);
    send_cmd_l(REQUEST_STOP);
exit:
    pthread_mutex_unlock(&hap.lock);
    ALOGE("haptic_stop entry");
}

void audio_extn_haptic_stop (struct audio_device *adev) {
    ALOGE("audio_extn_haptic_stop entry");
    haptic_stop(adev);
    ALOGE("audio_extn_haptic_stop exit");
}

void haptic_start(struct audio_device *adev)
{
    struct pcm *pcm;
    struct audio_usecase *usecase;
    int rc = 0;
    unsigned int flags = PCM_OUT|PCM_MONOTONIC;

    int hap_pcm_dev_id =
            platform_get_pcm_device_id(USECASE_AUDIO_PLAYBACK_HAPTIC,
                                       PCM_PLAYBACK);
    ALOGE("haptic_start entry");

    pthread_mutex_lock(&hap.lock);
    if (hap.state == STATE_ACTIVE) {
        goto exit;
    }

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


    hap.out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
    if (hap.out == NULL) {
        ALOGE("%s: keep_alive out is NULL", __func__);
        rc = -ENOMEM;
        goto exit;
    }

/* Populate haptic out stream,  usecase and add to usecase list to select speaker always.*/
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
        goto exit;
    }
    usecase->stream.out = hap.out;
    usecase->type = PCM_PLAYBACK;
    usecase->id = USECASE_AUDIO_PLAYBACK_HAPTIC;
    usecase->out_snd_device = SND_DEVICE_NONE;
    usecase->in_snd_device = SND_DEVICE_NONE;
    list_add_tail(&adev->usecase_list, &usecase->list);

    send_cmd_l(REQUEST_WRITE);
/* Wait for thread to start pcm write of haptic data */
    while (hap.state != STATE_ACTIVE) {
        pthread_cond_wait(&hap.cond, &hap.lock);
    }
exit:
    pthread_mutex_unlock(&hap.lock);
    ALOGE("haptic_start exit");
}

void audio_extn_haptic_start (struct audio_device *adev) {
    haptic_start(adev);
}
void audio_extn_haptic_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms)
{
    int ret, val;
    char value[32]={0};

    ALOGV("%s: enter", __func__);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VIBRATE_OPEN,
                               value, sizeof(value));
    if (ret >= 0) {
        if (!strncmp("true", value, sizeof("true"))) {
            ALOGD("Start Haptic Audio");
            haptic_start(adev);
        } else {
            ALOGD("Stop Haptic Audio");
            haptic_stop(adev);
        }
    }
}

