#pragma once

#include <memory>

#include <IUnityRenderingExtensions.h>
#include <api/video/i420_buffer.h>

#include "PlatformBase.h"
#include "ProfilerMarkerFactory.h"
#include "ScopedProfiler.h"

#if CUDA_PLATFORM
#include "Cuda/ICudaDevice.h"
#endif

#if __ANDROID__
#include "Android/Surface.h"
#endif

namespace unity
{
namespace webrtc
{
    using NativeTexPtr = void*;
    class ITexture2D;
    struct GpuMemoryBufferHandle;
    class ProfilerMarkerFactory;
    class IGraphicsDevice
#if CUDA_PLATFORM
        : public ICudaDevice
#endif
    {
    public:
        IGraphicsDevice(UnityGfxRenderer renderer, ProfilerMarkerFactory* profiler)
            : m_gfxRenderer(renderer)
            , m_profiler(profiler)
        {
        }
#if CUDA_PLATFORM
        virtual ~IGraphicsDevice() override = default;
#else
        virtual ~IGraphicsDevice() = default;
#endif
        virtual bool InitV() = 0;
        virtual void ShutdownV() = 0;
        virtual ITexture2D*
        CreateDefaultTextureV(uint32_t width, uint32_t height, UnityRenderingExtTextureFormat textureFormat) = 0;
        virtual rtc::scoped_refptr<::webrtc::VideoFrameBuffer> CreateVideoFrameBuffer(uint32_t width, uint32_t height, UnityRenderingExtTextureFormat textureFormat) { return nullptr; }
        virtual void* GetEncodeDevicePtrV() = 0;
        virtual bool CopyResourceV(ITexture2D* dest, ITexture2D* src) = 0;
        virtual bool CopyResourceFromNativeV(ITexture2D* dest, NativeTexPtr nativeTexturePtr) = 0;

        virtual bool CopyResourceFromBuffer(void* dest, rtc::scoped_refptr<::webrtc::VideoFrameBuffer> buffer) { return true; }
        virtual bool CopyToVideoFrameBuffer(rtc::scoped_refptr<::webrtc::VideoFrameBuffer>& buffer, void* texture)
        {
            return true;
        }

        virtual UnityGfxRenderer GetGfxRenderer() const { return m_gfxRenderer; }
        virtual std::unique_ptr<GpuMemoryBufferHandle> Map(ITexture2D* texture) = 0;
        virtual bool WaitSync(const ITexture2D* texture, uint64_t nsTimeout = 0) { return true; }
        virtual bool ResetSync(const ITexture2D* texture) { return true; }
        virtual bool WaitIdleForTest() { return true; }
        // Required for software encoding
        virtual ITexture2D*
        CreateCPUReadTextureV(uint32_t width, uint32_t height, UnityRenderingExtTextureFormat textureFormat) = 0;
        virtual rtc::scoped_refptr<::webrtc::I420Buffer> ConvertRGBToI420(ITexture2D* tex) = 0;
        virtual rtc::scoped_refptr<::webrtc::VideoFrameBuffer> ConvertToBuffer(void* texture) { return nullptr; }

#if __ANDROID__
        virtual std::unique_ptr<Surface> GetSurface(ANativeWindow* window) { return nullptr; }
#endif

    protected:
        UnityGfxRenderer m_gfxRenderer;
        ProfilerMarkerFactory* m_profiler;
    };

} // end namespace webrtc
} // end namespace unity
