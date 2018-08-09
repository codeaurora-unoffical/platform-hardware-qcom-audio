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


#define LOG_TAG "auto_effect_fnb"
#include <stdlib.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/audio_effect.h>
#include "auto_fnb.h"

/* Effect FNB UUID:  */
const effect_descriptor_t auto_effect_fnb_descriptor = {
        {0x6bc888f6, 0xa544, 0x11e8, 0x98d0, {0x52, 0x92, 0x69, 0xfb, 0x14, 0x59}}, // type
        {0x6bc88c84, 0xa544, 0x11e8, 0x98d0, {0x52, 0x92, 0x69, 0xfb, 0x14, 0x59}}, // uuid
        EFFECT_CONTROL_API_VERSION,
        (EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_DEVICE_IND | EFFECT_FLAG_NO_PROCESS),
        0, /* TODO */
        1,
        "Fade-n-Balance",
        "Qualcomm Technologies, Inc.",
};

// must be aligend with audcalparam cfg file
static const char* get_fnb_cmd_inst_name(int usage){

    switch (usage)
    case AUTO_EFFECT_USAGE_MUSIC:
        return "FNB_auto_amp_general_playback";
    //TODO: add other usages

    return "";
}

static const char*  get_fnb_en_inst_name(int usage){
    switch (usage){
    case AUTO_EFFECT_USAGE_MUSIC:
        return "fnb_en_copp_auto_amp_general_playback";
    }
    return "";
}

int auto_effect_fnb_get_parameter(effect_context_t *context, effect_param_t *p,
                            uint32_t *size)
{
    ALOGD("%s:",__func__);
    int r=-1;
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        p->status = -EINVAL;
        return r;
    }

    auto_effect_fnb_context_t * pfnbctx = (auto_effect_fnb_context_t *)context;

    // call function from audcalparam lib
    // map effect_param_t to fnb value type when audcalparam lib is used
    audcalparam_cmd_base_cfg_t cfg={0,48000};

    // extracting from parameter
    int voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);
    int32_t *param_tmp = (int32_t *)p->data;
    int32_t param = *param_tmp++;
    void *value = p->data + voffset;
    audcalparam_cmd_fnb_t valfnb={0,0,0,0};

    p->status = 0;
    if (p->vsize < sizeof(uint32_t)){
        p->status = -EINVAL;
        return r;
    }
    p->vsize = sizeof(uint32_t);

    *size = sizeof(effect_param_t) + voffset + p->vsize;

    //set the instance name
    char* instance_name = get_fnb_cmd_inst_name(context->usage);
    valfnb.filter_mask=0x03;
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_fnb(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_GET, &valfnb, &cfg);
    if (r==AUDCALPARAM_OK){
        // update ctx
        pfnbctx->fnb_value.balance = valfnb.balance;
        pfnbctx->fnb_value.fade = valfnb.fade;
        pfnbctx->fnb_value.filter_mask = 0x03;
        ALOGD("%s:Ret balance value is %d",__func__,valfnb.balance);
    }
    switch (param) {
    case AUTO_EFFECT_FNB_PARAM_BALANCE:
        ALOGD("AUTO_EFFECT_FNB_PARAM_BALANCE");
        // read from the context
        *(int32_t*)value=pfnbctx->fnb_value.balance;
        break;
    case AUTO_EFFECT_FNB_PARAM_FADE:
        ALOGD("AUTO_EFFECT_FNB_PARAM_FADE");
        // read from the context
        *(int32_t*)value=pfnbctx->fnb_value.fade;
        break;
    case AUTO_EFFECT_FNB_PARAM_USAGE:
        *(int32_t*)value = context->usage;
        break;
    case AUTO_EFFECT_FNB_PARAM_ENABLE:
       if (context->state==EFFECT_STATE_ACTIVE && r==AUDCALPARAM_OK)
            *(int32_t*)value = 1;
        else
            *(int32_t*)value = 0;
        r=0;
        break;
    case AUTO_EFFECT_FNB_PARAM_STATUS:
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

int auto_effect_fnb_set_parameter(effect_context_t *context, effect_param_t *p,
                            uint32_t size)
{
    ALOGD("%s:",__func__);
    int r=-1;
    p->status = 0;
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        p->status = -EINVAL;
        return r;
    }

    auto_effect_fnb_context_t * pfnbctx = (auto_effect_fnb_context_t *)context;

    // call function from audcalparam lib
    // map effect_param_t to fnb value type when audcalparam lib is used
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
    char* instance_name = get_fnb_cmd_inst_name(context->usage);
    switch (param) {
       case AUTO_EFFECT_FNB_PARAM_BALANCE:
        ALOGD("AUTO_EFFECT_FNB_PARAM_BALANCE");
        // store to effect context first
        pfnbctx->fnb_value.balance = *(int32_t*)value;
        pfnbctx->fnb_value.filter_mask|=0x01;
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_fnb(auto_effect_lib_ctl_inst.hsession,
                    instance_name, AUDCALPARAM_SET, &pfnbctx->fnb_value, &cfg);
        context->cached_val=1;
        break;
    case AUTO_EFFECT_FNB_PARAM_FADE:
        ALOGD("AUTO_EFFECT_FNB_PARAM_FADE");
         // store to effect context first
        pfnbctx->fnb_value.fade = *(int32_t*)value;
        pfnbctx->fnb_value.filter_mask|=0x02;
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_fnb(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &pfnbctx->fnb_value, &cfg);
        context->cached_val=1;
        break;
    case AUTO_EFFECT_FNB_PARAM_USAGE:
        context->usage = *(int32_t*)value;
        ALOGD("%s:Set usage=%d",__func__,context->usage);
        // trigger start for new usage
        r=auto_effect_fnb_start(context);
        if (r==AUDCALPARAM_OK)
            context->state = EFFECT_STATE_ACTIVE;
        break;
    case AUTO_EFFECT_FNB_PARAM_ENABLE:
        if (*(int32_t*)value){
           r = auto_effect_fnb_enable(context);
        } else {
           r = auto_effect_fnb_disable(context);
        }
        break;
    case AUTO_EFFECT_FNB_PARAM_STATUS:
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

int auto_effect_fnb_init(effect_context_t *context){
    ALOGD("%s:",__func__);
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return -EINVAL;
    }

    audcalparam_cmd_base_cfg_t cfg={1,48000};// default reading cfg: acdb file, sr=48000Hz
    auto_effect_fnb_context_t * pfnbctx = (auto_effect_fnb_context_t *)context;

    int r=0;

    //set default usage:none
    context->usage=AUTO_EFFECT_USAGE_NOTSET;

    pfnbctx->fnb_value=(audcalparam_cmd_fnb_t){.balance=0,.fade=0,.filter_mask=0x0};// zero on error

    context->cached_val=0;
    ALOGD("%s:ret=%d",__func__,r);

    return r;
}

int auto_effect_fnb_enable(effect_context_t *context){
    ALOGD("%s:",__func__);

    int r=-1;
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return -EINVAL;
    }
    //use module_param to enable
    uint32_t enable=1;
    uint32_t pbuf_len=sizeof(enable);
    char* instance_name = get_fnb_en_inst_name(context->usage);
    audcalparam_cmd_module_param_cfg_t cfg={{0,48000},0,0,0};//use mid and pid from cfg file
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_module_param(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_SET, (uint8_t*)&enable, &pbuf_len, &cfg);

    ALOGD("%s:ret=%d",__func__,r);

    return r;

}

