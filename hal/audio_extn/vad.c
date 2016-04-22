/* vad.c
Copyright (c) 2016, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

#define LOG_TAG "audio_hw_vad"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <pthread.h>
#include <inttypes.h>
#include <dlfcn.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include <cutils/misc.h>
#include "audio_extn.h"
#include "vad_param_key.h"

#ifdef VAD_ENABLED
#define VAD_PATH "/system/vendor/lib/libvad.so"

#define AUDIO_PARAMETER_KEY_VAD_DUMP_ENABLED "VAD_dump_enabled"
#define AUDIO_PARAMETER_KEY_VAD_PCM_DUMP_ENABLED "VAD_PCM_dump_enabled"

typedef enum vad_bit_type_t {
  VAD_BIT_TYPE_16BIT = 0,
  VAD_BIT_TYPE_24BIT = 1,
  VAD_BIT_TYPE_MAX
} vad_bit_type_t;

typedef enum vad_feature_mode_t {
  VAD_FEATURE_MODE_OFF = 0,
  VAD_FEATURE_MODE_ON = 1,
  VAD_FEATURE_MODE_MAX
} vad_feature_mode_t;

typedef struct vad_cfg_t {
  uint32_t  feature_mode;
  uint32_t  sampling_rate;
  uint32_t  in_data_type;
  uint16_t  num_look_ahead_blks;
  uint16_t  look_ahead_blk_size;
  uint16_t  num_channels;
} vad_cfg_t;

typedef struct vad_size_t {
  uint32_t  mem_size;
  uint32_t  scratch_mem_size;
} vad_size_t;

typedef struct vad_params_t {
  uint32_t  vad_feature_mode;                  // Feature mode
  uint16_t  vad_op_mode;                       // VAD op mode
  uint16_t  vad_threshold;                     // Threshold that is based on SNR estimator to detect presence of speech activation
  uint16_t  vad_input_gain;                    // Input Gain (Unity = 2048)
  uint16_t  vad_lookahead_nblks;               // Number of block to determine noise tracking window size
  uint16_t  vad_lookahead_blksize;             // Number of bytes per block to determine noise tracking window size
  uint16_t  vad_ensm_alpha;                    // Smoothing constant of instant computed input energy
  uint16_t  vad_hangover;                      // SCVAD hangover to avoid detector having rapid state transition from one to another in a small amount of time duration
  uint16_t  vad_min_noise_floor;               // Minimum noise floor gate to avoid VAD sensitivity to any small signals
} vad_params_t;

typedef struct vad_lib_t {
  void  *vad_handle;
} vad_lib_t;

typedef struct vad_circ_buf_t {
  bool      vad_status;
  uint8_t*  buffer;
  uint32_t  length;
  uint32_t  start;
  uint32_t  end;
  uint32_t  byte_count;
  uint32_t  frame_size_in_bytes;
  uint32_t  sample_size_in_bytes;
} vad_circ_buf_t;

/* VAD CSIM functions */
typedef int (*vad_init_t)(void *, vad_cfg_t *, uint8_t *, uint32_t);
typedef int (*vad_get_size_t) (vad_cfg_t *, vad_size_t *);
typedef int (*vad_process_t)(void *, uint8_t *, uint32_t, uint8_t *);
typedef int (*vad_get_param_t)(void *, uint8_t *, uint32_t, uint32_t, uint32_t *);
typedef int (*vad_set_param_t)(void *, uint8_t *, uint32_t, uint32_t);

typedef struct vad_func_t {
  vad_init_t           vad_init;
  vad_get_size_t       vad_get_size;
  vad_process_t        vad_process;
  vad_get_param_t      vad_get_param;
  vad_set_param_t      vad_set_param;
} vad_func_t;

