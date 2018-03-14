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

#ifndef ANDROID_VEHICLE_HAL_AUDIO_HELPER_H
#define ANDROID_VEHICLE_HAL_AUDIO_HELPER_H

#include <stdint.h>
#include <sys/types.h>

#include <utils/threads.h>
#include <utils/RefBase.h>
#include <utils/Vector.h>

#include <functional>
#include <iostream>

#include <android/hardware/automotive/vehicle/2.0/IVehicleCallback.h>
#include <android/hardware/automotive/vehicle/2.0/IVehicle.h>
#include <android/hardware/automotive/vehicle/2.0/types.h>

// for enums
#include "vehicle-hal-audio-helper-for-c.h"

using namespace ::android::hardware::automotive::vehicle::V2_0;
using ::android::hardware::automotive::vehicle::V2_0::VehicleProperty;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_handle;
using ::android::sp;

namespace android {

// ----------------------------------------------------------------------------

class VehicleHalAudioHelper : public IVehicleCallback {
public:

    VehicleHalAudioHelper(int64_t timeoutNs = FOCUS_WAIT_DEFAULT_TIMEOUT_NS);
    virtual ~VehicleHalAudioHelper();

    status_t init();

    void release();
    int32_t setParameters(const char* query);
    const char* getParameters(const char* query);
    void notifyStreamStarted(int32_t stream);
    void notifyStreamStopped(int32_t stream);

    vehicle_hal_audio_helper_focus_state getStreamFocusState(int32_t stream);

    bool waitForStreamFocus(int32_t stream, nsecs_t waitTimeNs);

    // from IVehicleCallback
    Return<void> onPropertyEvent(const hidl_vec <VehiclePropValue> & propValues) override;
    Return<void> onPropertySet(const VehiclePropValue & propValue) override;
    Return<void> onPropertySetError(StatusCode errorCode,
                                    int32_t    propId,
                                    int32_t    areaId) override;

private:
    void updatePropertiesLocked();

    class StreamState {
    public:
        int64_t timeoutStartNs;
        bool started;
        StreamState()
         : timeoutStartNs(0),
           started(false) { };
    };

    StreamState& getStreamStateLocked(int32_t streamNumber);

    StatusCode invokeGetPropConfigs(hidl_vec<int32_t> requestedProp);

    StatusCode invokeGet(VehiclePropValue *pRequestedPropValue);

    StatusCode invokeSet(VehiclePropValue *pRequestedPropValue);

private:
    const int64_t mTimeoutNs;
    sp<IVehicle> mVehicle;
    Mutex mLock;
    Condition mFocusWait;
    bool mHasFocusProperty;
    int32_t mAllowedStreams;
    VehiclePropValue mScratchValueFocus;
    VehiclePropValue mScratchValueStreamState;
    VehiclePropValue mAudioParams;
    Vector<StreamState> mStreamStates;
};

}; // namespace android

#endif /*ANDROID_VEHICLE_HAL_AUDIO_HELPER_H*/
