
/* amrnbtest.c - native AMRNB test application
 *
 * Based on native pcm test application platform/system/extras/sound/playwav.c
 *
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010, 2020 The Linux Foundation. All rights reserved.
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
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <errno.h>
#include <linux/msm_audio_amrnb.h>
#include <ion/ion.h>
#include "audiotest_def.h"
#include "ion_alloc.h"


typedef struct {
   char* inputfilename;
   char* outputfilename;
   int channelmode;
}stream_config;

int ionfd;
#define EOS 0x00000001
static int in_size =0;
static int out_size =0;
static int file_write=1;
static int eos_ack=0;
FILE * log_file = NULL;

#define MAX_PLAYBACK_STREAMS   105
stream_config stream_param[MAX_PLAYBACK_STREAMS];

#define AMRNBTEST_IBUFSZ (32*1024)
#define AMRNBTEST_NUM_IBUF 2
#define AMRNBTEST_IPMEM_SZ (AMRNBTEST_IBUFSZ * AMRNBTEST_NUM_IBUF)

#define AMRNBTEST_OBUFSZ (32*1024)
#define AMRNBTEST_NUM_OBUF 2
#define AMRNBTEST_OPMEM_SZ (AMRNBTEST_OBUFSZ * AMRNBTEST_NUM_OBUF)


 struct msm_audio_aio_buf aio_ip_buf[AMRNBTEST_NUM_IBUF];
 struct msm_audio_aio_buf aio_op_buf[AMRNBTEST_NUM_OBUF];

 static pthread_mutex_t avail_lock;
 static pthread_cond_t avail_cond;
 static pthread_mutex_t consumed_lock;
 static pthread_cond_t consumed_cond;
 static int data_is_available = 0;
 static int data_is_consumed = 0;
 static int in_free_indx;
 static int in_data_indx;
 static int out_free_indx;
 static int out_data_indx;


/* http://ccrma.stanford.edu/courses/422/projects/WaveFormat/ */
struct wav_header {		/* Simple wave header */
	char Chunk_ID[4];	/* Store "RIFF" */
	unsigned int Chunk_size;
	char Riff_type[4];	/* Store "WAVE" */
	char Chunk_ID1[4];	/* Store "fmt " */
	unsigned int Chunk_fmt_size;
	unsigned short Compression_code;	/*1 - 65,535,  1 - pcm */
	unsigned short Number_Channels;	/* 1 - 65,535 */
	unsigned int Sample_rate;	/*  1 - 0xFFFFFFFF */
	unsigned int Bytes_Sec;	/*1 - 0xFFFFFFFF */
	unsigned short Block_align;	/* 1 - 65,535 */
	unsigned short Significant_Bits_sample;	/* 1 - 65,535 */
	char Chunk_ID2[4];	/* Store "data" */
	unsigned int Chunk_data_size;
} __attribute__ ((packed));



static struct wav_header append_header = {
	{'R', 'I', 'F', 'F'}, 0, {'W', 'A', 'V', 'E'},
	{'f', 'm', 't', ' '}, 16, 1, 1, 8000, 16000, 2,
	16, {'d', 'a', 't', 'a'}, 0
};

typedef struct TIMESTAMP{
	unsigned long LowPart;
	unsigned long HighPart;
} __attribute__ ((packed)) TIMESTAMP;

struct meta_in_q6{
	unsigned char rsv[18];
	unsigned short offset;
	TIMESTAMP ntimestamp;
	unsigned int nflags;
} __attribute__ ((packed));

struct meta_out_dsp{
	unsigned int offset_to_frame;
	unsigned int frame_size;
	unsigned int encoded_pcm_samples;
	unsigned int msw_ts;
	unsigned int lsw_ts;
	unsigned int nflags;
} __attribute__ ((packed));

struct dec_meta_out{
	unsigned int rsv[7];
	unsigned int num_of_frames;
	struct meta_out_dsp meta_out_dsp[];
} __attribute__ ((packed));

struct meta_in{
	unsigned short offset;
	long long timestamp;
	unsigned int nflags;
} __attribute__ ((packed));
struct meta_out{
	unsigned short offset;
	long long timestamp;
	unsigned int nflags;
	unsigned short errflag;
	unsigned short sample_frequency;
	unsigned short channel;
	unsigned int tick_count;
} __attribute__ ((packed));

#define AUDIOTEST_MAX_TH_CXT 10 /* Maximum number of thread context */
struct audiotest_thread_context thread_context[AUDIOTEST_MAX_TH_CXT];
pthread_mutex_t audiotest_mutex = PTHREAD_MUTEX_INITIALIZER;

