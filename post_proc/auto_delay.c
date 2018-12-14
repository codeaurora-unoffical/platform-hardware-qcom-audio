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

#define LOG_TAG "auto_effect_delay"

#include <stdlib.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/audio_effect.h>
#include "auto_delay.h"

/* Effect Delay UUID:  */
const effect_descriptor_t auto_effect_delay_descriptor = {
        {0x8ee2bb42, 0xa5e7, 0x11e8, 0x98d0, {0x52, 0x92, 0x69, 0xfb, 0x14, 0x59}}, // type
        {0xa31574a6, 0xa5e7, 0x11e8, 0x98d0, {0x52, 0x92, 0x69, 0xfb, 0x14, 0x59}}, // uuid
        EFFECT_CONTROL_API_VERSION,
        (EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_DEVICE_IND | EFFECT_FLAG_NO_PROCESS),
        0, /* TODO */
        1,
        "Delay",
        "Qualcomm Technologies, Inc.",
};

// must be aligend with audcalparam cfg file
static const char* get_delay_cmd_inst_name(int usage)
{
    switch (usage) {
    case AUTO_EFFECT_USAGE_MUSIC:
        return "copp_delay";
    //TODO: add other usages
    }
    return "";
}

static const char*  get_delay_en_inst_name(int usage){
    switch (usage){
    case AUTO_EFFECT_USAGE_MUSIC:
        return "delay_en_copp_general_playback";
    }
    return "";
}

static int auto_effect_delay_update_ctx_from_adsp(effect_context_t *context){
    int r = -1;
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        return r;
    }

    auto_effect_delay_context_t *pdelayctx = (auto_effect_delay_context_t *)context;
    audcalparam_cmd_base_cfg_t cfg={0, 48000};
    //get the instance name
    char *instance_name = get_delay_cmd_inst_name(context->usage);
    // allocate space for temp delay vectors
    audcalparam_cmd_delay_t delayval;
    delayval.num_el = AUDCALPARAM_CMD_DELAY_EL_NUM_MAX;
    delayval.mask_lsb = NULL;
    delayval.mask_msb =NULL;
    delayval.value = NULL;
    delayval.mask_lsb = (uint32_t*)calloc(AUDCALPARAM_CMD_DELAY_EL_NUM_MAX, sizeof(uint32_t));
    if (delayval.mask_lsb==NULL)
       goto free;
    delayval.mask_msb = (uint32_t*)calloc(AUDCALPARAM_CMD_DELAY_EL_NUM_MAX, sizeof(uint32_t));
    if (delayval.mask_msb==NULL)
       goto free;
    delayval.value = (uint32_t*)calloc(AUDCALPARAM_CMD_DELAY_EL_NUM_MAX, sizeof(uint32_t));
    if (delayval.value==NULL)
       goto free;
    ALOGD("%s:cmd inst name %s",__func__, instance_name);
    if (instance_name[0]!='\0')
        r = auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_delay(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_GET, &delayval, &cfg);
    if (r == AUDCALPARAM_OK) {
        // copy to context
        pdelayctx->delay_value.num_el= delayval.num_el;
        memcpy(pdelayctx->delay_value.value,delayval.value,delayval.num_el*sizeof(delayval.value[0]));
        memcpy(pdelayctx->delay_value.mask_lsb,delayval.mask_lsb,delayval.num_el*sizeof(delayval.mask_lsb[0]));
        memcpy(pdelayctx->delay_value.mask_msb,delayval.mask_msb,delayval.num_el*sizeof(delayval.mask_msb[0]));
#if AUTOEFFECTS_DBG
        ALOGD("%s:cmd inst name %s",__func__, instance_name);
        ALOGD("%s:Get Delay Value:num el=%d ", __func__, pdelayctx->delay_value.num_el);
        if (r==AUDCALPARAM_OK){
            int i;
            for (i = 0; i < pdelayctx->delay_value.num_el; i++) {
                ALOGD("  mask_lsb[%d]=%d ", i, pdelayctx->delay_value.mask_lsb[i]);
                ALOGD("  mask_msb[%d]=%d ", i, pdelayctx->delay_value.mask_msb[i]);
                ALOGD("  value[%d]=%d ", i, pdelayctx->delay_value.value[i]);
            }
        }
#endif
    }
    else {
        ALOGE("%s: delay value reading error %d", __func__, r);
    }
