#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <array>

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
using Microsoft::WRL::ComPtr;

template < typename Interface >
class ComRef
{
public:
    ComRef(Interface* pInterface              ) : m_pInterface{ pInterface       } {}
    ComRef(ComPtr<Interface> const& pInterface) : m_pInterface{ pInterface.Get() } {}

    Interface* Get        () const { return m_pInterface; }
    Interface* operator-> () const { return m_pInterface; }
    operator Interface*   () const { return m_pInterface; }
    explicit operator bool() const { return m_pInterface != nullptr; }

private:
    Interface* m_pInterface;
};

using namespace DirectX;
// using DirectX::XMFLOAT3;
// using DirectX::XMFLOAT4;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw hr;
    }
}


namespace Root { template < typename... args > struct SignatureDescription; }




ComPtr<IDXGIAdapter1> FindHardwareAdapter(ComRef<IDXGIFactory1> pFactory, bool requestHighPerformanceAdapter = true);

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
ComPtr<IDXGIAdapter1> GetPreferredAdapter(ComRef<IDXGIFactory1> pFactory, bool requestHighPerformanceAdapter = true);



template < D3D12_DESCRIPTOR_HEAP_TYPE type >
class DescriptorHeap
{
public:
    UINT AllocateDescriptors(UINT count);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(UINT index) const;

    D3D12_CPU_DESCRIPTOR_HANDLE NextDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const;
    D3D12_GPU_DESCRIPTOR_HANDLE NextDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) const;

    ID3D12DescriptorHeap* Get            () const { return heap.Get(); }
    ID3D12DescriptorHeap* operator->     () const { return heap.Get(); }
    operator ID3D12DescriptorHeap*       () const { return heap.Get(); }
    operator ComRef<ID3D12DescriptorHeap>() const { return heap.Get(); }
    explicit operator bool               () const { return heap; }

    static DescriptorHeap<type> Create(ComRef<ID3D12Device> device, UINT count, D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

private:
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT                         descriptorSize  = 0;
    UINT                         descriptorCount = 0;
    UINT                         descriptorsUsed = 0;
};

class GpuSignalingFence
{
public:

    UINT64 SignalOnGpu();

    void WaitOnCpu(UINT64);

    void SignalAndDrainQueue() { WaitOnCpu(SignalOnGpu()); }

    static GpuSignalingFence Create(ComRef<ID3D12Device>, ComPtr<ID3D12CommandQueue>);

private:
    UINT64                     m_fenceValue = 0;
    ComPtr<ID3D12Fence>        m_fence;
    ComPtr<ID3D12CommandQueue> m_queue;
};

constexpr UINT FrameCount = 2;

struct RenderDevice
{
    ComPtr<ID3D12Device>       device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<IDXGISwapChain3>    swapChain;

    DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_RTV> rtvDescriptorHeap;

    D3D12_VIEWPORT               viewport;
    D3D12_RECT                   scissorRect;

    std::array<ComPtr<ID3D12Resource>        , FrameCount> renderTargets;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> commandAllocators;

    UINT64              frameFenceValues[FrameCount];
    UINT                frameIndex;

    GpuSignalingFence   fence;

    ComPtr<ID3D12RootSignature> CreateRootSignature(D3D12_ROOT_SIGNATURE_DESC  const& desc);
    ComPtr<ID3D12RootSignature> CreateRootSignature(D3D12_ROOT_SIGNATURE_DESC1 const& desc);

    template < typename... args > ComPtr<ID3D12RootSignature> CreateRootSignature();

    ComPtr<ID3D12GraphicsCommandList> CreateDirectCommandList(ComRef<ID3D12CommandAllocator> allocator, ComRef<ID3D12PipelineState> pipelineState = nullptr);

    void PresentFrame();

    static RenderDevice Create(HWND const hwnd);
};





constexpr D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc
{
    FALSE,FALSE,
    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
    D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
    D3D12_LOGIC_OP_NOOP,
    D3D12_COLOR_WRITE_ENABLE_ALL,
};