static void wait_for_data(void)
{
	pthread_mutex_lock(&avail_lock);

	while (data_is_available == 0) {
		pthread_cond_wait(&avail_cond, &avail_lock);
	}
	data_is_available = 0;
	pthread_mutex_unlock(&avail_lock);
}

static void data_available(void)
{
	pthread_mutex_lock(&avail_lock);
	if (data_is_available == 0) {
		data_is_available = 1;
		pthread_cond_broadcast(&avail_cond);
	}
	pthread_mutex_unlock(&avail_lock);
}

static void wait_for_data_consumed(void)
{
	pthread_mutex_lock(&consumed_lock);

	while (data_is_consumed == 0) {
		pthread_cond_wait(&consumed_cond, &consumed_lock);
	}
	data_is_consumed = 0;
	pthread_mutex_unlock(&consumed_lock);
}

static void data_consumed(void )
{
	pthread_mutex_lock(&consumed_lock);
	if (data_is_consumed == 0) {
		data_is_consumed = 1;
		pthread_cond_broadcast(&consumed_cond);
	}
	pthread_mutex_unlock(&consumed_lock);
}





static void *event_notify(void *arg)
{
	long ret_drv;
	struct audio_pvt_data *audio_data = (struct audio_pvt_data *) arg;
	int afd = audio_data->afd;
	struct msm_audio_event suspend_event;
	do {
		printf("event_notify thread started\n");
		suspend_event.timeout_ms = 0;
		ret_drv = ioctl(afd, AUDIO_GET_EVENT, &suspend_event);
		if (ret_drv < 0) {
			printf("event_notify thread exiting: \
				Got Abort event or timedout\n");
			break;
		} else {
			if (suspend_event.event_type == AUDIO_EVENT_SUSPEND) {
				printf("event_notify: RECEIVED EVENT FROM \
					DRIVER OF TYPE: AUDIO_EVENT_SUSPEND: \
					%d\n", suspend_event.event_type);
				audio_data->suspend = 1;
				sleep(1);
			} else if
			(suspend_event.event_type == AUDIO_EVENT_RESUME) {
				printf("event_notify: RECEIVED EVENT FROM \
					DRIVER OF TYPE: AUDIO_EVENT_RESUME : \
					%d\n", suspend_event.event_type);
				audio_data->suspend = 0;
			}
		}
	} while (1);
	return NULL;
}


static void create_wav_header(int Datasize)
{
	append_header.Chunk_size = Datasize + 8 + 16 + 12;
	append_header.Chunk_data_size = Datasize;
}

static int fill_buffer(void *buf, unsigned sz, void *cookie)
{
	struct meta_in_q6 meta;
	struct audio_pvt_data *audio_data = (struct audio_pvt_data *) cookie;
	unsigned cpy_size = (sz < audio_data->avail?sz:audio_data->avail);

	memset(meta.rsv, 0, sizeof(meta.rsv));

	#ifdef DEBUG_LOCAL
	char *temp;
	printf("%s:frame count %d\n", __func__, audio_data->frame_count);
	#endif
	if (audio_data->mode) {
			meta.ntimestamp.HighPart = 0;
			meta.ntimestamp.LowPart = (unsigned long long)(audio_data->frame_count * 0x10000);
		meta.offset = sizeof(struct meta_in_q6);
			audio_data->frame_count++;
	#ifdef DEBUG_LOCAL
                 printf("Meta In High part is %lu\n",
                                 meta.ntimestamp.HighPart);
                 printf("Meta In Low part is %lu\n",
                                 meta.ntimestamp.LowPart);
                 printf("Meta In ntimestamp: %llu\n", (((unsigned long long)
                                         meta.ntimestamp.HighPart << 32) +
                                         meta.ntimestamp.LowPart));
                 printf("meta in size %d\n", sizeof(struct meta_in_q6));
	#endif
	if (audio_data->avail == 0) {
			/* End of file, send EOS */
			meta.nflags = EOS;
			memcpy(buf, &meta, sizeof(struct meta_in_q6));
			return (sizeof(struct meta_in_q6));
	}
			meta.nflags = 0;
		memcpy(buf, &meta, sizeof(struct meta_in_q6));
			memcpy(((char *)buf + sizeof(struct meta_in_q6)), audio_data->next, cpy_size);
		#ifdef DEBUG_LOCAL
		temp = ((char*)buf + sizeof(struct meta_in_q6));
		printf("\nFirst three bytes 0x%2x:0x%2x:0x%2x\n", *temp, *(temp+1), *(temp+2));
		#endif
	} else {
			if (audio_data->avail == 0) {
					return 0;
			}
			audio_data->frame_count++;
			memcpy((char *)buf, audio_data->next, cpy_size);
		#ifdef DEBUG_LOCAL
		temp = (buf);
		printf("\nFirst three bytes 0x%2x:0x%2x:0x%2x\n", *temp, *(temp+1), *(temp+2));
		#endif
	}
		audio_data->next += cpy_size;
		audio_data->avail -= cpy_size;
	if (audio_data->mode)
		return cpy_size + sizeof(struct meta_in_q6);
	else
		return cpy_size;
}



