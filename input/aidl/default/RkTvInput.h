/*
 * Copyright 2023 Rockchip Electronics S.LSI Co. LTD
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

#pragma once

#include <aidl/rockchip/hardware/tv/input/BnRkTvInput.h>
#include <utils/KeyedVector.h>

#include <fmq/AidlMessageQueue.h>
#include <map>
#include <unordered_map>
#include "TvInputDeviceInfoWrapper.h"
#include "TvStreamConfigWrapper.h"

#include "TvDeviceV4L2Event.h"
#include "HinDev.h"

using namespace android;
using namespace std;
using AidlNativeHandle = ::aidl::android::hardware::common::NativeHandle;
using ::aidl::android::hardware::common::fmq::MQDescriptor;
using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::aidl::android::hardware::tv::input::ITvInputCallback;
using ::aidl::android::hardware::tv::input::TvInputDeviceInfoWrapper;
using ::aidl::android::hardware::tv::input::TvMessageEventType;
using ::aidl::android::hardware::tv::input::TvStreamConfigWrapper;
using ::aidl::rockchip::hardware::tv::input::RkTvPrivAppCmdInfo;
using ::aidl::rockchip::hardware::tv::input::RkTvStreamConfig;
using ::android::AidlMessageQueue;

namespace aidl {
namespace rockchip {
namespace hardware {
namespace tv {
namespace input {

using TvMessageEnabledMap = std::unordered_map<
        int32_t, std::unordered_map<int32_t, std::unordered_map<TvMessageEventType, bool>>>;

class RkTvInput : public BnRkTvInput {
  public:
    RkTvInput();

    ::ndk::ScopedAStatus privRkCmdFromApp(const RkTvPrivAppCmdInfo& cmdInfo) override;
    ::ndk::ScopedAStatus setRkCallback(const shared_ptr<ITvInputCallback>& in_callback) override;
    ::ndk::ScopedAStatus setRkTvMessageEnabled(int32_t deviceId, int32_t streamId,
                                             TvMessageEventType in_type, bool enabled) override;
    ::ndk::ScopedAStatus getRkTvMessageQueueDesc(
            MQDescriptor<int8_t, SynchronizedReadWrite>* out_queue, int32_t in_deviceId,
            int32_t in_streamId) override;
    ::ndk::ScopedAStatus getRkStreamConfigurations(int32_t in_deviceId,
            vector<RkTvStreamConfig>* _aidl_return) override;
    ::ndk::ScopedAStatus openRkStream(int32_t in_deviceId, int32_t in_streamId,
            vector<AidlNativeHandle>* _aidl_return) override;
    ::ndk::ScopedAStatus closeRkStream(int32_t in_deviceId, int32_t in_streamId) override;
    ::ndk::ScopedAStatus setRkPreviewInfo(int32_t       deviceId, int32_t streamId, int32_t initType) override;
    ::ndk::ScopedAStatus setSinglePreviewBuffer(int64_t in_bufId, const AidlNativeHandle& in_bufHandle) override;
    ::ndk::ScopedAStatus requestRkCapture(int32_t in_deviceId, int32_t in_streamId, int64_t in_buffId, int32_t in_seq) override;

    void init();
    int hin_dev_open(int deviceId, int type);

  private:
    //native_handle_t* createNativeHandle(int fd);

    shared_ptr<ITvInputCallback> mCallback;
    map<int32_t, shared_ptr<TvInputDeviceInfoWrapper>> mDeviceInfos;
    map<int32_t, map<int32_t, shared_ptr<TvStreamConfigWrapper>>> mStreamConfigs;
    TvMessageEnabledMap mTvMessageEventEnabled;
    //AidlMessageQueue<int8_t, SynchronizedReadWrite>* mFmqSynchronized;
};

}  // namespace input
}  // namespace tv
}  // namespace hardware
}  // namespace rockchip
}  // namespace aidl
