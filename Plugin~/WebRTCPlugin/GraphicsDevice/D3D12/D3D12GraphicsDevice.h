#pragma once

#include <comdef.h>
#include <d3d12.h>
#include <stdexcept>
#include <wrl/client.h>

#include <IUnityGraphicsD3D12.h>

#include "D3D12Texture2D.h"
#include "GraphicsDevice/Cuda/CudaContext.h"
#include "GraphicsDevice/IGraphicsDevice.h"

using namespace Microsoft::WRL;

namespace unity
{
namespace webrtc
{
    namespace webrtc = ::webrtc;

#define DefPtr(_a) _COM_SMARTPTR_TYPEDEF(_a, __uuidof(_a))
    DefPtr(ID3D12CommandAllocator);
    DefPtr(ID3D12GraphicsCommandList4);

    inline std::string HrToString(HRESULT hr)
    {

        char s_str[64] = {};
        sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
        return std::string(s_str);
    }

    class HrException : public std::runtime_error
    {
    public:
        HrException(HRESULT hr)
            : std::runtime_error(HrToString(hr))
            , m_hr(hr)
        {
        }
        HRESULT Error() const { return m_hr; }

    private:
        const HRESULT m_hr;
    };

    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw HrException(hr);
        }
    }

    class D3D12GraphicsDevice : public IGraphicsDevice
    {
    public:
        explicit D3D12GraphicsDevice(
            ID3D12Device* nativeDevice,
            IUnityGraphicsD3D12v5* unityInterface,
            UnityGfxRenderer renderer,
            ProfilerMarkerFactory* profiler);
        explicit D3D12GraphicsDevice(
            ID3D12Device* nativeDevice,
            ID3D12CommandQueue* commandQueue,
            UnityGfxRenderer renderer,
            ProfilerMarkerFactory* profiler);
        virtual ~D3D12GraphicsDevice();
        virtual bool InitV() override;
        virtual void ShutdownV() override;
        inline virtual void* GetEncodeDevicePtrV() override;

        virtual ITexture2D*
        CreateDefaultTextureV(uint32_t w, uint32_t h, UnityRenderingExtTextureFormat textureFormat) override;
        virtual bool CopyResourceV(ITexture2D* dest, ITexture2D* src) override;
        virtual bool CopyResourceFromNativeV(ITexture2D* dest, void* nativeTexturePtr) override;
        std::unique_ptr<GpuMemoryBufferHandle> Map(ITexture2D* texture) override;

        virtual ITexture2D*
        CreateCPUReadTextureV(uint32_t w, uint32_t h, UnityRenderingExtTextureFormat textureFormat) override;
        virtual rtc::scoped_refptr<webrtc::I420Buffer> ConvertRGBToI420(ITexture2D* tex) override;

        bool IsCudaSupport() override { return m_isCudaSupport; }
        CUcontext GetCUcontext() override { return m_cudaContext.GetContext(); }
        NV_ENC_BUFFER_FORMAT GetEncodeBufferFormat() override { return NV_ENC_BUFFER_FORMAT_ARGB; }

    private:
        D3D12Texture2D* CreateSharedD3D12Texture(uint32_t w, uint32_t h);
        HRESULT Signal(ID3D12Fence* fence);
        //void WaitForFence(ID3D12Fence* fence, HANDLE handle, uint64_t* fenceValue);
        void Barrier(
            ID3D12Resource* res,
            const D3D12_RESOURCE_STATES stateBefore,
            const D3D12_RESOURCE_STATES stateAfter,
            const UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

        ComPtr<ID3D12Device> m_d3d12Device;
        ComPtr<ID3D12CommandQueue> m_d3d12CommandQueue;

        bool m_isCudaSupport;
        CudaContext m_cudaContext;

        //[TODO-sin: 2019-12-2] //This should be allocated for each frame.
        ID3D12CommandAllocatorPtr m_commandAllocator;
        ID3D12GraphicsCommandList4Ptr m_commandList;

        // Fence to copy resource on GPU (and CPU if the texture was created with CPU-access)
    };

    //---------------------------------------------------------------------------------------------------------------------

    // use D3D11. See notes below
    void* D3D12GraphicsDevice::GetEncodeDevicePtrV() { return reinterpret_cast<void*>(m_d3d12Device.Get()); }

} // end namespace webrtc
} // end namespace unity

//---------------------------------------------------------------------------------------------------------------------
//[Note-sin: 2019-10-30]
// Since NVEncoder does not support DX12, we use a DX12 resource that can be shared with DX11, and then pass it
// the DX11 resource to NVidia Encoder
