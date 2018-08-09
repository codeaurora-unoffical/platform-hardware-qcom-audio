/*
 * Copyright (c) 2013-2014,2018, The Linux Foundation. All rights reserved.
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


#define LOG_TAG "auto_effect_bmt"
#include <stdlib.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/audio_effect.h>
#include "auto_bmt.h"

/* Effect BMT UUID:  */
const effect_descriptor_t auto_effect_bmt_descriptor = {
        {0x58b49329, 0xa4e2, 0x4d9c, 0x86ca, {0xf7, 0x0a, 0x2e, 0xe9, 0x27, 0x9e}}, // type
        {0xe039757b, 0xa367, 0x44e9, 0x9bbb, {0x63, 0x4a, 0xf0, 0xc5, 0x1c, 0xb7}}, // uuid
        EFFECT_CONTROL_API_VERSION,
        (EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_DEVICE_IND | EFFECT_FLAG_NO_PROCESS),
        0, /* TODO */
        1,
        "Bass-Mid-Treble",
        "Qualcomm Technologies, Inc.",
};

// must be aligend with audcalparam cfg file
static const char* get_bmt_cmd_inst_name(int usage){

    switch (usage)
    case AUTO_EFFECT_USAGE_MUSIC:
        return "BMT_auto_amp_general_playback";
    //TODO: add other usages

    return "";
}

static const char*  get_bmt_en_inst_name(int usage){
    switch (usage){
    case AUTO_EFFECT_USAGE_MUSIC:
        return "bmt_en_copp_auto_amp_general_playback";
    }
    return "";
}

int auto_effect_bmt_get_parameter(effect_context_t *context, effect_param_t *p,
                            uint32_t *size)
{
    ALOGD("%s:",__func__);
    int r=-1;
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        p->status = -EINVAL;
        return r;
    }

    auto_effect_bmt_context_t * pbmtctx = (auto_effect_bmt_context_t *)context;

    // call function from audcalparam lib
    // map effect_param_t to bmt value type when audcalparam lib is used
    audcalparam_cmd_base_cfg_t cfg={0,48000};

    // extracting from parameter
    int voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);
    int32_t *param_tmp = (int32_t *)p->data;
    int32_t param = *param_tmp++;
    void *value = p->data + voffset;
    audcalparam_cmd_bmt_t valbmt={0,0,0,0};

    p->status = 0;
    if (p->vsize < sizeof(uint32_t)){
        p->status = -EINVAL;
        return r;
    }
    p->vsize = sizeof(uint32_t);

    *size = sizeof(effect_param_t) + voffset + p->vsize;

    //set the instance name
    char* instance_name = get_bmt_cmd_inst_name(context->usage);
    valbmt.filter_mask=0x07;
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_bmt(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_GET, &valbmt, &cfg);
    if (r==AUDCALPARAM_OK){
        // update ctx
        pbmtctx->bmt_value.bass = valbmt.bass;
        pbmtctx->bmt_value.mid = valbmt.mid;
        pbmtctx->bmt_value.treble = valbmt.treble;
        pbmtctx->bmt_value.filter_mask = 0x07;
        ALOGD("%s:Ret bass value is %d",__func__,valbmt.bass);
    }
    switch (param) {
    case AUTO_EFFECT_BMT_PARAM_BASS:
        ALOGD("AUTO_EFFECT_BMT_PARAM_BASS");
        // read from the context
        *(int32_t*)value=pbmtctx->bmt_value.bass;
        break;
    case AUTO_EFFECT_BMT_PARAM_MID:
        ALOGD("AUTO_EFFECT_BMT_PARAM_MID");
        // read from the context
        *(int32_t*)value=pbmtctx->bmt_value.mid;
        break;
    case AUTO_EFFECT_BMT_PARAM_TREBLE:
        ALOGD("AUTO_EFFECT_BMT_PARAM_TREBLE");
        // read from the context
        *(int32_t*)value=pbmtctx->bmt_value.treble;
        break;
    case AUTO_EFFECT_BMT_PARAM_USAGE:
        *(int32_t*)value = context->usage;
        r=0;
        break;
    case AUTO_EFFECT_BMT_PARAM_ENABLE:
        if (context->state==EFFECT_STATE_ACTIVE && r==AUDCALPARAM_OK)
            *(int32_t*)value = 1;
        else
            *(int32_t*)value = 0;
        r=0;
        break;
    case AUTO_EFFECT_BMT_PARAM_STATUS:
        *(int32_t*)value=context->state;
        r=0;
        break;
    default:
        ALOGE("%s:Unknown param %d!",__func__,param);
        p->status = -EINVAL;
        return r;
    }

    if (r!=AUDCALPARAM_OK){
        ALOGE("Fail to get the parameter, take from context");
    }

    return r;
}