struct audiotest_thread_context* get_free_context(void) {
	unsigned char i;
	struct audiotest_thread_context *ret_val = NULL;

	pthread_mutex_lock(&audiotest_mutex);
	for (i=0; i < AUDIOTEST_MAX_TH_CXT; i++) {
		if (thread_context[i].used == false) {
			thread_context[i].used = true;
			ret_val = &thread_context[i];
			break;
		}
	}
	pthread_mutex_unlock(&audiotest_mutex);

	if (i == AUDIOTEST_MAX_TH_CXT) {
		fprintf(stderr, "%s : no free context struct\n", __FUNCTION__);
	}

	return ret_val;
}

void free_context(struct audiotest_thread_context *context) {
	unsigned char i;

	pthread_mutex_lock(&audiotest_mutex);
	for (i=0; i < AUDIOTEST_MAX_TH_CXT; i++) {
		if (&thread_context[i] == context) {
			thread_context[i].used = false;
			thread_context[i].cxt_id = AUDIOTEST_DEFAULT_ID;
			thread_context[i].config.private_data = NULL;
			break;
		}
	}
	pthread_mutex_unlock(&audiotest_mutex);
}

static void *amrnb_read_thread(void *arg)
{
	struct audio_pvt_data *audio_data = (struct audio_pvt_data *) arg;
	int afd = audio_data->afd;
	int total_len;
	int fd = 0;
	struct dec_meta_out *meta_out_ptr;
	struct meta_out_dsp *meta_out_dsp;
	struct msm_audio_aio_buf aio_buf;
	struct msm_audio_config config;
#ifdef AUDIOV2
	unsigned short dec_id;
#endif
	unsigned int first_frame_offset, idx;
	unsigned int total_frame_size;

	total_len = 0;
	if(file_write) {
		// Log PCM samples to a file
		fd = open(audio_data->outfile, O_RDWR | O_CREAT,
		  S_IRWXU | S_IRWXG | S_IRWXO);
		if (fd < 0) {
			perror("Cannot open audio sink device");
			return ((void*)-1);
		}
		lseek(fd, 44, SEEK_SET);  /* Set Space for Wave Header */
	} else {
		// Log PCM samples to pcm out driver
		fd = open(audio_data->outfile, O_WRONLY);
		if (fd < 0) {
			perror("Cannot open audio sink device");
			return ((void*)-1);
		}
#ifdef AUDIOV2
		if (ioctl(fd, AUDIO_GET_SESSION_ID, &dec_id)) {
			perror("could not get pcm decoder session id\n");
			goto err_state;
		}
		printf("pcm decoder session id %d\n", dec_id);
#if defined(TARGET_USES_QCOM_MM_AUDIO)
		if (devmgr_register_session(dec_id, DIR_RX) < 0) {
			perror("could not route pcm decoder stream\n");
			goto err_state;
		}
#endif
#endif
		if (ioctl(fd, AUDIO_GET_CONFIG, &config)) {
			perror("could not get pcm config");
			goto err_state;
		}
		config.channel_count = audio_data->channels;
		config.sample_rate = audio_data->freq;
		if (ioctl(fd, AUDIO_SET_CONFIG, &config)) {
			perror("could not set pcm config");
			goto err_state;
		}
		if (ioctl(fd, AUDIO_START, 0) < 0) {
			perror("could not start pcm playback node");
			goto err_state;
		}
	}
	while(1) {
		// Send free Read buffer
		aio_buf.buf_addr = aio_op_buf[out_free_indx].buf_addr;
		aio_buf.buf_len =  aio_op_buf[out_free_indx].buf_len;
		aio_buf.data_len = 0; // Driver will notify actual size
		aio_buf.private_data =  aio_op_buf[out_free_indx].private_data;
		wait_for_data();
#ifdef DEBUG_LOCAL
		printf("%s:free_idx %d, data_idx %d\n", __func__, out_free_indx, out_data_indx);
#endif
		out_free_indx = out_data_indx;
		printf("%s:ASYNC_READ addr %p len %d\n", __func__, aio_buf.buf_addr, aio_buf.buf_len);
		if (ioctl(afd, AUDIO_ASYNC_READ, &aio_buf) < 0) {
			printf("error on async read\n");
			break;
		}
		meta_out_ptr = (struct dec_meta_out *)aio_op_buf[out_free_indx].buf_addr;
		meta_out_dsp = (struct meta_out_dsp *)(((char *)meta_out_ptr + sizeof(struct dec_meta_out)));
		printf("nr of frames %d\n", meta_out_ptr->num_of_frames);
#ifdef DEBUG_LOCAL
		printf("%s:msw ts 0x%8x, lsw_ts 0x%8x, nflags 0x%8x\n", __func__,
			meta_out_dsp->msw_ts,
			meta_out_dsp->lsw_ts,
			meta_out_dsp->nflags);
#endif
		first_frame_offset = meta_out_dsp->offset_to_frame + sizeof(struct dec_meta_out);
		total_frame_size = 0;
		if(meta_out_ptr->num_of_frames != 0xFFFFFFFF) {
			// Go over all meta data field to find exact frame size
			for(idx=0; idx < meta_out_ptr->num_of_frames; idx++) {
				total_frame_size +=  meta_out_dsp->frame_size;
				meta_out_dsp++;
			}
			printf("total size %d\n", total_frame_size);
		} else {
			//OutPut EOS reached
			if (meta_out_dsp->nflags == EOS) {
				printf("%s:Received EOS at output port 0x%8x\n", __func__,
				meta_out_dsp->nflags);
				break;
			}
		}
		printf("%s: Read Size %d offset %d\n", __func__,
			total_frame_size, first_frame_offset);
		write(fd, ((char *)aio_op_buf[out_free_indx].buf_addr + first_frame_offset),
								total_frame_size);
		total_len +=  total_frame_size;
	}
	if(file_write) {
		append_header.Sample_rate = audio_data->freq;
		append_header.Number_Channels = audio_data->channels;
		append_header.Bytes_Sec = append_header.Sample_rate *
			append_header.Number_Channels * 2;
		append_header.Block_align = append_header.Number_Channels * 2;
		create_wav_header(total_len);
		lseek(fd, 0, SEEK_SET);
		write(fd, (char *)&append_header, 44);
	} else {
		sleep(1); // All buffers drained
#if defined(TARGET_USES_QCOM_MM_AUDIO) && defined(AUDIOV2)
		if (devmgr_unregister_session(dec_id, DIR_RX) < 0) {
			perror("could not deroute pcm decoder stream\n");
		}
#endif
	}
err_state:
	close(fd);
	printf("%s:exit\n", __func__);
	pthread_exit(NULL);
	return NULL;
}

