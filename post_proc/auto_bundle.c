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

#define LOG_TAG "auto_effects_bundle"
//#define LOG_NDEBUG 0

#include <stdlib.h>
#include <cutils/list.h>
#include <cutils/log.h>
#include <system/thread_defs.h>
#include <hardware/audio_effect.h>

#include "auto_bundle.h"

#include "auto_effect_util.h"
#include "auto_bmt.h"
#include "auto_volume.h"
#include "auto_fnb.h"
#include "auto_delay.h"
#include "auto_mute.h"
#include "auto_gain.h"

const effect_descriptor_t *descriptors[] = {
        &auto_effect_bmt_descriptor,
        &auto_effect_volume_descriptor,
        &auto_effect_fnb_descriptor,
        &auto_effect_delay_descriptor,
        NULL,
};

pthread_once_t once = PTHREAD_ONCE_INIT;
int init_status;
/*
 * list of created effects.
 * Updated by offload_effects_bundle_hal_start_output()
 * and offload_effects_bundle_hal_stop_output()
 */
struct listnode created_effects_list;
/*
 * list of active output streams.
 * Updated by offload_effects_bundle_hal_start_output()
 * and offload_effects_bundle_hal_stop_output()
 */
struct listnode active_outputs_list;
/*
 * lock must be held when modifying or accessing
 * created_effects_list or active_outputs_list
 */
pthread_mutex_t lock;


/*
 *  Local functions
 */
static void init_once() {
    list_init(&created_effects_list);
    list_init(&active_outputs_list);
    auto_effect_util_lib_ctl_init();
    pthread_mutex_init(&lock, NULL);

    init_status = 0;
}

int lib_init()
{
    pthread_once(&once, init_once);
    return init_status;
}

bool effect_exists(effect_context_t *context)
{
    struct listnode *node;

    list_for_each(node, &created_effects_list) {
        effect_context_t *fx_ctxt = node_to_item(node,
                                                 effect_context_t,
                                                 effects_list_node);
        if (fx_ctxt == context) {
            return true;
        }
    }
    return false;
}

output_context_t *get_output(audio_io_handle_t output)
{
    struct listnode *node;

    list_for_each(node, &active_outputs_list) {
        output_context_t *out_ctxt = node_to_item(node,
                                                  output_context_t,
                                                  outputs_list_node);
        if (out_ctxt->handle == output)
            return out_ctxt;
    }
    return NULL;
}

void add_effect_to_output(output_context_t * output, effect_context_t *context)
{
    struct listnode *fx_node;

    ALOGD("%s: e_ctxt %p, o_ctxt %p", __func__, context, output);
    list_for_each(fx_node, &output->effects_list) {
        effect_context_t *fx_ctxt = node_to_item(fx_node,
                                                 effect_context_t,
                                                 output_node);
        if (fx_ctxt == context)
            return;
    }
    list_add_tail(&output->effects_list, &context->output_node);
    if (context->ops.start){
        int r = context->ops.start(context/*, output*/);
        if (r==AUDCALPARAM_OK){
            // change state on succesfull reading from adsp
            context->state = EFFECT_STATE_ACTIVE;
            ALOGD("%s:Effect started for output",__func__);
        }
    }
}

void remove_effect_from_output(output_context_t * output,
                               effect_context_t *context)
{
    struct listnode *fx_node;

    ALOGD("%s: e_ctxt %p, o_ctxt %p", __func__, context, output);
    list_for_each(fx_node, &output->effects_list) {
        effect_context_t *fx_ctxt = node_to_item(fx_node,
                                                 effect_context_t,
                                                 output_node);
        if (fx_ctxt == context) {
            if (context->ops.stop)
                context->ops.stop(context/*, output*/);
            fx_ctxt->state = EFFECT_STATE_INITIALIZED;
            fx_ctxt->cached_val=0;
            list_remove(&context->output_node);
            return;
        }
    }
}

/*
 * Interface from audio HAL
 */