typedef struct vad_self_t {
  bool              isVadLoopDeinit;
  bool              isClientReading;
  bool              isVADThreadCreated;
  bool              isVADDumpEnabled;
  bool              isPCMDumpEnabled;

  uint8_t*          vad_init_buf;
  uint8_t*          vad_status_buffer;
  uint8_t           vad_output_byte_traverse;
  uint16_t          vad_set_param_num_frames_circ_buf;

  vad_lib_t         vad_lib;
  vad_size_t        vad_size;
  vad_cfg_t         vad_cfg;
  vad_params_t      vad_params;
  vad_func_t        vad_func;
  vad_circ_buf_t    circ_buf;
  pthread_cond_t    vad_loop_cond;
  pthread_mutex_t   vad_loop_mutex;
  pthread_t         vad_loop_thread;

  FILE            *in_read_fp;
} vad_self_t;

static vad_self_t self = {
  .isVadLoopDeinit = false,
  .isClientReading = true,
  .isVADThreadCreated = false,
  .isVADDumpEnabled = false,
  .isPCMDumpEnabled = false,
  .vad_init_buf = NULL,
  .vad_status_buffer = NULL,
  .vad_set_param_num_frames_circ_buf = 0,
  .circ_buf = {
    .vad_status = false,
    .buffer = NULL,
    .length = 0,
    .start = 0,
    .end = 0,
    .byte_count = 0,
    .frame_size_in_bytes = 0,
    .sample_size_in_bytes = 0
  },
  .in_read_fp = NULL
};

static void * vad_pcm_read_loop(void *context)
{
  struct stream_in *in = (struct stream_in *) context;
  struct audio_device *adev = in->dev;
  uint8_t* vad_process_ptr = NULL;
  int ret = 0;
  int i;
  int vad_detected_per_frame_count = 0;
  FILE *vad_fp, *pcm_fp;

  if (self.isVADDumpEnabled) {
    vad_fp = fopen("/data/misc/audio/vad_process_output.raw","w");
    if (vad_fp == NULL)
    {
        ALOGE("%s: vad_fp /data/misc/audio/vad_process_output.raw File Open Failed",
          __func__);
    }

    pcm_fp = fopen("/data/misc/audio/pcm_read_output.raw","w");
    if (pcm_fp == NULL)
    {
        ALOGE("%s: pcm_fp /data/misc/audio/pcm_read_output.raw File Open Failed",
          __func__);
    }
  }

  for(;;)
  {
    pthread_mutex_lock(&self.vad_loop_mutex);
    if (self.isVadLoopDeinit)
    {
      pthread_mutex_unlock(&self.vad_loop_mutex);
      break;
    }

    if (!in->pcm)
    {
      ALOGE("%s: no PCM input stream",
        __func__);
      ret = -EFAULT;
      break;
    }

    if (self.isClientReading) {
      pthread_cond_wait(&self.vad_loop_cond, &self.vad_loop_mutex);
      pthread_mutex_unlock(&self.vad_loop_mutex);
      ALOGD("%s: Received Signal, loop continuing",
          __func__);
      continue;
    }

    ret = pcm_read(in->pcm, &self.circ_buf.buffer[self.circ_buf.end], self.circ_buf.frame_size_in_bytes);

    if(ret < 0) {
      ALOGE("%s: PCM Read failed with status %d, sleeping for 20ms",
        __func__, ret);
      usleep(AUDIO_CAPTURE_PERIOD_DURATION_MSEC * 1000);
      continue;
    }

    if (self.isVADDumpEnabled)
      fwrite (&self.circ_buf.buffer[self.circ_buf.end], 1, self.circ_buf.frame_size_in_bytes, pcm_fp);

    vad_process_ptr = &self.circ_buf.buffer[self.circ_buf.end];
    ret = self.vad_func.vad_process(&self.vad_lib,
                                  &vad_process_ptr,
                                  (self.circ_buf.frame_size_in_bytes/(self.vad_cfg.in_data_type + 2)),
                                  &self.vad_status_buffer);

    if(ret) {
      ALOGE("%s: VAD Module Process failed with status %d",
        __func__, ret);
    }

    self.circ_buf.end += self.circ_buf.frame_size_in_bytes;
    self.circ_buf.byte_count += self.circ_buf.frame_size_in_bytes;

    if (self.circ_buf.end >= self.circ_buf.length)
        self.circ_buf.end -= self.circ_buf.length;

    if (self.circ_buf.byte_count > self.circ_buf.length) {
        ALOGE("%s: Circular Buffer Overrun at position %d",
          __func__, self.circ_buf.end);

        self.circ_buf.start += self.circ_buf.frame_size_in_bytes;
        self.circ_buf.byte_count = self.circ_buf.length;

        if (self.circ_buf.start >= self.circ_buf.length)
          self.circ_buf.start -= self.circ_buf.length;
    }

    ALOGD("%s: Circular buffer end at %d, start at %d. Byte count=%d",
        __func__, self.circ_buf.end, self.circ_buf.start, self.circ_buf.byte_count);

    pthread_mutex_unlock(&self.vad_loop_mutex);

    if (self.isVADDumpEnabled)
      /* Analyse VAD output to see if VAD detected */
      fwrite (self.vad_status_buffer, 1, self.circ_buf.frame_size_in_bytes, vad_fp);

    for (i = 0; i < self.circ_buf.frame_size_in_bytes; (i += self.vad_output_byte_traverse)) {
      if ((self.vad_status_buffer[i]) != 1) {
        vad_detected_per_frame_count = -1;
        break;
      }
    }

    if (vad_detected_per_frame_count >= 10)
      self.circ_buf.vad_status = true;
    else {
      self.circ_buf.vad_status = false;
      vad_detected_per_frame_count++;
    }
  }

  if (ret)
    adev->vad_stream_running = false;

  self.isVadLoopDeinit = false;
  if (self.isVADDumpEnabled)
    fclose(vad_fp);

  pthread_cond_signal(&self.vad_loop_cond);

  ALOGD("%s: Exiting",
      __func__);

  return NULL;
}