static void *amrnb_write_thread(void *arg)
{
	struct msm_audio_aio_buf aio_buf;
	struct audio_pvt_data *audio_data = (struct audio_pvt_data *) arg;
	int afd = audio_data->afd, sz;
	struct meta_in_q6 *meta_in_ptr;
	int eos=0;

	while(1) {
		if(!eos) {
			// Copy write buffer
	 		aio_buf.buf_addr = aio_ip_buf[in_free_indx].buf_addr;
			aio_buf.buf_len =  aio_ip_buf[in_free_indx].buf_len;
			aio_buf.private_data =  aio_ip_buf[in_free_indx].private_data;
			sz = fill_buffer(aio_buf.buf_addr, in_size, audio_data);
			if (sz == sizeof(struct meta_in_q6)) { //NT mode EOS
				printf("%s:Done reading file\n", __func__);
				printf("%s:Send EOS on I/N Put\n", __func__);
				aio_buf.data_len = sz;
				aio_ip_buf[in_free_indx].data_len = sz;
				eos = 1;
			} else if (sz == 0){ // Tunnel mode EOS
				eos = 1;
				break;
			} else {
				aio_buf.data_len = sz;
				aio_ip_buf[in_free_indx].data_len = sz;
			}
			printf("%s:ASYNC_WRITE addr %p len %d\n", __func__, aio_buf.buf_addr,aio_buf.data_len);
			ioctl(afd, AUDIO_ASYNC_WRITE, &aio_buf);
		}
		wait_for_data_consumed();
#ifdef DEBUG_LOCAL
		printf("%s:free_idx %d, data_idx %d\n", __func__, in_free_indx, in_data_indx);
#endif
		in_free_indx = in_data_indx;
		meta_in_ptr = (struct meta_in_q6 *)aio_ip_buf[in_data_indx].buf_addr;
		//Input EOS reached
		if (meta_in_ptr->nflags == EOS) {
			printf("%s:Received EOS buffer back at i/p 0x%8x\n", __func__, meta_in_ptr->nflags);
			break;
		}
	}
	if(!audio_data->mode && eos) {
		printf("%s:Wait for data to drain out\n", __func__);
		fsync(afd);
		eos_ack = 1;
		sleep(1);
		ioctl(afd, AUDIO_ABORT_GET_EVENT, 0);
	}
	printf("%s:exit\n", __func__);
	// Free memory done as part of initiate play
	pthread_exit(NULL);
	return NULL;
}