int auto_effect_bmt_set_parameter(effect_context_t *context, effect_param_t *p,
                            uint32_t size)
{
    ALOGD("%s:",__func__);
    int r=-1;
    p->status = 0;
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        p->status = -EINVAL;
        return r;
    }

    auto_effect_bmt_context_t * pbmtctx = (auto_effect_bmt_context_t *)context;

    // call function from audcalparam lib
    // map effect_param_t to bmt value type when audcalparam lib is used

    audcalparam_cmd_base_cfg_t cfg={0,48000};

    // extracting from parameter
    int voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);
    int32_t *param_tmp = (int32_t *)p->data;
    int32_t param = *param_tmp++;
    void *value = p->data + voffset;

    // check size of value
    if (p->vsize!=sizeof(param)){
        ALOGE("%s:Provided value size is %d, expected=%d!",__func__,p->vsize,sizeof(param));
        p->status = -EINVAL;
        return r;
    }

    //set the instance name
    char* instance_name = get_bmt_cmd_inst_name(context->usage);
    switch (param) {
       case AUTO_EFFECT_BMT_PARAM_BASS:
        ALOGD("AUTO_EFFECT_BMT_PARAM_BASS");
        // store to effect context first
        pbmtctx->bmt_value.bass = *(int32_t*)value;
        pbmtctx->bmt_value.filter_mask|=0x01;
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_bmt(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &pbmtctx->bmt_value, &cfg);
        context->cached_val=1;
        break;
    case AUTO_EFFECT_BMT_PARAM_MID:
        ALOGD("AUTO_EFFECT_BMT_PARAM_MID");
         // store to effect context first
        pbmtctx->bmt_value.mid = *(int32_t*)value;
        pbmtctx->bmt_value.filter_mask|=0x02;
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_bmt(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &pbmtctx->bmt_value, &cfg);
        context->cached_val=1;
        break;
    case AUTO_EFFECT_BMT_PARAM_TREBLE:
        ALOGD("AUTO_EFFECT_BMT_PARAM_TREBLE");
        // store to effect context first
        pbmtctx->bmt_value.treble = *(int32_t*)value;
        pbmtctx->bmt_value.filter_mask|=0x04;
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_bmt(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &pbmtctx->bmt_value, &cfg);
        context->cached_val=1;
        break;
    case AUTO_EFFECT_BMT_PARAM_USAGE:
        context->usage = *(int32_t*)value;
        ALOGD("%s:Set usage=%d",__func__,context->usage);
        // trigger start for new usage
        r=auto_effect_bmt_start(context);
        if (r==AUDCALPARAM_OK)
            context->state = EFFECT_STATE_ACTIVE;
        break;
    case AUTO_EFFECT_BMT_PARAM_ENABLE:
        ALOGD("AUTO_EFFECT_BMT_PARAM_ENABLE");
        if (*(int32_t*)value){
           r = auto_effect_bmt_enable(context);
        } else {
           r = auto_effect_bmt_disable(context);
        }
        break;
    case AUTO_EFFECT_BMT_PARAM_STATUS:
        r=0;
        break;
    default:
        ALOGE("%s:Unknown param %d!",__func__,param);
        p->status = -EINVAL;
        return r;
    }
    if (r!=AUDCALPARAM_OK){
        ALOGE("Fail to set the parameter to ADSP");
    }

    return r;
}

int auto_effect_bmt_init(effect_context_t *context){
    ALOGD("%s:",__func__);
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return -EINVAL;
    }

    audcalparam_cmd_base_cfg_t cfg={1,48000};// default reading cfg: acdb file, sr=48000Hz
    auto_effect_bmt_context_t * pbmtctx = (auto_effect_bmt_context_t *)context;

    int r=0;

    //set default usage:none
    context->usage=AUTO_EFFECT_USAGE_NOTSET;

    pbmtctx->bmt_value=(audcalparam_cmd_bmt_t){.bass=0,.mid=0,.treble=0,.filter_mask=0x0};// zero on error

    context->cached_val=0;
    ALOGD("%s:ret=%d",__func__,r);

    return r;
}

int auto_effect_bmt_enable(effect_context_t *context){
    ALOGD("%s:",__func__);

    int r=-1;
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return -EINVAL;
    }
    //use module_param to enable
    uint32_t enable=1;
    uint32_t pbuf_len=sizeof(enable);
    char* instance_name = get_bmt_en_inst_name(context->usage);
    audcalparam_cmd_module_param_cfg_t cfg={{0,48000},0,0,0};//use mid and pid from cfg file
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_module_param(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_SET, (uint8_t*)&enable, &pbuf_len, &cfg);

    ALOGD("%s:ret=%d",__func__,r);

    return r;

}

int auto_effect_bmt_disable(effect_context_t *context){
    ALOGD("%s:",__func__);
    int r=-1;

    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){

        return -EINVAL;
    }

    //use module_param
    uint32_t enable=0;
    uint32_t pbuf_len=sizeof(enable);
    char* instance_name = get_bmt_en_inst_name(context->usage);
    audcalparam_cmd_module_param_cfg_t cfg={{0,48000},0,0,0};//use mid and pid from cfg file
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_module_param(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_SET, (uint8_t*)&enable, &pbuf_len, &cfg);

    ALOGD("%s:ret=%d",__func__,r);

    return r;

}

int auto_effect_bmt_start(effect_context_t *context/*, output_context_t *output usecase*/){
    ALOGD("%s:",__func__);

    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){

        return -EINVAL;
    }
    int r=-1;

    audcalparam_cmd_base_cfg_t cfg={0,48000};// default reading cfg: adsp, sr=48000Hz

    auto_effect_bmt_context_t * pbmtctx = (auto_effect_bmt_context_t *)context;

    char* instance_name = get_bmt_cmd_inst_name(context->usage);
    // set enable
    r=auto_effect_bmt_enable(context);

    if (context->cached_val){
        // store to ADSP from ctx
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_bmt(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &pbmtctx->bmt_value, &cfg);
        ALOGD("%s:Values applied from cache %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    } else {
        pbmtctx->bmt_value.filter_mask=0x07;
        // read from ADPS
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_bmt(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_GET, &pbmtctx->bmt_value, &cfg);
        ALOGD("%s:Values read from ADSP %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    }

    ALOGD("%s:ret=%d",__func__,r);
    return r;
}

int auto_effect_bmt_stop(effect_context_t *context/*, output_context_t *output*/){
    ALOGD("%s:",__func__);
    int r=0;

    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){

        return -EINVAL;
    }
    //set disable
    r=auto_effect_bmt_disable(context);
    ALOGD("%s:ret=%d",__func__,r);
    return r;
}

