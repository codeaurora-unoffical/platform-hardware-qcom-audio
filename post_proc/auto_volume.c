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

#define LOG_TAG "auto_effect_volume"

#include <stdlib.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/audio_effect.h>
#include "auto_volume.h"

/* Effect Volume UUID:  */
const effect_descriptor_t auto_effect_volume_descriptor = {
        {0x2000a924, 0x9a1f, 0x11e8, 0x9eb6, {0x52, 0x92, 0x69, 0xfb, 0x14, 0x59}}, // type
        {0x3d844dd4, 0xa367, 0x11e8, 0x9eb6, {0x52, 0x92, 0x69, 0xfb, 0x14, 0x59}}, // uuid
        EFFECT_CONTROL_API_VERSION,
        (EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_DEVICE_IND | EFFECT_FLAG_NO_PROCESS),
        0, /* TODO */
        1,
        "Volume",
        "Qualcomm Technologies, Inc.",
};

int auto_effect_volume_get_parameter(effect_context_t *context, effect_param_t *p, uint32_t *size)
{
    int r = -1;
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        p->status = -EINVAL;
        return -1;
    }

    auto_effect_volume_context_t * pvolumectx = (auto_effect_volume_context_t *)context;
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
        return -1;
    }
    p->vsize = sizeof(uint32_t);

    *size = sizeof(effect_param_t) + voffset + p->vsize;

    switch (param) {
    case AUTO_EFFECT_VOLUME_PARAM_STATUS:
        *(int32_t*)value=context->state;
        r=0;
        break;
    case AUTO_EFFECT_VOLUME_PARAM_VOL_INDEX:
        r = auto_effect_volume_update_ctx_from_adsp(&pvolumectx->volume_value ,context->usage);
        if (vsize / sizeof(int) >= pvolumectx->volume_value.num_el)
            vsize = pvolumectx->volume_value.num_el * sizeof(int);
        else
            ALOGE("%s: Not enough memory provided to read volume values (given=%d, exp=%d)", __func__, vsize / sizeof(int), pvolumectx->volume_value.num_el);
        memcpy(value, pvolumectx->volume_value.value, vsize);
        p->vsize = vsize;
        break;
    case AUTO_EFFECT_GAIN_PARAM_GAIN:{
        uint32_t vidx=0;
        uint32_t* pdata=(uint32_t*)value;
        r = auto_effect_gain_update_ctx_from_adsp(&pvolumectx->gain_value, context->usage);
        pdata[vidx] = pvolumectx->gain_value.num_el;
        vidx++;
        if (vsize / sizeof(int) < 2*pvolumectx->gain_value.num_el+1){
            ALOGE("%s: Not enough memory provided to read gain values (given=%d, exp=%d)", __func__, vsize / sizeof(int), pvolumectx->gain_value.num_el);
            break;
        } else {
            memcpy(&pdata[vidx], pvolumectx->gain_value.value,pvolumectx->gain_value.num_el*sizeof(int));
            vidx+=pvolumectx->gain_value.num_el;
            memcpy(&pdata[vidx], pvolumectx->gain_value.type,  pvolumectx->gain_value.num_el*sizeof(int));
            p->vsize = 2*pvolumectx->gain_value.num_el*sizeof(int)+4;
        }
    }
        break;
    case AUTO_EFFECT_MUTE_PARAM_MUTE:{
        uint32_t vidx=0;
        uint32_t* pdata=(uint32_t*)value;
        r = auto_effect_mute_update_ctx_from_adsp(&pvolumectx->mute_value, context->usage);
        pdata[vidx] = pvolumectx->mute_value.num_el;
        vidx++;
        if (vsize / sizeof(int) < 2*pvolumectx->mute_value.num_el+1){
            ALOGE("%s: Not enough memory provided to read mute values (given=%d, exp=%d)", __func__, vsize / sizeof(int), pvolumectx->mute_value.num_el);
            p->vsize = sizeof(int);//only num ch can be returned
            break;
        } else {
            memcpy(&pdata[vidx], pvolumectx->mute_value.value, pvolumectx->mute_value.num_el*sizeof(int));
            vidx+=pvolumectx->mute_value.num_el;
            memcpy(&pdata[vidx], pvolumectx->mute_value.type,  pvolumectx->mute_value.num_el*sizeof(int));
            p->vsize = 2*pvolumectx->mute_value.num_el*sizeof(int)+4;// TODO: test corner-case length of returned arrays by providing max channel num!!!
        }
    }
        break;
    case AUTO_EFFECT_VOLUME_PARAM_USAGE:
        *(int32_t*)value = context->usage;
        break;
    default:
        ALOGE("%s:Unknown param %d!", __func__, param);
        p->status = -EINVAL;
        return r;
    }
    *size = sizeof(effect_param_t) + voffset + p->vsize;
    return r;
}

