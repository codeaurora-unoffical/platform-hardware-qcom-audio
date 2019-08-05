/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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
#define LOG_NDEBUG 0
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
#include <math.h>

#include <cutils/list.h>
#include "qahw_api.h"
#include "qahw_defs.h"

static int sock_event_fd = -1;

pthread_t data_event_th = -1;
pthread_attr_t data_event_attr;

qahw_module_handle_t *primary_hal_handle = NULL;

FILE * log_file = NULL;
volatile bool stop_loopback = false;
volatile bool exit_process_thread = false;
const char *log_filename = NULL;
uint32_t play_duration_in_seconds = 600,play_duration_elapsed_msec = 0,play_duration_in_msec = 0;
uint32_t source_device = AUDIO_DEVICE_IN_WIRED_HEADSET;
uint32_t sink_device = AUDIO_DEVICE_OUT_WIRED_HEADSET;
uint32_t dtmf_source_device = 0;
static float loopback_gain = 1.0;

#define AFE_SOURCE_PORT_ID 0
#define DTMF_SOURCE_PORT_ID 1

#define DEVICE_SOURCE 0
#define DEVICE_SINK 1

#define MAX_MODULE_NAME_LENGTH  100

/* Function declarations */
void usage();

typedef enum source_port_type {
    SOURCE_PORT_NONE,
    SOURCE_PORT_MIC
} source_port_type_t;

typedef enum source_port_state {
    SOURCE_PORT_INACTIVE=0,
    SOURCE_PORT_ACTIVE,
} source_port_state_t;

typedef struct source_port_config {
    source_port_type_t source_port_type;
    source_port_state_t source_port_state;
} source_port_config_t;

typedef struct afe_loopback_config {
    qahw_module_handle_t *hal_handle;
    audio_devices_t devices;
    struct audio_port_config source_config;
    struct audio_port_config sink_config;
    audio_patch_handle_t patch_handle;
    source_port_config_t source_port_config;
} afe_loopback_config_t;

afe_loopback_config_t g_afe_loopback_config;

void break_signal_handler(int signal __attribute__((unused)))
{
   stop_loopback = true;
   fprintf(log_file,"\nEvent thread loop exiting prematurely");
}

int poll_data_event_init()
{
    struct sockaddr_nl sock_addr;
    int sz = (64*1024);
    int soc;

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.nl_family = AF_NETLINK;
    sock_addr.nl_pid = getpid();
    sock_addr.nl_groups = 0xffffffff;

    soc = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (soc < 0) {
        return 0;
    }

    setsockopt(soc, SOL_SOCKET, SO_RCVBUFFORCE, &sz, sizeof(sz));

    if (bind(soc, (struct sockaddr*) &sock_addr, sizeof(sock_addr)) < 0) {
        close(soc);
        return 0;
    }

    sock_event_fd = soc;

    return (soc > 0);
}

void init_afe_loopback_config(afe_loopback_config_t **p_afe_loopback_config)
{
    fprintf(log_file,"\nInitializing global afe loopback config");
    g_afe_loopback_config.hal_handle = NULL;

    audio_devices_t out_device = sink_device; // Get output device mask from connected device
    audio_devices_t in_device = source_device;

    g_afe_loopback_config.devices = (sink_device | source_device);
    fprintf(log_file,"\n sink_device (%08x) source_device ((%08x) device_mask (%08x)", sink_device, source_device, g_afe_loopback_config.devices);

    /* Patch source port config init */
    g_afe_loopback_config.source_config.id = AFE_SOURCE_PORT_ID;
    g_afe_loopback_config.source_config.role = AUDIO_PORT_ROLE_SOURCE;
    g_afe_loopback_config.source_config.type = AUDIO_PORT_TYPE_DEVICE;
    g_afe_loopback_config.source_config.config_mask =
                        (AUDIO_PORT_CONFIG_ALL ^ AUDIO_PORT_CONFIG_GAIN);
    g_afe_loopback_config.source_config.sample_rate = 48000;
    g_afe_loopback_config.source_config.channel_mask =
                        AUDIO_CHANNEL_IN_STEREO; // Using OUT as this is digital data and not mic capture
    g_afe_loopback_config.source_config.format = AUDIO_FORMAT_PCM_16_BIT;
    /*TODO: add gain */
    g_afe_loopback_config.source_config.ext.device.hw_module =
                        AUDIO_MODULE_HANDLE_NONE;
    g_afe_loopback_config.source_config.ext.device.type = source_device;

    /* Patch sink port config init */
    g_afe_loopback_config.sink_config.id = AFE_SOURCE_PORT_ID;
    g_afe_loopback_config.sink_config.role = AUDIO_PORT_ROLE_SINK;
    g_afe_loopback_config.sink_config.type = AUDIO_PORT_TYPE_DEVICE;
    g_afe_loopback_config.sink_config.config_mask =
                            (AUDIO_PORT_CONFIG_ALL ^ AUDIO_PORT_CONFIG_GAIN);
    g_afe_loopback_config.sink_config.sample_rate = 48000;
    g_afe_loopback_config.sink_config.channel_mask =
                             AUDIO_CHANNEL_OUT_STEREO;
    g_afe_loopback_config.sink_config.format = AUDIO_FORMAT_PCM_16_BIT;

    g_afe_loopback_config.sink_config.ext.device.hw_module =
                            AUDIO_MODULE_HANDLE_NONE;
    g_afe_loopback_config.sink_config.ext.device.type = sink_device;

    /* Init patch handle */
    g_afe_loopback_config.patch_handle = AUDIO_PATCH_HANDLE_NONE;

    memset(&g_afe_loopback_config.source_port_config,0,sizeof(source_port_config_t));
    g_afe_loopback_config.source_port_config.source_port_type = SOURCE_PORT_MIC;
    g_afe_loopback_config.source_port_config.source_port_state = SOURCE_PORT_INACTIVE;

    poll_data_event_init();
    *p_afe_loopback_config = &g_afe_loopback_config;

    fprintf(log_file,"\nDone Initializing global afe loopback config");
}