static int vad_init ()
{
  int ret = 0;
  int mem_size_written = 0;
  int init_buf_mem_size = 0;

  ALOGD("%s: start",__func__);

  //Calculate circular buffer length in bytes
  self.circ_buf.sample_size_in_bytes = self.vad_cfg.in_data_type + 2;
  self.circ_buf.frame_size_in_bytes = (self.vad_cfg.sampling_rate / 50) *
                    self.circ_buf.sample_size_in_bytes;

  if (self.vad_set_param_num_frames_circ_buf == 0)
    self.circ_buf.length = (self.vad_cfg.sampling_rate / 50) *
                      self.circ_buf.sample_size_in_bytes *
                      self.vad_cfg.num_look_ahead_blks *
                      self.vad_cfg.look_ahead_blk_size;
  else
    self.circ_buf.length = (self.vad_cfg.sampling_rate / 50) *
                      self.circ_buf.sample_size_in_bytes *
                      self.vad_set_param_num_frames_circ_buf;

  self.circ_buf.buffer = (uint8_t*) calloc (self.circ_buf.length, sizeof(uint8_t));
  self.vad_status_buffer = (uint8_t*) calloc (self.circ_buf.frame_size_in_bytes, sizeof(uint8_t));
  memset ((void*) self.circ_buf.buffer, 0, self.circ_buf.length);
  memset ((void*) self.vad_status_buffer, 0, self.circ_buf.frame_size_in_bytes);

  if (self.circ_buf.buffer == NULL) {
    ALOGE("%s: Circular buffer calloc failed for size %d",
          __func__, self.circ_buf.length);
    ret = -ENOMEM;
    goto error;
  }

  if (self.vad_status_buffer == NULL) {
    ALOGE("%s: VAD Status buffer calloc failed for size %d",
          __func__, self.circ_buf.frame_size_in_bytes);
    ret = -ENOMEM;
    goto error;
  }

  ALOGD("%s: Circular buffer created with size %d and VAD status buffer created with size %d",
    __func__, self.circ_buf.length, self.circ_buf.frame_size_in_bytes);

  //Calculate VAD output byte traversal
  if (self.vad_cfg.in_data_type == VAD_BIT_TYPE_16BIT)
    self.vad_output_byte_traverse = 2;
  else
    self.vad_output_byte_traverse = 4;

  pthread_cond_init(&self.vad_loop_cond, (const pthread_condattr_t *) NULL);
  pthread_mutex_init(&self.vad_loop_mutex, (const pthread_mutexattr_t *) NULL);

  if (self.isPCMDumpEnabled) {
    self.in_read_fp = fopen("/data/misc/audio/vad_in_read_output.raw","w");
    if (self.in_read_fp == NULL)
    {
        ALOGE("%s: self.in_read_fp /data/misc/audio/vad_in_read_output.raw File Open Failed",
          __func__);
    }
  }

  ret = self.vad_func.vad_get_size(&self.vad_cfg, &self.vad_size);

  if (ret) {
    ALOGE("%s: Failed to get VAD Module Size with error %d",
          __func__, ret);
    goto error;
  }
  else
    ALOGD("%s: VAD memsize=%d scratch_size=%d",__func__, self.vad_size.mem_size, self.vad_size.scratch_mem_size);

  init_buf_mem_size = self.vad_size.mem_size + self.vad_size.scratch_mem_size;

  if ((init_buf_mem_size - self.vad_size.mem_size) != self.vad_size.scratch_mem_size) {
    ALOGE("%s: VAD Init Buffer size Overflow for mem_size %d and scratch_mem_size %d",
          __func__, self.vad_size.mem_size, self.vad_size.scratch_mem_size);
    ret = -EOVERFLOW;
    goto error;
  }

  self.vad_init_buf = (uint8_t*) calloc (init_buf_mem_size, sizeof(uint8_t));

  if (self.vad_init_buf != NULL)
    memset(self.vad_init_buf, 0, sizeof(init_buf_mem_size));
  else {
    ALOGE("%s: VAD Init Buffer calloc failed for size %d",
          __func__, init_buf_mem_size);
    ret = -ENOMEM;
    goto error;
  }

  ret = self.vad_func.vad_init(&self.vad_lib, &self.vad_cfg, self.vad_init_buf, init_buf_mem_size);

  if (ret) {
    ALOGE("%s: Failed to initialize VAD Module with error %d",
          __func__, ret);
    goto error;
  }

  ret = self.vad_func.vad_get_param(&self.vad_lib, &self.vad_params, 0, sizeof(self.vad_params), &mem_size_written);

  if (ret) {
    ALOGE("%s: Failed to Get VAD Module Params with error %d",
          __func__, ret);
    goto error;
  }
  else
  {
    ALOGD("%s: mem_size_written=%d, vad_feature_enable=%d vad_op_mode=%d vad_threshold=%d vad_input_gain=%d",
          __func__, mem_size_written,
          self.vad_params.vad_feature_mode,
          self.vad_params.vad_op_mode,
          self.vad_params.vad_threshold,
          self.vad_params.vad_input_gain);

    ALOGD("%s: vad_lookahead_nblks=%d vad_lookahead_blksize=%d vad_ensm_alpha=%d vad_hangover=%d vad_min_noise_floor=%d",
          __func__, self.vad_params.vad_lookahead_nblks,
          self.vad_params.vad_lookahead_blksize,
          self.vad_params.vad_ensm_alpha,
          self.vad_params.vad_hangover,
          self.vad_params.vad_min_noise_floor);
  }

  self.vad_params.vad_feature_mode = 1;
  self.vad_params.vad_threshold = 3072;
  self.vad_params.vad_hangover = 15;
  self.vad_params.vad_min_noise_floor = 450;

  ret = self.vad_func.vad_set_param(&self.vad_lib, &self.vad_params, 0, sizeof(self.vad_params));

  if (ret) {
    ALOGE("%s: Failed to Set VAD Module Params with error %d",
          __func__, ret);
    goto error;
  }
  else
    ALOGD("%s: VAD Set Param Successful", __func__);

  ret = self.vad_func.vad_get_param(&self.vad_lib, &self.vad_params, 0, sizeof(self.vad_params), &mem_size_written);

  if (ret) {
    ALOGE("%s: Failed to Get VAD Module Params with error %d",
          __func__, ret);
    goto error;
  }
  else
  {
    ALOGD("%s: mem_size_written=%d, vad_feature_enable=%d vad_op_mode=%d vad_threshold=%d vad_input_gain=%d",
          __func__, mem_size_written,
          self.vad_params.vad_feature_mode,
          self.vad_params.vad_op_mode,
          self.vad_params.vad_threshold,
          self.vad_params.vad_input_gain);

    ALOGD("%s: vad_lookahead_nblks=%d vad_lookahead_blksize=%d vad_ensm_alpha=%d vad_hangover=%d vad_min_noise_floor=%d",
          __func__, self.vad_params.vad_lookahead_nblks,
          self.vad_params.vad_lookahead_blksize,
          self.vad_params.vad_ensm_alpha,
          self.vad_params.vad_hangover,
          self.vad_params.vad_min_noise_floor);
  }

  ALOGD("%s: finish",__func__);
  return 0;
error:
  if (self.circ_buf.buffer != NULL) {
    free(self.circ_buf.buffer);
    self.circ_buf.buffer = NULL;
  }
  if (self.vad_status_buffer != NULL) {
    free(self.vad_status_buffer);
    self.vad_status_buffer = NULL;
  }
  if (self.vad_init_buf != NULL) {
    free(self.vad_init_buf);
    self.vad_init_buf= NULL;
  }
  return ret;
}

