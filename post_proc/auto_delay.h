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
#ifndef AUTO_DELAY_H_
#define AUTO_DELAY_H_

#include "auto_bundle.h"
#include "auto_effect_util.h"

extern const effect_descriptor_t auto_effect_delay_descriptor;

typedef struct auto_effect_delay_context_s
{
    effect_context_t common;
    audcalparam_cmd_delay_t delay_value;
} auto_effect_delay_context_t;

typedef enum
{
    AUTO_EFFECT_DELAY_PARAM_ENABLE=0,
    AUTO_EFFECT_DELAY_PARAM_DELAY_MCH,
    AUTO_EFFECT_DELAY_PARAM_STATUS, //API: return state: initialized, active
    AUTO_EFFECT_DELAY_PARAM_USAGE // set on effect construction, no API
} auto_effect_delay_params_t;

int auto_effect_delay_get_parameter(effect_context_t *context, effect_param_t *p, uint32_t *size);
int auto_effect_delay_set_parameter(effect_context_t *context, effect_param_t *p, uint32_t size);
int auto_effect_delay_init(effect_context_t *context);
int auto_effect_delay_start(effect_context_t *context);
int auto_effect_delay_stop(effect_context_t *context);
int auto_effect_delay_enable(effect_context_t *context);
int auto_effect_delay_disable(effect_context_t *context);
int auto_effect_delay_release(effect_context_t *context);

#endif