__attribute__ ((visibility ("default")))
int auto_effects_bundle_hal_start_output(audio_io_handle_t output, int usecase)
{
    int ret = 0;
    struct listnode *node;
    output_context_t * out_ctxt = NULL;

    ALOGD("%s: output %d, usecase %d, usage=%d", __func__, output, usecase, auto_effect_util_get_usage(usecase));

    if (lib_init() != 0)
        return init_status;

    pthread_mutex_lock(&lock);
    if (get_output(output) != NULL) {
        ALOGE("%s output already started", __func__);
        ret = -ENOSYS;
        goto exit;
    }
    // allocate output ctx
    out_ctxt = (output_context_t *)
                                 malloc(sizeof(output_context_t));
    if (!out_ctxt) {
        ALOGE("%s fail to allocate for output context", __func__);
        ret = -ENOMEM;
        goto exit;
    }
    out_ctxt->handle = output;
    out_ctxt->usecase = usecase;

    list_init(&out_ctxt->effects_list);
    // start effect with ioId==output, create effect list for the output
    list_for_each(node, &created_effects_list) {
        effect_context_t *fx_ctxt = node_to_item(node,
                                                 effect_context_t,
                                                 effects_list_node);
        if (fx_ctxt->out_handle == output) {
            if (fx_ctxt->ops.start){
                fx_ctxt->usage=auto_effect_util_get_usage(usecase);
                ret=fx_ctxt->ops.start(fx_ctxt/*, out_ctxt */);
                if (!ret){
                    fx_ctxt->state = EFFECT_STATE_ACTIVE;
                    ALOGD("%s:state changed to active",__func__);
                }
            }
            list_add_tail(&out_ctxt->effects_list, &fx_ctxt->output_node);
        }
        // start non active effects with other ioId(sessionId=0) and usecase==usecase
        else if (fx_ctxt->sessionId==0 && fx_ctxt->usage==auto_effect_util_get_usage(usecase) && fx_ctxt->state == EFFECT_STATE_INITIALIZED) {
            if (fx_ctxt->ops.start){
                // usage was set explicitly on effect construction
                ret = fx_ctxt->ops.start(fx_ctxt/*, out_ctxt */);
                if (!ret){
                    fx_ctxt->state = EFFECT_STATE_ACTIVE;
                    ALOGD("%s:state changed to active",__func__);
                }
            }
        }
    }
    list_add_tail(&active_outputs_list, &out_ctxt->outputs_list_node);

exit:
    pthread_mutex_unlock(&lock);
    return ret;
}

__attribute__ ((visibility ("default")))
int auto_effects_bundle_hal_stop_output(audio_io_handle_t output, int usecase)
{
    int ret = 0;
    // struct listnode *node;
    struct listnode *fx_node;
    output_context_t *out_ctxt;

    ALOGD("%s output %d, usecase %d", __func__, output, usecase);

    if (lib_init() != 0)
        return init_status;

    pthread_mutex_lock(&lock);
    // get output context with ioId=output
    out_ctxt = get_output(output);
    if (out_ctxt == NULL) {
        ALOGE("%s output not started", __func__);
        ret = -ENOSYS;
        goto exit;
    }
    // stop all effects attached to the output
    list_for_each(fx_node, &out_ctxt->effects_list) {
        effect_context_t *fx_ctxt = node_to_item(fx_node,
                                                 effect_context_t,
                                                 output_node);
        if (fx_ctxt->ops.stop)
            fx_ctxt->ops.stop(fx_ctxt/*, out_ctxt*/);
        fx_ctxt->state = EFFECT_STATE_INITIALIZED;
        ALOGD("%s:state changed to initialized",__func__);
    }
    list_remove(&out_ctxt->outputs_list_node);
    free(out_ctxt);

    // stop all active effects with the same usecase and sessionId==0
    list_for_each(fx_node, &created_effects_list) {
        effect_context_t *fx_ctxt = node_to_item(fx_node,
                                                 effect_context_t,
                                                 effects_list_node);
        if (fx_ctxt->sessionId==0 && fx_ctxt->usage==auto_effect_util_get_usage(usecase) && fx_ctxt->state == EFFECT_STATE_ACTIVE){
            if (fx_ctxt->ops.stop)
                fx_ctxt->ops.stop(fx_ctxt/*, out_ctxt*/);
            fx_ctxt->state = EFFECT_STATE_INITIALIZED;
            // fx_ctxt->cached_val=0; // don't set to zero to reapply the value next time the usecase active
            ALOGD("%s:state changed to initialized",__func__);
        }

    }

exit:
    pthread_mutex_unlock(&lock);
    return ret;
}

