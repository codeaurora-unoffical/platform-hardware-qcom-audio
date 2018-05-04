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
#define LOG_TAG "VehicleHalAudioHelper"

#include <utils/SystemClock.h>
#include <utils/Log.h>

#include "VehicleHalAudioHelper.h"

//#define DBG
#ifdef DBG
#define LOGD(x...) ALOGD(x)
#else
#define LOGD(x...)
#endif

static SubscribeOptions reqAudioProperties[] = {
    {
        .propId = static_cast<int32_t>(VehicleProperty::AUDIO_FOCUS),
        .flags = SubscribeFlags::DEFAULT
    },
};

enum {
    VEHICLE_AUDIO_STREAM_STATE_INDEX_STATE = 0,
    VEHICLE_AUDIO_STREAM_STATE_INDEX_STREAM = 1,
};

enum {
    VEHICLE_AUDIO_STREAM_STATE_STOPPED = 0,
    VEHICLE_AUDIO_STREAM_STATE_STARTED = 1,
};

namespace android {

// ----------------------------------------------------------------------------

// Initialize static variables
VehicleHalAudioHelper* VehicleHalAudioHelper::mVehicleHalAudioHelper = NULL;
Mutex VehicleHalAudioHelper::mLock;


VehicleHalAudioHelper::VehicleHalAudioHelper()
    : mTimeoutNs(FOCUS_WAIT_DEFAULT_TIMEOUT_NS),
      mHasFocusProperty(false) {
}

VehicleHalAudioHelper::~VehicleHalAudioHelper() {
    // nothing to do
}

VehicleHalAudioHelper* VehicleHalAudioHelper::getInstance() {

    // Create one static instance of the class
    if (mVehicleHalAudioHelper == NULL) {
        Mutex::Autolock autoLock(mLock);
        if (mVehicleHalAudioHelper == NULL) {
            mVehicleHalAudioHelper = new VehicleHalAudioHelper();
            mVehicleHalAudioHelper->initLocked();
        }
    }
    return mVehicleHalAudioHelper;
}

void VehicleHalAudioHelper::destroy() {

    // Delete the instance
    if (mVehicleHalAudioHelper != NULL) {
        mVehicleHalAudioHelper->release();
        delete mVehicleHalAudioHelper;
        mVehicleHalAudioHelper = NULL;
    }
}

status_t VehicleHalAudioHelper::initLocked() {

    // Connect to the Vehicle HAL so we can monitor state
    ALOGI("%s: Connecting to Vehicle HAL", __func__);
    if (mVehicle != NULL) {
        ALOGD("%s: Vehicle Interface already initialized", __func__);
        return NO_ERROR;
    }
    mVehicle = IVehicle::getService();
    if(mVehicle.get() == NULL) {
        ALOGE("%s: Vehicle Interface is not initialized", __func__);
        return UNKNOWN_ERROR;
    }

    mScratchValueStreamState.prop = static_cast<int32_t>(VehicleProperty::AUDIO_STREAM_STATE);
    mScratchValueStreamState.value.int32Values = std::vector<int32_t>(2);
    mScratchValueStreamState.timestamp = 0;
    mScratchValueFocus.prop = static_cast<int32_t>(VehicleProperty::AUDIO_FOCUS);
    mScratchValueFocus.value.int32Values = std::vector<int32_t>(4);
    mScratchValueFocus.timestamp = 0;

    updatePropertiesLocked();

    return NO_ERROR;
}

void VehicleHalAudioHelper::updatePropertiesLocked() {

    hidl_vec<int32_t> properties =
        { static_cast<int32_t>(VehicleProperty::AUDIO_FOCUS) };

    if (invokeGetPropConfigs(properties) == StatusCode::OK) {
        mHasFocusProperty = true;

        hidl_vec<SubscribeOptions> options;
        options.setToExternal(reqAudioProperties, sizeof(reqAudioProperties) / sizeof(SubscribeOptions));
        StatusCode status = mVehicle->subscribe(this, options);
        if (status != StatusCode::OK) {
            ALOGE("%s: Subscription to vehicle notifications failed with status %d",
                __func__, status);
        }

        if (invokeGet(&mScratchValueFocus) == StatusCode::OK) {
            mAllowedStreams =
                mScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::STREAMS)];
            ALOGI("Initial focus state 0x%x", mAllowedStreams);
        } else {
            ALOGE("%s: Focus not available from vehicle, assume focus always granted",
                __func__);
            mAllowedStreams = 0xffffffff;
        }
    } else {
        ALOGI("%s: No focus property from vehicle, assume focus always granted",
            __func__);
        mHasFocusProperty = false;
        mAllowedStreams = 0xffffffff;
    }
    // FIXME: initialization is done by streamState constructor already
    for (size_t i = 0; i < VEHICLE_HAL_AUDIO_HELPER_STREAMS_MAX; i++) {
        mStreamStates[i].timeoutStartNs = 0;
        mStreamStates[i].started = false;
    }
}

