/* vad_param_key.h
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

#ifdef VAD_ENABLED

/*  These keys are used to control VAD through the Audio Manager
    using AudioManager.setParameters(<string>) and
    AudioManager.getParameters(<string>). The string used should
    be a "key=value" pair.  For example, VAD_enabled is the key,
    while it supports values "true" and "false"
    */

#define AUDIO_PARAMETER_KEY_VAD_ENABLED  "VAD_enabled"
  /* VAD_enabled enables the VAD. It supports the following values:
      - false, true
      */

#define AUDIO_PARAMETER_KEY_VAD_SAMPLING_RATE  "VAD_sampling_rate"
  /* VAD_sampling_rate must be set when VAD_enabled=false.
     It supports the following values:
      - 8000, 16000, 32000, 48000
      */

#define AUDIO_PARAMETER_KEY_VAD_BIT_TYPE  "VAD_bit_type"
  /* VAD_bit_type must be set when VAD_enabled=false.
     It supports the following values:
      - 16, 24
      */

#define AUDIO_PARAMETER_KEY_VAD_NUM_FRAMES_IN_CIRC_BUF "VAD_num_frames_in_circ_buf"
  /* VAD_num_frames_in_circ_buf specifies the number of frames stored
     in the VAD circular buffer used for storing captured audio data history.
     Each frame is 20ms long. This must be set when VAD_enabled=false.
     The following the range of values are supported:
      - 10 to 150
      */

#define AUDIO_PARAMETER_KEY_VAD_DETECTED  "VAD_detected"
  /* VAD_detected can only be used with getParameters(). This should
     only be used when VAD_enabled=true to see if voice is detected
     or not.  The returned string will be either "true" or "false".
      */

#endif /* VAD_ENABLED */