int auto_effect_volume_set_parameter(effect_context_t *context, effect_param_t *p, uint32_t size)
{
    int r = -1;
    p->status = 0;
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        p->status = -EINVAL;
        return -EINVAL;
    }

    auto_effect_volume_context_t *pvolumectx = (auto_effect_volume_context_t *)context;
    audcalparam_cmd_base_cfg_t cfg={0, 48000};

    // extracting from parameter
    int voffset = ((p->psize - 1) / sizeof(int32_t) + 1) * sizeof(int32_t);
    int32_t *param_tmp = (int32_t *)p->data;
    int32_t param = *param_tmp++;
    void *value = p->data + voffset;
    int32_t vsize = p->vsize;

    //get the instance name
    char *instance_name = get_volume_cmd_inst_name(context->usage);
    switch (param) {
    case AUTO_EFFECT_VOLUME_PARAM_STATUS:
        r=0;
        break;
    case AUTO_EFFECT_VOLUME_PARAM_VOL_INDEX:
        r = auto_effect_volume_update_ctx_from_adsp(&pvolumectx->volume_value, context->usage);
        if (vsize / sizeof(int) >= pvolumectx->volume_value.num_el)
            vsize = pvolumectx->volume_value.num_el * sizeof(int);
        else
            ALOGE("%s: Not enough memory provided to read volume values (given=%d, exp=%d)", __func__, vsize / sizeof(int), pvolumectx->volume_value.num_el);
        memcpy(pvolumectx->volume_value.value, value, vsize);
        if (r == AUDCALPARAM_OK) {
            if (instance_name[0]!='\0')
                r = auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_volume_idx(auto_effect_lib_ctl_inst.hsession,
                    instance_name, AUDCALPARAM_SET, &pvolumectx->volume_value, &cfg);
        }
        context->cached_val|=AUTO_EFFECT_CACHED_VOL_IDX;
        break;
    case AUTO_EFFECT_GAIN_PARAM_GAIN:{
        uint32_t i;
        uint32_t vidx=0;
        uint32_t* pdata=(uint32_t*)value;
        // chnum
        uint32_t chnum = pdata[vidx++];
        r = auto_effect_gain_update_ctx_from_adsp(&pvolumectx->gain_value, context->usage);
        if (chnum<=0 || chnum>AUDCALPARAM_CMD_GAIN_EL_NUM_MAX){
            ALOGE("%s: Num el read is invalid(=%d)", __func__, pvolumectx->gain_value.num_el);
            break;
        }
        uint32_t* pgain = (uint32_t*)&pdata[vidx];
        vidx += chnum;
        uint32_t* pchtype = (uint32_t*)&pdata[vidx];
        // for channels 0..chnum set gain
        for (i=0;i<chnum;i++){
            int j=-1;
            j=auto_effect_volume_get_idx_by_chtype (pvolumectx->gain_value.type, pchtype[i], pvolumectx->gain_value.num_el);
            if (j>=0)
                pvolumectx->gain_value.value[j] = pgain[i];
        }
        instance_name = get_gain_cmd_inst_name(context->usage);
        if (r==AUDCALPARAM_OK){
            r = auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_gain(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &pvolumectx->gain_value, &cfg);
        }
        context->cached_val|=AUTO_EFFECT_CACHED_GAIN;
    }
        break;
    case AUTO_EFFECT_MUTE_PARAM_MUTE:{
        uint32_t i,j;
        uint32_t vidx=0;
        uint32_t* pdata=(uint32_t*)value;
        // chnum
        uint32_t chnum = pdata[vidx++];
        r = auto_effect_mute_update_ctx_from_adsp(&pvolumectx->mute_value, context->usage);
        if (chnum<=0 || chnum>AUDCALPARAM_CMD_MUTE_EL_NUM_MAX){
            ALOGE("%s: Num el read is invalid(=%d)", __func__, pvolumectx->mute_value.num_el);
            break;
        }
        uint32_t* pmute = (uint32_t*)&pdata[vidx];
        vidx += chnum;
        uint32_t* pchtype = (uint32_t*)&pdata[vidx];
        // for channels 0..chnum set mute
        for (i=0;i<chnum;i++){
            int j=-1;
            j = auto_effect_volume_get_idx_by_chtype (pvolumectx->mute_value.type, pchtype[i], pvolumectx->mute_value.num_el);
            if (j>=0)
                pvolumectx->mute_value.value[j] = pmute[i];
        }
        instance_name = get_mute_cmd_inst_name(context->usage);
        if (r==AUDCALPARAM_OK){
            r = auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_mute(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &pvolumectx->mute_value, &cfg);
        }
        context->cached_val|=AUTO_EFFECT_CACHED_MUTE;
    }
        break;
    case AUTO_EFFECT_VOLUME_PARAM_USAGE:
        context->usage = *(int32_t *)value;
        ALOGD("%s:Set usage = %d", __func__, context->usage);
        // trigger start for new usage
        r=auto_effect_volume_start(context);
        if (r==AUDCALPARAM_OK)
            context->state = EFFECT_STATE_ACTIVE;
        break;
    default:
        ALOGE("%s: Unknown param %d!", __func__, param);
        p->status = -EINVAL;
        return r;
    }
    if (r != AUDCALPARAM_OK) {
        ALOGE("%s: Fail to set the parameter to ADSP", __func__);
    }

    return r;
}