void deinit_afe_loopback_config()
{
    g_afe_loopback_config.hal_handle = NULL;

    g_afe_loopback_config.devices = AUDIO_DEVICE_NONE;
}

void read_data_from_fd(const char* path, int *value)
{
    int fd = -1;
    char buf[16];
    int ret;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        ALOGE("Unable open fd for file %s", path);
        return;
    }

    ret = read(fd, buf, 15);
    if (ret < 0) {
        ALOGE("File %s Data is empty", path);
        close(fd);
        return;
    }

    buf[ret] = '\0';
    *value = atoi(buf);
    close(fd);
}

int actual_channels_from_audio_infoframe(int infoframe_channels)
{
    if (infoframe_channels > 0 && infoframe_channels < 8) {
      /* refer CEA-861-D Table 17 Audio InfoFrame Data Byte 1 */
        return (infoframe_channels+1);
    }
    fprintf(log_file,"\nInfoframe channels 0, need to get from stream, returning default 2");
    return 2;
}


void stop_afe_loopback(
            afe_loopback_config_t *afe_loopback_config)
{
    if(afe_loopback_config->patch_handle != AUDIO_PATCH_HANDLE_NONE)
        fprintf(log_file,"\nStopping current loopback session");
        qahw_release_audio_patch(afe_loopback_config->hal_handle,
                                 afe_loopback_config->patch_handle);
    afe_loopback_config->patch_handle = AUDIO_PATCH_HANDLE_NONE;
}

int create_run_afe_loopback(
            afe_loopback_config_t *afe_loopback_config)
{
    int rc=0;
    qahw_module_handle_t *module_handle = afe_loopback_config->hal_handle;

    struct audio_port_config *source_configs[] = {&afe_loopback_config->source_config};
    struct audio_port_config *sink_configs[] = {&afe_loopback_config->sink_config};

    fprintf(log_file,"\nCreating audio patch");
    if (afe_loopback_config->patch_handle != AUDIO_PATCH_HANDLE_NONE) {
        fprintf(log_file,"\nPatch already existing, release the patch before opening a new patch");
        return rc;
    }
    if (dtmf_source_device) {
        g_afe_loopback_config.source_config.id = DTMF_SOURCE_PORT_ID;
    }

    rc = qahw_create_audio_patch(module_handle,
                        1,
                        &afe_loopback_config->source_config,
                        1,
                        &afe_loopback_config->sink_config,
                        &afe_loopback_config->patch_handle);
    fprintf(log_file,"\nCreate patch returned %d",rc);
    if(!rc) {
        if (dtmf_source_device) {
            struct audio_port_config sink_gain_config;
            /* Convert loopback gain to millibels */
            int loopback_gain_in_millibels = 2000 * log10(loopback_gain);
            sink_gain_config.gain.index = 0;
            sink_gain_config.gain.mode = AUDIO_GAIN_MODE_JOINT;
            sink_gain_config.gain.channel_mask = 4;
            sink_gain_config.gain.values[0] = 697;
            sink_gain_config.gain.values[1] = 1209;
            sink_gain_config.gain.values[2] = 0xFFFF;
            sink_gain_config.gain.values[3] = loopback_gain_in_millibels;
            sink_gain_config.id = afe_loopback_config->sink_config.id;
            sink_gain_config.role = afe_loopback_config->sink_config.role;
            sink_gain_config.type = afe_loopback_config->sink_config.type;
            sink_gain_config.config_mask = AUDIO_PORT_CONFIG_GAIN;

            (void)qahw_set_audio_port_config(afe_loopback_config->hal_handle,
                    &sink_gain_config);
        }
    }

    return rc;
}

