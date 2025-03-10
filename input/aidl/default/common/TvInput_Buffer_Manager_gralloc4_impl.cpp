/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define LOG_TAG "TvInputBufferManager"

#include <utils/Log.h>
#include <hardware/hardware_rockchip.h>
#include "TvInput_Buffer_Manager_gralloc4_impl.h"
#include <hwbinder/IPCThreadState.h>
#include <sync/sync.h>
#include <drm_fourcc.h>
//#include "LogHelper.h"

#include <linux/videodev2.h>

using android::hardware::graphics::mapper::V4_0::Error;
using android::hardware::graphics::mapper::V4_0::IMapper;
using android::hardware::graphics::allocator::V4_0::IAllocator;
using android::hardware::graphics::common::V1_2::BufferUsage;
using android::hardware::graphics::mapper::V4_0::BufferDescriptor;
using android::hardware::hidl_vec;

using android::gralloc4::MetadataType_PlaneLayouts;
using android::gralloc4::MetadataType_BufferId;
using android::gralloc4::decodeBufferId;
using android::gralloc4::decodePlaneLayouts;
using android::gralloc4::MetadataType_Usage;
using android::gralloc4::decodeUsage;
using android::gralloc4::MetadataType_PlaneLayouts;
using android::gralloc4::decodePlaneLayouts;
using android::gralloc4::MetadataType_PixelFormatFourCC;
using android::gralloc4::decodePixelFormatFourCC;
using android::gralloc4::MetadataType_PixelFormatModifier;
using android::gralloc4::decodePixelFormatModifier;
using android::gralloc4::MetadataType_PixelFormatRequested;
using android::gralloc4::decodePixelFormatRequested;
using android::gralloc4::MetadataType_AllocationSize;
using android::gralloc4::decodeAllocationSize;
using android::gralloc4::MetadataType_LayerCount;
using android::gralloc4::decodeLayerCount;
using android::gralloc4::MetadataType_Dataspace;
using android::gralloc4::decodeDataspace;
using android::gralloc4::MetadataType_Crop;
using android::gralloc4::decodeCrop;
using android::gralloc4::MetadataType_Width;
using android::gralloc4::decodeWidth;
using android::gralloc4::MetadataType_Height;
using android::gralloc4::decodeHeight;
using ::android::Mutex;

using aidl::android::hardware::graphics::common::Dataspace;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::ExtendableType;
using aidl::android::hardware::graphics::common::PlaneLayout;
using aidl::android::hardware::graphics::common::Rect;
using aidl::android::hardware::graphics::common::PlaneLayoutComponentType;
using android::hardware::graphics::common::V1_2::PixelFormat;

#define IMPORTBUFFER_CB 1