int auto_effect_volume_init(effect_context_t *context)
{
    ALOGD("%s:", __func__);
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        return -EINVAL;
    }

    auto_effect_volume_context_t *pvolumectx = (auto_effect_volume_context_t *)context;

    // allocate space for volume vectors
    // pvolumectx->volume_value.num_el = AUDCALPARAM_CMD_VOL_IDX_EL_NUM_MAX;
    pvolumectx->volume_value.num_el = 0;
    pvolumectx->volume_value.value = (uint32_t*)calloc(AUDCALPARAM_CMD_VOL_IDX_EL_NUM_MAX, sizeof(uint32_t));
    // gain
    pvolumectx->gain_value.value = (uint32_t*)calloc(AUDCALPARAM_CMD_GAIN_EL_NUM_MAX, sizeof(uint32_t));
    pvolumectx->gain_value.type = (uint32_t*)calloc(AUDCALPARAM_CMD_GAIN_EL_NUM_MAX, sizeof(uint32_t));
    // pvolumectx->gain_value.num_el = AUDCALPARAM_CMD_GAIN_EL_NUM_MAX;
    pvolumectx->gain_value.num_el = 0;
    // set default channel type
    auto_effect_util_chtype_init( pvolumectx->gain_value.type,  AUDCALPARAM_CMD_GAIN_EL_NUM_MAX);
    //mute
    // allocate space for mute vectors
    pvolumectx->mute_value.value = (uint32_t*)calloc(AUDCALPARAM_CMD_MUTE_EL_NUM_MAX, sizeof(uint32_t));
    pvolumectx->mute_value.type = (uint32_t*)calloc(AUDCALPARAM_CMD_MUTE_EL_NUM_MAX, sizeof(uint32_t));
    // pvolumectx->mute_value.num_el = AUDCALPARAM_CMD_MUTE_EL_NUM_MAX;
    pvolumectx->mute_value.num_el = 0;
    auto_effect_util_chtype_init( pvolumectx->mute_value.type,  AUDCALPARAM_CMD_MUTE_EL_NUM_MAX);

    //set default usage
    context->usage=AUTO_EFFECT_USAGE_NOTSET;
    context->cached_val=0;

    return 0;
}