static void vad_deinit (struct audio_device *adev)
{
  pthread_mutex_lock(&self.vad_loop_mutex);
  self.isVadLoopDeinit = true;
  pthread_mutex_unlock(&self.vad_loop_mutex);

  if(self.isVADThreadCreated) {
    pthread_cond_signal(&self.vad_loop_cond);
    pthread_cond_wait(&self.vad_loop_cond, &self.vad_loop_mutex);
    pthread_mutex_unlock(&self.vad_loop_mutex);
  }

  /* Locked by adev_set_parameter() which calls vad_deinit() */
  pthread_mutex_unlock(&adev->lock);
  adev->vad_stream->stream.common.standby(&adev->vad_stream->stream.common);
  adev->device.close_input_stream(adev, adev->vad_stream);
  pthread_mutex_lock(&adev->lock);
  adev->vad_stream_running = false;

  //Destroy pthread constructs and circular buffer
  pthread_cond_destroy(&self.vad_loop_cond);
  pthread_mutex_destroy(&self.vad_loop_mutex);
  free(self.circ_buf.buffer);
  free(self.vad_status_buffer);
  free(self.vad_init_buf);

  if (self.isPCMDumpEnabled)
    fclose(self.in_read_fp);
}

int audio_extn_vad_init(struct audio_device *adev)
{
  int ret = 0;
  self.vad_cfg.feature_mode = VAD_FEATURE_MODE_ON;
  self.vad_cfg.sampling_rate = 8000;
  self.vad_cfg.in_data_type = VAD_BIT_TYPE_16BIT;
  self.vad_cfg.num_look_ahead_blks = 5;
  self.vad_cfg.look_ahead_blk_size = 5;   // 500ms look-ahead for characterization
  self.vad_cfg.num_channels = 1;
  adev->voice_barge_in_enabled = false;
  adev->vad_stream_running = false;

  if (access(VAD_PATH, R_OK) == 0) {
    self.vad_lib.vad_handle = dlopen(VAD_PATH, RTLD_NOW);
        if (self.vad_lib.vad_handle == NULL) {
          ALOGE("%s: DLOPEN failed for %s", __func__, VAD_PATH);
          ret = -ENOENT;
    } else {
      ALOGD("%s: DLOPEN successful for %s", __func__, VAD_PATH);
      self.vad_func.vad_init = (vad_init_t)dlsym(self.vad_lib.vad_handle,
                                                        "scVAD_init");
      if (!self.vad_func.vad_init) {
          ALOGE("%s: Could not find the symbol scVAD_init from %s",
                __func__, VAD_PATH);
          ret = -ENOENT;
      }

      self.vad_func.vad_process = (vad_process_t)dlsym(self.vad_lib.vad_handle,
                                                        "scVAD_process");
      if (!self.vad_func.vad_process) {
          ALOGE("%s: Could not find the symbol scVAD_process from %s",
                __func__, VAD_PATH);
          ret = -ENOENT;
      }

      self.vad_func.vad_get_size = (vad_get_size_t)dlsym(self.vad_lib.vad_handle,
                                                        "scVAD_getsize");
      if (!self.vad_func.vad_get_size) {
          ALOGE("%s: Could not find the symbol scVAD_getsize from %s",
                __func__, VAD_PATH);
          ret = -ENOENT;
      }

      self.vad_func.vad_get_param = (vad_get_param_t)dlsym(self.vad_lib.vad_handle,
                                                        "scVAD_get_param");
      if (!self.vad_func.vad_get_param) {
          ALOGE("%s: Could not find the symbol scVAD_get_param from %s",
                __func__, VAD_PATH);
          ret = -ENOENT;
      }

      self.vad_func.vad_set_param = (vad_set_param_t)dlsym(self.vad_lib.vad_handle,
                                                        "scVAD_set_param");
      if (!self.vad_func.vad_set_param) {
          ALOGE("%s: Could not find the symbol scVAD_set_param from %s",
                __func__, VAD_PATH);
          ret = -ENOENT;
      }
    }
  }
  else
    ret = -ENOENT;

  return ret;
}