/*
 * Effect operations
 */
int set_config(effect_context_t *context, effect_config_t *config)
{
    context->config = *config;

    if (context->ops.reset)
        context->ops.reset(context);

    return 0;
}

void get_config(effect_context_t *context, effect_config_t *config)
{
    *config = context->config;
}


/*
 * Effect Library Interface Implementation
 */
int effect_lib_create(const effect_uuid_t *uuid,
                         int32_t sessionId,
                         int32_t ioId,
                         effect_handle_t *pHandle) {
    int ret;
    int i;

    ALOGD("%s: sessionId: %d, ioId: %d", __func__, sessionId, ioId);
    if (lib_init() != 0)
        return init_status;

    if (pHandle == NULL || uuid == NULL)
        return -EINVAL;

    for (i = 0; descriptors[i] != NULL; i++) {
        if (memcmp(uuid, &descriptors[i]->uuid, sizeof(effect_uuid_t)) == 0)
            break;
    }

    if (descriptors[i] == NULL)
        return -EINVAL;

    effect_context_t *context;

    if (memcmp(uuid, &auto_effect_bmt_descriptor.uuid, sizeof(effect_uuid_t)) == 0) {

        auto_effect_bmt_context_t *bmt_ctxt = (auto_effect_bmt_context_t *) calloc(1, sizeof(auto_effect_bmt_context_t));


        if (bmt_ctxt == NULL) {
            return ENOMEM;
        }
        context = (effect_context_t *)bmt_ctxt;
        context->ops.init = auto_effect_bmt_init;
        context->ops.set_parameter = auto_effect_bmt_set_parameter;
        context->ops.get_parameter = auto_effect_bmt_get_parameter;
        context->ops.enable = auto_effect_bmt_enable;
        context->ops.disable = auto_effect_bmt_disable;
        context->ops.start = auto_effect_bmt_start;
        context->ops.stop = auto_effect_bmt_stop;
        context->desc = &auto_effect_bmt_descriptor;
        ALOGD("AUTO BMT Effect created");
    } else if (memcmp(uuid, &auto_effect_volume_descriptor.uuid, sizeof(effect_uuid_t)) == 0) {
        auto_effect_volume_context_t *volume_ctxt = (auto_effect_volume_context_t *)calloc(1, sizeof(auto_effect_volume_context_t));
        if (volume_ctxt == NULL) {
            return -ENOMEM;
        }

        context = (effect_context_t *)volume_ctxt;
        context->ops.init = auto_effect_volume_init;
        context->ops.set_parameter = auto_effect_volume_set_parameter;
        context->ops.get_parameter = auto_effect_volume_get_parameter;
        context->ops.start = auto_effect_volume_start;
        context->ops.release = auto_effect_volume_release;
        context->desc = &auto_effect_volume_descriptor;
        ALOGD("%s: AUTO Volume Effect created", __func__);
    } else if (memcmp(uuid, &auto_effect_fnb_descriptor.uuid, sizeof(effect_uuid_t)) == 0) {

        auto_effect_fnb_context_t *fnb_ctxt = (auto_effect_fnb_context_t *) calloc(1, sizeof(auto_effect_fnb_context_t));

        if (fnb_ctxt == NULL) {
            return ENOMEM;
        }
        context = (effect_context_t *)fnb_ctxt;
        context->ops.init = auto_effect_fnb_init;
        context->ops.set_parameter = auto_effect_fnb_set_parameter;
        context->ops.get_parameter = auto_effect_fnb_get_parameter;
        context->ops.enable = auto_effect_fnb_enable;
        context->ops.disable = auto_effect_fnb_disable;
        context->ops.start = auto_effect_fnb_start;
        context->ops.stop = auto_effect_fnb_stop;
        context->desc = &auto_effect_fnb_descriptor;
        ALOGD("AUTO FNB Effect created");
    } else if (memcmp(uuid, &auto_effect_delay_descriptor.uuid, sizeof(effect_uuid_t)) == 0) {
        auto_effect_delay_context_t *delay_ctxt = (auto_effect_delay_context_t *)calloc(1, sizeof(auto_effect_delay_context_t));
        if (delay_ctxt == NULL) {
            return -ENOMEM;
        }
        context = (effect_context_t *)delay_ctxt;
        context->ops.init = auto_effect_delay_init;
        context->ops.set_parameter = auto_effect_delay_set_parameter;
        context->ops.get_parameter = auto_effect_delay_get_parameter;
        context->ops.start = auto_effect_delay_start;
        context->ops.stop = auto_effect_delay_stop;
        context->ops.enable = auto_effect_delay_enable;
        context->ops.disable = auto_effect_delay_disable;
        context->ops.release = auto_effect_delay_release;
        context->desc = &auto_effect_delay_descriptor;
        ALOGD("%s: AUTO Delay Effect created", __func__);
    } else {
        return -EINVAL;
    }

    context->itfe = &effect_interface;
    context->state = EFFECT_STATE_UNINITIALIZED;
    context->out_handle = (audio_io_handle_t)ioId;
    context->sessionId = sessionId;

    ret = context->ops.init(context);
    if (ret < 0) {
        ALOGE("%s init failed", __func__);
        free(context);
        return ret;
    }

    context->state = EFFECT_STATE_INITIALIZED;

    pthread_mutex_lock(&lock);
    list_add_tail(&created_effects_list, &context->effects_list_node);
    if (sessionId!=0){
        output_context_t *out_ctxt = get_output(ioId);//output already active
        if (out_ctxt != NULL){
            context->usage = auto_effect_util_get_usage(out_ctxt->usecase);
            ALOGD("%s:Add effect to output for ioId=%d (usage=%d)",__func__, ioId, context->usage);
            add_effect_to_output(out_ctxt, context);
        }
    }
    pthread_mutex_unlock(&lock);

    *pHandle = (effect_handle_t)context;

    ALOGD("%s created context %p", __func__, context);

    return 0;

}