int auto_effect_volume_start(effect_context_t *context)
{
    ALOGD("%s:", __func__);
    //change state moved to bundle
    if (context == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
        return -EINVAL;
    }

    int r = -1;
    audcalparam_cmd_base_cfg_t cfg={0, 48000};// default reading cfg: adsp, sr=48000Hz

    auto_effect_volume_context_t *pvolumectx = (auto_effect_volume_context_t *)context;

    if (context->cached_val&AUTO_EFFECT_CACHED_VOL_IDX){
        r = auto_effect_volume_update_adsp_from_ctx(&pvolumectx->volume_value, context->usage);
        ALOGD("%s:Vol idx applied from cache %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    } else {
        // store volume values to ctx
        r = auto_effect_volume_update_ctx_from_adsp(&pvolumectx->volume_value, context->usage);
        ALOGD("%s:Vol idx read from ADSP %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    }

    if (context->cached_val&AUTO_EFFECT_CACHED_GAIN){
        r = auto_effect_gain_update_adsp_from_ctx (&pvolumectx->gain_value, context->usage);
        ALOGD("%s:Gain applied from cache %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    } else {
        r = auto_effect_gain_update_ctx_from_adsp(&pvolumectx->gain_value, context->usage);
        ALOGD("%s:Gain read from ADSP %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    }

    if (context->cached_val&AUTO_EFFECT_CACHED_MUTE){
        r = auto_effect_mute_update_adsp_from_ctx (&pvolumectx->mute_value, context->usage);
        ALOGD("%s:Mute applied from cache %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    } else {
        r = auto_effect_mute_update_ctx_from_adsp(&pvolumectx->mute_value, context->usage);
        ALOGD("%s:Mute read from ADSP %s",__func__,r==AUDCALPARAM_OK?"OK":"NOK");
    }

    ALOGD("%s: return = %d", __func__, r);

    return r;
}

int auto_effect_volume_release(effect_context_t *context){
    ALOGD("%s:",__func__);
    if (context==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return -EINVAL;
    }

    auto_effect_volume_context_t * pvolumectx = (auto_effect_volume_context_t *)context;

    int r=0;
    // release space for volume vectors
    if (pvolumectx->volume_value.value!=NULL){
        free(pvolumectx->volume_value.value);
        pvolumectx->volume_value.value=NULL;
    }
    // release space for gain vectors
    if (pvolumectx->gain_value.value!=NULL){
        free(pvolumectx->gain_value.value);
        pvolumectx->gain_value.value=NULL;
    }
    if (pvolumectx->gain_value.type!=NULL){
        free(pvolumectx->gain_value.type);
        pvolumectx->gain_value.type=NULL;
    }
    // release space for mute vectors
    if (pvolumectx->mute_value.value!=NULL){
        free(pvolumectx->mute_value.value);
        pvolumectx->mute_value.value=NULL;
    }
    if (pvolumectx->mute_value.type!=NULL){
        free(pvolumectx->mute_value.type);
        pvolumectx->mute_value.type=NULL;
    }

    ALOGD("%s:ret=%d",__func__,r);

    return r;
}
