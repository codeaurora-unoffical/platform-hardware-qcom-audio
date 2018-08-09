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

#ifndef AUTO_BMT_H_
#define AUTO_BMT_H_

#include "auto_bundle.h"
#include "auto_effect_util.h"
// #include "audcalparam_api.h"

extern const effect_descriptor_t auto_effect_bmt_descriptor;

typedef struct auto_effect_bmt_context_s
{
    effect_context_t common;
    audcalparam_cmd_bmt_t bmt_value;
} auto_effect_bmt_context_t;

typedef enum
{
    AUTO_EFFECT_BMT_PARAM_ENABLE=0,
    AUTO_EFFECT_BMT_PARAM_BASS, //API
    AUTO_EFFECT_BMT_PARAM_MID, //API
    AUTO_EFFECT_BMT_PARAM_TREBLE, //API
    AUTO_EFFECT_BMT_PARAM_STATUS, //API: return state: initialized, active
    AUTO_EFFECT_BMT_PARAM_USAGE // set on effect construction, no API
} auto_effect_bmt_params_t;


int auto_effect_bmt_get_parameter(effect_context_t *context, effect_param_t *p,
                            uint32_t *size);

int auto_effect_bmt_set_parameter(effect_context_t *context, effect_param_t *p,
                            uint32_t size);

int auto_effect_bmt_set_device(effect_context_t *context,  uint32_t device);

int auto_effect_bmt_set_mode(effect_context_t *context);

int auto_effect_bmt_reset(effect_context_t *context);

int auto_effect_bmt_init(effect_context_t *context);

int auto_effect_bmt_enable(effect_context_t *context);

int auto_effect_bmt_disable(effect_context_t *context);

int auto_effect_bmt_start(effect_context_t *context/*, output_context_t *output*/);

int auto_effect_bmt_stop(effect_context_t *context/*, output_context_t *output*/);

#endif