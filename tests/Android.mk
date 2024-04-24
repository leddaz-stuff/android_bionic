#
# Copyright (C) 2012 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

ifeq ($(HOST_OS)-$(HOST_ARCH),$(filter $(HOST_OS)-$(HOST_ARCH),linux-x86 linux-x86_64))

# -----------------------------------------------------------------------------
# Compile time tests.
# -----------------------------------------------------------------------------

include $(CLEAR_VARS)

LOCAL_ADDITIONAL_DEPENDENCIES := \
    $(LOCAL_PATH)/Android.mk \
    $(LOCAL_PATH)/touch-obj-on-success

LOCAL_CXX := $(LOCAL_PATH)/touch-obj-on-success \
    $(LLVM_PREBUILTS_PATH)/clang++ \

LOCAL_CLANG := true
LOCAL_MODULE := bionic-compile-time-tests1-clang++
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-BSD
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_NOTICE_FILE := $(LOCAL_PATH)/NOTICE
LOCAL_TIDY := false
LOCAL_CPPFLAGS := -Wall -Wno-error
LOCAL_CPPFLAGS += -fno-color-diagnostics -ferror-limit=10000 -Xclang -verify
LOCAL_CPPFLAGS += -DCOMPILATION_TESTS=1 -Wformat-nonliteral
LOCAL_CPPFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1
LOCAL_SRC_FILES := clang_fortify_tests.cpp

include $(BUILD_STATIC_LIBRARY)

FORTIFY_LEVEL :=

include $(CLEAR_VARS)

LOCAL_ADDITIONAL_DEPENDENCIES := \
    $(LOCAL_PATH)/Android.mk \
    $(LOCAL_PATH)/touch-obj-on-success

LOCAL_CXX := $(LOCAL_PATH)/touch-obj-on-success \
    $(LLVM_PREBUILTS_PATH)/clang++ \

LOCAL_CLANG := true
LOCAL_MODULE := bionic-compile-time-tests2-clang++
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-BSD
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_NOTICE_FILE := $(LOCAL_PATH)/NOTICE
LOCAL_TIDY := false
LOCAL_CPPFLAGS := -Wall -Wno-error
LOCAL_CPPFLAGS += -fno-color-diagnostics -ferror-limit=10000 -Xclang -verify
LOCAL_CPPFLAGS += -DCOMPILATION_TESTS=1 -Wformat-nonliteral
LOCAL_CPPFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
LOCAL_SRC_FILES := clang_fortify_tests.cpp

include $(BUILD_STATIC_LIBRARY)

FORTIFY_LEVEL :=


endif # linux-x86

include $(call first-makefiles-under,$(LOCAL_PATH))