constexpr D3D12_BLEND_DESC defaultBlendDesc
{
    FALSE,
    FALSE,
    {
        defaultRenderTargetBlendDesc,
        defaultRenderTargetBlendDesc,
        defaultRenderTargetBlendDesc,
        defaultRenderTargetBlendDesc,
        defaultRenderTargetBlendDesc,
        defaultRenderTargetBlendDesc,
        defaultRenderTargetBlendDesc,
        defaultRenderTargetBlendDesc,
    },
};

constexpr D3D12_RASTERIZER_DESC defaultRasterizerDesc
{
    .FillMode              = D3D12_FILL_MODE_SOLID,
    .CullMode              = D3D12_CULL_MODE_BACK,
    .FrontCounterClockwise = FALSE,
    .DepthBias             = D3D12_DEFAULT_DEPTH_BIAS,
    .DepthBiasClamp        = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
    .SlopeScaledDepthBias  = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
    .DepthClipEnable       = TRUE,
    .MultisampleEnable     = FALSE,
    .AntialiasedLineEnable = FALSE,
    .ForcedSampleCount     = 0,
    .ConservativeRaster    = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
};



ComPtr<ID3D12Resource> CreateUploadConstantBuffer(ComRef<ID3D12Device>, UINT size, D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_GENERIC_READ);

template < typename T >
struct ConstantBuffer
{
    ComPtr<ID3D12Resource> pResource;
    T*                     pBuffer = nullptr;

    T* Get                         () const { return pBuffer; }
    T* operator->                  () const { return pBuffer; }
    operator T*                    () const { return pBuffer; }
    operator ID3D12Resource*       () const { return pResource.Get(); }
    operator ComRef<ID3D12Resource>() const { return pResource.Get(); }
    explicit operator bool         () const { return pResource; }

    operator D3D12_GPU_VIRTUAL_ADDRESS() const { return pResource->GetGPUVirtualAddress(); }

    static ConstantBuffer Create(ComRef<ID3D12Device>, D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_GENERIC_READ);
};