int effect_lib_release(effect_handle_t handle)
{
    effect_context_t *context = (effect_context_t *)handle;
    int status;

    if (lib_init() != 0)
        return init_status;

    ALOGV("%s context %p", __func__, handle);
    pthread_mutex_lock(&lock);
    status = -EINVAL;
    if (effect_exists(context)) {
        output_context_t *out_ctxt = get_output(context->out_handle);
        if (out_ctxt != NULL)
            remove_effect_from_output(out_ctxt, context);
        list_remove(&context->effects_list_node);
        if (context->ops.release)
            context->ops.release(context);
        free(context);
        status = 0;
    }
    pthread_mutex_unlock(&lock);
    ALOGD("%s: status %d", __func__, status);
    return status;
}

int effect_lib_get_descriptor(const effect_uuid_t *uuid,
                              effect_descriptor_t *descriptor)
{
    int i;

    if (lib_init() != 0)
        return init_status;

    if (descriptor == NULL || uuid == NULL) {
        ALOGD("%s called with NULL pointer", __func__);
        return -EINVAL;
    }

    for (i = 0; descriptors[i] != NULL; i++) {
        if (memcmp(uuid, &descriptors[i]->uuid, sizeof(effect_uuid_t)) == 0) {
            *descriptor = *descriptors[i];
            return 0;
        }
    }

    return  -EINVAL;
}