void VehicleHalAudioHelper::release() {

    Mutex::Autolock autoLock(mLock);

    if (mVehicle.get() == NULL) {
        ALOGD("%s: Vehicle Interface already released", __func__);
        return;
    }

    for (size_t i = 0; i < sizeof(reqAudioProperties) / sizeof(SubscribeOptions); i++) {
        StatusCode status = mVehicle->unsubscribe(this, reqAudioProperties[i].propId);
        if (status != StatusCode::OK) {
            ALOGE("%s: Unsubscription to vehicle notifications failed with status %d",
                __func__, status);
        }
    }
}

static int32_t streamFlagToStreamNumber(int32_t streamFlag) {

    int32_t flag = 0x1;

    for (int32_t i = 0; i < 32; i++) {
        if ((flag & streamFlag) != 0) {
            return i;
        }
        flag = flag << 1;
    }
    return -1;
}

VehicleHalAudioHelper::StreamState* VehicleHalAudioHelper::getStreamStateLocked(
        int32_t streamNumber) {

    if (streamNumber >= VEHICLE_HAL_AUDIO_HELPER_STREAMS_MAX) {
        return NULL;
    }
    return &mStreamStates[streamNumber];
}

VehicleHalAudioHelper::StreamState* VehicleHalAudioHelper::getStreamState(
        int32_t streamNumber) {

    Mutex::Autolock autoLock(mLock);
    return getStreamStateLocked(streamNumber);
}

void VehicleHalAudioHelper::setFocusTimeout(int64_t timeoutNs) {

    Mutex::Autolock autoLock(mLock);
    mTimeoutNs = timeoutNs;
}

int32_t VehicleHalAudioHelper::setParameters(const char* query) {
    Mutex::Autolock autoLock(mLock);

    mAudioParams.prop = static_cast<int32_t>(VehicleProperty::AUDIO_PARAMETERS);
    mAudioParams.value.stringValue = query;

    StatusCode status = invokeSet(&mAudioParams);
    if (status != StatusCode::OK) {
        ALOGE("setParameters, failed with status %d",
            status);
        return BAD_VALUE;
    }
    return OK;
}

const char* VehicleHalAudioHelper::getParameters(const char* query) {
    const char* reply = NULL;
    Mutex::Autolock autoLock(mLock);

    mAudioParams.prop = static_cast<int32_t>(VehicleProperty::AUDIO_PARAMETERS);
    mAudioParams.value.stringValue = query;

    if(mVehicle.get() == NULL) {
        ALOGE("%s: Vehicle Interface is not initialized", __func__);
        return NULL;
    } else {
        StatusCode status = invokeGet(&mAudioParams);
        if (status != StatusCode::OK) {
            ALOGE("getParameters, failed with status %d",
            status);
            return NULL;
        }

        reply = mAudioParams.value.stringValue.c_str();
        if (reply == NULL) {
            ALOGE("%s: getParameters failed, retuning null",__func__);
            return NULL;
        }
    }

    return reply;

}

