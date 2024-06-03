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

#define LOG_TAG "android.hardware.tv.input-service.example"

#include <aidl/android/hardware/tv/input/ITvInput.h>
#include <utils/Log.h>
#include "RkTvInput.h"
//#undef NDEBUG
#define LOG_NDDEBUG 0

using ::aidl::android::hardware::tv::input::ITvInput;
using ::aidl::android::hardware::tv::input::TvInputEvent;
using ::aidl::android::hardware::tv::input::TvInputEventType;
using ::aidl::android::hardware::tv::input::TvInputType;
using ::aidl::android::hardware::tv::input::TvMessage;
using ::aidl::android::hardware::tv::input::TvMessageEvent;
using ::aidl::android::hardware::tv::input::TvMessageEventType;

namespace aidl {
namespace rockchip {
namespace hardware {
namespace tv {
namespace input {

typedef struct tv_input_private {
    //tv_input_device_t device;

    // Callback related data
    shared_ptr<ITvInputCallback> callback;
    HinDevImpl* mDev;
    int mStreamType;
    bool isOpened;
    bool isInitialized;
} tv_input_private_t;

static tv_input_private_t *s_TvInputPriv;
static int DEVICE_INFO_INDEX_HDMIIN = 0;
static int DEVICE_ID_HDMIIN = 0;
static int STREAM_ID_HDMIIN = 0;
static int s_HinDevStreamWidth = 1280;
static int s_HinDevStreamHeight = 720;
static int s_HinDevStreamFormat = DEFAULT_TVHAL_STREAM_FORMAT;
static native_handle_t* out_buffer = nullptr;
static native_handle_t* out_cancel_buffer = nullptr;
static AidlMessageQueue<int8_t, SynchronizedReadWrite>* mFmqSynchronized;
static Mutex mLock;

void sendTvMessage(const std::string sub_type, long groupId) {
    TvMessageEvent event;
    event.type = TvMessageEventType::OTHER;
    event.streamId = STREAM_ID_HDMIIN;
    TvMessage topTvMessage;
    topTvMessage.subType = "device_id";
    event.messages.push_back(topTvMessage);

    TvMessage tvMessage;
    tvMessage.groupId = groupId;
    tvMessage.subType = sub_type;
    int strLen = strlen(sub_type.c_str());
    tvMessage.dataLengthBytes = strLen + 1;
    mFmqSynchronized->write((const signed char *)(sub_type.c_str()), tvMessage.dataLengthBytes);
    event.messages.push_back(tvMessage);
    s_TvInputPriv->callback->notifyTvMessageEvent(event);
}

NotifyQueueDataCallback dataCallback(tv_input_capture_result_t result, uint64_t buff_id) {
    //arm64: long = uint64_t
    sendTvMessage("stream_capture", buff_id);
    return 0;
}

V4L2EventCallBack hinDevEventCallback(int event_type) {
    ALOGW("%s event type: %d", __FUNCTION__, event_type);
    if (s_TvInputPriv == NULL || s_TvInputPriv->mDev == NULL) {
        ALOGE("%s s_TvInputPriv or dev NULL", __FUNCTION__);
        return 0;
    }
    if (!s_TvInputPriv->isOpened) {
       ALOGE("%s The device is not open ", __FUNCTION__);
       return 0;
    }
    bool isHdmiIn = false;
    bool allowSendMessage = false;
    switch (event_type) {
        case V4L2_EVENT_CTRL: {
            {
                Mutex::Autolock autoLock(mLock);
                ALOGI("%s do V4L2_EVENT_CTRL", __FUNCTION__);
                if (s_TvInputPriv->mDev) {
                    isHdmiIn = s_TvInputPriv->mDev->get_HdmiIn(false);
                    ALOGW("%s event type: %d, isHdmiIn=%d", __FUNCTION__, event_type, isHdmiIn);
                    if (!isHdmiIn) {
                        std::map<std::string, std::string> data;
                        s_TvInputPriv->mDev->deal_priv_message("hdmiinout", data);
                        allowSendMessage = true;
                    }
                }
            }
            if (allowSendMessage) {
                sendTvMessage("hdmiinout", STREAM_ID_HDMIIN);
                /*
                 * tell AudioPreviewThread to stop when we can not get hdmiin
                 */
                sendTvMessage("audio_present=0", STREAM_ID_HDMIIN);
            }
            break;
        }
        case V4L2_EVENT_SOURCE_CHANGE: {
            {
                Mutex::Autolock autoLock(mLock);
                ALOGI("%s do V4L2_EVENT_SOURCE_CHANGE", __FUNCTION__);
                if (s_TvInputPriv->mDev) {
                    isHdmiIn = s_TvInputPriv->mDev->get_current_sourcesize(s_HinDevStreamWidth, s_HinDevStreamHeight,s_HinDevStreamFormat);
                    s_TvInputPriv->mDev->markPcieEpNeedRestart(PCIE_CMD_HDMIIN_SOURCE_CHANGE);
                    allowSendMessage = true;
                }
            }
            if (allowSendMessage) {
                TvInputEvent event;
                event.type = TvInputEventType::STREAM_CONFIGURATIONS_CHANGED;
                //event.deviceInfo = mDeviceInfos[1]->deviceInfo;
                s_TvInputPriv->callback->notify(event);
            }
            break;
        }
        case RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST: {
            {
                Mutex::Autolock autoLock(mLock);
                ALOGI("%s do RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST", __FUNCTION__);
                if (s_TvInputPriv->mDev) {
                    std::map<std::string, std::string> data;
                    s_TvInputPriv->mDev->deal_priv_message("hdmiinout", data);
                    allowSendMessage = true;
                }
            }
            if (allowSendMessage) {
                sendTvMessage("hdmiinout", STREAM_ID_HDMIIN);
            }
            break;
        }
        case RK_HDMIRX_V4L2_EVENT_AUDIOINFO: {
            int present = 0;
            {
                Mutex::Autolock autoLock(mLock);
                ALOGI("%s do RK_HDMIRX_V4L2_EVENT_AUDIOINFO", __FUNCTION__);
                if (s_TvInputPriv->mDev && s_TvInputPriv->mDev->started()) {
                    present = s_TvInputPriv->mDev->get_HdmiAudioPresent();
                    allowSendMessage = true;
                }
            }
            if (allowSendMessage) {
                char audio_present[16];
                sprintf(audio_present, "audio_present=%d", present);
                sendTvMessage(audio_present, STREAM_ID_HDMIIN);
             }
             break;
        }
        case CMD_HDMIIN_RESET:
            sendTvMessage("hdmiinreset", STREAM_ID_HDMIIN);
            //s_TvInputPriv->mDev->send_ep_stream(PCIE_CMD_HDMIIN_RESET, NULL);
            break;
    }
    ALOGW("%s width:%d, height:%d, format:0x%x,%d", __FUNCTION__,
        s_HinDevStreamWidth, s_HinDevStreamHeight, s_HinDevStreamFormat, isHdmiIn);
    return 0;
}

NotifyTvPcieCallback tvPcieCallback(pcie_user_cmd_st cmd_st) {
    ALOGD("%s cmd=%d", __FUNCTION__, cmd_st.cmd);
    if (s_TvInputPriv == NULL || s_TvInputPriv->mDev == NULL) {
        ALOGE("%s s_TvInputPriv or dev NULL", __FUNCTION__);
        return 0;
    }
    if (!s_TvInputPriv->isOpened) {
       ALOGE("%s The device is not open ", __FUNCTION__);
       return 0;
    }
    if (cmd_st.cmd == PCIE_CMD_HDMIIN_CTRL) {
        s_TvInputPriv->mDev->setInDevConnected(cmd_st.inDevConnected);
        //TODO
    } else if (cmd_st.cmd == PCIE_CMD_HDMIIN_SOURCE_CHANGE) {
        s_HinDevStreamWidth = cmd_st.frameWidth;
        s_HinDevStreamHeight = cmd_st.frameHeight;
        s_HinDevStreamFormat = cmd_st.framePixelFormat;
        TvInputEvent event;
        event.type = TvInputEventType::STREAM_CONFIGURATIONS_CHANGED;
        s_TvInputPriv->callback->notify(event);
    } else if (cmd_st.cmd == PCIE_CMD_HDMIIN_INPUTIN_OUT) {
        s_TvInputPriv->mDev->setInDevConnected(false);
        //TODO
    } else if (cmd_st.cmd == PCIE_CMD_HDMIIN_RESET) {
        //TODO
    } else {
        return 0;
    }
    ALOGE("%s width:%d, height:%d, format:0x%x, connect=%d", __FUNCTION__, s_HinDevStreamWidth,
        s_HinDevStreamHeight, s_HinDevStreamFormat, cmd_st.inDevConnected);
    return 0;
}

NotifyCommandCallback commandCallback(tv_input_command command) {
    hinDevEventCallback(command.command_id);
    return 0;
}
int RkTvInput::hin_dev_open(int deviceId, int type)
{
    ALOGW("%s deviceId=%d, type=%d", __FUNCTION__, deviceId, type);
    if (!s_TvInputPriv->isOpened /*&& deviceId == SOURCE_DTV*/) {
        /*if (deviceId >=  MAX_HIN_DEVICE_SUPPORTED) {
            ALOGD("provided device id out of bounds , deviceid = %d .\n" , deviceId);
            return -EINVAL;
        }*/
        if (!s_TvInputPriv->mDev) {
            HinDevImpl* hinDevImpl = NULL;
            hinDevImpl = new HinDevImpl;
            if (!hinDevImpl) {
                ALOGE("%s no memory to new hinDevImpl", __FUNCTION__);
                return -ENOMEM;
            }
            s_TvInputPriv->mDev = hinDevImpl;
            s_TvInputPriv->mDev->set_data_callback((V4L2EventCallBack)hinDevEventCallback);
            s_TvInputPriv->mDev->set_command_callback((NotifyCommandCallback)commandCallback);
            s_TvInputPriv->mDev->setTvPcieCallback((NotifyTvPcieCallback)tvPcieCallback);
            if (s_TvInputPriv->mDev->findDevice(deviceId, s_HinDevStreamWidth, s_HinDevStreamHeight,s_HinDevStreamFormat)!= 0) {
                ALOGE("hinDevImpl->findDevice %d failed!", deviceId);
                delete s_TvInputPriv->mDev;
                s_TvInputPriv->mDev = nullptr;
                return -ENOMEM;
            }
            ALOGW("hinDevImpl->findDevice %d ,%d,0x%x,0x%x!", s_HinDevStreamWidth,s_HinDevStreamHeight,s_HinDevStreamFormat,DEFAULT_V4L2_STREAM_FORMAT);
            s_TvInputPriv->isOpened = true;
        }
    }
    return 0;
}
RkTvInput::RkTvInput() {}

void RkTvInput::init() {
    ALOGW("%s", __FUNCTION__);
    // Set up TvInputDeviceInfo and TvStreamConfig
    mDeviceInfos[DEVICE_INFO_INDEX_HDMIIN] = shared_ptr<TvInputDeviceInfoWrapper>(
            new TvInputDeviceInfoWrapper(DEVICE_ID_HDMIIN, TvInputType::OTHER, true));
    mStreamConfigs[DEVICE_INFO_INDEX_HDMIIN] = {
            {STREAM_ID_HDMIIN, shared_ptr<TvStreamConfigWrapper>(new TvStreamConfigWrapper(STREAM_ID_HDMIIN, 720, 1080, false))}};

    s_TvInputPriv = new tv_input_private_t;
    // s_TvInputPriv = (struct tv_input_private_t *) calloc (1, sizeof (struct tv_input_private_t));
    //memset(s_TvInputPriv->mDev,0,sizeof(s_TvInputPriv->mDev));
    //memset(s_TvInputPriv, 0, sizeof(struct tv_input_private_t));
    //s_TvInputPriv->mDev = NULL;
    //s_TvInputPriv->isOpened = false;
    //s_TvInputPriv->isInitialized = false;
    //s_TvInputPriv->callback = callback;
    static constexpr size_t kNumElementsInQueue = 1024;
    static constexpr size_t kElementSizeBytes = sizeof(int32_t);
    mFmqSynchronized = new AidlMessageQueue<int8_t, SynchronizedReadWrite>(kNumElementsInQueue*kElementSizeBytes);
}

::ndk::ScopedAStatus RkTvInput::setRkCallback(const shared_ptr<ITvInputCallback>& in_callback) {
    ALOGW("%s", __FUNCTION__);

    mCallback = in_callback;

    TvInputEvent event;
    event.type = TvInputEventType::DEVICE_AVAILABLE;

    event.deviceInfo = mDeviceInfos[DEVICE_INFO_INDEX_HDMIIN]->deviceInfo;
    mCallback->notify(event);

    s_TvInputPriv->mDev = NULL;
    s_TvInputPriv->isOpened = false;
    s_TvInputPriv->isInitialized = false;
    s_TvInputPriv->callback = mCallback;

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RkTvInput::setRkTvMessageEnabled(int32_t deviceId, int32_t streamId,
                                                  TvMessageEventType in_type, bool enabled) {
    ALOGW("%s deviceId=%d, streamId=%d", __FUNCTION__, deviceId, streamId);

    if (mStreamConfigs.count(deviceId) == 0) {
        ALOGW("Device with id %d isn't available", deviceId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_ARGUMENTS);
    }

    mTvMessageEventEnabled[deviceId][streamId][in_type] = enabled;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RkTvInput::getRkTvMessageQueueDesc(
        MQDescriptor<int8_t, SynchronizedReadWrite>* out_queue, int32_t deviceId,
        int32_t streamId) {
    ALOGW("%s deviceId=%d, streamId=%d", __FUNCTION__, deviceId, streamId);
    if (mStreamConfigs.count(deviceId) == 0) {
        ALOGW("Device with id %d isn't available", deviceId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_ARGUMENTS);
    }
    *out_queue = mFmqSynchronized->dupeDesc();

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RkTvInput::getRkStreamConfigurations(int32_t deviceId,
                                                      vector<RkTvStreamConfig>* _aidl_return) {
    ALOGW("%s deviceId=%d", __FUNCTION__, deviceId);

    if (mStreamConfigs.count(deviceId) == 0) {
        ALOGE("Device with id %d isn't available", deviceId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_ARGUMENTS);
    }
    if (hin_dev_open(deviceId, 0) < 0) {
        ALOGE("Open hdmi failed!!!");
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
    }

    for (auto const& iconfig : mStreamConfigs[deviceId]) {
        RkTvStreamConfig config;
        config.base = iconfig.second->streamConfig;
        config.deviceId = deviceId;
        config.width = s_HinDevStreamWidth;
        config.height = s_HinDevStreamHeight;
        config.format = s_HinDevStreamFormat;
        config.usage = RK_GRALLOC_USAGE_STRIDE_ALIGN_64;
        config.buffCount = APP_PREVIEW_BUFF_CNT;
        _aidl_return->push_back(config);
    }

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RkTvInput::openRkStream(int32_t deviceId, int32_t streamId,
                                         vector<AidlNativeHandle>* _aidl_return) {
    ALOGW("%s deviceId=%d, streamId=%d, type=", __FUNCTION__, deviceId, streamId);

    if (mStreamConfigs.count(deviceId) == 0 ||
        mStreamConfigs[deviceId].count(streamId) == 0) {
        ALOGE("%s Stream with device id %d, stream id %d isn't available", __FUNCTION__, deviceId, streamId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_ARGUMENTS);
    }
    if (mStreamConfigs[deviceId][streamId]->isOpen) {
        ALOGE("%s Stream with device id %d, stream id %d is already opened", __FUNCTION__, deviceId, streamId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
    }

    if (!s_TvInputPriv) {
        ALOGE("%s s_TvInputPriv is NULL failed", __FUNCTION__);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
    }
    if(s_TvInputPriv->isInitialized) {
        int width = s_HinDevStreamWidth;
        int height = s_HinDevStreamHeight;

        if(s_TvInputPriv->mDev->set_format(width, height, s_HinDevStreamFormat)) {
            ALOGE("%s set_format failed! force release", __func__);
            //closeStream(deviceId, streamId);
            return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
        }
        int dst_width = 0, dst_height = 0;
        bool use_zme = s_TvInputPriv->mDev->check_zme(width, height, &dst_width, &dst_height);
        if(use_zme) {
            s_TvInputPriv->mDev->set_crop(0, 0, dst_width, dst_height);
        } else {
            s_TvInputPriv->mDev->set_crop(0, 0, width, height);
        }
        /*if (stream->base_stream.type & TYPE_SIDEBAND_WINDOW) {
            ALOGD("stream->base_stream.type & TYPE_SIDEBAND_WINDOW");
            s_TvInputPriv->mStreamType = TV_STREAM_TYPE_INDEPENDENT_VIDEO_SOURCE;
            stream->base_stream.sideband_stream_source_handle = native_handle_clone(s_TvInputPriv->mDev->getSindebandBufferHandle());
            out_buffer = stream->base_stream.sideband_stream_source_handle;
            if (s_TvInputPriv->mDev->getSindebandCancelBufferHandle() == NULL) {
                ALOGD("%s cancel buffer handle is NULL", __FUNCTION__);
            } else {
                stream->sideband_cancel_stream_source_handle = native_handle_clone(s_TvInputPriv->mDev->getSindebandCancelBufferHandle());
                out_cancel_buffer = stream->sideband_cancel_stream_source_handle;
            }
        }*/
        if (s_TvInputPriv->mDev->getSindebandBufferHandle() != NULL) {
            out_buffer = native_handle_clone(s_TvInputPriv->mDev->getSindebandBufferHandle());
        }
        s_TvInputPriv->mDev->start();
        /*update audio info when open*/
        hinDevEventCallback(RK_HDMIRX_V4L2_EVENT_AUDIOINFO);
        if (out_buffer) {
            mStreamConfigs[deviceId][streamId]->handle = out_buffer;
            //*_aidl_return = makeToAidl(mStreamConfigs[deviceId][streamId]->handle);
            _aidl_return->push_back(makeToAidl(mStreamConfigs[deviceId][streamId]->handle));
        }
    }

    /*mStreamConfigs[in_deviceId][in_streamId]->handle = createNativeHandle(in_streamId);
    *_aidl_return = makeToAidl(mStreamConfigs[in_deviceId][in_streamId]->handle);*/
    mStreamConfigs[deviceId][streamId]->isOpen = true;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RkTvInput::closeRkStream(int32_t deviceId, int32_t streamId) {
    ALOGW("%s device_id=%d, stream_id=%d", __FUNCTION__, deviceId, streamId);

    if (mStreamConfigs.count(deviceId) == 0 ||
        mStreamConfigs[deviceId].count(streamId) == 0) {
        ALOGW("Stream with device id %d, stream id %d isn't available", deviceId, streamId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_ARGUMENTS);
    }
    if (!mStreamConfigs[deviceId][streamId]->isOpen) {
        ALOGW("Stream with device id %d, stream id %d is already closed", deviceId, streamId);
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
    }

    if (s_TvInputPriv) {
        if (s_TvInputPriv->mDev) {
            if (out_buffer) {
                native_handle_close(out_buffer);
                native_handle_delete(out_buffer);
                out_buffer=NULL;
            }
            if (out_cancel_buffer) {
                native_handle_close(out_cancel_buffer);
                native_handle_delete(out_cancel_buffer);
                out_cancel_buffer = nullptr;
            }
            s_TvInputPriv->mDev->stop();
            s_TvInputPriv->isInitialized = false;
            s_TvInputPriv->isOpened = false;
            //delete s_TvInputPriv->mDev;
            {
                ALOGW("%s wait lock to set mDev NULL", __FUNCTION__);
                Mutex::Autolock autoLock(mLock);
                ALOGW("%s enter lock and set mDev NULL", __FUNCTION__);
                s_TvInputPriv->mDev = nullptr;
            }
            if (deviceId < 0 && streamId == 0) {
                ALOGD("%s,invail deviceId=%d, streamId=%d return -EINVAL",
                    __FUNCTION__, deviceId, streamId);
                return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_ARGUMENTS);
            }
            mStreamConfigs[deviceId][streamId]->isOpen = false;
            return ::ndk::ScopedAStatus::ok();
        }
    }

    //native_handle_delete(mStreamConfigs[deviceId][streamId]->handle);
    //mStreamConfigs[deviceId][streamId]->handle = nullptr;
    mStreamConfigs[deviceId][streamId]->isOpen = false;
    return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
}

::ndk::ScopedAStatus RkTvInput::privRkCmdFromApp(const RkTvPrivAppCmdInfo& cmdInfo) {
    ALOGW("%s", __FUNCTION__);
    if (s_TvInputPriv && s_TvInputPriv->isInitialized && s_TvInputPriv->mDev) {
        std::map<std::string, std::string> data;
        for (size_t i = 0; i < cmdInfo.data.size(); i++) {
            data.insert({cmdInfo.data[i].key, cmdInfo.data[i].value});
        }
        s_TvInputPriv->mDev->deal_priv_message(cmdInfo.action.c_str(), data);
        return ::ndk::ScopedAStatus::ok();
    }
    return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
}

::ndk::ScopedAStatus RkTvInput::setRkPreviewInfo(int32_t       deviceId, int32_t streamId, int32_t initType) {
    if (s_TvInputPriv && s_TvInputPriv->mDev && !s_TvInputPriv->isInitialized) {
        if (s_TvInputPriv->mDev->init(deviceId, initType,
                s_HinDevStreamWidth, s_HinDevStreamHeight,s_HinDevStreamFormat)!= 0) {
            ALOGE("hinDevImpl->init %d failed!", deviceId);
            return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
        }
        s_TvInputPriv->isInitialized = true;
    }
    if(s_TvInputPriv->isInitialized){
        s_TvInputPriv->mDev->set_preview_info(0, 0, 0, 0);
        return ::ndk::ScopedAStatus::ok();
    }
    return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
}

::ndk::ScopedAStatus RkTvInput::setSinglePreviewBuffer(int64_t bufId, const AidlNativeHandle& bufHandle) {
    ALOGW("%s bufId=%" PRIu64, __FUNCTION__, bufId);
    if (!s_TvInputPriv->isInitialized || !s_TvInputPriv->mDev) {
        return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
    }
    s_TvInputPriv->mDev->set_preview_buffer(makeFromAidl(bufHandle), bufId);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus RkTvInput::requestRkCapture(int32_t deviceId, int32_t streamId, int64_t buffId, int32_t seq) {
    ALOGW("%s deviceId=%d, streamId=%d, bufId=%" PRIu64 ", seq=%d", __FUNCTION__, deviceId, streamId, buffId, seq);
    if (s_TvInputPriv && s_TvInputPriv->isInitialized && s_TvInputPriv->mDev /*&& buffer != nullptr*/) {
        //requestInfo.seq = seq;
        s_TvInputPriv->mDev->set_preview_callback((NotifyQueueDataCallback)dataCallback);
        s_TvInputPriv->mDev->request_capture(/*buffer,*/ buffId);
        ::ndk::ScopedAStatus::ok();
    }
    return ::ndk::ScopedAStatus::fromServiceSpecificError(ITvInput::STATUS_INVALID_STATE);
}


}  // namespace input
}  // namespace tv
}  // namespace hardware
}  // namespace rockchip
}  // namespace aidl