int audio_extn_vad_deinit() {

  if (self.vad_lib.vad_handle != NULL)
    dlclose(self.vad_lib.vad_handle);

  return 0;
}

void audio_extn_vad_set_parameters (struct audio_device *adev,
                                    struct str_parms *parms)
{
  int err = 0;
  char * value = NULL;
  char * endptr;
  int len;
  char * kv_pairs = str_parms_to_str(parms);

  if(kv_pairs == NULL) {
      ALOGE("[%s] key-value pair is NULL",__func__);
      return;
  }

  ALOGD("%s: enter: %s", __func__, kv_pairs);

  len = strlen(kv_pairs);
  value = (char*)calloc(len, sizeof(char));

  if(value == NULL) {
      ALOGE("[%s] failed to allocate memory",__func__);
      return;
  }

  err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VAD_ENABLED,
                          value, len);

  if (err >= 0)
  {
    if (!strncmp("true", value, sizeof("true")))
    {
      adev->voice_barge_in_enabled = true;
      ALOGD("%s: Value of VOICE BARGE-IN set to true!",__func__);
      vad_init();
    }
    else
    {
      ALOGD("%s: Value of VOICE BARGE-IN set to false!",__func__);
      if (adev->voice_barge_in_enabled) {
        adev->voice_barge_in_enabled = false;
        vad_deinit(adev);
      }
    }
  }

  err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VAD_SAMPLING_RATE,
                          value, len);

  if (err >= 0)
  {
    if (!adev->voice_barge_in_enabled)
    {
      if (!strncmp("8000", value, sizeof("8000")))
        self.vad_cfg.sampling_rate = 8000;
      else if (!strncmp("16000", value, sizeof("16000")))
        self.vad_cfg.sampling_rate = 16000;
      else if (!strncmp("32000", value, sizeof("32000")))
        self.vad_cfg.sampling_rate = 32000;
      else if (!strncmp("48000", value, sizeof("48000")))
        self.vad_cfg.sampling_rate = 48000;
      else
        ALOGE("%s: Unrecognized Voice Barge-In Sampling Rate %s!",__func__, value);

      ALOGD("%s: Value of Voice Barge-in Sampling Rate is set to %s",__func__, value);
    }
    else
      ALOGE("%s: Voice Barge-in Sampling Rate can only be set when disabled!",__func__);
  }

  err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VAD_BIT_TYPE,
                          value, len);

  if (err >= 0)
  {
    if (!adev->voice_barge_in_enabled)
    {
      if (!strncmp("16", value, sizeof("16")))
        self.vad_cfg.in_data_type = 0;
      else if (!strncmp("24", value, sizeof("24")))
        self.vad_cfg.in_data_type = 1;
      else
        ALOGE("%s: Unrecognized Voice Barge-In Bit Type %s!",__func__, value);

      ALOGD("%s: Value of Voice Barge-in Bit Type is set to %s",__func__, value);
    }
    else
      ALOGE("%s: Voice Barge-in Bit Type can only be set when disabled!",__func__);
  }

  err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VAD_DUMP_ENABLED,
                          value, len);

  if (err >= 0)
  {
    if (!adev->voice_barge_in_enabled)
    {
      if (!strncmp("true", value, sizeof("true"))) {
        self.isVADDumpEnabled = true;
        ALOGD("%s: VAD Dump in /data/misc/audio/... is enabled!",__func__);
      }
      else {
        ALOGD("%s: VAD Dump disabled!",__func__);
        self.isVADDumpEnabled = false;
      }
    }
    else
      ALOGE("%s: Voice Barge-in VAD Dump Enable can only be set when disabled!",__func__);
  }

  err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VAD_PCM_DUMP_ENABLED,
                          value, len);

  if (err >= 0)
  {
    if (!adev->voice_barge_in_enabled)
    {
      if (!strncmp("true", value, sizeof("true"))) {
        self.isPCMDumpEnabled = true;
        ALOGD("%s: PCM Dump in /data/misc/audio/... is enabled!",__func__);
      }
      else {
        ALOGD("%s: PCM Dump disabled!",__func__);
        self.isPCMDumpEnabled = false;
      }
    }
    else
      ALOGE("%s: Voice Barge-in PCM Dump Enable can only be set when disabled!",__func__);
  }

  err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VAD_NUM_FRAMES_IN_CIRC_BUF,
                          value, len);

  if (err >= 0)
  {
    if (!adev->voice_barge_in_enabled)
    {
      intmax_t vad_num_frames = strtoimax(value, &endptr, 10);
      if ((vad_num_frames <= 500) &&
          (vad_num_frames >= 10))
      {
        self.vad_set_param_num_frames_circ_buf = vad_num_frames;
        ALOGD("%s: Circular Buffer Size set to %d frames",__func__, self.vad_set_param_num_frames_circ_buf);
      }
      else
        ALOGE("%s: Invalid number %s for number of frames. Value should be between 10-500",__func__, value);
    }
    else
      ALOGE("%s: Voice Barge-in Circular Buffer Size can only be set when disabled!",__func__);
  }
}

