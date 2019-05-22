/*
* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

/* Test app to capture event updates from kernel */
/*#define LOG_NDEBUG 0*/
#include <fcntl.h>
#include <linux/netlink.h>
#include <getopt.h>
#include <pthread.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <utils/Log.h>

#include <cutils/list.h>
#include "qahw_api.h"
#include "qahw_defs.h"

static struct dtmf_detect_pkt {
    uint16_t low_freq;
    uint16_t high_freq;
} dtmf_detect_pkt;

static struct dtmf_detect_event_data {
    uint32_t event_type;
    uint32_t payload_len;
    struct dtmf_detect_pkt payload;
} dtmf_detect_event_data;

qahw_module_handle_t *primary_hal_handle = NULL;
static bool is_dtmf_detect_enabled = 0;

static int qahw_voice_test_device_switch(qahw_stream_handle_t *out_handle) {
    char cmd;
    char routing_string[100];
    int result = 0;

    fprintf(stderr,"Select a device:\n");
    fprintf(stderr,"0 - None\n");
    fprintf(stderr,"1 - Earpiece\n");
    fprintf(stderr,"2 - Speaker\n");
    fprintf(stderr,"4 - Wired Headset\n");
    fprintf(stderr,"8 - Wired Headphone\n");

    scanf(" %c", &cmd);

    switch(cmd)
    {
        case '0':
        case '1':
        case '2':
        case '4':
        case '8':
            fprintf(stderr,"set device to %c\n", cmd);
            sprintf(routing_string, "routing=%c", cmd);
            result = qahw_out_set_parameters(out_handle, routing_string);
            fprintf(stderr,"set routing result %d\n", result);
            break;
        default:
            fprintf(stderr,"Unrecognized device value %c\n", cmd);
            break;
    }

    return result;
}

static int qahw_voice_test_set_mic_mute(void) {
    char cmd;
    int result = 0;
    bool is_mute_enabled;

    fprintf(stderr,"Select an option:\n");
    fprintf(stderr,"0 - Un-mute Microphone\n");
    fprintf(stderr,"1 - Mute Microphone\n");

    scanf(" %c", &cmd);

    switch(cmd)
    {
        case '0':
            fprintf(stderr,"set Mic mute to false\n");
            is_mute_enabled = false;
            break;
        case '1':
            fprintf(stderr,"set Mic mute to true\n");
            is_mute_enabled = true;
            break;
        default:
            fprintf(stderr,"Unrecognized mic value %c\n", cmd);
            goto mute_done;
    }

    result = qahw_set_mic_mute(primary_hal_handle, is_mute_enabled);
    fprintf(stderr,"set mute result %d\n", result);

mute_done:
    return result;
}

static int qahw_voice_test_set_volume(void) {
    float cmd;
    char mute_string[100];
    int result = 0;

    fprintf(stderr,"Select enter value between 0.0 and 1.0:\n");

    scanf(" %f", &cmd);

    if ((cmd < 0.0) || (cmd > 1.0)) {
        fprintf(stderr,"Invalid float value %.1f\n", cmd);
        goto volume_done;
    }

    result = qahw_set_voice_volume(primary_hal_handle, cmd);
    fprintf(stderr,"set volume to %.1f\n", cmd);

volume_done:
    return result;
}

static int qahw_voice_test_dtmf_hdlr(qahw_stream_callback_event_t event,
                                      void *param,
                                      void *cookie)
{
    struct dtmf_detect_event_data *event_data = (struct dtmf_detect_event_data *) param;

    if (event_data->payload_len != sizeof(struct dtmf_detect_pkt))
        fprintf(stderr,"Wrong packet size %d\n", event_data->payload_len);
    else
        fprintf(stderr,"Rx DTMF Tone low=%dHz high=%dHz Detected!\n",
            event_data->payload.low_freq, event_data->payload.high_freq);
}