static void *amrnb_dec_event(void *arg)
{
        struct audio_pvt_data *audio_data = (struct audio_pvt_data *) arg;
	int afd = audio_data->afd, rc;
	struct msm_audio_event event;
	int eof = 0;
	struct dec_meta_out *meta_out_ptr;
	struct meta_out_dsp *meta_out_dsp;
	struct meta_in_q6 *meta_in_ptr;
	pthread_t evt_read_thread = 0;
	pthread_t evt_write_thread = 0;

	eos_ack = 0;
	if (audio_data->mode) // Non Tunnel mode
		pthread_create(&evt_read_thread, NULL, amrnb_read_thread, (void *) audio_data);
	pthread_create(&evt_write_thread, NULL, amrnb_write_thread, (void *) audio_data);
	// Till EOF not reached in NT or till eos not reached in tunnel
	while((!eof && audio_data->mode) || (!eos_ack && !audio_data->mode)) {
		// Wait till timeout
		event.timeout_ms = 0;
		rc = ioctl(afd, AUDIO_GET_EVENT, &event);
		if (rc < 0) {
	  		printf("%s: errno #%d", __func__, errno);
	  		continue;
		}
#ifdef DEBUG_LOCAL
		printf("%s:AUDIO_GET_EVENT event %d \n", __func__, event.event_type);
#endif
		switch(event.event_type) {
			case AUDIO_EVENT_READ_DONE:
				if(event.event_payload.aio_buf.buf_len == 0)
					printf("Warning buf_len Zero\n");
				if (event.event_payload.aio_buf.data_len >= sizeof(struct dec_meta_out)) {
		  			printf("%s: READ_DONE: addr %p len %d\n", __func__,
						event.event_payload.aio_buf.buf_addr,
						event.event_payload.aio_buf.data_len);
					meta_out_ptr = (struct dec_meta_out *)event.event_payload.aio_buf.buf_addr;
					out_data_indx =(int) event.event_payload.aio_buf.private_data;
					meta_out_dsp = (struct meta_out_dsp *)(((char *)meta_out_ptr + sizeof(struct dec_meta_out)));
					//OutPut EOS reached
					if (meta_out_dsp->nflags == EOS) {
			  			eof = 1;
						printf("%s:Received EOS event at output 0x%8x\n", __func__,
						meta_out_dsp->nflags);
					}
					data_available();
				} else {
					printf("%s:AUDIO_EVENT_READ_DONE:unexpected length\n", __func__);
				}
		 		break;
			case AUDIO_EVENT_WRITE_DONE:
				if (event.event_payload.aio_buf.data_len >= sizeof(struct meta_in_q6)) {
					printf("%s:WRITE_DONE: addr %p len %d\n", __func__,
						event.event_payload.aio_buf.buf_addr,
						event.event_payload.aio_buf.data_len);
					meta_in_ptr = (struct meta_in_q6 *)event.event_payload.aio_buf.buf_addr;
					in_data_indx =(int) event.event_payload.aio_buf.private_data;
					//Input EOS reached
					if (meta_in_ptr->nflags == EOS) {
						printf("%s:Received EOS at input 0x%8x\n", __func__, meta_in_ptr->nflags);
					}
					data_consumed();
				} else {
					printf("%s:AUDIO_EVENT_WRITE_DONE:unexpected length\n", __func__);
				}
				break;
			default:
				printf("%s: -Unknown event- %d\n", __func__, event.event_type);
				break;
		}
	}
	if(audio_data->mode)
		pthread_join(evt_read_thread, NULL);
	else
		pthread_join(evt_write_thread, NULL);
	printf("%s:exit\n", __func__);
	pthread_exit(NULL);
	return NULL;
}