void VehicleHalAudioHelper::notifyStreamStarted(int32_t stream) {

    LOGD("notifyStreamStarted, stream:0x%x", stream);

    Mutex::Autolock autoLock(mLock);

    if (!mHasFocusProperty) {
        return;
    }

    int32_t streamNumber = streamFlagToStreamNumber(stream);
    if (streamNumber < 0) {
        ALOGE("notifyStreamStarted, wrong stream:0x%x", stream);
        return;
    }

    StreamState* state = getStreamStateLocked(streamNumber);
    if (state == NULL) {
        ALOGE("notifyStreamStarted, NULL state");
        return;
    }

    Mutex::Autolock autoLock2(state->lock);

#ifdef VHAL_HELPER_FOCUS_REQUEST
    /* FIXME: Focus should be requested by framework before stream start.
     * This is a workaround to request focus upon stream start for testing. */
    VehiclePropValue tScratchValueFocus;
    tScratchValueFocus.prop = static_cast<int32_t>(VehicleProperty::AUDIO_FOCUS);
    tScratchValueFocus.value.int32Values = std::vector<int32_t>(4);
    tScratchValueFocus.timestamp = 0;
    tScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::FOCUS)] =
        static_cast<int32_t>(VehicleAudioFocusRequest::REQUEST_GAIN);
    tScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::STREAMS)] = stream;
    tScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::EXTERNAL_FOCUS_STATE)] = 0;
    tScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::AUDIO_CONTEXTS)] = 0;
    if (invokeSet(&tScratchValueFocus) != StatusCode::OK) {
        ALOGE("notifyStreamStarted, failed to request focus for stream 0x%x", stream);
    }
#endif

    if (state->started) {
        /* FIXME: proceed with set to vehicle hal to allow codec control to happen.
         * This is to workaround media and offload sharing the same bus device for
         * streaming, where offload may start before media stream stops and cause
         * mismatch in codec control. */
        ALOGI("notifyStreamStarted, stream 0x%x already started", stream);
        // return;
    }

    state->started = true;
    state->timeoutStartNs = elapsedRealtimeNano();
    mScratchValueStreamState.prop = static_cast<int32_t>(VehicleProperty::AUDIO_STREAM_STATE);
    mScratchValueStreamState.value.int32Values = std::vector<int32_t>(2);
    mScratchValueStreamState.value.int32Values[VEHICLE_AUDIO_STREAM_STATE_INDEX_STATE] =
            VEHICLE_AUDIO_STREAM_STATE_STARTED;
    mScratchValueStreamState.value.int32Values[VEHICLE_AUDIO_STREAM_STATE_INDEX_STREAM] =
            streamNumber;
    mScratchValueStreamState.timestamp = android::elapsedRealtimeNano();

    StatusCode status = invokeSet(&mScratchValueStreamState);
    if (status != StatusCode::OK) {
        ALOGE("notifyStreamStarted, failed for stream 0x%x with status %d",
            streamNumber, status);
    }
}

void VehicleHalAudioHelper::notifyStreamStopped(int32_t stream) {

    LOGD("notifyStreamStopped, stream:0x%x", stream);

    Mutex::Autolock autoLock(mLock);

    if (!mHasFocusProperty) {
        return;
    }

    int32_t streamNumber = streamFlagToStreamNumber(stream);
    if (streamNumber < 0) {
        ALOGE("notifyStreamStopped, wrong stream:0x%x", stream);
        return;
    }

    StreamState* state = getStreamStateLocked(streamNumber);
    if (state == NULL) {
        ALOGE("notifyStreamStopped, NULL state");
        return;
    }

    Mutex::Autolock autoLock2(state->lock);

#ifdef VHAL_HELPER_FOCUS_REQUEST
    /* FIXME: Focus should be requested by framework before stream start.
     * This is a workaound to release focus upon stream stop for testing. */
    VehiclePropValue tScratchValueFocus;
    tScratchValueFocus.prop = static_cast<int32_t>(VehicleProperty::AUDIO_FOCUS);
    tScratchValueFocus.value.int32Values = std::vector<int32_t>(4);
    tScratchValueFocus.timestamp = 0;
    tScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::FOCUS)] =
        static_cast<int32_t>(VehicleAudioFocusRequest::REQUEST_RELEASE);
    tScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::STREAMS)] = stream;
    tScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::EXTERNAL_FOCUS_STATE)] = 0;
    tScratchValueFocus.value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::AUDIO_CONTEXTS)] = 0;
    if (invokeSet(&tScratchValueFocus) != StatusCode::OK) {
        ALOGE("notifyStreamStopped, failed to release focus for stream 0x%x", stream);
    }
