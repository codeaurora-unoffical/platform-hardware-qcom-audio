/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "VehicleHalAudioHelper-C"

#include <utils/SystemClock.h>
#include "VehicleHalAudioHelper.h"
#include "vehicle-hal-audio-helper-for-c.h"

extern "C" {

vehicle_hal_audio_helper_t* vehicle_hal_audio_helper_create(nsecs_t timeout) {
    android::VehicleHalAudioHelper* helperObj = android::VehicleHalAudioHelper::getInstance();
    if (helperObj == NULL) {
        return NULL;
    }
    helperObj->setFocusTimeout(timeout);
    vehicle_hal_audio_helper_t *helper = new vehicle_hal_audio_helper_t();
    if (helper == NULL) {
        return NULL;
    }
    helper->obj = helperObj;
    return helper;
}

vehicle_hal_audio_helper_t* vehicle_hal_audio_helper_create_with_default_timeout() {
    return vehicle_hal_audio_helper_create(FOCUS_WAIT_DEFAULT_TIMEOUT_NS);
}

void vehicle_hal_audio_helper_destroy(vehicle_hal_audio_helper_t* helper) {
    if (helper == NULL) {
        return;
    }
    delete helper;
}

void vehicle_hal_audio_helper_notify_stream_started(vehicle_hal_audio_helper_t* helper,
        int32_t stream) {
    if (helper == NULL) {
        return;
    }
    android::VehicleHalAudioHelper* helperObj =
            (android::VehicleHalAudioHelper*) helper->obj;
    helperObj->notifyStreamStarted(stream);
}

void vehicle_hal_audio_helper_notify_stream_stopped(vehicle_hal_audio_helper_t* helper,
        int32_t stream) {
    if (helper == NULL) {
        return;
    }
    android::VehicleHalAudioHelper* helperObj =
            (android::VehicleHalAudioHelper*) helper->obj;
    helperObj->notifyStreamStopped(stream);
}

int vehicle_hal_audio_helper_get_stream_focus_state(
        vehicle_hal_audio_helper_t* helper, int32_t stream) {
    if (helper == NULL) { // caller should validate helper
        return VEHICLE_HAL_AUDIO_HELPER_FOCUS_STATE_TIMEOUT;
    }
    android::VehicleHalAudioHelper* helperObj =
            (android::VehicleHalAudioHelper*) helper->obj;
    return helperObj->getStreamFocusState(stream);
}

int vehicle_hal_audio_helper_wait_for_stream_focus(vehicle_hal_audio_helper_t* helper,
        int32_t stream, nsecs_t waitTimeNs) {
    if (helper == NULL) { // caller should validate helper
        return 0;
    }
    android::VehicleHalAudioHelper* helperObj =
            (android::VehicleHalAudioHelper*) helper->obj;
    if (helperObj->waitForStreamFocus(stream, waitTimeNs)) {
        return 1;
    }
    return 0;
}

int32_t vehicle_hal_audio_helper_set_parameters(vehicle_hal_audio_helper_t* helper,
        const char *query) {
    int32_t ret;
    if (helper == NULL) { // caller should validate helper
        ALOGE(" vhal helper is NULL");
        return 0;
    }
    android::VehicleHalAudioHelper* helperObj =
            (android::VehicleHalAudioHelper*) helper->obj;
    ret = helperObj->setParameters(query);
    if (ret < 0){
        ALOGE("%s: set parameters failed",__func__);
        return ret;
    }
    return 0;
}

const char* vehicle_hal_audio_helper_get_parameters(vehicle_hal_audio_helper_t* helper,
        const char *query) {
    const char *reply = NULL;
    if (helper == NULL) { // caller should validate helper
        ALOGE("vhal helper handle is NULL");
        return NULL;
    }
    android::VehicleHalAudioHelper* helperObj =
            (android::VehicleHalAudioHelper*) helper->obj;
    reply = helperObj->getParameters(query);
    if (reply == NULL) {
        ALOGE("%s: get parameters failed",__func__);
        return NULL;
    }
    return reply;
}

}
