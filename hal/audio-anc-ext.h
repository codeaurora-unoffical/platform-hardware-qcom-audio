/* Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#ifndef _AUDIO_ANC_EXT_H
#define _AUDIO_ANC_EXT_H

#define AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_CMD_TYPE     "ext_audio_anc_cmd_type"

#define AUDIO_PARAMETER_KEY_EXT_AUDIO_ANC_EXT_ALGO_MODULE_ID  "ext_audio_anc_algo_module_id"

enum {
   ANC_EXT_CMD_INVALID = 0,
   ANC_EXT_CMD_START,                     //1
   ANC_EXT_CMD_STOP,                      //2
   ANC_EXT_CMD_RPM,                       //3
   ANC_EXT_CMD_BYPASS_MODE,               //4
   ANC_EXT_CMD_ALGO_MODULE,               //5
   ANC_EXT_CMD_ALGO_CALIBRATION,          //6
   ANC_EXT_CMD_MAX,
};


struct audio_anc_rpm_info_ext {
   int32_t     rpm;
};

struct audio_anc_bypass_mode_ext {
   int32_t     mode;
};
struct audio_anc_algo_module_info_ext {
   int32_t     module_id;
};

struct audio_anc_algo_calibration_info_ext {
   uint32_t module_id;
   uint32_t param_id;
   uint32_t payload_size;
   /* bytes of payload specificed in payload_size followed  */
   /* The payload should be in align of 32 bit             */
};

int audio_anc_ext_init(void);

int audio_anc_ext_deinit(void);

int audio_anc_ext_send_param(int32_t param, void* data_p);

int audio_anc_ext_get_param(int32_t param, void* data_p);

#endif /* _AUDIO_ANC_EXT_H */