#endif

    if (!state->started) {
        ALOGI("notifyStreamStopped, stream 0x%x already stopped", stream);
        // return;
    }

    state->started = false;
    state->timeoutStartNs = 0;
    mScratchValueStreamState.prop = static_cast<int32_t>(VehicleProperty::AUDIO_STREAM_STATE);
    mScratchValueStreamState.value.int32Values = std::vector<int32_t>(2);
    mScratchValueStreamState.value.int32Values[VEHICLE_AUDIO_STREAM_STATE_INDEX_STATE] =
            VEHICLE_AUDIO_STREAM_STATE_STOPPED;
    mScratchValueStreamState.value.int32Values[VEHICLE_AUDIO_STREAM_STATE_INDEX_STREAM] =
            streamNumber;
    mScratchValueStreamState.timestamp = android::elapsedRealtimeNano();

    StatusCode status = invokeSet(&mScratchValueStreamState);
    if (status != StatusCode::OK) {
        ALOGE("notifyStreamStopped, failed for stream 0x%x with status %d",
            streamNumber, status);
    }
}

vehicle_hal_audio_helper_focus_state VehicleHalAudioHelper::getStreamFocusState(
        int32_t stream) {

    LOGD("getStreamFocusState, stream:0x%x", stream);

    if ((mAllowedStreams & stream) == stream) {
        return VEHICLE_HAL_AUDIO_HELPER_FOCUS_STATE_FOCUS;
    }

    int32_t streamNumber = streamFlagToStreamNumber(stream);
    if (streamNumber < 0) {
        ALOGE("getStreamFocusState, wrong stream:0x%x", stream);
        return VEHICLE_HAL_AUDIO_HELPER_FOCUS_STATE_TIMEOUT;
    }

    StreamState* state = getStreamState(streamNumber);
    if (state == NULL) {
        ALOGE("getStreamFocusState, NULL state");
        return VEHICLE_HAL_AUDIO_HELPER_FOCUS_STATE_TIMEOUT;
    }

    Mutex::Autolock autoLock(state->lock);

    if (state->timeoutStartNs == 0) {
        if (state->started) {
            state->timeoutStartNs = elapsedRealtimeNano();
        }
    } else {
        int64_t now = elapsedRealtimeNano();
        if ((state->timeoutStartNs + mTimeoutNs) < now) {
            return VEHICLE_HAL_AUDIO_HELPER_FOCUS_STATE_TIMEOUT;
        }
    }
    return VEHICLE_HAL_AUDIO_HELPER_FOCUS_STATE_NO_FOCUS;
}

bool VehicleHalAudioHelper::waitForStreamFocus(int32_t stream, nsecs_t waitTimeNs) {

    int64_t currentTime = android::elapsedRealtimeNano();
    int64_t finishTime = currentTime + waitTimeNs;

    int32_t streamNumber = streamFlagToStreamNumber(stream);
    if (streamNumber < 0) {
        ALOGE("waitForStreamFocus, wrong stream:0x%x", stream);
        return false;
    }

    StreamState* state = getStreamState(streamNumber);
    if (state == NULL) {
        ALOGE("waitForStreamFocus, NULL state");
        return false;
    }

    Mutex::Autolock autoLock(state->lock);

    while (true) {
        if ((stream & mAllowedStreams) == stream) {
            LOGD("waitForStreamFocus: stream 0x%x has focus", stream);
            return true;
        }
        currentTime = android::elapsedRealtimeNano();
        if (currentTime >= finishTime) {
            break;
        }
        nsecs_t waitTime = finishTime - currentTime;
        state->focusWait.waitRelative(state->lock, waitTime);
    }
    LOGD("waitForStreamFocus: stream 0x%x no focus", stream);
    return false;
}