/*
 * Effect Control Interface Implementation
 */

/* Stub function for effect interface: never called for offloaded effects */
/* called for hw accelerated effects */
int effect_process(effect_handle_t self,
                       audio_buffer_t *inBuffer __unused,
                       audio_buffer_t *outBuffer __unused)
{
    effect_context_t * context = (effect_context_t *)self;
    int status = 0;

    ALOGD("%s", __func__);

    pthread_mutex_lock(&lock);
    if (!effect_exists(context)) {
        status = -ENOSYS;
        goto exit;
    }

    if (context->state != EFFECT_STATE_ACTIVE) {
        status = -ENODATA;
        goto exit;
    }

    if (context->ops.process)
        status = context->ops.process(context, inBuffer, outBuffer);
exit:
    pthread_mutex_unlock(&lock);
    return status;
}

int effect_command(effect_handle_t self, uint32_t cmdCode, uint32_t cmdSize,
                   void *pCmdData, uint32_t *replySize, void *pReplyData)
{

    effect_context_t * context = (effect_context_t *)self;
    int retsize;
    int status = 0;

    pthread_mutex_lock(&lock);

    if (!effect_exists(context)) {
        status = -ENOSYS;
        goto exit;
    }

    ALOGD("%s: ctxt %p, cmd %d", __func__, context, cmdCode);
    if (context == NULL || context->state == EFFECT_STATE_UNINITIALIZED) {
        status = -ENOSYS;
        goto exit;
    }

    switch (cmdCode) {
    case EFFECT_CMD_INIT:
        if (pReplyData == NULL || *replySize != sizeof(int)) {
            status = -EINVAL;
            goto exit;
        }
        if (context->ops.init)
            *(int *) pReplyData = context->ops.init(context);
        else
            *(int *) pReplyData = 0;
        break;
    case EFFECT_CMD_SET_CONFIG:
        if (pCmdData == NULL || cmdSize != sizeof(effect_config_t)
                || pReplyData == NULL || *replySize != sizeof(int)) {
            status = -EINVAL;
            goto exit;
        }
        *(int *) pReplyData = set_config(context, (effect_config_t *) pCmdData);
        break;
    case EFFECT_CMD_GET_CONFIG:
        if (pReplyData == NULL ||
            *replySize != sizeof(effect_config_t)) {
            status = -EINVAL;
            goto exit;
        }
        get_config(context, (effect_config_t *)pReplyData);
        break;
    case EFFECT_CMD_RESET:
        if (context->ops.reset)
            context->ops.reset(context);
        break;
    case EFFECT_CMD_ENABLE:
        if (pReplyData == NULL || *replySize != sizeof(int)) {
            status = -EINVAL;
            goto exit;
        }
        if (context->state != EFFECT_STATE_INITIALIZED) {
            status = -ENOSYS;
            goto exit;
        }
        if (context->ops.enable)
            context->ops.enable(context);
        *(int *)pReplyData = 0;
        ALOGD("%s:CMD_ENABLE",__func__);
        break;
    case EFFECT_CMD_DISABLE:
        if (pReplyData == NULL || *replySize != sizeof(int)) {
            status = -EINVAL;
            goto exit;
        }
        if (context->ops.disable)
            context->ops.disable(context);
        *(int *)pReplyData = 0;
        ALOGD("%s:CMD_ENABLE",__func__);
        break;
    case EFFECT_CMD_GET_PARAM: {
        if (pCmdData == NULL ||
            cmdSize < (int)(sizeof(effect_param_t) + sizeof(uint32_t)) ||
            pReplyData == NULL ||
            *replySize < (int)(sizeof(effect_param_t) + sizeof(uint32_t) + sizeof(uint16_t)) ||
            // constrain memcpy below
            ((effect_param_t *)pCmdData)->psize > *replySize - sizeof(effect_param_t)) {
            status = -EINVAL;
            ALOGE("EFFECT_CMD_GET_PARAM invalid command cmdSize %d *replySize %d",
                  cmdSize, *replySize);
            goto exit;
        }
        effect_param_t *q = (effect_param_t *)pCmdData;
        memcpy(pReplyData, pCmdData, sizeof(effect_param_t) + q->psize);
        effect_param_t *p = (effect_param_t *)pReplyData;
        if (context->ops.get_parameter){
            int r=context->ops.get_parameter(context, p, replySize);
        }
    } break;
    case EFFECT_CMD_SET_PARAM: {
        if (pCmdData == NULL ||
            cmdSize < (int)(sizeof(effect_param_t) + sizeof(uint32_t) +
                            sizeof(uint16_t)) ||
            pReplyData == NULL || *replySize != sizeof(int32_t)) {
            status = -EINVAL;
            ALOGE("EFFECT_CMD_SET_PARAM invalid command cmdSize %d *replySize %d",
                  cmdSize, *replySize);
            goto exit;
        }
        *(int32_t *)pReplyData = 0;
        effect_param_t *p = (effect_param_t *)pCmdData;
        if (context->ops.set_parameter){
            int r = context->ops.set_parameter(context, p, *replySize);
            if (r<(int)0 && p->status<(int)0){
                *(int32_t *)pReplyData = 1;//return error, wrong input param
                ALOGE("%s: erros setting param:wrong input param",__func__);
            } else if (r) {
                // state transition on failure from ADSP
                ALOGD("%s:Failed to set the parameter (effect state=%d)",__func__, context->state);
            }
        }
    } break;
    case EFFECT_CMD_SET_DEVICE: {
        uint32_t device;
        ALOGV("\t EFFECT_CMD_SET_DEVICE start");
        if (pCmdData == NULL || cmdSize < sizeof(uint32_t)) {
            status = -EINVAL;
            ALOGE("EFFECT_CMD_SET_DEVICE invalid command cmdSize %d", cmdSize);
            goto exit;
        }
        device = *(uint32_t *)pCmdData;
        if (context->ops.set_device)
            context->ops.set_device(context, device);
    } break;
    case EFFECT_CMD_SET_VOLUME:
    case EFFECT_CMD_SET_AUDIO_MODE:
        break;
    default:
        if (cmdCode >= EFFECT_CMD_FIRST_PROPRIETARY && context->ops.command)
            status = context->ops.command(context, cmdCode, cmdSize,
                                          pCmdData, replySize, pReplyData);
        else {
            ALOGE("%s Invalid or not implemented command %d", __func__, cmdCode);
            status = -EINVAL;
        }
        break;
    }

exit:
    pthread_mutex_unlock(&lock);

    return status;
}

/* Effect Control Interface Implementation: get_descriptor */
int effect_get_descriptor(effect_handle_t   self,
                          effect_descriptor_t *descriptor)
{
    effect_context_t *context = (effect_context_t *)self;

    if (!effect_exists(context) || (descriptor == NULL))
        return -EINVAL;

    *descriptor = *context->desc;

    return 0;
}

bool effect_is_active(effect_context_t * ctxt) {
    return ctxt->state == EFFECT_STATE_ACTIVE;
}

/* effect_handle_t interface implementation for offload effects */
const struct effect_interface_s effect_interface = {
    effect_process,
    effect_command,
    effect_get_descriptor,
    NULL,
};

__attribute__ ((visibility ("default")))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    tag : AUDIO_EFFECT_LIBRARY_TAG,
    version : EFFECT_LIBRARY_API_VERSION,
    name : "Auto Effects Bundle Library",
    implementor : "QTI",
    create_effect : effect_lib_create,
    release_effect : effect_lib_release,
    get_descriptor : effect_lib_get_descriptor,
};