free:
    if (delayval.mask_lsb!=NULL)
        free(delayval.mask_lsb);
    if (delayval.mask_msb!=NULL)
        free(delayval.mask_msb);
    if (delayval.value!=NULL)
        free(delayval.value);
    return r;
}

int auto_effect_delay_get_parameter(effect_context_t *context, effect_param_t *p, uint32_t *size)
{
    int r = -1;
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        p->status = -EINVAL;
        return r;
    }

    auto_effect_delay_context_t *pdelayctx = (auto_effect_delay_context_t *)context;
    audcalparam_cmd_base_cfg_t cfg={0, 48000};

    // extracting from parameter
    int voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);
    int32_t *param_tmp = (int32_t *)p->data;
    int32_t param = *param_tmp++;
    void *value = p->data + voffset;
    int32_t vsize = p->vsize;
    p->status = 0;
    if (p->vsize < sizeof(uint32_t)) {
        p->status = -EINVAL;
        return r;
    }
    p->vsize = sizeof(uint32_t);

    *size = sizeof(effect_param_t) + voffset + p->vsize;

    switch (param) {
    case AUTO_EFFECT_DELAY_PARAM_ENABLE:
        if (context->state==EFFECT_STATE_ACTIVE)
            *(int32_t*)value = 1;
        else
            *(int32_t*)value = 0;
        r=0;
        break;
    case AUTO_EFFECT_DELAY_PARAM_STATUS:
        *(int32_t *)value = context->state;
        r=0;
        break;
    case AUTO_EFFECT_DELAY_PARAM_DELAY_MCH:{
        uint32_t vidx=0;
        uint32_t* pdata=(uint32_t*)value;
        r = auto_effect_delay_update_ctx_from_adsp(context);
        pdata[vidx] = pdelayctx->delay_value.num_el;
        vidx++;
        if (vsize / sizeof(int) < 3*pdelayctx->delay_value.num_el+1){
            ALOGE("%s: Not enough memory provided to read delay values (given=%d, exp=%d)", __func__, vsize / sizeof(int), pdelayctx->delay_value.num_el);
            break;
        } else {
            memcpy(&pdata[vidx], pdelayctx->delay_value.value, pdelayctx->delay_value.num_el*sizeof(int));
            vidx+=pdelayctx->delay_value.num_el;
            memcpy(&pdata[vidx], pdelayctx->delay_value.mask_lsb, pdelayctx->delay_value.num_el*sizeof(int));
            vidx+=pdelayctx->delay_value.num_el;
            memcpy(&pdata[vidx], pdelayctx->delay_value.mask_msb, pdelayctx->delay_value.num_el*sizeof(int));
            p->vsize = 3*pdelayctx->delay_value.num_el*sizeof(int)+4;
        }
        break;
    }
    case AUTO_EFFECT_DELAY_PARAM_USAGE:
        *(int32_t *)value = context->usage;
        break;
    default:
        ALOGE("%s:Unknown param %d!", __func__, param);
        p->status = -EINVAL;
        return r;
    }
    *size = sizeof(effect_param_t) + voffset + p->vsize;

    return r;
}