Return<void> VehicleHalAudioHelper::onPropertyEvent(const hidl_vec <VehiclePropValue> & propValues) {

    bool changed = false;

    do {
        Mutex::Autolock autoLock(mLock);
        if (mVehicle.get() == NULL) { // already released
            return Return<void>();
        }
        for(size_t i = 0; i < propValues.size(); i++) {
            ALOGD("%s: Vehicle Property: 0x%x", __func__, propValues[i].prop);
            switch((propValues[i].prop)) {
                case (static_cast<int32_t>(VehicleProperty::AUDIO_FOCUS)):
                    mAllowedStreams =
                        propValues[i].value.int32Values[static_cast<int32_t>(VehicleAudioFocusIndex::STREAMS)];
                    ALOGI("%s: Audio Focus Change 0x%x", __func__, mAllowedStreams);
                    changed = true;
                    break;
                default:
                    ALOGD("%s: Unknown Vehicle Property: 0x%x",
                        __func__, propValues[i].prop);
                    break;
            }
        }
        if (changed) {
            for (size_t i = 0; i < VEHICLE_HAL_AUDIO_HELPER_STREAMS_MAX; i++) {
                if (mAllowedStreams & (0x1 << i)) {
                    Mutex::Autolock autoLock(mStreamStates[i].lock);
                    mStreamStates[i].focusWait.signal();
                }
            }
        }
    } while (false);

    return Return<void>();
}

Return<void> VehicleHalAudioHelper::onPropertySet(const VehiclePropValue & propValue) {
    // Ignore the direct set calls (we don't expect to make any anyway)
    ALOGD("%s: Vehicle Property: 0x%x", __func__, propValue.prop);
    return Return<void>();
}

Return<void> VehicleHalAudioHelper::onPropertySetError(StatusCode errorCode,
                                int32_t    propId,
                                int32_t    areaId) {
    // We don't set values, so we don't listen for set errors
    ALOGD("%s: Error Code: %d, Property ID: 0x%x, Area ID: %d",
        __func__, errorCode, propId, areaId);
    return Return<void>();
}

// TODO: how to monitor vehicle HAL restart

StatusCode VehicleHalAudioHelper::invokeGetPropConfigs(hidl_vec<int32_t> requestedProp) {
    ALOGD("invokeGetPropConfigs");

    StatusCode status = StatusCode::INVALID_ARG;
	hidl_vec<VehiclePropConfig> propConfigs;
    bool called = false;

    // Call the Vehicle HAL, which will block until the callback is complete
    mVehicle->getPropConfigs(requestedProp,
                             [&propConfigs, &status, &called]
                             (StatusCode s, const hidl_vec<VehiclePropConfig>& c) {
                                 status = s;
                                 propConfigs = c;
                                 called = true;
                       }
    );
    LOGD("invokeGetPropConfigs: status %d, size %zu", status, propConfigs.size());
    if (!called) {
        ALOGE("Vehicle HAL query did not run as expected.");
    }

    return status;
}

StatusCode VehicleHalAudioHelper::invokeGet(VehiclePropValue * pRequestedPropValue) {

    ALOGD("invokeGet: 0x%x", pRequestedPropValue->prop);

    StatusCode status = StatusCode::TRY_AGAIN;
    bool called = false;

    // Call the Vehicle HAL, which will block until the callback is complete
    mVehicle->get(*pRequestedPropValue,
                  [pRequestedPropValue, &status, &called]
                  (StatusCode s, const VehiclePropValue& v) {
                      status = s;
                      *pRequestedPropValue = v;
                      called = true;
                  }
    );
    LOGD("invokeGet: status %d", status);
    if (!called) {
        ALOGE("Vehicle HAL query did not run as expected.");
    }

    return status;
}

StatusCode VehicleHalAudioHelper::invokeSet(VehiclePropValue * pRequestedPropValue) {

    ALOGD("invokeSet 0x%x", pRequestedPropValue->prop);

    StatusCode status = StatusCode::TRY_AGAIN;

    // Call the Vehicle HAL, which will block until the callback is complete
    status = mVehicle->set(*pRequestedPropValue);
    LOGD("invokeSet: status %d", status);

    return status;
}

}; // namespace android