namespace common {

namespace {

static constexpr Error kTransactionError = Error::NO_RESOURCES;

#define GRALLOC_ARM_METADATA_TYPE_NAME "arm.graphics.ArmMetadataType"
const static IMapper::MetadataType ArmMetadataType_PLANE_FDS
{
	GRALLOC_ARM_METADATA_TYPE_NAME,
	// static_cast<int64_t>(aidl::arm::graphics::ArmMetadataType::PLANE_FDS)
    1   // 就是上面的 'PLANE_FDS'
};

static IMapper &get_mapperservice()
{
    static android::sp<IMapper> cached_service = IMapper::getService();
    return *cached_service;
}

static IAllocator &get_allocservice()
{
    static android::sp<IAllocator> cached_service = IAllocator::getService();
    return *cached_service;
}

static inline void sBufferDescriptorInfo(std::string name, uint32_t width, uint32_t height,
                                         PixelFormat format, uint32_t layerCount, uint64_t usage,
                                         IMapper::BufferDescriptorInfo* outDescriptorInfo) {
    outDescriptorInfo->name = name;
    outDescriptorInfo->width = width;
    outDescriptorInfo->height = height;
    outDescriptorInfo->layerCount = layerCount;
    outDescriptorInfo->format = static_cast<android::hardware::graphics::common::V1_2::PixelFormat>(format);
    outDescriptorInfo->usage = usage;
    outDescriptorInfo->reservedSize = 0;
}

uint64_t getValidUsageBits() {
    static const uint64_t validUsageBits = []() -> uint64_t {
        uint64_t bits = 0;
        for (const auto bit :
             android::hardware::hidl_enum_range<android::hardware::graphics::common::V1_2::BufferUsage>()) {
            bits = bits | bit;
        }
        return bits;
    }();
    return validUsageBits;
}

template <typename T>
static int get_metadata(IMapper &mapper, buffer_handle_t handle, IMapper::MetadataType type,
                        android::status_t (*decode)(const hidl_vec<uint8_t> &, T *), T *value)
{
	void *handle_arg = const_cast<native_handle_t *>(handle);
	assert(handle_arg);
	assert(value);
	assert(decode);

	int err = 0;
	mapper.get(handle_arg, type, [&err, value, decode](Error error, const hidl_vec<uint8_t> &metadata)
	            {
		            if (error != Error::NONE)
		            {
			            err = android::BAD_VALUE;
			            return;
		            }
		            err = decode(metadata, value);
		        });
	return err;
}

android::status_t static decodeArmPlaneFds(const hidl_vec<uint8_t>& input, std::vector<int64_t>* fds)
{
    assert (fds != nullptr);
    int64_t size = 0;

    memcpy(&size, input.data(), sizeof(int64_t));
    if (size < 0)
    {
        return android::BAD_VALUE;
    }

    fds->resize(size);

    const uint8_t *tmp = input.data() + sizeof(int64_t);
    memcpy(fds->data(), tmp, sizeof(int64_t) * size);

    return android::NO_ERROR;
}

}
// static
TvInputBufferManager* TvInputBufferManager::GetInstance() {
    static TvInputBufferManagerImpl instance;
    return &instance;
}

// static
int TvInputBufferManagerImpl::GetHalPixelFormat(buffer_handle_t buffer) {
    ALOGV("GetHalPixelFormat %p", buffer);
    auto &mapper = get_mapperservice();
    // android::PixelFormat format;    // *format_requested
    PixelFormat format;    // *format_requested

    int err = get_metadata(mapper, buffer, MetadataType_PixelFormatRequested, decodePixelFormatRequested, &format);
    if (err != android::OK)
    {
        ALOGE("ailed to get pixel_format_requested. err : %d", err);
        return err;
    }
    ALOGV("GetHalPixelFormat %d", (int)format);

    return (int)format;
}

// static
uint32_t TvInputBufferManager::GetNumPlanes(buffer_handle_t buffer) {
    ALOGV("GetNumPlanes %p", buffer);
    int hal_pixel_format = GetInstance()->GetHalPixelFormat(buffer);
    /* only support one physical plane now */
    switch (hal_pixel_format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_BGR_888:
        return 1;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
    case HAL_PIXEL_FORMAT_YCrCb_NV12:
    case HAL_PIXEL_FORMAT_YCbCr_444_888:
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP: // NV21
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED: //NV12
    case HAL_PIXEL_FORMAT_YCbCr_422_SP_10:
        return 2;
    default :
        return 1;
    }
    ALOGE("Unknown format: %d", hal_pixel_format);
    return 0;
}

// static
uint32_t TvInputBufferManager::GetV4L2PixelFormat(buffer_handle_t buffer) {
    ALOGV("GetV4L2PixelFormat %p", buffer);
    int hal_pixel_format = GetInstance()->GetHalPixelFormat(buffer);
    ALOGV("hal_pixel_format %d", hal_pixel_format);

    switch (hal_pixel_format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
        return V4L2_PIX_FMT_ABGR32;
    case HAL_PIXEL_FORMAT_BGR_888:
        return V4L2_PIX_FMT_BGR24;
    // There is no standard V4L2 pixel format corresponding to
    // DRM_FORMAT_xBGR8888.  We use our own V4L2 format extension
    // V4L2_PIX_FMT_RGBX32 here.
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
        return V4L2_PIX_FMT_RGBX32;

    case HAL_PIXEL_FORMAT_BLOB:
        return V4L2_PIX_FMT_JPEG;
        // Semi-planar formats.
    case HAL_PIXEL_FORMAT_YCrCb_NV12:
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
        return V4L2_PIX_FMT_NV12;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        return V4L2_PIX_FMT_NV21;
    case HAL_PIXEL_FORMAT_YCbCr_444_888:
        return V4L2_PIX_FMT_NV24;

    default:
      return V4L2_PIX_FMT_NV12;
    }
    ALOGE("Could not convert format  to V4L2 pixel format");
    return 0;
}

int TvInputBufferManagerImpl::GetWidth(buffer_handle_t handle)
{
    auto &mapper = get_mapperservice();
    uint64_t width;

    int err = get_metadata(mapper, handle, MetadataType_Width, decodeWidth, &width);
    if (err != android::OK)
    {
        ALOGE(" %s error: %d", __FUNCTION__, err);
    }

    return (int)width;
}

int TvInputBufferManagerImpl::GetHeight(buffer_handle_t handle)
{
    auto &mapper = get_mapperservice();
    uint64_t height;

    int err = get_metadata(mapper, handle, MetadataType_Height, decodeHeight, &height);
    if (err != android::OK)
    {
        ALOGE(" %s error: %d", __FUNCTION__, err);
    }

    return (int)height;
}

int TvInputBufferManagerImpl::GetHandleBufferSize(buffer_handle_t handle) {
    ALOGV("GetHandleBufferSize handle:%p", handle);

    auto &mapper = get_mapperservice();
    uint64_t bufferSize;

    int err = get_metadata(mapper, handle, MetadataType_AllocationSize, decodeAllocationSize, &bufferSize);
    if (err != android::OK)
    {
        ALOGE("Failed to get allocation_size. err : %d", err);
        return err;
    }

    return (int)bufferSize;
}

// static
size_t TvInputBufferManager::GetPlaneStride(buffer_handle_t buffer,
                                           size_t plane) {
    ALOGV("GetPlaneStride %p plane:%zu", buffer, plane);
    if (plane >= GetNumPlanes(buffer)) {
        ALOGE(" %s Invalid plane: : %zu", __FUNCTION__, plane);
        return 0;
    }

    auto &mapper = get_mapperservice();
    std::vector<PlaneLayout> layouts;
    int format_requested;

    format_requested = GetInstance()->GetHalPixelFormat(buffer);

    /* 若 'format_requested' "不是" HAL_PIXEL_FORMAT_YCrCb_NV12_10, 则 ... */
    if ( format_requested != HAL_PIXEL_FORMAT_YCrCb_NV12_10 )
    {
        int err = get_metadata(mapper, buffer, MetadataType_PlaneLayouts, decodePlaneLayouts, &layouts);
        if (err != android::OK || layouts.size() < 1)
        {
            ALOGE(" %s Failed to get plane layouts. err: %d", __FUNCTION__, err);
            return err;
        }

        if ( layouts.size() < plane )
        {
            ALOGE("it's not reasonable to get global pixel_stride of buffer with planes less than : %zu", plane);
        }

        //ALOGD(" w/h: %ld x %ld, stride:%ld size:%ld ", layouts[plane].widthInSamples, layouts[plane].heightInSamples,
        //         layouts[plane].strideInBytes, layouts[plane].totalSizeInBytes);

        return layouts[plane].strideInBytes;
    }
    /* 否则, 即 'format_requested' "是" HAL_PIXEL_FORMAT_YCrCb_NV12_10, 则 ... */
    else
    {
        uint64_t width;
        int byte_stride;

        width = GetInstance()->GetWidth(buffer);

        // .trick : from CSY : 分配 rk_video_decoder 输出 buffers 时, 要求的 byte_stride of buffer in NV12_10, 已经通过 width 传入.
        //          原理上, NV12_10 的 pixel_stride 和 byte_stride 是不同的, 但是这里保留 之前 rk_drm_gralloc 的赋值方式.
        byte_stride = (int)width;
        return byte_stride;
    }

}

// static
size_t TvInputBufferManager::GetPlaneSize(buffer_handle_t buffer, size_t plane) {
    ALOGV("GetPlaneSize %p plane:%zu", buffer, plane);
    if (plane >= GetNumPlanes(buffer)) {
        ALOGE(" %s Invalid plane: %zu", __FUNCTION__, plane);
        return 0;
    }

    auto &mapper = get_mapperservice();
    std::vector<PlaneLayout> layouts;
    int format_requested;

    format_requested = GetInstance()->GetHalPixelFormat(buffer);

    /* 若 'format_requested' "不是" HAL_PIXEL_FORMAT_YCrCb_NV12_10, 则 ... */
    if ( format_requested != HAL_PIXEL_FORMAT_YCrCb_NV12_10 )
    {
        int err = get_metadata(mapper, buffer, MetadataType_PlaneLayouts, decodePlaneLayouts, &layouts);
        if (err != android::OK || layouts.size() < 1)
        {
            ALOGE(" %s Failed to get plane layouts. err: %d", __FUNCTION__, err);
            return err;
        }

        if ( layouts.size() < plane )
        {
            ALOGE("it's not reasonable to get global pixel_stride of buffer with planes less than %zu", plane);
        }

        //ALOGD(" w/h: %ld x %ld, stride:%ld size:%ld ", layouts[plane].widthInSamples, layouts[plane].heightInSamples,
        //         layouts[plane].strideInBytes, layouts[plane].totalSizeInBytes);

        return layouts[plane].totalSizeInBytes;
    }
    /* 否则, 即 'format_requested' "是" HAL_PIXEL_FORMAT_YCrCb_NV12_10, 则 ... */
    else
    {
        uint64_t width;
        int byte_stride;

        width = GetInstance()->GetWidth(buffer);

        // .trick : from CSY : 分配 rk_video_decoder 输出 buffers 时, 要求的 byte_stride of buffer in NV12_10, 已经通过 width 传入.
        //          原理上, NV12_10 的 pixel_stride 和 byte_stride 是不同的, 但是这里保留 之前 rk_drm_gralloc 的赋值方式.
        byte_stride = (int)width;
        return byte_stride;
    }
}

status_t TvInputBufferManagerImpl::validateBufferDescriptorInfo(
        IMapper::BufferDescriptorInfo* descriptorInfo) const {
    uint64_t validUsageBits = getValidUsageBits();

    if (descriptorInfo->usage & ~validUsageBits) {
        ALOGE("buffer descriptor contains invalid usage bits 0x%" PRIx64,
              descriptorInfo->usage & ~validUsageBits);
        return android::BAD_VALUE;
    }
    return android::NO_ERROR;
}

status_t TvInputBufferManagerImpl::createDescriptor(void* bufferDescriptorInfo,
                                          void* outBufferDescriptor) const {
    IMapper::BufferDescriptorInfo* descriptorInfo =
            static_cast<IMapper::BufferDescriptorInfo*>(bufferDescriptorInfo);
    BufferDescriptor* outDescriptor = static_cast<BufferDescriptor*>(outBufferDescriptor);

    status_t status = validateBufferDescriptorInfo(descriptorInfo);
    if (status != android::NO_ERROR) {
        return status;
    }

    Error error;
    auto hidl_cb = [&](const auto& tmpError, const auto& tmpDescriptor) {
        error = tmpError;
        if (error != Error::NONE) {
            return;
        }
        *outDescriptor = tmpDescriptor;
    };

    auto &mapper = get_mapperservice();
    android::hardware::Return<void> ret = mapper.createDescriptor(*descriptorInfo, hidl_cb);

    return static_cast<status_t>((ret.isOk()) ? error : kTransactionError);
}

status_t TvInputBufferManagerImpl::importBuffer(buffer_handle_t rawHandle,
                                      buffer_handle_t* outBufferHandle) const {
    ALOGV("import rawBuffer :%p", rawHandle);
    Error error;
    auto &mapper = get_mapperservice();
    auto ret = mapper.importBuffer(android::hardware::hidl_handle(rawHandle), [&](const auto& tmpError, const auto& tmpBuffer) {
        error = tmpError;
        if (error != Error::NONE) {
            return;
        }
        *outBufferHandle = static_cast<buffer_handle_t>(tmpBuffer);
    });

    return static_cast<status_t>((ret.isOk()) ? error : kTransactionError);
}

status_t TvInputBufferManagerImpl::freeBuffer(buffer_handle_t bufferHandle) const {
    ALOGV("freeBuffer %p", bufferHandle);
    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    auto &mapper = get_mapperservice();
    auto ret = mapper.freeBuffer(buffer);

    auto error = (ret.isOk()) ? static_cast<Error>(ret) : kTransactionError;
    ALOGE_IF(error != Error::NONE, "freeBuffer(%p) failed with %d", buffer, error);
    return static_cast<status_t>((ret.isOk()) ? error : kTransactionError);
}


TvInputBufferManagerImpl::TvInputBufferManagerImpl() {
}

TvInputBufferManagerImpl::~TvInputBufferManagerImpl() {
}

int TvInputBufferManagerImpl::Allocate(size_t width,
                                      size_t height,
                                      uint32_t format,
                                      uint64_t usage,
                                      BufferType type,
                                      buffer_handle_t* out_buffer,
                                      uint32_t* out_stride) {
    if (type == GRALLOC) {
        return AllocateGrallocBuffer(width, height, format, usage, out_buffer,
                                     out_stride);
    } else {
        ALOGE("Invalid buffer type: %d", type);
        return -EINVAL;
    }
}

int TvInputBufferManagerImpl::Free(buffer_handle_t buffer) {
    ALOGD("Free %p", buffer);

    auto context_it = buffer_context_.find(buffer);
    if (context_it == buffer_context_.end()) {
        ALOGE("Failed Unknown buffer %p", buffer);
        return -EINVAL;
    }

    auto buffer_context = context_it->second.get();

    if (buffer_context->type == GRALLOC) {
        #if IMPORTBUFFER_CB == 1
        if (buffer) {
            freeBuffer(buffer);           
            buffer = nullptr;
        }
        #else
        if (buffer) {
            auto abuffer = const_cast<native_handle_t*>(
                    buffer);
            native_handle_close(abuffer);
            native_handle_delete(abuffer);
            buffer = nullptr;
        }
        #endif
        return 0;
    } else {
        // TODO(jcliang): Implement deletion of SharedMemory.
        return -EINVAL;
    }
}


int TvInputBufferManagerImpl::FreeLocked(buffer_handle_t buffer) {
    ALOGD("Free %p", buffer);

    #if IMPORTBUFFER_CB == 1
    if (buffer) {
        freeBuffer(buffer);           
        buffer = nullptr;
    }
    #else
    if (buffer) {
        auto abuffer = const_cast<native_handle_t*>(
                buffer);
        native_handle_close(abuffer);
        native_handle_delete(abuffer);
        buffer = nullptr;
    }
    #endif
    return 0;
}

int TvInputBufferManagerImpl::Register(buffer_handle_t buffer, buffer_handle_t* outbuffer) {
    ALOGV("Register buffer:%p", buffer);
    auto context_it = buffer_context_.find(buffer);
    if (context_it != buffer_context_.end()) {
        context_it->second->usage++;
        return 1;
    }

    std::unique_ptr<BufferContext> buffer_context(new struct BufferContext);

    buffer_context->type = GRALLOC;

    int ret = importBuffer(buffer, outbuffer);

    ALOGD("after register buffer:%p outbufferptr:%p *outbuffer:%p", buffer, outbuffer, *outbuffer);

    if (ret) {
        ALOGE("Failed to register gralloc buffer");
        return ret;
    }
    
    buffer_context->usage = 1;
    buffer_context_[*outbuffer] = std::move(buffer_context);
    ALOGV("Register buffer ok");

    return 0;
}

int TvInputBufferManagerImpl::Deregister(buffer_handle_t buffer) {
    ALOGV("Deregister %p", buffer);

    auto context_it = buffer_context_.find(buffer);
    if (context_it == buffer_context_.end()) {
        ALOGE("Failed Unknown buffer %p", buffer);
        return -EINVAL;
    }
    auto buffer_context = context_it->second.get();
    if (buffer_context->type == GRALLOC) {
        if (!--buffer_context->usage) {
            // Unmap all the existing mapping of bo.
            buffer_context_.erase(context_it);

            int ret = freeBuffer(buffer);

            if (ret) {
                ALOGE("Failed to unregister gralloc buffer");
                return ret;
            }
        }
        return 0;
    } else {
        ALOGE("Invalid buffer type: %d", buffer_context->type);
        return -EINVAL;
    }
}

int TvInputBufferManagerImpl::ImportBufferLocked(buffer_handle_t& rawHandle) {
    Error error;
    buffer_handle_t importedHandle;
    auto &mapper = get_mapperservice();
    auto ret = mapper.importBuffer(android::hardware::hidl_handle(rawHandle), [&](const auto& tmpError, const auto& tmpBuffer) {
        error = tmpError;
        if (error == Error::NONE) {
            importedHandle = static_cast<buffer_handle_t>(tmpBuffer);
        }
    });
    if (error != Error::NONE) {
        return -EINVAL;
    }

    rawHandle = importedHandle;
    ALOGD("%s rawBuffer :%p, outHandle = %p", __FUNCTION__, rawHandle, importedHandle);
    return 0;
}

int TvInputBufferManagerImpl::Lockinternal(buffer_handle_t bufferHandle,
                                  uint32_t flags,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t width,
                                  uint32_t height,
                                  void** out_addr) {
    ALOGV("lock buffer:%p   %d, %d, %d, %d, %d", bufferHandle, x, y,width, height, flags);

    auto context_it = buffer_context_.find(bufferHandle);
    if (context_it == buffer_context_.end()) {
        ALOGE("Failed Unknown buffer %p", bufferHandle);
        return -EINVAL;
    }
    auto buffer_context = context_it->second.get();

    uint32_t num_planes = GetNumPlanes(bufferHandle);
    if (!num_planes) {
        return -EINVAL;
    }

    if (buffer_context->type == GRALLOC) {
        auto &mapper = get_mapperservice();
        auto buffer = const_cast<native_handle_t*>(bufferHandle);
        ALOGD("lock buffer:%p", &buffer);

        IMapper::Rect accessRegion = {(int)x, (int)y, (int)width, (int)height};

        android::hardware::hidl_handle acquireFenceHandle; // dummy

        Error error;
        auto ret = mapper.lock(buffer,
                               flags,
                               accessRegion,
                               acquireFenceHandle,
                               [&](const auto& tmpError, const auto& tmpData) {
                                    error = tmpError;
                                    if (error != Error::NONE) {
                                        return;
                                    }
                                    *out_addr = tmpData;
                               });

        error = (ret.isOk()) ? error : kTransactionError;

        ALOGE_IF(error != Error::NONE, "lock(%p, ...) failed: %d", bufferHandle, error);

        return (int)error;
    } else {
        ALOGE("Invalid buffer type: %d", buffer_context->type);
        return -EINVAL;
    }

    return 0;
}

int TvInputBufferManagerImpl::Unlockinternal(buffer_handle_t bufferHandle) {
    ALOGV("Unlock buffer:%p", bufferHandle);

    auto context_it = buffer_context_.find(bufferHandle);
    if (context_it == buffer_context_.end()) {
        ALOGE("Failed Unknown buffer %p", bufferHandle);
        return -EINVAL;
    }
    auto buffer_context = context_it->second.get();
    if (buffer_context->type == GRALLOC) {
        auto &mapper = get_mapperservice();
        auto buffer = const_cast<native_handle_t*>(bufferHandle);
        ALOGV("Unlock buffer:%p", buffer);

        int releaseFence = -1;
        Error error;
        auto ret = mapper.unlock(buffer,
                                 [&](const auto& tmpError, const auto& tmpReleaseFence)
                                 {
            error = tmpError;
            if (error != Error::NONE) {
                return;
            }

            auto fenceHandle = tmpReleaseFence.getNativeHandle(); // 预期 unlock() 不会返回有效的 release_fence.
            if (fenceHandle && fenceHandle->numFds == 1)
            {
                ALOGE("got unexpected valid fd of release_fence : %d", fenceHandle->data[0]);

                int fd = dup(fenceHandle->data[0]);
                if (fd >= 0) {
                    releaseFence = fd;
                } else {
                    ALOGE("failed to dup unlock release fence");
                    sync_wait(fenceHandle->data[0], -1);
                }
            }
                                 });

        if (!ret.isOk()) {
            error = kTransactionError;
        }

        if (error != Error::NONE) {
            ALOGE("unlock(%p) failed with %d", bufferHandle, error);
        }

    }

    return 0;
}

int TvInputBufferManagerImpl::Lock(buffer_handle_t bufferHandle,
                                  uint32_t flags,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t width,
                                  uint32_t height,
                                  void** out_addr) {
    ALOGV("lock buffer:%p   %d, %d, %d, %d, %d", bufferHandle, x, y,width, height, flags);

    auto context_it = buffer_context_.find(bufferHandle);
    if (context_it == buffer_context_.end()) {
        ALOGE("Failed Unknown buffer %p", bufferHandle);
        return -EINVAL;
    }
    auto buffer_context = context_it->second.get();

    uint32_t num_planes = GetNumPlanes(bufferHandle);
    if (!num_planes) {
        return -EINVAL;
    }
    if (num_planes > 2) {
        ALOGE("Lock called on multi-planar buffer %p", bufferHandle);
        return -EINVAL;
    }

    if (buffer_context->type == GRALLOC) {
        auto &mapper = get_mapperservice();
        auto buffer = const_cast<native_handle_t*>(bufferHandle);
        ALOGV("lock buffer:%p", &buffer);

        IMapper::Rect accessRegion = {(int)x, (int)y, (int)width, (int)height};

        android::hardware::hidl_handle acquireFenceHandle; // dummy

        Error error;
        auto ret = mapper.lock(buffer,
                               flags,
                               accessRegion,
                               acquireFenceHandle,
                               [&](const auto& tmpError, const auto& tmpData) {
                                    error = tmpError;
                                    if (error != Error::NONE) {
                                        return;
                                    }
                                    *out_addr = tmpData;
                               });

        error = (ret.isOk()) ? error : kTransactionError;

        ALOGE_IF(error != Error::NONE, "lock(%p, ...) failed: %d", bufferHandle, error);

        return (int)error;
    } else {
        ALOGE("Invalid buffer type: %d", buffer_context->type);
        return -EINVAL;
    }

    return 0;
}


int TvInputBufferManagerImpl::LockLocked(buffer_handle_t bufferHandle,
                                  uint32_t flags,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t width,
                                  uint32_t height,
                                  void** out_addr) {
    ALOGV("lock buffer:%p   %d, %d, %d, %d, %d", bufferHandle, x, y,width, height, flags);

    uint32_t num_planes = GetNumPlanes(bufferHandle);
    if (!num_planes) {
        return -EINVAL;
    }
    if (num_planes > 2) {
        ALOGE("Lock called on multi-planar buffer %p", bufferHandle);
        return -EINVAL;
    }

    if (true) {
        auto &mapper = get_mapperservice();
        auto buffer = const_cast<native_handle_t*>(bufferHandle);
        ALOGV("lock buffer:%p", &buffer);

        IMapper::Rect accessRegion = {(int)x, (int)y, (int)width, (int)height};

        android::hardware::hidl_handle acquireFenceHandle; // dummy

        Error error;
        auto ret = mapper.lock(buffer,
                               flags,
                               accessRegion,
                               acquireFenceHandle,
                               [&](const auto& tmpError, const auto& tmpData) {
                                    error = tmpError;
                                    if (error != Error::NONE) {
                                        return;
                                    }
                                    *out_addr = tmpData;
                               });

        error = (ret.isOk()) ? error : kTransactionError;

        ALOGE_IF(error != Error::NONE, "lock(%p, ...) failed: %d", bufferHandle, error);

        return (int)error;
    } else {
        ALOGE("Invalid buffer type");
        return -EINVAL;
    }

    return 0;
}

int TvInputBufferManagerImpl::LockYCbCr(buffer_handle_t buffer,
                                       uint32_t flags,
                                       uint32_t x,
                                       uint32_t y,
                                       uint32_t width,
                                       uint32_t height,
                                       struct android_ycbcr* out_ycbcr) {
    ALOGV("LockYCbCr");

