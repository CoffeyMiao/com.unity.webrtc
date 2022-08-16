#include "pch.h"

#include <third_party/libyuv/include/libyuv/convert.h>

#if __ANDROID__
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer_jni.h>
#endif

#include "GraphicsDevice/GraphicsUtility.h"
#include "UnityVulkanInterfaceFunctions.h"
#include "VulkanGraphicsDevice.h"
#include "VulkanTexture2D.h"
#include "VulkanUtility.h"
#include "WebRTCMacros.h"
#include "NativeFrameBuffer.h"

#if CUDA_PLATFORM
#include "GraphicsDevice/Cuda/GpuMemoryBufferCudaHandle.h"
#else
#include "GpuMemoryBuffer.h"
#endif

namespace unity
{
namespace webrtc
{

    VulkanGraphicsDevice::VulkanGraphicsDevice(
        UnityGraphicsVulkan* unityVulkan,
        const VkInstance instance,
        const VkPhysicalDevice physicalDevice,
        const VkDevice device,
        const VkQueue graphicsQueue,
        const uint32_t queueFamilyIndex,
        UnityGfxRenderer renderer,
        ProfilerMarkerFactory* profiler)
        : IGraphicsDevice(renderer, profiler)
        , m_unityVulkan(unityVulkan)
        , m_physicalDevice(physicalDevice)
        , m_device(device)
        , m_graphicsQueue(graphicsQueue)
        , m_commandPool(nullptr)
        , m_queueFamilyIndex(queueFamilyIndex)
        , m_allocator(nullptr)
        , m_instance(instance)
#if CUDA_PLATFORM
        , m_isCudaSupport(false)
#endif
    {
        if (profiler)
            m_maker = profiler->CreateMarker(
                "VulkanGraphicsDevice.CopyImage", kUnityProfilerCategoryOther, kUnityProfilerMarkerFlagDefault, 0);
    }

    //---------------------------------------------------------------------------------------------------------------------
    bool VulkanGraphicsDevice::InitV()
    {
#if CUDA_PLATFORM
        m_isCudaSupport = InitCudaContext();
#endif
        return VK_SUCCESS == CreateCommandPool();
    }

#if CUDA_PLATFORM
    bool VulkanGraphicsDevice::InitCudaContext()
    {
        if (!VulkanUtility::LoadInstanceFunctions(m_instance))
            return false;
        if (!VulkanUtility::LoadDeviceFunctions(m_device))
            return false;
        CUresult result = m_cudaContext.Init(m_instance, m_physicalDevice);
        if (CUDA_SUCCESS != result)
            return false;
        return true;
    }
#endif

    //---------------------------------------------------------------------------------------------------------------------

    void VulkanGraphicsDevice::ShutdownV()
    {
#if CUDA_PLATFORM
        m_cudaContext.Shutdown();
#endif
        VULKAN_SAFE_DESTROY_COMMAND_POOL(m_device, m_commandPool, m_allocator)
    }

    //---------------------------------------------------------------------------------------------------------------------

    std::unique_ptr<UnityVulkanImage> VulkanGraphicsDevice::AccessTexture(void* ptr) const
    {
        // cannot do resource uploads inside renderpass
        m_unityVulkan->EnsureOutsideRenderPass();

        std::unique_ptr<UnityVulkanImage> unityVulkanImage = std::make_unique<UnityVulkanImage>();

        VkImageSubresource subResource { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        if (!m_unityVulkan->AccessTexture(
                ptr,
                &subResource,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                kUnityVulkanResourceAccess_PipelineBarrier,
                unityVulkanImage.get()))
        {
            return nullptr;
        }
        return unityVulkanImage;
    }

    static VkResult BeginCommandBuffer(VkCommandBuffer commandBuffer)
    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        return vkBeginCommandBuffer(commandBuffer, &beginInfo);
    }

    static VkResult QueueSubmit(VkQueue queue, VkCommandBuffer commandBuffer, VkFence fence)
    {
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        return vkQueueSubmit(queue, 1, &submitInfo, fence);
    }

    ITexture2D* VulkanGraphicsDevice::CreateTexture(void* texture)
    {
        //UnityVulkanImage* unityVulkanImage = static_cast<UnityVulkanImage*>(texture);
        //return VulkanTexture2D();
        RTC_DCHECK(false);
        return nullptr;
    }


