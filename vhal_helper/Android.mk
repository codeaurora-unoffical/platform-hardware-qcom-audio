# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
LOCAL_PATH:= $(call my-dir)

ifeq ($(AUDIO_FEATURE_ENABLED_VHAL_HELPER), true)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/include \
    $(TOP)/android/hardware/automotive/vehicle/2.0

LOCAL_SRC_FILES += \
    src/VehicleHalAudioHelper.cpp \
    src/vehicle-hal-audio-helper-for-c.cpp

LOCAL_SHARED_LIBRARIES += \
    libutils \
    liblog \
    libhidlbase \
    libhidltransport \
    libhwbinder \
    android.hardware.automotive.vehicle@2.0 \
    android.hardware.automotive.vehicle@2.0-manager-lib-shared

LOCAL_MODULE := libvehiclehalaudiohelper
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

endif  #ifeq ($(AUDIO_FEATURE_ENABLED_VHAL_HELPER), true)