void audio_extn_vad_get_parameters (struct audio_device *adev,
                                    struct str_parms *query,
                                    struct str_parms *reply)
{
  int ret = 0;
  char value[512] = {0};
  char int_str_reply[512] = {0};

  ret = str_parms_get_str(query,AUDIO_PARAMETER_KEY_VAD_ENABLED,
                          value,sizeof(value));
  if (ret >= 0) {
      str_parms_add_str(reply, AUDIO_PARAMETER_KEY_VAD_ENABLED,
                        adev->voice_barge_in_enabled?"true":"false");
  }
  ret = str_parms_get_str(query,AUDIO_PARAMETER_KEY_VAD_DETECTED,
                          value,sizeof(value));
  if (ret >= 0) {
      str_parms_add_str(reply, AUDIO_PARAMETER_KEY_VAD_DETECTED,
                        self.circ_buf.vad_status?"true":"false");
  }
  ret = str_parms_get_str(query,AUDIO_PARAMETER_KEY_VAD_SAMPLING_RATE,
                          value,sizeof(value));
  if (ret >= 0) {
      snprintf(int_str_reply, sizeof(int_str_reply), "%d", self.vad_cfg.sampling_rate);
      str_parms_add_str(reply, AUDIO_PARAMETER_KEY_VAD_SAMPLING_RATE,
                        int_str_reply);
  }
  ret = str_parms_get_str(query,AUDIO_PARAMETER_KEY_VAD_BIT_TYPE,
                          value,sizeof(value));
  if (ret >= 0) {
      snprintf(int_str_reply, sizeof(int_str_reply), "%d", (16 + (8 * self.vad_cfg.in_data_type)));
      str_parms_add_str(reply, AUDIO_PARAMETER_KEY_VAD_BIT_TYPE,
                        int_str_reply);
  }
  ret = str_parms_get_str(query,AUDIO_PARAMETER_KEY_VAD_NUM_FRAMES_IN_CIRC_BUF,
                          value,sizeof(value));
  if (ret >= 0) {
      snprintf(int_str_reply, sizeof(int_str_reply), "%d", self.vad_set_param_num_frames_circ_buf);
      str_parms_add_str(reply, AUDIO_PARAMETER_KEY_VAD_NUM_FRAMES_IN_CIRC_BUF,
                        int_str_reply);
  }