    // Returns null if failed
    ITexture2D* VulkanGraphicsDevice::CreateDefaultTextureV(
        const uint32_t width, const uint32_t height, UnityRenderingExtTextureFormat format)
    {
        std::unique_ptr<VulkanTexture2D> vulkanTexture = std::make_unique<VulkanTexture2D>(width, height, format);
        if (!vulkanTexture->Init(m_physicalDevice, m_device, m_commandPool))
        {
            RTC_LOG(LS_ERROR) << "VulkanTexture2D::Init failed.";
            return nullptr;
        }
        return vulkanTexture.release();
    }

    rtc::scoped_refptr<::webrtc::VideoFrameBuffer>
    VulkanGraphicsDevice::CreateVideoFrameBuffer(uint32_t width, uint32_t height, UnityRenderingExtTextureFormat textureFormat)
    {
        return NativeFrameBuffer::Create(width, height, textureFormat, this);
    }

    ITexture2D*
    VulkanGraphicsDevice::CreateCPUReadTextureV(uint32_t width, uint32_t height, UnityRenderingExtTextureFormat format)
    {
        std::unique_ptr<VulkanTexture2D> vulkanTexture = std::make_unique<VulkanTexture2D>(width, height, format);
        if (!vulkanTexture->InitCpuRead(m_physicalDevice, m_device, m_commandPool))
        {
            RTC_LOG(LS_ERROR) << "VulkanTexture2D::InitCpuRead failed.";
            return nullptr;
        }
        return vulkanTexture.release();
    }

    //---------------------------------------------------------------------------------------------------------------------
    bool VulkanGraphicsDevice::CopyResourceV(ITexture2D* dest, ITexture2D* src)
    {
        VulkanTexture2D* destTexture = reinterpret_cast<VulkanTexture2D*>(dest);
        VulkanTexture2D* srcTexture = reinterpret_cast<VulkanTexture2D*>(src);
        if (destTexture == srcTexture)
            return false;
        if (destTexture == nullptr || srcTexture == nullptr)
            return false;

        VkCommandBuffer commandBuffer = destTexture->GetCommandBuffer();
        VkResult result = BeginCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "BeginCommandBuffer failed. result:" << result;
            return false;
        }

