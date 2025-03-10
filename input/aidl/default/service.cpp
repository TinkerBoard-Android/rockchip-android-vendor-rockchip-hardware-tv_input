/*
 * Copyright 2022 The Android Open Source Project
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

#define LOG_TAG "android.hardware.tv.input-service.rockchip"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <utils/Log.h>

#include "TvInput.h"
#include "RkTvInput.h"

using ::aidl::android::hardware::tv::input::TvInput;
using aidl::rockchip::hardware::tv::input::RkTvInput;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(8);
    std::shared_ptr<TvInput> tvInput = ndk::SharedRefBase::make<TvInput>();
    tvInput->init();
    ndk::SpAIBinder tvInputBinder = tvInput->asBinder();

    std::shared_ptr<RkTvInput> rkTvInput = ndk::SharedRefBase::make<RkTvInput>();
    binder_status_t status = AIBinder_setExtension(tvInputBinder.get(), rkTvInput->asBinder().get());
    CHECK(STATUS_OK == status);
    rkTvInput->init();

    const std::string instance = std::string() + TvInput::descriptor + "/default";
    status = AServiceManager_addService(tvInputBinder.get(), instance.c_str());
    CHECK(status == STATUS_OK);

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reached
}