static int qahw_voice_test_set_dtmf(qahw_stream_handle_t *out_handle) {
    int status = 0;
    int dtmf_gain = 0;
    int dtmf_detect = 0;
    char cmd;
    char dtmf_tone_string[100];
    bool is_dtmf_menu = 1;
    bool is_dtmf_gen_cmd = 0;

    while (is_dtmf_menu)
    {
        fprintf(stderr,"------ DTMF Menu Options ------\n");
        fprintf(stderr,"g - Set DTMF Gain\n");
        fprintf(stderr,"d - Set DTMF Detection\n");
        fprintf(stderr,"s - Stop DTMF Tone\n");
        fprintf(stderr,"x - Exit DTMF Menu\n");
        fprintf(stderr,"---- Available DTMF Tones ----\n");
        fprintf(stderr,"0\n");
        fprintf(stderr,"1\n");
        fprintf(stderr,"2\n");
        fprintf(stderr,"3\n");
        fprintf(stderr,"4\n");
        fprintf(stderr,"5\n");
        fprintf(stderr,"6\n");
        fprintf(stderr,"7\n");
        fprintf(stderr,"8\n");
        fprintf(stderr,"9\n");
        fprintf(stderr,"*\n");
        fprintf(stderr,"#\n");

        scanf(" %c", &cmd);
        switch(cmd)
        {
            case 'g':
                fprintf(stderr,"Set DTMF Gain to:\n");
                scanf(" %d", &dtmf_gain);
                sprintf(dtmf_tone_string, "dtmf_tone_gain=%d", dtmf_gain);
                is_dtmf_gen_cmd = 1;
                break;
            case 's':
                fprintf(stderr,"set tone to off\n");
                sprintf(dtmf_tone_string, "dtmf_tone_off");
                is_dtmf_gen_cmd = 1;
                break;
            case 'd':
                fprintf(stderr,"Set DTMF Detection to 0(OFF) or 1(ON)\n");
                scanf(" %d", &dtmf_detect);
                if (dtmf_detect)
                {
                    sprintf(dtmf_tone_string, "dtmf_detect=true");
                    fprintf(stderr,"Setting DTMF detection to TRUE\n");
                    status = qahw_out_set_parameters(out_handle, dtmf_tone_string);
                    fprintf(stderr,"set dtmf cmd result %d\n", status);
                    status = qahw_out_set_callback(out_handle, qahw_voice_test_dtmf_hdlr, NULL);
                    fprintf(stderr,"set dtmf detect callback result %d\n", status);
                    is_dtmf_detect_enabled = 1;
                }
                else
                {
                    sprintf(dtmf_tone_string, "dtmf_detect=false");
                    fprintf(stderr,"Setting DTMF detection to FALSE\n");
                    status = qahw_out_set_parameters(out_handle, dtmf_tone_string);
                    fprintf(stderr,"set dtmf cmd result %d\n", status);
                    status = qahw_out_set_callback(out_handle, NULL, NULL);
                    fprintf(stderr,"unset dtmf detect callback result %d\n", status);
                    is_dtmf_detect_enabled = 0;
                }
                break;
            case '0':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=941;dtmf_high_freq=1336");
                is_dtmf_gen_cmd = 1;
                break;
            case '1':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=697;dtmf_high_freq=1209");
                is_dtmf_gen_cmd = 1;
                break;
            case '2':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=697;dtmf_high_freq=1336");
                is_dtmf_gen_cmd = 1;
                break;
            case '3':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=697;dtmf_high_freq=1477");
                is_dtmf_gen_cmd = 1;
                break;
            case '4':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=770;dtmf_high_freq=1209");
                is_dtmf_gen_cmd = 1;
                break;
            case '5':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=770;dtmf_high_freq=1336");
                is_dtmf_gen_cmd = 1;
                break;
            case '6':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=770;dtmf_high_freq=1477");
                is_dtmf_gen_cmd = 1;
                break;
            case '7':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=852;dtmf_high_freq=1209");
                is_dtmf_gen_cmd = 1;
                break;
            case '8':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=852;dtmf_high_freq=1336");
                is_dtmf_gen_cmd = 1;
                break;
            case '9':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=852;dtmf_high_freq=1477");
                is_dtmf_gen_cmd = 1;
                break;
            case '*':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=941;dtmf_high_freq=1209");
                is_dtmf_gen_cmd = 1;
                break;
            case '#':
                fprintf(stderr,"set tone to %c\n", cmd);
                sprintf(dtmf_tone_string, "dtmf_low_freq=941;dtmf_high_freq=1477");
                is_dtmf_gen_cmd = 1;
                break;
            case 'x':
                is_dtmf_menu = 0;
                break;
            default:
                fprintf(stderr,"Unrecognized command %c\n", cmd);
                break;
        }

        if (is_dtmf_gen_cmd) {
            status = qahw_out_set_parameters(out_handle, dtmf_tone_string);
            fprintf(stderr,"set dtmf cmd result %d\n", status);
            is_dtmf_gen_cmd = 0;
        }
    }

    return status;
}