        // Transition the src texture layout.
        result = VulkanUtility::DoImageLayoutTransition(
            commandBuffer,
            srcTexture->GetImage(),
            srcTexture->GetTextureFormat(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "DoImageLayoutTransition failed. result:" << result;
            return false;
        }

        // Transition the dst texture layout.
        result = VulkanUtility::DoImageLayoutTransition(
            commandBuffer,
            destTexture->GetImage(),
            destTexture->GetTextureFormat(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "DoImageLayoutTransition failed. result:" << result;
            return false;
        }

        result = VulkanUtility::CopyImage(
            commandBuffer,
            srcTexture->GetImage(),
            destTexture->GetImage(),
            destTexture->GetWidth(),
            destTexture->GetHeight());
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "CopyImage failed. result:" << result;
            return false;
        }
        // transition the src texture layout back to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        result = VulkanUtility::DoImageLayoutTransition(
            commandBuffer,
            srcTexture->GetImage(),
            srcTexture->GetTextureFormat(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "DoImageLayoutTransition failed. result:" << result;
            return false;
        }
        result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "vkEndCommandBuffer failed. result:" << result;
            return false;
        }
        VkFence fence = destTexture->GetFence();
        result = QueueSubmit(m_graphicsQueue, commandBuffer, fence);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "vkQueueSubmit failed. result:" << result;
            return false;
        }
        return true;
    }

    //---------------------------------------------------------------------------------------------------------------------
    bool VulkanGraphicsDevice::CopyResourceFromNativeV(ITexture2D* dest, void* nativeTexturePtr)
    {
        if (nullptr == dest || nullptr == nativeTexturePtr)
            return false;

        VulkanTexture2D* destTexture = reinterpret_cast<VulkanTexture2D*>(dest);
        UnityVulkanImage* unityVulkanImage = static_cast<UnityVulkanImage*>(nativeTexturePtr);
        VkCommandBuffer commandBuffer = destTexture->GetCommandBuffer();
        VkResult result = BeginCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "BeginCommandBuffer failed. result:" << result;
            return false;
        }

        // Transition the src texture layout.
        result = VulkanUtility::DoImageLayoutTransition(
            commandBuffer,
            unityVulkanImage->image,
            unityVulkanImage->format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "DoImageLayoutTransition failed. result:" << result;
            return false;
        }

        // Transition the dst texture layout.
        result = VulkanUtility::DoImageLayoutTransition(
            commandBuffer,
            destTexture->GetImage(),
            destTexture->GetTextureFormat(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "DoImageLayoutTransition failed. result:" << result;
            return false;
        }

        VkImage image = unityVulkanImage->image;
        if (destTexture->GetImage() == image)
            return false;

        {
            std::unique_ptr<const ScopedProfiler> profiler;
            if (m_profiler)
                profiler = m_profiler->CreateScopedProfiler(*m_maker);

            // The layouts of All VulkanTexture2D should be VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            // so no transition for destTex
            result = VulkanUtility::CopyImage(
                commandBuffer, image, destTexture->GetImage(), destTexture->GetWidth(), destTexture->GetHeight());
            if (result != VK_SUCCESS)
            {
                RTC_LOG(LS_ERROR) << "CopyImage failed. result:" << result;
                return false;
            }
        }
        result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "vkEndCommandBuffer failed. result:" << result;
            return false;
        }
        VkFence fence = destTexture->GetFence();
        result = QueueSubmit(m_graphicsQueue, commandBuffer, fence);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_ERROR) << "vkQueueSubmit failed. result:" << result;
            return false;
        }
        return true;
    }

    VkResult VulkanGraphicsDevice::CreateCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_queueFamilyIndex;
        poolInfo.flags = 0;

        return vkCreateCommandPool(m_device, &poolInfo, m_allocator, &m_commandPool);
    }

    rtc::scoped_refptr<webrtc::I420Buffer> VulkanGraphicsDevice::ConvertRGBToI420(ITexture2D* tex)
    {
        VulkanTexture2D* vulkanTexture = static_cast<VulkanTexture2D*>(tex);
        const int32_t width = static_cast<int32_t>(tex->GetWidth());
        const int32_t height = static_cast<int32_t>(tex->GetHeight());
        const VkDeviceMemory dstImageMemory = vulkanTexture->GetTextureImageMemory();
        VkImageSubresource subresource { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        VkSubresourceLayout subresourceLayout;
        vkGetImageSubresourceLayout(m_device, vulkanTexture->GetImage(), &subresource, &subresourceLayout);
        const int32_t rowPitch = static_cast<int32_t>(subresourceLayout.rowPitch);

        void* data;
        std::vector<uint8_t> dst;
        dst.resize(vulkanTexture->GetTextureImageMemorySize());
        const VkResult result = vkMapMemory(m_device, dstImageMemory, 0, VK_WHOLE_SIZE, 0, &data);
        if (result != VK_SUCCESS)
        {
            return nullptr;
        }
        std::memcpy(static_cast<void*>(dst.data()), data, dst.size());

        vkUnmapMemory(m_device, dstImageMemory);

        // convert format to i420
        rtc::scoped_refptr<webrtc::I420Buffer> i420Buffer = webrtc::I420Buffer::Create(width, height);
        libyuv::ARGBToI420(
            dst.data(),
            rowPitch,
            i420Buffer->MutableDataY(),
            i420Buffer->StrideY(),
            i420Buffer->MutableDataU(),
            i420Buffer->StrideU(),
            i420Buffer->MutableDataV(),
            i420Buffer->StrideV(),
            width,
            height);

        return i420Buffer;
    }

    std::unique_ptr<GpuMemoryBufferHandle>
    VulkanGraphicsDevice::Map(ITexture2D* texture, GpuMemoryBufferHandle::AccessMode mode)
    {
#if CUDA_PLATFORM
        if (!IsCudaSupport())
            return nullptr;

        VulkanTexture2D* vulkanTexture = static_cast<VulkanTexture2D*>(texture);
        void* exportHandle = VulkanUtility::GetExportHandle(m_device, vulkanTexture->GetTextureImageMemory());
        if (!exportHandle)
        {
            RTC_LOG(LS_ERROR) << "cannot get export handle";
            throw;
        }
        size_t memorySize = vulkanTexture->GetTextureImageMemorySize();
        Size size(static_cast<int>(texture->GetWidth()), static_cast<int>(texture->GetHeight()));
        return GpuMemoryBufferCudaHandle::CreateHandle(GetCUcontext(), exportHandle, memorySize, size, mode);
#elif __ANDROID__
        VulkanTexture2D* vulkanTexture = static_cast<VulkanTexture2D*>(texture);
        VkDeviceMemory memory = vulkanTexture->GetTextureImageMemory();


        VkExportMemoryAllocateInfo exportMemoryInfo = {};
        exportMemoryInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        exportMemoryInfo.pNext = nullptr;
        exportMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
//        VkAndroidHardwareBufferFormatPropertiesANDROID ahb_format_props = {};
//        VkAndroidHardwareBufferPropertiesANDROID ahb_props = {};
//
//        VkExternalFormatANDROID external_format;
//        external_format.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
//        external_format.pNext = nullptr;

        AHardwareBuffer* buffer = nullptr;
//        AHardwareBuffer_Desc ahb_desc = {};

        VkMemoryGetAndroidHardwareBufferInfoANDROID bufferInfo = {};
        bufferInfo.sType  = VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
        bufferInfo.pNext  = nullptr;
        bufferInfo.memory = memory;

        VkResult result = vkGetMemoryAndroidHardwareBufferANDROID(m_device, &bufferInfo, &buffer);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "vkGetMemoryAndroidHardwareBufferANDROID failed. result=" << result;
            return nullptr;
        }
        std::unique_ptr<AHardwareBufferHandle> handle = std::make_unique<AHardwareBufferHandle>();
        handle->buffer = buffer;
        return std::move(handle);
#else
        return nullptr;
#endif
    }

    bool VulkanGraphicsDevice::WaitIdleForTest()
    {
        VkResult result = vkQueueWaitIdle(m_graphicsQueue);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "vkQueueWaitIdle failed. result:" << result;
            return false;
        }
        return true;
    }

    bool VulkanGraphicsDevice::WaitSync(const ITexture2D* texture, uint64_t nsTimeout)
    {
        const VulkanTexture2D* vulkanTexture = static_cast<const VulkanTexture2D*>(texture);
        VkFence fence = vulkanTexture->GetFence();
        VkResult result = vkWaitForFences(m_device, 1, &fence, true, nsTimeout);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "vkWaitForFences failed. result:" << result;
            return false;
        }
        return true;
    }

    bool VulkanGraphicsDevice::ResetSync(const ITexture2D* texture)
    {
        const VulkanTexture2D* vulkanTexture = static_cast<const VulkanTexture2D*>(texture);
        VkCommandBuffer commandBuffer = vulkanTexture->GetCommandBuffer();
        VkFence fence = vulkanTexture->GetFence();

        VkResult result = vkGetFenceStatus(m_device, fence);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "vkGetFenceStatus failed. result:" << result;
            return false;
        }
        result = vkResetFences(m_device, 1, &fence);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "vkResetFences failed. result:" << result;
            return false;
        }
        result = vkResetCommandBuffer(commandBuffer, 0);
        if (result != VK_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "vkResetCommandBuffer failed. result:" << result;
            return false;
        }
        return true;
    }

#if __ANDROID__
    std::unique_ptr<Surface> VulkanGraphicsDevice::GetSurface(ANativeWindow* window)
    {
        VkSurfaceKHR surface;
        VkAndroidSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
        createInfo.window = window;

        VkResult result = vkCreateAndroidSurfaceKHR(m_instance, &createInfo, nullptr, &surface);
        if(result != VK_SUCCESS)
        {
            RTC_LOG(LS_INFO) << "PFN_vkCreateAndroidSurfaceKHR failed. result:" << result;
            return nullptr;
        }
        return CreateVulkanSurface(surface, m_device, m_physicalDevice);
    }
#endif


} // end namespace webrtc
} // end namespace unity
