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
#ifndef AUTO_VOLUME_H_
#define AUTO_VOLUME_H_

#include "auto_bundle.h"
#include "auto_effect_util.h"

#define AUTO_EFFECT_CACHED_VOL_IDX 1
#define AUTO_EFFECT_CACHED_GAIN 2
#define AUTO_EFFECT_CACHED_MUTE 4


extern const effect_descriptor_t auto_effect_volume_descriptor;

typedef struct auto_effect_volume_context_s
{
    effect_context_t common;
    audcalparam_cmd_volume_idx_t volume_value;
    audcalparam_cmd_gain_t gain_value;
    audcalparam_cmd_mute_t mute_value;

} auto_effect_volume_context_t;

typedef enum
{
    AUTO_EFFECT_VOLUME_PARAM_STATUS=0, //API: return state: initialized, active
    AUTO_EFFECT_VOLUME_PARAM_VOL_INDEX,
    AUTO_EFFECT_GAIN_PARAM_GAIN, //API
    AUTO_EFFECT_MUTE_PARAM_MUTE, //API
    AUTO_EFFECT_VOLUME_PARAM_USAGE // set on effect construction, no API
} auto_effect_volume_params_t;

int auto_effect_volume_get_parameter(effect_context_t *context, effect_param_t *p, uint32_t *size);
int auto_effect_volume_set_parameter(effect_context_t *context, effect_param_t *p, uint32_t size);
int auto_effect_volume_init(effect_context_t *context);
int auto_effect_volume_start(effect_context_t *context);
int auto_effect_volume_release(effect_context_t *context);

#endif