int auto_effect_fnb_disable(effect_context_t *context){
    ALOGD("%s:",__func__);
    int r=-1;

    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){

        return -EINVAL;
    }

    //use module_param
    uint32_t enable=0;
    uint32_t pbuf_len=sizeof(enable);
    char* instance_name = get_fnb_en_inst_name(context->usage);
    audcalparam_cmd_module_param_cfg_t cfg={{0,48000},0,0,0};//use mid and pid from cfg file
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_module_param(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_SET, (uint8_t*)&enable, &pbuf_len, &cfg);

    ALOGD("%s:ret=%d",__func__,r);

    return r;

}

int auto_effect_fnb_start(effect_context_t *context/*, output_context_t *output*/){
    ALOGD("%s:",__func__);

    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){

        return -EINVAL;
    }
    int r=-1;

    audcalparam_cmd_base_cfg_t cfg={0,48000};// default reading cfg: adsp, sr=48000Hz

    auto_effect_fnb_context_t * pfnbctx = (auto_effect_fnb_context_t *)context;

    char* instance_name = get_fnb_cmd_inst_name(context->usage);
    // set enable
    r=auto_effect_fnb_enable(context);
    if (context->cached_val){
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_fnb(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &pfnbctx->fnb_value, &cfg);
        ALOGD("%s:Values applied from cache %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    } else {
        pfnbctx->fnb_value.filter_mask=0x03;
        // read from ADPS
        if (instance_name[0]!='\0')
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_fnb(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_GET, &pfnbctx->fnb_value, &cfg);
        ALOGD("%s:Values read from ADSP %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    }

    ALOGD("%s:ret=%d",__func__,r);
    return r;
}

int auto_effect_fnb_stop(effect_context_t *context/*, output_context_t *output*/){
    ALOGD("%s:",__func__);
    int r=0;

    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){

        return -EINVAL;
    }
    //set disable
    r=auto_effect_fnb_disable(context);
    ALOGD("%s:ret=%d",__func__,r);
    return r;
}

