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

#include <stdlib.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <dlfcn.h>
#include "auto_effect_util.h"

auto_effect_lib_ctl_t auto_effect_lib_ctl_inst;

#define AUDCALPARAM_CFG_FILE "/vendor/etc/audcalparam_commands.cfg"

#define AUTO_EFFECT_CHTYPE_PRESET_LEN 6
const int auto_effect_chtype_preset[AUTO_EFFECT_CHTYPE_PRESET_LEN]= {AUTO_EFFECT_CH_TYPE_L,AUTO_EFFECT_CH_TYPE_R,AUTO_EFFECT_CH_TYPE_LFE,AUTO_EFFECT_CH_TYPE_C,AUTO_EFFECT_CH_TYPE_LS,AUTO_EFFECT_CH_TYPE_RS};

int auto_effect_util_lib_ctl_init(void){
    int r;
    const char *error = NULL;
    dlerror();

    auto_effect_lib_ctl_inst.effect_lib_ctl_handle=dlopen(LIB_AUDCALPARAM_API, RTLD_NOW);
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_handle == NULL || (error = dlerror()) != NULL) {
        ALOGE("Error when dlopen %s error=%s",LIB_AUDCALPARAM_API,error);
        return EXIT_FAILURE;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_init = (auto_effect_lib_ctl_init_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_session_init");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_init == NULL || (error = dlerror()) != NULL){
         ALOGE("Error when dlym audcalparam_session_init, %s",error);
         goto cleanup;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_deinit = (auto_effect_lib_ctl_deinit_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_session_deinit");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_deinit == NULL || (error = dlerror()) != NULL){
        ALOGE("Error when dlym audcalparam_session_deinit, %s",error);
        goto cleanup;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_bmt = (auto_effect_lib_ctl_cmd_bmt_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_cmd_bmt");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_bmt == NULL || (error = dlerror()) != NULL){
        ALOGE("Error when dlym effect_lib_ctl_cmd_bmt, %s",error);
        goto cleanup;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_fnb = (auto_effect_lib_ctl_cmd_fnb_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_cmd_fnb");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_fnb == NULL || (error = dlerror()) != NULL){
        ALOGE("Error when dlym effect_lib_ctl_cmd_fnb, %s",error);
        goto cleanup;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_delay = (auto_effect_lib_ctl_cmd_delay_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_cmd_delay");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_delay == NULL || (error = dlerror()) != NULL){
        ALOGE("Error when dlym effect_lib_ctl_cmd_delay, %s",error);
        goto cleanup;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_mute = (auto_effect_lib_ctl_cmd_mute_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_cmd_mute");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_mute == NULL || (error = dlerror()) != NULL){
        ALOGE("Error when dlym effect_lib_ctl_cmd_mute, %s",error);
        goto cleanup;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_gain = (auto_effect_lib_ctl_cmd_gain_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_cmd_gain");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_gain == NULL || (error = dlerror()) != NULL){
        ALOGE("Error when dlym effect_lib_ctl_cmd_gain, %s",error);
        goto cleanup;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_module_param = (auto_effect_lib_ctl_cmd_module_param_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_cmd_module_param");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_module_param == NULL || (error = dlerror()) != NULL){
        ALOGE("Error when dlym effect_lib_ctl_cmd_module_param, %s",error);
        goto cleanup;
    }
    auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_volume_idx = (auto_effect_lib_ctl_cmd_volume_idx_t)dlsym(auto_effect_lib_ctl_inst.effect_lib_ctl_handle, "audcalparam_cmd_volume_idx");
    if (auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_volume_idx == NULL || (error = dlerror()) != NULL){
        ALOGE("Error when dlym effect_lib_ctl_cmd_volume_idx, %s",error);
        goto cleanup;
    }
    r = auto_effect_lib_ctl_inst.effect_lib_ctl_init(&auto_effect_lib_ctl_inst.hsession, AUDCALPARAM_CFG_FILE, "apq8096-adp-agave-snd-card");
    ALOGD("Effect lib ctl init return %d",r);
    if (r==AUDCALPARAM_OK)
        return EXIT_SUCCESS;
cleanup:
    dlclose(auto_effect_lib_ctl_inst.effect_lib_ctl_handle);
    auto_effect_lib_ctl_inst.effect_lib_ctl_handle = NULL;

    return EXIT_FAILURE;
}

int auto_effect_util_get_usage(int usecase){
    switch (usecase){
    case USECASE_AUDIO_PLAYBACK_DEEP_BUFFER:
    case USECASE_AUDIO_PLAYBACK_LOW_LATENCY:
    case USECASE_AUDIO_PLAYBACK_MULTI_CH:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD:
#ifdef MULTIPLE_OFFLOAD_ENABLED
    case USECASE_AUDIO_PLAYBACK_OFFLOAD2:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD3:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD4:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD5:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD6:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD7:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD8:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD9:
#endif
    case USECASE_AUDIO_DIRECT_PCM_OFFLOAD:
#ifdef BUS_ADDRESS_ENABLED
    case USECASE_AUDIO_PLAYBACK_MEDIA:
#endif
        return AUTO_EFFECT_USAGE_MUSIC;
#ifdef BUS_ADDRESS_ENABLED
    // TODO: check navigation usage!
    case USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION:
        return AUTO_EFFECT_USAGE_NAVIGATION;
#endif
    default:
        return AUTO_EFFECT_USAGE_NOTSET;
    }
}

int auto_effect_util_chtype_init (int *pchtype, int len){
    int i,r=-1;
    if (pchtype==NULL)
        goto exit;
    for (i=0;i<AUTO_EFFECT_CHTYPE_PRESET_LEN;i++){
        pchtype[i] = auto_effect_chtype_preset[i];
    }
    for (i=AUTO_EFFECT_CHTYPE_PRESET_LEN;i<len;i++){
        pchtype[i] = i+1;
    }
exit:
    return r;
}

// must be aligend with audcalparam cfg file
const char* get_mute_cmd_inst_name(int usage){

    switch (usage)
    case AUTO_EFFECT_USAGE_MUSIC:
        return "popp_mute_auto";
    //TODO: add other usages

    return "";
}

// must be aligend with audcalparam cfg file
const char* get_gain_cmd_inst_name(int usage){

    switch (usage)
    case AUTO_EFFECT_USAGE_MUSIC:
        return "popp_gain_auto";
    //TODO: add other usages

    return "";
}

// must be aligend with audcalparam cfg file
const char* get_volume_cmd_inst_name(int usage)
{
    switch (usage) {
    case AUTO_EFFECT_USAGE_MUSIC:
        return "popp_volume_idx_auto";
    //TODO: add other usages
    }
    return "";
}

int auto_effect_volume_get_idx_by_chtype (int * pchtype, int chtype, int chtypelen){
    int j=-1, found=0;
    // for channels 0..chnum set mute
    for (j=0;j<chtypelen;j++){
        if (pchtype[j]==chtype){
            found=1;
            break;
        }
    }
    if (found)
        return j;
    else
        return -1;
}

int auto_effect_volume_update_adsp_from_ctx(audcalparam_cmd_volume_idx_t * pvolumectx, int usage){

    int r=-1;

    if (pvolumectx==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return r;
    }

    audcalparam_cmd_base_cfg_t cfg={0, 48000};// default reading cfg: adsp, sr=48000Hz

    char *volidx_instance_name = get_volume_cmd_inst_name(usage);

    if (volidx_instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_volume_idx(auto_effect_lib_ctl_inst.hsession,
            volidx_instance_name, AUDCALPARAM_SET, pvolumectx, &cfg);

    return r;
 }

int auto_effect_mute_update_adsp_from_ctx(audcalparam_cmd_mute_t * pmutectx, int usage){
    int r=-1;
    if (pmutectx==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return r;
    }

    audcalparam_cmd_base_cfg_t cfg={0,48000};
    char* instance_name = get_mute_cmd_inst_name(usage);
    ALOGD("%s: start with cmd instance %s",__func__,instance_name);
    // allocate space for temp mute vectors
    audcalparam_cmd_mute_t muteval;
    uint32_t val[AUDCALPARAM_CMD_MUTE_EL_NUM_MAX];
    uint32_t type[AUDCALPARAM_CMD_MUTE_EL_NUM_MAX];
    muteval.value = val;
    muteval.type = type;
    muteval.num_el = AUDCALPARAM_CMD_MUTE_EL_NUM_MAX;
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_mute(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_GET, &muteval, &cfg);
    if (r==AUDCALPARAM_OK){
        //store to ctx
        ALOGD("%s:Read num of channels %d",__func__, muteval.num_el);
        // apply value from ctx to temp val
        int i,j,apply=0;
        for (i=0;i<pmutectx->num_el;i++){
            for (j=0;j<muteval.num_el;j++){
                if (muteval.type[j]==pmutectx->type[i]){
                    // set value according to type
                    muteval.value[j]=pmutectx->value[i];
                    apply=1;
                    break;
                }
            }
        }
        if (apply)
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_mute(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &muteval, &cfg);
    }

    return r;
}

int auto_effect_mute_update_ctx_from_adsp( audcalparam_cmd_mute_t * pmutectx, int usage){

    int r=-1;
    if (pmutectx==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return r;
    }

    audcalparam_cmd_base_cfg_t cfg={0,48000};

    //set the instance name
    char* instance_name = get_mute_cmd_inst_name(usage);
    // update effect ctx reading from ADSP
    audcalparam_cmd_mute_t muteval;
    uint32_t val[AUDCALPARAM_CMD_MUTE_EL_NUM_MAX];
    uint32_t type[AUDCALPARAM_CMD_MUTE_EL_NUM_MAX];
    muteval.value = val;
    muteval.type = type;
    muteval.num_el = AUDCALPARAM_CMD_MUTE_EL_NUM_MAX;
    pmutectx->num_el=AUDCALPARAM_CMD_MUTE_EL_NUM_MAX;// read with maximum number of channels(memory is provided)
    if (instance_name[0]!='\0'){
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_mute(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_GET, &muteval, &cfg);
        if (r!=AUDCALPARAM_OK ){
            ALOGE("%s:Mute value reading error %d",__func__,r);
        } else {
            //store to ctx
            pmutectx->num_el= muteval.num_el;
            memcpy(pmutectx->value,muteval.value,muteval.num_el*sizeof(muteval.value[0]));
            memcpy(pmutectx->type,muteval.type,muteval.num_el*sizeof(muteval.type[0]));
            ALOGD("%s:Read num of channels %d",__func__, muteval.num_el);
#if AUTOEFFECTS_DBG
            ALOGD("%s:cmd inst name",__func__, instance_name);
            ALOGD("%s:Get Mute Value:num el=%d ",__func__, pmutectx->num_el);
            int i;
            for (i=0; i<pmutectx->num_el;i++){
                ALOGD("  chtype[%d]=%d ",i,pmutectx->type[i]);
                ALOGD("  chmute[%d]=%d ",i,pmutectx->value[i]);
            }
#endif
        }
    }
    return r;
}

int auto_effect_gain_update_adsp_from_ctx(audcalparam_cmd_gain_t *pgainctx, int usage){
    int r=-1;
    if (pgainctx==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return r;
    }

    audcalparam_cmd_base_cfg_t cfg={0,48000};
    char* instance_name = get_gain_cmd_inst_name(usage);
    ALOGD("%s: start with cmd instance %s",__func__,instance_name);
    // allocate space for temp gain vectors
    audcalparam_cmd_gain_t gainval;
    uint32_t val[AUDCALPARAM_CMD_MUTE_EL_NUM_MAX];
    uint32_t type[AUDCALPARAM_CMD_MUTE_EL_NUM_MAX];
    gainval.value = val;
    gainval.type = type;
    gainval.num_el = AUDCALPARAM_CMD_MUTE_EL_NUM_MAX;
    if (instance_name[0]!='\0')
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_gain(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_GET, &gainval, &cfg);
    if (r==AUDCALPARAM_OK){
        //store to ctx
        ALOGD("%s:Read num of channels %d",__func__, gainval.num_el);
        // apply value from ctx to temp val
        int i,j,apply=0;
        for (i=0;i<pgainctx->num_el;i++){
            for (j=0;j<gainval.num_el;j++){
                if (gainval.type[j]==pgainctx->type[i]){
                    // set value according to type
                    gainval.value[j]=pgainctx->value[i];
                    apply=1;
                    break;
                }
            }
        }
        if (apply)
            r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_gain(auto_effect_lib_ctl_inst.hsession,
                instance_name, AUDCALPARAM_SET, &gainval, &cfg);
    }

    return r;
}

int auto_effect_gain_update_ctx_from_adsp(audcalparam_cmd_gain_t *pgainctx, int usage){

    int r=-1;
    if (pgainctx==NULL || /*auto_effect_lib_ctl_inst==NULL ||*/ auto_effect_lib_ctl_inst.hsession==NULL){
        return r;
    }

    audcalparam_cmd_base_cfg_t cfg={0,48000};

    //set the instance name
    char* instance_name = get_gain_cmd_inst_name(usage);
    // update effect ctx reading from ADSP
    audcalparam_cmd_gain_t gainval;
    uint32_t val[AUDCALPARAM_CMD_MUTE_EL_NUM_MAX];
    uint32_t type[AUDCALPARAM_CMD_MUTE_EL_NUM_MAX];
    gainval.value = val;
    gainval.type = type;
    gainval.num_el = AUDCALPARAM_CMD_MUTE_EL_NUM_MAX;
    pgainctx->num_el=AUDCALPARAM_CMD_MUTE_EL_NUM_MAX;// read with maximum number of channels(memory is provided)
    if (instance_name[0]!='\0'){
        r=auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_gain(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_GET, &gainval, &cfg);
        if (r!=AUDCALPARAM_OK ){
            ALOGE("%s:Mute value reading error %d",__func__,r);
        } else {
            //store to ctx
            pgainctx->num_el= gainval.num_el;
            memcpy(pgainctx->value,gainval.value,gainval.num_el*sizeof(gainval.value[0]));
            memcpy(pgainctx->type,gainval.type,gainval.num_el*sizeof(gainval.type[0]));
            ALOGD("%s:Read num of channels %d",__func__, gainval.num_el);
#if AUTOEFFECTS_DBG
            ALOGD("%s:cmd inst name",__func__, instance_name);
            ALOGD("%s:Get Mute Value:num el=%d ",__func__, pgainctx->num_el);
            int i;
            for (i=0; i<pgainctx->num_el;i++){
                ALOGD("  chtype[%d]=%d ",i,pgainctx->type[i]);
                ALOGD("  chgain[%d]=%d ",i,pgainctx->value[i]);
            }
#endif
        }
    }
    return r;
}

int auto_effect_volume_update_ctx_from_adsp(audcalparam_cmd_volume_idx_t *pvolumectx, int usage){
    int r = -1;
    if (pvolumectx == NULL || auto_effect_lib_ctl_inst.hsession == NULL) {
       return -1;
    }

    audcalparam_cmd_base_cfg_t cfg={0, 48000};
    //get the instance name
    char *instance_name = get_volume_cmd_inst_name(usage);
    pvolumectx->num_el = AUDCALPARAM_CMD_VOL_IDX_EL_NUM_MAX;
    if (instance_name[0]!='\0'){
        r = auto_effect_lib_ctl_inst.effect_lib_ctl_cmd_volume_idx(auto_effect_lib_ctl_inst.hsession,
            instance_name, AUDCALPARAM_GET, pvolumectx, &cfg);
        if (r != AUDCALPARAM_OK) {
            ALOGE("%s: volume value reading error %d",__func__, r);
        }
    }
#if AUTOEFFECTS_DBG
    ALOGD("%s:cmd inst name",__func__, instance_name);
    ALOGD("%s:Get Volume:num el=%d ", __func__, pvolumectx->num_el);
    if (r == AUDCALPARAM_OK){
        int i;
        for (i = 0; i < pvolumectx->num_el; i++) {
            ALOGD("  value[%d]=%d ", i, pvolumectx->value[i]);
        }
    }
#endif

    return r;
}