  free(value);
}

void audio_extn_vad_circ_buf_create_read_loop(struct stream_in *in)
{
  ALOGD("%s: Creating circular buffer read loop",
          __func__);

  pthread_create(&self.vad_loop_thread, (const pthread_attr_t *) NULL,
                vad_pcm_read_loop, in);

  self.isVADThreadCreated = true;
}

void audio_extn_vad_circ_buf_start_read_loop()
{
  ALOGD("%s: Starting circular buffer read loop",
          __func__);

  self.isClientReading = false;
  pthread_cond_signal(&self.vad_loop_cond);
}

size_t audio_extn_vad_circ_buf_read(struct stream_in *in,
                                        void *buffer, size_t bytes)
{
  int ret = 0;
  uint32_t first_partition_size, second_partition_size = 0;
  uint8_t* read_ptr;

  ALOGV("%s: start",
      __func__);

  if (bytes > self.circ_buf.length) {
    ALOGE("%s: Read size %d is too large for buffer length %d",
          __func__, (int) bytes, self.circ_buf.length);
    ret = -EINVAL;
    goto read_done;
  }

  pthread_mutex_lock(&self.vad_loop_mutex);
  self.isClientReading = true;

  if (self.circ_buf.byte_count == 0)
  {
    ALOGV("%s: No data available in circular buffer at position %d. Do PCM_READ()",
          __func__, self.circ_buf.start);
    ret = pcm_read(in->pcm, buffer, bytes);
    if (self.isPCMDumpEnabled)
      fwrite (buffer, 1, bytes, self.in_read_fp);
    goto read_done;
  }

  if ( bytes > self.circ_buf.byte_count ) {
    ALOGE("%s: Read size %d is too large for %d available bytes in buffer",
          __func__, (int) bytes, self.circ_buf.byte_count);
    ret = -ENODATA;
    goto read_done;
  }

  /* Copy two separate parts if bytes needed exceeds data from start index to n of array */
  if ((bytes + self.circ_buf.start) > self.circ_buf.length)
  {
    first_partition_size = self.circ_buf.length - self.circ_buf.start;
    second_partition_size = bytes - (self.circ_buf.length - self.circ_buf.start);
    memcpy(buffer, &self.circ_buf.buffer[self.circ_buf.start], first_partition_size);

    if (self.isPCMDumpEnabled)
      fwrite (buffer, 1, first_partition_size, self.in_read_fp);

    read_ptr = (uint8_t *) buffer;
    read_ptr += first_partition_size;
    memcpy((void *)read_ptr, &self.circ_buf.buffer[0], second_partition_size);

    if (self.isPCMDumpEnabled)
      fwrite (read_ptr, 1, second_partition_size, self.in_read_fp);

    self.circ_buf.start = second_partition_size;
  }
  else
  {
    memcpy(buffer, &self.circ_buf.buffer[self.circ_buf.start], bytes);
    if (self.isPCMDumpEnabled)
      fwrite (buffer, 1, bytes, self.in_read_fp);

    self.circ_buf.start += bytes;
  }

  if (self.circ_buf.start == self.circ_buf.length)
    self.circ_buf.start = 0;

  self.circ_buf.byte_count -= bytes;
  ret = 0;

read_done:
  pthread_mutex_unlock(&self.vad_loop_mutex);
  ALOGV("%s: finish at position %d",
    __func__, self.circ_buf.start);
  return ret;
}

#endif /* VAD_ENABLED */