    auto context_it = buffer_context_.find(buffer);
    if (context_it == buffer_context_.end()) {
        ALOGE("Failed Unknown buffer %p", buffer);
        return -EINVAL;
    }
    auto buffer_context = context_it->second.get();
    uint32_t num_planes = GetNumPlanes(buffer);
    if (!num_planes) {
        return -EINVAL;
    }
    if (num_planes < 2) {
        //ALOGE("LockYCbCr called on single-planar buffer %lx", buffer_context->buffer_id);
        return -EINVAL;
    }

    //Todo wgh
    //DCHECK_LE(num_planes, 3u);

    if (buffer_context->type == GRALLOC) {
        auto &mapper = get_mapperservice();
        std::vector<PlaneLayout> planeLayouts;
        
        int error = get_metadata(mapper, buffer, MetadataType_PlaneLayouts, decodePlaneLayouts, &planeLayouts);
        if (error != android::NO_ERROR) {
            return error;
        }
        
        void* data = nullptr;
        error = Lockinternal(buffer, flags, x, y, width, height, &data);
        if (error != android::NO_ERROR) {
            return error;
        }

        android_ycbcr ycbcr;

        ycbcr.y = nullptr;
        ycbcr.cb = nullptr;
        ycbcr.cr = nullptr;
        ycbcr.ystride = 0;
        ycbcr.cstride = 0;
        ycbcr.chroma_step = 0;

        for (const auto& planeLayout : planeLayouts) {
            for (const auto& planeLayoutComponent : planeLayout.components) {
                if (!android::gralloc4::isStandardPlaneLayoutComponentType(planeLayoutComponent.type)) {
                    continue;
                }
                if (0 != planeLayoutComponent.offsetInBits % 8) {
                    Unlockinternal(buffer);
                    return android::BAD_VALUE;
                }

                uint8_t* tmpData = static_cast<uint8_t*>(data) + planeLayout.offsetInBytes +
                        (planeLayoutComponent.offsetInBits / 8);
                uint64_t sampleIncrementInBytes;

                auto type = static_cast<PlaneLayoutComponentType>(planeLayoutComponent.type.value);
                switch (type) {
                    case PlaneLayoutComponentType::Y:
                        if ((ycbcr.y != nullptr) || (planeLayoutComponent.sizeInBits != 8) ||
                            (planeLayout.sampleIncrementInBits != 8)) {
                            Unlockinternal(buffer);
                            return android::BAD_VALUE;
                        }
                        ycbcr.y = tmpData;
                        ycbcr.ystride = planeLayout.strideInBytes;
                        break;

                    case PlaneLayoutComponentType::CB:
                    case PlaneLayoutComponentType::CR:
                        if (planeLayout.sampleIncrementInBits % 8 != 0) {
                            Unlockinternal(buffer);
                            return android::BAD_VALUE;
                        }

                        sampleIncrementInBytes = planeLayout.sampleIncrementInBits / 8;
                        if ((sampleIncrementInBytes != 1) && (sampleIncrementInBytes != 2)) {
                            Unlockinternal(buffer);
                            return android::BAD_VALUE;
                        }

                        if (ycbcr.cstride == 0 && ycbcr.chroma_step == 0) {
                            ycbcr.cstride = planeLayout.strideInBytes;
                            ycbcr.chroma_step = sampleIncrementInBytes;
                        } else {
                            if ((static_cast<int64_t>(ycbcr.cstride) != planeLayout.strideInBytes) ||
                                (ycbcr.chroma_step != sampleIncrementInBytes)) {
                                Unlockinternal(buffer);
                                return android::BAD_VALUE;
                            }
                        }

                        if (type == PlaneLayoutComponentType::CB) {
                            if (ycbcr.cb != nullptr) {
                                Unlockinternal(buffer);
                                return android::BAD_VALUE;
                            }
                            ycbcr.cb = tmpData;
                        } else {
                            if (ycbcr.cr != nullptr) {
                                Unlockinternal(buffer);
                                return android::BAD_VALUE;
                            }
                            ycbcr.cr = tmpData;
                        }
                        break;
                    default:
                        break;
                };
            }
        }

        *out_ycbcr = ycbcr;

    }
    ALOGV("lock ycbcr ok");