constexpr UINT CalculateConstantBufferByteSize(UINT byteSize)
{
    // Constant buffer size is required to be aligned.
    return (byteSize + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
}

template < typename T >
inline ConstantBuffer<T> ConstantBuffer<T>::Create(ComRef<ID3D12Device>  device, D3D12_RESOURCE_STATES initState)
{
    ConstantBuffer<T> result{};

    result.pResource = CreateUploadConstantBuffer(device, sizeof(T), initState);

    // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
    D3D12_RANGE const readRange{ .Begin = 0, .End = 0 }; // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(result.pResource->Map(0, &readRange, reinterpret_cast<void**>(&result.pBuffer)));

    return result;
}


inline void TransitionResource(ComRef<ID3D12GraphicsCommandList> commandList, ComRef<ID3D12Resource> resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER const barrier{
        .Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition{
            .pResource   = resource,
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            .StateBefore = from,
            .StateAfter  = to,
        },
    };
    commandList->ResourceBarrier(1, &barrier);
}

class SinglePassCommandList
{
public:

    template < typename... RootParams >
    static SinglePassCommandList Create(RootParams&&...);

private:
    ComPtr<ID3D12RootSignature>       m_root;
    ComPtr<ID3D12PipelineState>       m_state;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
};



namespace Root
{

template < UINT base, UINT count = 1 > struct SrvRange
{
    static constexpr D3D12_DESCRIPTOR_RANGE1 Range{ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = count, .BaseShaderRegister = base, .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
};

template < UINT base, UINT count = 1 > struct SamplerRange
{
    static constexpr D3D12_DESCRIPTOR_RANGE1 Range{ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, .NumDescriptors = count, .BaseShaderRegister = base, .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
};



template < D3D12_SHADER_VISIBILITY visibility, typename... ranges > struct Table
{
    static constexpr std::array<D3D12_DESCRIPTOR_RANGE1, sizeof...(ranges)> Ranges{ ranges::Range... };
    static constexpr D3D12_ROOT_PARAMETER1                                  Parameter
    {
        .ParameterType    = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
        .DescriptorTable  { .NumDescriptorRanges = Ranges.size(), .pDescriptorRanges = Ranges.data(), },
        .ShaderVisibility = visibility,
    };
};

template < D3D12_SHADER_VISIBILITY visibility, UINT reg, UINT space, D3D12_ROOT_DESCRIPTOR_FLAGS flags > struct Cbv
{
    static constexpr D3D12_ROOT_PARAMETER1 Parameter
    {
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
        .Descriptor{
            .ShaderRegister = reg,
            .RegisterSpace  = space,
            .Flags = flags,
        },
        .ShaderVisibility = visibility,
    };
};



template < typename... parameters > struct ParameterList
{
    static constexpr std::array<D3D12_ROOT_PARAMETER1, sizeof...(parameters)> Parameters { parameters::Parameter... };

    static constexpr auto Size = sizeof...(parameters);
    static constexpr auto Data = Parameters.data();

    template < typename parameter > static constexpr ParameterList<parameter, parameters...> Cons{};
};



template < D3D12_ROOT_SIGNATURE_FLAGS flags > struct Flags{};



template <> struct SignatureDescription<>
{
    static constexpr ParameterList<> Parameters{};
    static constexpr D3D12_ROOT_SIGNATURE_DESC1 Desc{
        .NumParameters     = 0,
        .pParameters       = nullptr,
        .NumStaticSamplers = 0,
        .pStaticSamplers   = nullptr,
        .Flags             = D3D12_ROOT_SIGNATURE_FLAG_NONE,
    };

    operator D3D12_ROOT_SIGNATURE_DESC1 const&() const { return Desc; }
};

template < D3D12_SHADER_VISIBILITY visibility, typename... ranges, typename... args > struct SignatureDescription<Table<visibility, ranges...>, args...>
{
private:
    using Next = SignatureDescription<args...>;
public:
    static constexpr auto Parameters = Next::Parameters.Cons<Table<visibility, ranges...>>;
    static constexpr D3D12_ROOT_SIGNATURE_DESC1 Desc{
        .NumParameters     = Parameters.Size             ,
        .pParameters       = Parameters.Data             ,
        .NumStaticSamplers = Next::Desc.NumStaticSamplers,
        .pStaticSamplers   = Next::Desc.pStaticSamplers  ,
        .Flags             = Next::Desc.Flags            ,
    };

    operator D3D12_ROOT_SIGNATURE_DESC1 const&() const { return Desc; }
};

template < D3D12_SHADER_VISIBILITY visibility, UINT reg, UINT space, D3D12_ROOT_DESCRIPTOR_FLAGS flags, typename... args > struct SignatureDescription<Cbv<visibility, reg, space, flags>, args...>
{
private:
    using Next = SignatureDescription<args...>;
public:
    static constexpr auto Parameters = Next::Parameters.Cons<Cbv<visibility, reg, space, flags>>;
    static constexpr D3D12_ROOT_SIGNATURE_DESC1 Desc{
        .NumParameters     = Parameters.Size             ,
        .pParameters       = Parameters.Data             ,
        .NumStaticSamplers = Next::Desc.NumStaticSamplers,
        .pStaticSamplers   = Next::Desc.pStaticSamplers  ,
        .Flags             = Next::Desc.Flags            ,
    };

    operator D3D12_ROOT_SIGNATURE_DESC1 const&() const { return Desc; }
};

template < D3D12_ROOT_SIGNATURE_FLAGS flags, typename... args > struct SignatureDescription<Flags<flags>, args...>
{
private:
    using Next = SignatureDescription<args...>;
public:
    static constexpr auto Parameters = Next::Parameters;
    static constexpr D3D12_ROOT_SIGNATURE_DESC1 Desc{
        .NumParameters     = Next::Desc.NumParameters    ,
        .pParameters       = Next::Desc.pParameters      ,
        .NumStaticSamplers = Next::Desc.NumStaticSamplers,
        .pStaticSamplers   = Next::Desc.pStaticSamplers  ,
        .Flags             = Next::Desc.Flags | flags    ,
    };

    operator D3D12_ROOT_SIGNATURE_DESC1 const&() const { return Desc; }
};

}
// namespace Root

template < typename... args >
inline ComPtr<ID3D12RootSignature> RenderDevice::CreateRootSignature()
{
    return CreateRootSignature(Root::SignatureDescription<args...>{});
}