int auto_effect_delay_set_parameter(effect_context_t *context, effect_param_t *p, uint32_t size)
{
    ALOGD("%s:", __func__);

    int r = -1;
    p->status = 0;
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        p->status = -EINVAL;
        return -EINVAL;
    }

    auto_effect_delay_context_t *pdelayctx = (auto_effect_delay_context_t *)context;

    audcalparam_cmd_base_cfg_t cfg={0, 48000};

    // extracting from parameter
    int voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);
    int32_t *param_tmp = (int32_t *)p->data;
    int32_t param = *param_tmp++;
    void *value = p->data + voffset;
    int32_t vsize = p->vsize;

    // check size of value
    if (p->vsize / sizeof(int) > 3+AUDCALPARAM_CMD_DELAY_EL_NUM_MAX+1) {
        ALOGE("%s:Provided value size is %d, expected=%d!", __func__, p->vsize, sizeof(param));
        p->status = -EINVAL;
        return r;
    }
    ALOGD("%s: param=%d", __func__, param);
    //get the instance name
    char *instance_name = get_delay_cmd_inst_name(context->usage);
    //don't update context since the complete multichannel delay (all fields) param will be set
    switch (param) {
    case AUTO_EFFECT_DELAY_PARAM_ENABLE:
        if (*(int32_t*)value){
           r = auto_effect_delay_enable(context);
        } else {
           r = auto_effect_delay_disable(context);
        }
        break;
    case AUTO_EFFECT_DELAY_PARAM_STATUS:
        //empty
        break;
    case AUTO_EFFECT_DELAY_PARAM_DELAY_MCH:{
        ALOGD("%s: AUTO_EFFECT_DELAY_PARAM_DELAY_MCH", __func__);
        uint32_t vidx=0;
        uint32_t* pdata=(uint32_t*)value;
        // chnum
        if (pdata[vidx]>0 && pdata[vidx]<=AUDCALPARAM_CMD_DELAY_EL_NUM_MAX){
            pdelayctx->delay_value.num_el = pdata[vidx];
            vidx++;
        } else {
            ALOGE("%s: Num el read is invalid(=%d)", __func__, pdelayctx->delay_value.num_el);
            break;
        }
        if (vsize / sizeof(int) < 3*pdelayctx->delay_value.num_el+1) {
            ALOGE("%s: Not enough memory provided to read delay values (given=%d, exp=%d)", __func__, vsize / sizeof(int), pdelayctx->delay_value.num_el);
            break;
        } else {
            //copy delay values
            memcpy(pdelayctx->delay_value.value, &pdata[vidx], pdelayctx->delay_value.num_el*sizeof(int));
            vidx+= pdelayctx->delay_value.num_el;
            // copy mask lsb
            memcpy(pdelayctx->delay_value.mask_lsb, &pdata[vidx], pdelayctx->delay_value.num_el*sizeof(int));
            vidx+= pdelayctx->delay_value.num_el;
            // copy mask msb
            memcpy(pdelayctx->delay_value.mask_msb, &pdata[vidx], pdelayctx->delay_value.num_el*sizeof(int));
            if (instance_name[0]!='\0')
                r = auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_delay(auto_effect_lib_ctl_inst.hsession,
                    instance_name, AUDCALPARAM_SET, &pdelayctx->delay_value, &cfg);
            context->cached_val=1;
        }
        break;
    }
    case AUTO_EFFECT_DELAY_PARAM_USAGE:
        context->usage = *(int32_t *)value;
        ALOGD("%s:Set usage = %d", __func__, context->usage);
        // trigger start for new usage
        r=auto_effect_delay_start(context);
        if (r==AUDCALPARAM_OK)
            context->state = EFFECT_STATE_ACTIVE;
        break;
    default:
        ALOGE("%s:Unknown param %d!", __func__, param);
        p->status = -EINVAL;
        return r;
    }
    if (r != AUDCALPARAM_OK) {
        ALOGE("%s: Fail to set the parameter to ADSP", __func__);
    }

    return r;
}

int auto_effect_delay_init(effect_context_t *context)
{
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        return -EINVAL;
    }

    auto_effect_delay_context_t *pdelayctx = (auto_effect_delay_context_t *)context;

    // allocate space for Delay vectors
    // pdelayctx->delay_value.num_el = AUDCALPARAM_CMD_DELAY_EL_NUM_MAX;
    pdelayctx->delay_value.num_el = 0;
    pdelayctx->delay_value.mask_lsb = (uint32_t*)calloc(AUDCALPARAM_CMD_DELAY_EL_NUM_MAX, sizeof(uint32_t));
    if (pdelayctx->delay_value.mask_lsb==NULL)
       goto error;
    pdelayctx->delay_value.mask_msb = (uint32_t*)calloc(AUDCALPARAM_CMD_DELAY_EL_NUM_MAX, sizeof(uint32_t));
    if ( pdelayctx->delay_value.mask_msb==NULL)
       goto error;
    pdelayctx->delay_value.value = (uint32_t*)calloc(AUDCALPARAM_CMD_DELAY_EL_NUM_MAX, sizeof(uint32_t));
    if (pdelayctx->delay_value.value==NULL)
       goto error;
    //set default usage
    context->usage = AUTO_EFFECT_USAGE_NOTSET;
    context->cached_val=0;
    ALOGD("%s:Init OK",__func__);
    return 0;