static qahw_module_handle_t *load_hal(audio_devices_t dev)
{
    if (primary_hal_handle == NULL) {
        primary_hal_handle = qahw_load_module(QAHW_MODULE_ID_PRIMARY);
        if (primary_hal_handle == NULL) {
            fprintf(stderr,"failure in Loading primary HAL");
            goto exit;
        }
    }

exit:
    return primary_hal_handle;
}

/*
* this function unloads all the loaded hal modules so this should be called
* after all the stream playback are concluded.
*/
static int unload_hals(void) {
    if (primary_hal_handle) {
        qahw_unload_module(primary_hal_handle);
        primary_hal_handle = NULL;
    }
    return 1;
}


void source_data_event_handler(afe_loopback_config_t *afe_loopback_config)
{
    int status =0;
    source_port_type_t source_port_type = afe_loopback_config->source_port_config.source_port_type;

    fprintf(log_file,"\nSource port state : %d", afe_loopback_config->source_port_config.source_port_state);

    if(afe_loopback_config->source_port_config.source_port_state == SOURCE_PORT_INACTIVE) {
        fprintf(log_file,"\nCreate and start afe loopback session begin");
        status = create_run_afe_loopback(afe_loopback_config);
        if(status)
        {
            fprintf(log_file,"\nCreate audio patch failed with status %d",status);
            stop_afe_loopback(afe_loopback_config);
        } else
            afe_loopback_config->source_port_config.source_port_state = SOURCE_PORT_ACTIVE;
    } else if(afe_loopback_config->source_port_config.source_port_state == SOURCE_PORT_ACTIVE) {
        stop_afe_loopback(afe_loopback_config);
        afe_loopback_config->source_port_config.source_port_state = SOURCE_PORT_INACTIVE;
    }
}

void process_loopback_data(void *ptr)
{
    char buffer[64*1024];
    struct pollfd fds;
    int i, count, status;
    int j;
    char *dev_path = NULL;
    char *switch_state = NULL;
    char *switch_name = NULL;
    afe_loopback_config_t *afe_loopback_config = &g_afe_loopback_config;

    fprintf(log_file,"\nEvent thread loop");
    source_data_event_handler(afe_loopback_config);
    while(!stop_loopback) {
        usleep(100*1000);
        play_duration_elapsed_msec += 100;
        if(play_duration_in_msec <= play_duration_elapsed_msec)
        {
            stop_loopback = true;
            fprintf(log_file,"\nElapsed set playback duration %d seconds, exiting test",play_duration_in_msec/1000);
        }
    }

    source_data_event_handler(afe_loopback_config);

    fprintf(log_file,"\nStop afe loopback done");

    exit_process_thread = true;
}

void set_device(uint32_t device_type, uint32_t device_id)
{
    afe_loopback_config_t *afe_loopback_config = &g_afe_loopback_config;
    switch( device_type )
    {
        case DEVICE_SINK:
            afe_loopback_config->sink_config.ext.device.type = device_id;
        break;
        case DEVICE_SOURCE:
            afe_loopback_config->source_config.ext.device.type = device_id;
        break;
    }
}

