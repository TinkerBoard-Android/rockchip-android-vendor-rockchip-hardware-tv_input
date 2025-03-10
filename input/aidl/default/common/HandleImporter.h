/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef TVINPUT_COMMON_1_0_HANDLEIMPORTED_H
#define TVINPUT_COMMON_1_0_HANDLEIMPORTED_H

#include <android/hardware/graphics/mapper/2.0/IMapper.h>
#include <android/hardware/graphics/mapper/3.0/IMapper.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <cutils/native_handle.h>
#include <utils/Mutex.h>

using android::hardware::graphics::mapper::V2_0::IMapper;
using android::hardware::graphics::mapper::V2_0::YCbCrLayout;

namespace android {
namespace hardware {
namespace tvinput {
namespace common {
namespace helper {

// Borrowed from graphics HAL. Use this until gralloc mapper HAL is working
class HandleImporter {
public:
    HandleImporter();

    // In IComposer, any buffer_handle_t is owned by the caller and we need to
    // make a clone for hwcomposer2.  We also need to translate empty handle
    // to nullptr.  This function does that, in-place.
    bool importBuffer(buffer_handle_t& handle);
    void freeBuffer(buffer_handle_t handle);
    bool importFence(const native_handle_t* handle, int& fd) const;
    void closeFence(int fd) const;

    // Locks 1-D buffer. Assumes caller has waited for acquire fences.
    void* lock(buffer_handle_t& buf, uint64_t cpuUsage, size_t size);

    // Locks 2-D buffer. Assumes caller has waited for acquire fences.
    void* lock(buffer_handle_t& buf, uint64_t cpuUsage, const IMapper::Rect& accessRegion);

    // Assumes caller has waited for acquire fences.
    YCbCrLayout lockYCbCr(buffer_handle_t& buf, uint64_t cpuUsage,
                          const IMapper::Rect& accessRegion);

    int unlock(buffer_handle_t& buf); // returns release fence

private:
    void initializeLocked();
    void cleanup();

    template<class M, class E>
    bool importBufferInternal(const sp<M> mapper, buffer_handle_t& handle);
    template<class M, class E>
    YCbCrLayout lockYCbCrInternal(const sp<M> mapper, buffer_handle_t& buf, uint64_t cpuUsage,
            const IMapper::Rect& accessRegion);
    template<class M, class E>
    int unlockInternal(const sp<M> mapper, buffer_handle_t& buf);

    Mutex mLock;
    bool mInitialized;
    sp<IMapper> mMapperV2;
    sp<graphics::mapper::V3_0::IMapper> mMapperV3;
    sp<graphics::mapper::V4_0::IMapper> mMapperV4;
};

} // namespace helper
} // namespace common
} // namespace tvinput
} // namespace hardware
} // namespace android

#endif // TVINPUT_COMMON_1_0_HANDLEIMPORTED_H