error:
   if (pdelayctx->delay_value.value!=NULL){
        free(pdelayctx->delay_value.value);
        pdelayctx->delay_value.value=NULL;
    }
    if (pdelayctx->delay_value.mask_lsb!=NULL){
        free(pdelayctx->delay_value.mask_lsb);
        pdelayctx->delay_value.mask_lsb=NULL;
    }
    if (pdelayctx->delay_value.mask_msb!=NULL){
        free(pdelayctx->delay_value.mask_msb);
        pdelayctx->delay_value.mask_msb=NULL;
    }
    ALOGD("%s:Init NOK",__func__);
    return -1;

}

int auto_effect_delay_enable(effect_context_t *context){

    int r=-1;
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return -EINVAL;
    }
    //use module_param to enable
    uint32_t enable=1;
    uint32_t pbuf_len=sizeof(enable);
    char* instance_name = get_delay_en_inst_name(context->usage);
    audcalparam_cmd_module_param_cfg_t cfg={{0,48000},0,0,0};//use mid and pid from cfg file
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_module_param(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_SET, (uint8_t*)&enable, &pbuf_len, &cfg);

    ALOGD("%s:ret=%d",__func__,r);

    return r;

}

int auto_effect_delay_disable(effect_context_t *context){
    int r=-1;
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return -EINVAL;
    }
    //use module_param to enable
    uint32_t enable=0;
    uint32_t pbuf_len=sizeof(enable);
    char* instance_name = get_delay_en_inst_name(context->usage);
    audcalparam_cmd_module_param_cfg_t cfg={{0,48000},0,0,0};//use mid and pid from cfg file
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_module_param(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_SET, (uint8_t*)&enable, &pbuf_len, &cfg);

    ALOGD("%s:ret=%d",__func__,r);

    return r;

}

int auto_effect_delay_start(effect_context_t *context)
{
    ALOGD("%s:", __func__);

    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        return -EINVAL;
    }

    int r = -1;
    audcalparam_cmd_base_cfg_t cfg={0, 48000};// default reading cfg: adsp, sr=48000Hz

    auto_effect_delay_context_t *pdelayctx = (auto_effect_delay_context_t *)context;
    char *instance_name = get_delay_cmd_inst_name(context->usage);
    // set enable
    r=auto_effect_delay_enable(context);
    ALOGD("%s:Enable ret=%d", __func__, r);

    if (context->cached_val){
        if (r==AUDCALPARAM_OK){
            // store to ADSP from ctx
            ALOGD("%s:Try to apply cached values",__func__);
            if (instance_name[0]!='\0')
                r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_delay(auto_effect_lib_ctl_inst.hsession,
                    instance_name, AUDCALPARAM_SET, &pdelayctx->delay_value, &cfg);
        }
        ALOGD("%s:Values applied from cache %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    } else {
        if (r==AUDCALPARAM_OK){
            r = auto_effect_delay_update_ctx_from_adsp(context);
        }
        ALOGD("%s:Values read from ADSP %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    }

    ALOGD("%s: return = %d", __func__, r);
    return r;
}

int auto_effect_delay_stop(effect_context_t *context)
{
    ALOGD("%s:", __func__);
    int r;
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        return -EINVAL;
    }
    //set disable
    r=auto_effect_delay_disable(context);
    ALOGD("%s:ret=%d",__func__,r);
    return r;
}

int auto_effect_delay_release(effect_context_t *context){
    ALOGD("%s:",__func__);
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return -EINVAL;
    }

    auto_effect_delay_context_t * pdelayctx = (auto_effect_delay_context_t *)context;

    int r=0;
    // release space for delay vectors
    if (pdelayctx->delay_value.value!=NULL){
        free(pdelayctx->delay_value.value);
        pdelayctx->delay_value.value=NULL;
    }
    if (pdelayctx->delay_value.mask_lsb!=NULL){
        free(pdelayctx->delay_value.mask_lsb);
        pdelayctx->delay_value.mask_lsb=NULL;
    }
    if (pdelayctx->delay_value.mask_msb!=NULL){
        free(pdelayctx->delay_value.mask_msb);
        pdelayctx->delay_value.mask_msb=NULL;
    }

    ALOGD("%s:ret=%d",__func__,r);

    return r;
}