static int do_amr_play(struct audtest_config *clnt_config)  //initiate_play
{
    struct audio_pvt_data *audio_data = (struct audio_pvt_data *) clnt_config->private_data;
	unsigned n = 0;
	pthread_t evt_thread;
	int sz;
	int rc = -1;
	audio_data->mode = 1;
#ifdef AUDIOV2
	int dec_id;
#endif
	int afd;

	struct msm_audio_aio_buf aio_buf;
	struct msm_audio_buf_cfg buf_cfg;
	struct msm_audio_config config;
	unsigned int open_flags;

	struct mmap_info *in_ion_buf[AMRNBTEST_NUM_IBUF];
    struct mmap_info *out_ion_buf[AMRNBTEST_NUM_OBUF];

        audio_data->freq = 48000;
        audio_data->channels = clnt_config->channel_mode;
        audio_data->bitspersample = 16;

	if(((in_size + sizeof(struct meta_in_q6)) > AMRNBTEST_IBUFSZ) ||
		(out_size > AMRNBTEST_OBUFSZ)) {
			perror("configured input / output size more"\
			"than pmem allocation");
			return -1;
	}

	ionfd = ion_open();

	if(ionfd < 0)
	{
		perror("ion device open failed \n");
		return -1;
	}

	if (audio_data->mode)
		open_flags = O_RDWR | O_NONBLOCK;
	else
		open_flags = O_WRONLY | O_NONBLOCK;
	afd = open("/dev/msm_amrnb", open_flags);

	if (afd < 0) {
		perror("Cannot open AMRNB device");
		return -1;
	}

	audio_data->afd = afd; /* Store */

	if (audio_data->mode) {
		/* PCM config */
		if (ioctl(afd, AUDIO_GET_CONFIG, &config)) {
			perror("could not get config");
			goto err_state1;
		}
		config.sample_rate = audio_data->freq;
		config.channel_count = audio_data->channels;
		config.bits = audio_data->bitspersample;

		if (ioctl(afd, AUDIO_SET_CONFIG, &config)) {
			perror("could not set config");
			goto err_state1;
		}
		printf("pcm config sample_rate=%d channels=%d bitspersample=%d \n",
			config.sample_rate, config.channel_count, config.bits);
	} else {
#ifdef AUDIOV2
		if (ioctl(afd, AUDIO_GET_SESSION_ID, &dec_id)) {
			perror("could not get decoder session id\n");
			goto err_state1;
		}
#if defined(TARGET_USES_QCOM_MM_AUDIO)
		if (devmgr_register_session(dec_id, DIR_RX) < 0) {
			goto err_state1;
		}
#endif
#endif
	}
	audio_data->frame_count	= 0;
	if(ioctl(afd, AUDIO_GET_BUF_CFG, &buf_cfg)) {
		printf("Error getting AUDIO_GET_BUF_CONFIG\n");
		goto err_state2;
	}
	printf("Default meta_info_enable = 0x%8x\n", buf_cfg.meta_info_enable);
	printf("Default frames_per_buf = 0x%8x\n", buf_cfg.frames_per_buf);
	if (audio_data->mode) {
		// NT mode support meta info
		buf_cfg.meta_info_enable = 1;
		if(ioctl(afd, AUDIO_SET_BUF_CFG, &buf_cfg)) {
			printf("Error setting AUDIO_SET_BUF_CONFIG\n");
			goto err_state2;
		}
	}
	pthread_cond_init(&avail_cond, 0);
	pthread_mutex_init(&avail_lock, 0);
	pthread_cond_init(&consumed_cond, 0);
	pthread_mutex_init(&consumed_lock, 0);
	data_is_available = 0;
	data_is_consumed = 0;
	in_free_indx=0;
	out_free_indx=0;
	if ((ioctl(afd, AUDIO_START, 0))< 0 ) {
		printf("amrnbtest: unable to start driver\n");
		goto err_state2;
	}
	if (audio_data->mode) {
		/* non - tunnel portion */
		printf("selected non-tunnel part\n");
		// Register read buffers
		for (n = 0; n < AMRNBTEST_NUM_OBUF; n++) {
			out_ion_buf[n] = alloc_ion_buffer(ionfd, AMRNBTEST_OBUFSZ);
			if (!out_ion_buf[n]) {
				printf("\n alloc_ion_buffer: out_ion_buf[n] allocation failed\n");
			    goto err_state2;
            }

			rc = audio_register_ion(afd, out_ion_buf[n]);
            if (-1 == rc) {
                printf("\n audio_register_ion: out_ion_buf[n] failed\n");
				free_ion_buffer(ionfd, &out_ion_buf[n]);
                                 goto err_state2;
            }
			// Read buffers local structure
		 	aio_op_buf[n].buf_addr = out_ion_buf[n]->pBuffer;
			aio_op_buf[n].buf_len = out_size + sizeof(struct dec_meta_out);
			aio_op_buf[n].data_len = 0; // Driver will notify actual size
			aio_op_buf[n].private_data = (void *)n; //Index
		}
		// Send n-1 Read buffer
		for (n = 0; n < (AMRNBTEST_NUM_OBUF-1); n++) {
		 	aio_buf.buf_addr = aio_op_buf[n].buf_addr;
			aio_buf.buf_len = aio_op_buf[n].buf_len;
			aio_buf.data_len = aio_op_buf[n].data_len;
			aio_buf.private_data = aio_op_buf[n].private_data;
			aio_buf.mfield_sz = sizeof(struct dec_meta_out);
			printf("ASYNC_READ addr %p len %d\n", aio_buf.buf_addr,
				aio_buf.buf_len);
			if (ioctl(afd, AUDIO_ASYNC_READ, &aio_buf) < 0) {
				printf("error on async read\n");
				goto err_state2;
			}
		}
		//Indicate available free buffer as (n-1)
		out_free_indx = AMRNBTEST_NUM_OBUF-1;
	}
	//Register Write  buffer
	for (n = 0; n < AMRNBTEST_NUM_IBUF; n++) {
		in_ion_buf[n] = alloc_ion_buffer(ionfd, AMRNBTEST_OBUFSZ);
		if (!out_ion_buf[n]) {
			printf("\n alloc_ion_buffer: out_ion_buf[n] allocation failed\n");
		    goto err_state2;
		}

		rc = audio_register_ion(afd, in_ion_buf[n]);
		if (-1 == rc) {
		     printf("\n audio_register_ion: out_ion_buf[n] failed\n");
			 free_ion_buffer(ionfd, &in_ion_buf[n]);
		     goto err_state2;
        }
		// Write buffers local structure
		aio_ip_buf[n].buf_addr = in_ion_buf[n]->pBuffer;
		aio_ip_buf[n].buf_len = AMRNBTEST_IBUFSZ;
		aio_ip_buf[n].data_len = 0; // Driver will notify actual size
		aio_ip_buf[n].private_data = (void *)n; //Index
	}
	// Send n-1 write buffer
	for (n = 0; n < (AMRNBTEST_NUM_IBUF-1); n++) {
		aio_buf.buf_addr = aio_ip_buf[n].buf_addr;
		aio_buf.buf_len = aio_ip_buf[n].buf_len;
		if ((sz = fill_buffer(aio_buf.buf_addr, in_size, audio_data)) < 0)
			goto err_state2;
		aio_buf.data_len = sz;
		aio_ip_buf[n].data_len = sz;
		aio_buf.private_data = aio_ip_buf[n].private_data;
		printf("ASYNC_WRITE addr %p len %d\n", aio_buf.buf_addr,
			aio_buf.data_len);
		rc = ioctl(afd, AUDIO_ASYNC_WRITE, &aio_buf);
		if(rc < 0) {
			printf( "error on async write=%d\n",rc);
			goto err_state2;
		}
	}
	//Indicate available free buffer as (n-1)
	in_free_indx = AMRNBTEST_NUM_IBUF-1;
	pthread_create(&evt_thread, NULL, amrnb_dec_event, (void *) audio_data);
	pthread_join(evt_thread, NULL);
	printf("AUDIO_STOP as event thread completed\n");
done:
	rc = 0;
	ioctl(afd, AUDIO_STOP, 0);
err_state2:
	if (audio_data->mode) {
		for (n = 0; n < AMRNBTEST_NUM_OBUF; n++) {
			free_ion_buffer(ionfd, &out_ion_buf[n]);
		}
	}
	for (n = 0; n < AMRNBTEST_NUM_IBUF; n++) {
		free_ion_buffer(ionfd, &in_ion_buf[n]);
	}
	if (!audio_data->mode) {
#if defined(TARGET_USES_QCOM_MM_AUDIO) && defined(AUDIOV2)
		if (devmgr_unregister_session(dec_id, DIR_RX) < 0)
			printf("error closing stream\n");
#endif
	}
err_state1:
	close(afd);
	return rc;
}