int main(int argc, char *argv[]) {

    int status = 0;
    source_port_type_t source_port_type = SOURCE_PORT_NONE;
    log_file = stdout;
    afe_loopback_config_t    *afe_loopback_config = NULL;
    afe_loopback_config_t *temp = NULL;

    struct option long_options[] = {
        /* These options set a flag. */
        {"sink-device", required_argument,    0, 's'},
        {"dtmf-source-device", required_argument,    0, 'd'},
        {"play-duration",  required_argument,    0, 'p'},
        {"play-volume",  required_argument,    0, 'v'},
        {"help",          no_argument,          0, 'h'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;

    while ((opt = getopt_long(argc,
                              argv,
                              "-s:d:p:v:h",
                              long_options,
                              &option_index)) != -1) {

        fprintf(log_file, "for argument %c, value is %s", opt, optarg);

        switch (opt) {
        case 's':
            sink_device = atoi(optarg);

            if (sink_device == AUDIO_DEVICE_OUT_SPEAKER)
                source_device = AUDIO_DEVICE_IN_BACK_MIC;
            else if (sink_device == AUDIO_DEVICE_OUT_WIRED_HEADSET)
                source_device = AUDIO_DEVICE_IN_WIRED_HEADSET;
            else if (sink_device == AUDIO_DEVICE_OUT_EARPIECE)
                source_device = AUDIO_DEVICE_IN_BUILTIN_MIC;
            else return -EINVAL;

            /* Set devices */
            set_device(DEVICE_SINK, sink_device);
            set_device(DEVICE_SOURCE, source_device);
            break;
        case 'd':
            dtmf_source_device = atoi(optarg);
            break;
        case 'p':
            play_duration_in_seconds = atoi(optarg);
            break;
        case 'v':
            loopback_gain = atof(optarg);
            break;
        case 'h':
        default :
            usage();
            return 0;
            break;
        }
    }

    fprintf(log_file,"\nAFE loopback test begin");
    if (play_duration_in_seconds < 0 | play_duration_in_seconds > 360000) {
            fprintf(log_file,
                    "\nPlayback duration %d invalid or unsupported(range : 1 to 360000, defaulting to 600 seconds )",
                    play_duration_in_seconds);
            play_duration_in_seconds = 600;
    }
    play_duration_in_msec = play_duration_in_seconds * 1000;

    /* Register the SIGINT to close the App properly */
    if (signal(SIGINT, break_signal_handler) == SIG_ERR) {
        fprintf(log_file, "Failed to register SIGINT:%d",errno);
        fprintf(stderr, "Failed to register SIGINT:%d",errno);
    }

    /* Initialize global afe loopback struct */
    init_afe_loopback_config(&temp);
    afe_loopback_config = &g_afe_loopback_config;

    /* Load HAL */
    fprintf(log_file,"\nLoading HAL for loopback usecase begin");
    primary_hal_handle = load_hal(afe_loopback_config->devices);
    if (primary_hal_handle == NULL) {
        fprintf(log_file,"\n Failure in Loading HAL, exiting");
        /* Set the exit_process_thread flag for exiting test */
        exit_process_thread = true;
        goto exit_afe_loopback_test;
    }
    afe_loopback_config->hal_handle = primary_hal_handle;
    fprintf(log_file,"\nLoading HAL for loopback usecase done");

    pthread_attr_init(&data_event_attr);
    fprintf(log_file,"\nData thread init done");
    pthread_attr_setdetachstate(&data_event_attr, PTHREAD_CREATE_JOINABLE);
    fprintf(log_file,"\nData thread setdetachstate done");

    fprintf(log_file,"\nData thread create");
    pthread_create(&data_event_th, &data_event_attr,
                       (void *) process_loopback_data, NULL);
    fprintf(log_file,"\nMain thread loop exit");

exit_afe_loopback_test:
    /* Wait for process thread to exit */
    while (!exit_process_thread) {
        usleep(10*1000);
    }
    fprintf(log_file,"\nJoining loopback thread");
    status = pthread_join(data_event_th, NULL);
    fprintf(log_file, "\n thread join completed, status:%d ", status);
    exit_process_thread = false;

    fprintf(log_file,"\nUnLoading HAL for loopback usecase begin");
    unload_hals();
    fprintf(log_file,"\nUnLoading HAL for loopback usecase end");

    deinit_afe_loopback_config();
    afe_loopback_config = NULL;

    fprintf(log_file,"\nTranscode loopback test end");
    return 0;
}

void usage()
{
    fprintf(log_file,"\nUsage : afe_loopback_test\n"
                           " -s <sink_device_id>\n"
                           " -d <dtmf as source(range 0 and 1)>\n"
                           " -p <duration_in_seconds>\n"
                           " -v <volume>\n"
                           " -h <help>\n");
    fprintf(log_file,"\nExample to play for 1 minute on speaker device with volume unity: afe_loopback_test -p 60 -s 2");
    fprintf(log_file,"\nExample to play for 5 minutes on headphone device: afe_loopback_test -p 300 -s 4 ");
    fprintf(log_file,"\nExample to play dtmf tone for 5 minutes on headphone device: afe_loopback_test -p 300 -s 4 -d 1");
    fprintf(log_file,"\nHelp : afe_loopback_test -h");
 }