    return 0;
}

int TvInputBufferManagerImpl::Unlock(buffer_handle_t bufferHandle) {
    ALOGV("Unlock buffer:%p", bufferHandle);

    auto context_it = buffer_context_.find(bufferHandle);
    if (context_it == buffer_context_.end()) {
        ALOGE("Failed Unknown buffer %p", bufferHandle);
        return -EINVAL;
    }
    auto buffer_context = context_it->second.get();
    if (buffer_context->type == GRALLOC) {
        auto &mapper = get_mapperservice();
        auto buffer = const_cast<native_handle_t*>(bufferHandle);
        ALOGV("Unlock buffer:%p", buffer);

        int releaseFence = -1;
        Error error;
        auto ret = mapper.unlock(buffer,
                                 [&](const auto& tmpError, const auto& tmpReleaseFence)
                                 {
            error = tmpError;
            if (error != Error::NONE) {
                return;
            }

            auto fenceHandle = tmpReleaseFence.getNativeHandle(); // 预期 unlock() 不会返回有效的 release_fence.
            if (fenceHandle && fenceHandle->numFds == 1)
            {
                ALOGE("got unexpected valid fd of release_fence : %d", fenceHandle->data[0]);

                int fd = dup(fenceHandle->data[0]);
                if (fd >= 0) {
                    releaseFence = fd;
                } else {
                    ALOGE("failed to dup unlock release fence");
                    sync_wait(fenceHandle->data[0], -1);
                }
            }
                                 });

        if (!ret.isOk()) {
            error = kTransactionError;
        }

        if (error != Error::NONE) {
            ALOGE("unlock(%p) failed with %d", bufferHandle, error);
        }

    }

    return 0;
}


int TvInputBufferManagerImpl::UnlockLocked(buffer_handle_t bufferHandle) {
    ALOGV("Unlock buffer:%p", bufferHandle);

    auto &mapper = get_mapperservice();
    auto buffer = const_cast<native_handle_t*>(bufferHandle);
    ALOGV("Unlock buffer:%p", buffer);

    int releaseFence = -1;
    Error error;
    auto ret = mapper.unlock(buffer,
                             [&](const auto& tmpError, const auto& tmpReleaseFence)
                             {
        error = tmpError;
        if (error != Error::NONE) {
            return;
        }

        auto fenceHandle = tmpReleaseFence.getNativeHandle(); // 预期 unlock() 不会返回有效的 release_fence.
        if (fenceHandle && fenceHandle->numFds == 1)
        {
            ALOGE("got unexpected valid fd of release_fence : %d", fenceHandle->data[0]);

            int fd = dup(fenceHandle->data[0]);
            if (fd >= 0) {
                releaseFence = fd;
            } else {
                ALOGE("failed to dup unlock release fence");
                sync_wait(fenceHandle->data[0], -1);
            }
        }
                             });

    if (!ret.isOk()) {
        error = kTransactionError;
    }

    if (error != Error::NONE) {
        ALOGE("unlock(%p) failed with %d", bufferHandle, error);
    }
    
    return 0;
}

int TvInputBufferManagerImpl::FlushCache(buffer_handle_t buffer) {
    int fd = -1;

    auto &mapper = get_mapperservice();
    std::vector<int64_t> fds;

    int err = get_metadata(mapper, buffer, ArmMetadataType_PLANE_FDS, decodeArmPlaneFds, &fds);
    if (err != android::OK)
    {
        ALOGE("Failed to get plane_fds. err : %d", err);
        return err;
    }
    assert (fds.size() > 0);

    fd = (int)(fds[0]);
    if (fd == -1) {
        ALOGE("get fd error for buffer %p", buffer);
        return -EINVAL;
    }

    /*struct dma_buf_sync sync_args;
    sync_args.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync_args))
        return -EINVAL;*/

    return 0;
}

int TvInputBufferManagerImpl::GetBufferId(buffer_handle_t buffer) {
    uint64_t buffer_id = -1;

    auto &mapper = get_mapperservice();

    int err = get_metadata(mapper, buffer, MetadataType_BufferId, decodeBufferId, &buffer_id);
    if (err != android::OK)
    {
        ALOGE("Failed to get plane_fds. err : %d", err);
        return err;
    }
    ALOGD("GetBufferId buffer:%p, fd=%d", buffer, (int32_t)buffer_id);
    return (int32_t)buffer_id;
}

int TvInputBufferManagerImpl::GetHandleFd(buffer_handle_t buffer) {
    int fd = -1;
    auto &mapper = get_mapperservice();
    std::vector<int64_t> fds;

    int err = get_metadata(mapper, buffer, ArmMetadataType_PLANE_FDS, decodeArmPlaneFds, &fds);
    if (err != android::OK)
    {
        ALOGE("Failed to get plane_fds. err : %d", err);
        return err;
    }
    assert (fds.size() > 0);

    fd = (int)(fds[0]);
    //ALOGD("GetHandleFd buffer:%p, fd = %d.", buffer, fd);

    return fd;
}

int TvInputBufferManagerImpl::AllocateGrallocBuffer(size_t width,
                                                   size_t height,
                                                   uint32_t format,
                                                   uint64_t usage,
                                                   buffer_handle_t* out_buffer,
                                                   uint32_t* out_stride) {
    ALOGD("AllocateGrallocBuffer %zu, %zu, %u, %" PRIu64, width, height, format, usage);

    IMapper::BufferDescriptorInfo descriptorInfo;
    sBufferDescriptorInfo("tv_input_SidebandStream", width, height, (PixelFormat)format, 1/*layerCount*/, usage, &descriptorInfo);

    BufferDescriptor descriptor;
    status_t error = createDescriptor(static_cast<void*>(&descriptorInfo),
                                              static_cast<void*>(&descriptor));
    if (error != android::NO_ERROR) {
        return error;
    }

    std::unique_ptr<BufferContext> buffer_context(new struct BufferContext);

    buffer_context->buffer_id = reinterpret_cast<uint64_t>(buffer_context.get());
    buffer_context->type = GRALLOC;

    int bufferCount = 1;
    auto &allocator = get_allocservice();
    auto ret = allocator.allocate(descriptor, bufferCount,
                                    [&](const auto& tmpError, const auto& tmpStride,
                                        const auto& tmpBuffers) {
                                        error = static_cast<status_t>(tmpError);
                                        if (tmpError != Error::NONE) {
                                            return;
                                        }

                                        #if IMPORTBUFFER_CB == 1
                                            for (uint32_t i = 0; i < bufferCount; i++) {
                                                error = importBuffer(tmpBuffers[i],
                                                                             out_buffer);
                                                if (error != android::NO_ERROR) {
                                                    for (uint32_t j = 0; j < i; j++) {
                                                        freeBuffer(*out_buffer);
                                                        *out_buffer = nullptr;
                                                    }
                                                    return;
                                                }
                                            }
                                        #else
                                            for (uint32_t i = 0; i < bufferCount; i++) {
                                                *out_buffer = native_handle_clone(
                                                        tmpBuffers[i].getNativeHandle());
                                                if (!out_buffer) {
                                                    for (uint32_t j = 0; j < i; j++) {
                                                        //auto buffer = const_cast<native_handle_t*>(
                                                        //        out_buffer);
                                                        native_handle_close(out_buffer);
                                                        native_handle_delete(out_buffer);
                                                        *out_buffer = nullptr;
                                                    }
                                                }
                                            }
                                        #endif
                                        *out_stride = tmpStride;
                                    });

    if (!ret.isOk())
        return -EINVAL;

    ALOGD("AllocateGrallocBuffer %p", *out_buffer);
    buffer_context->usage = 1;
    buffer_context_[*out_buffer] = std::move(buffer_context);

    // make sure the kernel driver sees BC_FREE_BUFFER and closes the fds now
    android::hardware::IPCThreadState::self()->flushCommands();

    return (ret.isOk()) ? error : static_cast<status_t>(kTransactionError);
}

}  // namespace common