static int play_amr_file(struct audtest_config *config, int fd, size_t count)
{
	struct audio_pvt_data *audio_data = (struct audio_pvt_data *) config->private_data;
	int ret_val = 0;
	char *content_buf;

	audio_data->next = (char*)malloc(count);

	printf(" play_file: count=%d\n", count);

	if (!audio_data->next) {
		fprintf(stderr,"could not allocate %d bytes\n", count);
		return -1;
	}

	audio_data->org_next = audio_data->next;
	content_buf = audio_data->org_next;

	if (read(fd, audio_data->next, count) != (ssize_t)count) {
		fprintf(stderr,"could not read %d bytes\n", count);
		free(content_buf);
		return -1;
	}

	audio_data->avail = count;
	audio_data->org_avail = audio_data->avail;

	ret_val = do_amr_play(config);
	free(content_buf);
	return ret_val;
}

int amrnb_play(struct audtest_config *config)
{
	struct stat stat_buf;
	int fd;

	fprintf(stderr, "Inside amrnb_play\n");

	if (config == NULL) {
		return -1;
	}

	fprintf(stderr, "amrnb_play\n");

	fd = open(config->file_name, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "playamr: cannot open '%s'\n", config->file_name);
		return -1;
	}

	(void)fstat(fd, &stat_buf);

	return play_amr_file(config, fd, stat_buf.st_size);
}