int main(int argc, char *argv[]) {

    char cmd;
    int status = 0;
    int session_id = 297816064;
    audio_io_handle_t handle = 0x999;
    audio_config_t config;
    qahw_stream_handle_t *out_handle;
    const char* stream_name = "voice_stream";
    char session_id_string[100];
    char dtmf_tone_string[100];
    const char* routing = "routing=1";

    primary_hal_handle = qahw_load_module(QAHW_MODULE_ID_PRIMARY);
    if (primary_hal_handle == NULL) {
        fprintf(stderr,"failure in Loading primary HAL\n");
        return -1;
    }

    fprintf(stderr,"Loaded primary HAL\n");

    status = qahw_open_output_stream(primary_hal_handle,
                                     handle,
                                     AUDIO_DEVICE_OUT_WIRED_HEADSET,
                                     AUDIO_OUTPUT_FLAG_PRIMARY,
                                     &config,
                                     &out_handle,
                                     stream_name);
    fprintf(stderr,"opened output stream status %d\n", status);

    //Set call state to CALL_ACTIVE
    sprintf(session_id_string, "vsid=%d;call_state=2", session_id);
    status = qahw_set_parameters(primary_hal_handle, session_id_string);
    fprintf(stderr,"set call session to CALL_ACTIVE status %d\n", status);

    status = qahw_set_mode(primary_hal_handle, AUDIO_MODE_IN_CALL);
    fprintf(stderr,"set mode status %d\n", status);

    qahw_out_set_parameters(out_handle, routing);
    fprintf(stderr,"set routing status %d\n", status);

    while (1)
    {
        fprintf(stderr,"------ Voice Call Menu Options ------\n");
        fprintf(stderr,"d - Device Switch Menu\n");
        fprintf(stderr,"m - Mute Menu\n");
        fprintf(stderr,"v - Volume Menu\n");
        fprintf(stderr,"t - DTMF Tone Menu\n");
        fprintf(stderr,"x - Exit test application\n");


        scanf(" %c", &cmd);
        switch(cmd)
        {
        case 'd':
            status = qahw_voice_test_device_switch(out_handle);
            break;
        case 'm':
            status = qahw_voice_test_set_mic_mute();
            break;
        case 'v':
            status = qahw_voice_test_set_volume();
            break;
        case 't':
            status = qahw_voice_test_set_dtmf(out_handle);
            break;
        case 'x':
            goto exit;
        default:
            fprintf(stderr,"Unrecognized command %c\n", cmd);
            break;
        }
    }

exit:
    //disable dtmf detection if enabled
    if (is_dtmf_detect_enabled)
    {
        sprintf(dtmf_tone_string, "dtmf_detect=false");
        fprintf(stderr,"Setting DTMF detection to FALSE\n");
        status = qahw_out_set_parameters(out_handle, dtmf_tone_string);
        fprintf(stderr,"set dtmf cmd result %d\n", status);
        status = qahw_out_set_callback(out_handle, NULL, NULL);
        fprintf(stderr,"unset dtmf detect callback result %d\n", status);
        is_dtmf_detect_enabled = 0;
    }

    status = qahw_close_output_stream(out_handle);
    fprintf(stderr,"closed output stream status %d\n", status);

    //Set call state to CALL_INACTIVE
    sprintf(session_id_string, "vsid=%d;call_state=1", session_id);
    status = qahw_set_parameters(primary_hal_handle, session_id_string);
    fprintf(stderr,"set call session to CALL_INACTIVE status %d\n", status);

    status = qahw_unload_module(primary_hal_handle);
    fprintf(stderr,"unload HAL module status %d\n", status);
    return 0;
}

