/*
 * Copyright (c) 2013,2018, The Linux Foundation. All rights reserved.
 * Not a Contribution.
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
 */

#ifndef AUTO_EFFECT_UTIL_H_
#define AUTO_EFFECT_UTIL_H_

#include <stdlib.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <audio_hw.h>
#include "audcalparam_api.h"

enum USECASES_ENUM {
    AUTO_AMP_GENERAL_PLAYBACK
};
extern const char *USECASES_NAMES[];

int auto_effect_util_lib_ctl_init(void);

#if defined(__LP64__)
#define LIB_AUDCALPARAM_API "/vendor/lib64/libaudcalparam.so"
#else
#define LIB_AUDCALPARAM_API "/vendor/lib/libaudcalparam.so"
#endif

typedef int (*auto_effect_lib_ctl_init_t)(audcalparam_session_t **h,
                                        char *cfg_file_name,
                                        char *snd_card_name);

typedef int (*auto_effect_lib_ctl_deinit_t)(audcalparam_session_t *h);

typedef int (*auto_effect_lib_ctl_cmd_bmt_t)(audcalparam_session_t *h,
                                           char *cmd_instance_name,
                                           audcalparam_cmd_dir_t dir,
                                           audcalparam_cmd_bmt_t *value,
                                           audcalparam_cmd_base_cfg_t *cfg);

typedef int (*auto_effect_lib_ctl_cmd_fnb_t)(audcalparam_session_t *h,
                                           char* cmd_instance_name,
                                           audcalparam_cmd_dir_t dir,
                                           audcalparam_cmd_fnb_t* value,
                                           audcalparam_cmd_base_cfg_t* cfg);

typedef int (*auto_effect_lib_ctl_cmd_delay_t)(audcalparam_session_t *h,
                                             char *cmd_instance_name,
                                             audcalparam_cmd_dir_t dir,
                                             audcalparam_cmd_delay_t *value,
                                             audcalparam_cmd_base_cfg_t *cfg);

typedef int (*auto_effect_lib_ctl_cmd_mute_t)(audcalparam_session_t *h,
                                            char *cmd_instance_name,
                                            audcalparam_cmd_dir_t dir,
                                            audcalparam_cmd_mute_t *value,
                                            audcalparam_cmd_base_cfg_t *cfg);

typedef int (*auto_effect_lib_ctl_cmd_gain_t)(audcalparam_session_t *h,
                                            char *cmd_instance_name,
                                            audcalparam_cmd_dir_t dir,
                                            audcalparam_cmd_gain_t *value,
                                            audcalparam_cmd_base_cfg_t *cfg);

typedef int (*auto_effect_lib_ctl_cmd_module_param_t)(audcalparam_session_t *h,
                                                    char *cmd_instance_name,
                                                    audcalparam_cmd_dir_t dir,
                                                    uint8_t *pbuf,
                                                    uint32_t *pbuf_len,
                                                    audcalparam_cmd_module_param_cfg_t *cfg);

typedef int (*auto_effect_lib_ctl_cmd_volume_idx_t)(audcalparam_session_t *h,
                                                    char *cmd_instance_name,
                                                    audcalparam_cmd_dir_t dir,
                                                    audcalparam_cmd_volume_idx_t *value,
                                                    audcalparam_cmd_base_cfg_t *cfg);

typedef struct auto_effect_lib_ctl_t{
    void * effect_lib_ctl_handle;
    audcalparam_session_t *hsession;
    auto_effect_lib_ctl_init_t effect_lib_ctl_init;
    auto_effect_lib_ctl_deinit_t effect_lib_ctl_deinit;
    auto_effect_lib_ctl_cmd_bmt_t effect_lib_ctl_cmd_bmt;
    auto_effect_lib_ctl_cmd_fnb_t effect_lib_ctl_cmd_fnb;
    auto_effect_lib_ctl_cmd_delay_t effect_lib_ctl_cmd_delay;
    auto_effect_lib_ctl_cmd_mute_t effect_lib_ctl_cmd_mute;
    auto_effect_lib_ctl_cmd_gain_t effect_lib_ctl_cmd_gain;
    auto_effect_lib_ctl_cmd_module_param_t effect_lib_ctl_cmd_module_param;
    auto_effect_lib_ctl_cmd_volume_idx_t effect_lib_ctl_cmd_volume_idx;
}auto_effect_lib_ctl_t;

extern auto_effect_lib_ctl_t auto_effect_lib_ctl_inst;

// convert usecase to usage
enum {
    AUTO_EFFECT_USAGE_NOTSET=0,
    AUTO_EFFECT_USAGE_MUSIC,
    AUTO_EFFECT_USAGE_NAVIGATION
//TODO: add _RADIO,...
};
// channel types enum from adsp grok api/audio/inc/adsp_media_fmt.h
enum {
  AUTO_EFFECT_CH_TYPE_L=1, // 1:left
  AUTO_EFFECT_CH_TYPE_R, // 2:right
  AUTO_EFFECT_CH_TYPE_C, // 3:center
  AUTO_EFFECT_CH_TYPE_LS, // 4:left-surround
  AUTO_EFFECT_CH_TYPE_RS, // 5:right-surround
  AUTO_EFFECT_CH_TYPE_LFE, // 6:low-frequency effect
  AUTO_EFFECT_CH_TYPE_CS, // 7:center-surround
  AUTO_EFFECT_CH_TYPE_CB=AUTO_EFFECT_CH_TYPE_CS, // 7:center-back
  AUTO_EFFECT_CH_TYPE_LB, // 8:left-back
  AUTO_EFFECT_CH_TYPE_RB, // 9:right-back
  AUTO_EFFECT_CH_TYPE_MAX=AUTO_EFFECT_CH_TYPE_RB
};

int auto_effect_util_get_usage(int usecase);

int auto_effect_util_chtype_init(int *pchtype, int len);

const char* get_mute_cmd_inst_name(int usage);

const char* get_gain_cmd_inst_name(int usage);

const char* get_volume_cmd_inst_name(int usage);

int auto_effect_mute_update_adsp_from_ctx( audcalparam_cmd_mute_t * pmutectx, int usage);

int auto_effect_mute_update_ctx_from_adsp( audcalparam_cmd_mute_t * pmutectx, int usage);

int auto_effect_gain_update_adsp_from_ctx(audcalparam_cmd_gain_t *pgainctx, int usage);

int auto_effect_gain_update_ctx_from_adsp(audcalparam_cmd_gain_t *pgainctx, int usage);

int auto_effect_volume_update_adsp_from_ctx(audcalparam_cmd_volume_idx_t * pvolumectx, int usage);

int auto_effect_volume_update_ctx_from_adsp(audcalparam_cmd_volume_idx_t *pvolumectx, int usage);

int auto_effect_volume_get_idx_by_chtype (int * pchtype, int chtype, int chtypelen);

#endif