void* playamrnb_thread(void* arg) {

	fprintf(stderr,"Inside playamrnb_thread\n");
	struct audiotest_thread_context *context =
		(struct audiotest_thread_context*) arg;
	int ret_val;

	fprintf(stderr,"start of playamrnb_thread\n");

	ret_val = amrnb_play(&context->config);
	free_context(context);
	pthread_exit((void*) ret_val);

    return NULL;
}

int amrnbplay_read_params(void* filedata) {

	struct audiotest_thread_context *context;
	struct audio_pvt_data *audio_data;
	audio_data = (struct audio_pvt_data *) malloc(sizeof(struct audio_pvt_data));
	stream_config *params = (stream_config*) filedata;

	int ret_val = 0;
	if ((context = get_free_context()) == NULL) {
		ret_val = -1;
		return ret_val;
	}
	fprintf(stderr, "start of amrnbplay_read_params \n");

	if(!audio_data) {
		ret_val = -1;
		return ret_val;
	}

	context->config.file_name = params->inputfilename;
	audio_data->outfile = params->outputfilename;
	out_size = 8192;
	in_size = 320;

	fprintf(stderr, "create amrnbthread of amrnbplay_read_params \n");


	context->type = AUDIOTEST_TEST_MOD_AMRNB_DEC;
	context->config.private_data = (struct audio_pvt_data *) audio_data;
	context->config.channel_mode = params->channelmode;
	fprintf(stderr, "2nd create amrnbthread of amrnbplay_read_params \n");

	amrnb_play(&context->config);
	return ret_val;
}

int main(int argc, char* argv[])
{
	int i = 0;
    int opt = 0;
    int option_index = 0;
    log_file = stdout;

    struct option long_options[] = {
        /* These options set a flag. */
        {"input-file",     required_argument,    0, 'i'},
        {"output-file", 	required_argument,    0, 'o'},
        {"channels", 	required_argument,    0, 'c'},
	};

    while ((opt = getopt_long(argc,
                             argv,
                             "i:o:c:",
                             long_options,
                             &option_index)) != -1) {

       fprintf(log_file, "for argument %c, value is %s\n", opt, optarg);

       switch (opt) {
       case 'i':
           stream_param[i].inputfilename = optarg;
           break;
       case 'o':
           stream_param[i].outputfilename = optarg;
           break;
		case 'c':
			stream_param[i].channelmode = (int)strtol(optarg, NULL, 0);
			break;
	   }
	}

	amrnbplay_read_params((void *)&stream_param[i]);


return 0;
}

const char *amrnbplay_help_txt =
"Play AMR file: type \n\
echo \"playamrnb path_of_file -id=xxx timeout=x -volume=x\" > /data/audio_test \n\
timeout = x (value in seconds) \n\
Volume = 0 (default, volume=100), 1 (test different volumes continuously) \n\
Bits per sample = 16 bits \n ";

void amrnbplay_help_menu(void) {
	printf("%s\n", amrnbplay_help_txt);
}

const char *amrnbrec_help_txt =
"Record amrnb file: type \n\
echo \"recamrnb path_of_file -id=xxx -timeout=x\" > /data/audio_test \n\
timeout = x (value in seconds) \n ";

void amrnbrec_help_menu(void) {
	printf("%s\n", amrnbrec_help_txt);
}
