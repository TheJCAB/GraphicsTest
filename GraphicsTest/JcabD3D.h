#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <array>
#include <span>
#include <variant>
#include <functional>


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


namespace Root     { template < typename... args > struct Signature; }
namespace Pipeline {                               struct State;     }
namespace Compute  {                               struct State;     }



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

    ComPtr<IUnknown> Extract() && { return std::move(heap); }

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
    GpuSignalingFence(ComRef<ID3D12Device>, ComPtr<ID3D12CommandQueue>);

    GpuSignalingFence(GpuSignalingFence&&)            = delete;
    GpuSignalingFence& operator=(GpuSignalingFence&&) = delete;

    UINT64 SignalOnGpu();

    void WaitOnCpu(UINT64);

    void WaitForGpuIdle() { WaitOnCpu(SignalOnGpu()); }

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

    DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_RTV        > rtvDescriptorHeap;
    DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_DSV        > dsvDescriptorHeap;
    DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV> srvDescriptorHeap;
    DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER    > samplerDescriptorHeap;

    uint32_t Width  = 0;
    uint32_t Height = 0;

    D3D12_VIEWPORT               viewport;
    D3D12_RECT                   scissorRect;

    ComPtr<ID3D12CommandAllocator> assetsCommandAllocator;

    ComPtr<ID3D12GraphicsCommandList> commandList;

    std::array<ComPtr<ID3D12Resource>        , FrameCount> renderTargets;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> commandAllocators;

    UINT64              frameFenceValues[FrameCount];
    UINT                frameIndex;
    UINT                previousFrameIndex = 0;

    std::shared_ptr<GpuSignalingFence> fence;

    ComPtr<ID3D12CommandAllocator> CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE) const;

    ComPtr<ID3D12RootSignature> Create(D3D12_ROOT_SIGNATURE_DESC  const&) const;
    ComPtr<ID3D12RootSignature> Create(D3D12_ROOT_SIGNATURE_DESC1 const&) const;

    template < typename... args > ComPtr<ID3D12RootSignature> CreateRootSignature() const;

    ComPtr<ID3D12PipelineState> Create(Pipeline::State const&) const;
    ComPtr<ID3D12PipelineState> Create(Compute::State const&) const;

    ComPtr<ID3D12GraphicsCommandList> CreateDirectCommandList(ComRef<ID3D12CommandAllocator>, ComRef<ID3D12PipelineState> = nullptr) const;

    void UploadSrvDescriptor(UINT index, ComRef<ID3D12Resource>);
    void UploadSrvDescriptor(UINT index, ComRef<ID3D12Resource>, D3D12_SHADER_RESOURCE_VIEW_DESC const&);
    void UploadUavDescriptor(UINT index, ComRef<ID3D12Resource>, D3D12_UNORDERED_ACCESS_VIEW_DESC const&);

    UINT AllocateSrvDescriptor(ComRef<ID3D12Resource>, D3D12_SHADER_RESOURCE_VIEW_DESC const&);
    UINT AllocateUavDescriptor(ComRef<ID3D12Resource>, D3D12_UNORDERED_ACCESS_VIEW_DESC const&);

    void ExecuteCommandList(ComRef<ID3D12GraphicsCommandList>);

    void WaitForGpuIdle() { fence->WaitForGpuIdle(); }

    void StartFrame  ();
    void PresentFrame();

    static RenderDevice Create(HWND hwnd, uint32_t width, uint32_t height);
};

void ExecuteCommandList(ComRef<ID3D12CommandQueue>, ComRef<ID3D12GraphicsCommandList>);

ComPtr<ID3D12Resource> CreateUploadBuffer(ComRef<ID3D12Device>, UINT size); // CPU-mapped, usable to render or to upload data.
ComPtr<ID3D12Resource> CreateGpuBuffer   (ComRef<ID3D12Device>, UINT size); // UAV-ready buffer for GPU use only.



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

constexpr D3D12_RANGE NullRange{ 0, 0 };

constexpr D3D12_HEAP_PROPERTIES UploadHeapProperties
{
    .Type                 = D3D12_HEAP_TYPE_UPLOAD,
    .CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
    .CreationNodeMask     = 1,
    .VisibleNodeMask      = 1,
};
    D3D12_HEAP_PROPERTIES const heap{
        .Type                 = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask     = 1,
        .VisibleNodeMask      = 1,
    };

constexpr D3D12_HEAP_PROPERTIES StaticBufferHeapProperties
{
    .Type                 = D3D12_HEAP_TYPE_DEFAULT,
    .CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
    .CreationNodeMask     = 1,
    .VisibleNodeMask      = 1,
};

struct BufferDesc : D3D12_RESOURCE_DESC
{
    BufferDesc(UINT const size, D3D12_RESOURCE_FLAGS const flags = D3D12_RESOURCE_FLAG_NONE)
        : D3D12_RESOURCE_DESC
          {
            .Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER,
            .Alignment        = 0,
            .Width            = size,
            .Height           = 1,
            .DepthOrArraySize = 1,
            .MipLevels        = 1,
            .Format           = DXGI_FORMAT_UNKNOWN,
            .SampleDesc       { .Count = 1, .Quality = 0 },
            .Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            .Flags            = flags,
          }
    {}
};

struct Texture2DDesc : D3D12_RESOURCE_DESC
{
    Texture2DDesc(uint32_t const width, uint32_t const height, uint32_t const mips, DXGI_FORMAT const format, D3D12_RESOURCE_FLAGS const flags = D3D12_RESOURCE_FLAG_NONE)
        : D3D12_RESOURCE_DESC
          {
            .Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            .Alignment        = 0,
            .Width            = width,
            .Height           = height,
            .DepthOrArraySize = 1,
            .MipLevels        = static_cast<uint16_t>(mips),
            .Format           = format,
            .SampleDesc       { .Count = 1, .Quality = 0 },
            .Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            .Flags            = flags,
          }
    {}
};

constexpr D3D12_DEPTH_STENCILOP_DESC DefaultStencilOp
{
    .StencilFailOp      = D3D12_STENCIL_OP_KEEP,
    .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
    .StencilPassOp      = D3D12_STENCIL_OP_KEEP,
    .StencilFunc        = D3D12_COMPARISON_FUNC_ALWAYS,
};

constexpr D3D12_DEPTH_STENCIL_DESC DefaultDepthStencilDesc
{
    .DepthEnable      = TRUE,
    .DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ALL,
    .DepthFunc        = D3D12_COMPARISON_FUNC_GREATER,
    .StencilEnable    = FALSE,
};



ComPtr<ID3D12Resource> CreateUploadConstantBuffer(ComRef<ID3D12Device>, UINT size, D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_GENERIC_READ);

template < typename T, size_t Count = 1 >
struct ConstantBuffer
{
    ComPtr<ID3D12Resource> pResource;
    std::span<T>           pBuffer{};

    std::span<T> Get               () const { return pBuffer; }
    T* operator->                  () const { return pBuffer.data(); }
    T& operator[]                  (size_t i) const { return pBuffer[i]; }
    operator T*                    () const { return pBuffer.data; }
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

template < typename T, size_t Count >
inline ConstantBuffer<T, Count> ConstantBuffer<T, Count>::Create(ComRef<ID3D12Device>  device, D3D12_RESOURCE_STATES initState)
{
    ConstantBuffer<T, Count> result{};

    result.pResource = CreateUploadConstantBuffer(device, sizeof(std::array<T, Count>), initState);

    // We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
    D3D12_RANGE const readRange{ .Begin = 0, .End = 0 }; // We do not intend to read from this resource on the CPU.
    T* pBuffer = nullptr;
    ThrowIfFailed(result.pResource->Map(0, &readRange, reinterpret_cast<void**>(&pBuffer)));
    result.pBuffer = std::span{ pBuffer, Count };

    return result;
}


inline void TransitionResource(ComRef<ID3D12GraphicsCommandList> commandList, ComRef<ID3D12Resource> resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER const barrier
    {
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

inline void UavBarrier(ComRef<ID3D12GraphicsCommandList> commandList, ComRef<ID3D12Resource> resource)
{
    D3D12_RESOURCE_BARRIER const barrier
    {
        .Type  = D3D12_RESOURCE_BARRIER_TYPE_UAV,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .UAV   { .pResource = resource },
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

template < UINT base, UINT count = 1, UINT space = 0 > struct SrvRange
{
    static constexpr D3D12_DESCRIPTOR_RANGE1 Range{ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV, .NumDescriptors = count, .BaseShaderRegister = base, .RegisterSpace = space, .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
};

template < UINT base, UINT count = 1, UINT space = 0 > struct UavRange
{
    static constexpr D3D12_DESCRIPTOR_RANGE1 Range{ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV, .NumDescriptors = count, .BaseShaderRegister = base, .RegisterSpace = space, .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
};

template < UINT base, UINT count = 1, UINT space = 0 > struct SamplerRange
{
    static constexpr D3D12_DESCRIPTOR_RANGE1 Range{ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, .NumDescriptors = count, .BaseShaderRegister = base, .RegisterSpace = space, .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
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

template < D3D12_ROOT_PARAMETER_TYPE type, D3D12_SHADER_VISIBILITY visibility, UINT reg, UINT space, D3D12_ROOT_DESCRIPTOR_FLAGS flags > struct Descriptor
{
    static constexpr D3D12_ROOT_PARAMETER1 Parameter
    {
        .ParameterType = type,
        .Descriptor{
            .ShaderRegister = reg,
            .RegisterSpace  = space,
            .Flags = flags,
        },
        .ShaderVisibility = visibility,
    };
};

template < D3D12_SHADER_VISIBILITY visibility, UINT reg, UINT space, D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE> using Cbv = Descriptor<D3D12_ROOT_PARAMETER_TYPE_CBV, visibility, reg, space, flags>;
template < D3D12_SHADER_VISIBILITY visibility, UINT reg, UINT space, D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE> using Srv = Descriptor<D3D12_ROOT_PARAMETER_TYPE_SRV, visibility, reg, space, flags>;

template < D3D12_SHADER_VISIBILITY visibility, UINT reg, UINT space, UINT count > struct Constants
{
    static constexpr D3D12_ROOT_PARAMETER1 Parameter
    {
        .ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
        .Constants{ .ShaderRegister = reg, .RegisterSpace = space, .Num32BitValues = count },
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



template <> struct Signature<>
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

template < D3D12_SHADER_VISIBILITY visibility, typename... ranges, typename... args > struct Signature<Table<visibility, ranges...>, args...>
{
private:
    using Next = Signature<args...>;
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

template < D3D12_ROOT_PARAMETER_TYPE type, D3D12_SHADER_VISIBILITY visibility, UINT reg, UINT space, D3D12_ROOT_DESCRIPTOR_FLAGS flags, typename... args > struct Signature<Descriptor<type, visibility, reg, space, flags>, args...>
{
private:
    using Next = Signature<args...>;
public:
    static constexpr auto Parameters = Next::Parameters.Cons<Descriptor<type, visibility, reg, space, flags>>;
    static constexpr D3D12_ROOT_SIGNATURE_DESC1 Desc{
        .NumParameters     = Parameters.Size             ,
        .pParameters       = Parameters.Data             ,
        .NumStaticSamplers = Next::Desc.NumStaticSamplers,
        .pStaticSamplers   = Next::Desc.pStaticSamplers  ,
        .Flags             = Next::Desc.Flags            ,
    };

    operator D3D12_ROOT_SIGNATURE_DESC1 const&() const { return Desc; }
};

template < D3D12_SHADER_VISIBILITY visibility, UINT reg, UINT space, UINT count, typename... args > struct Signature<Constants<visibility, reg, space, count>, args...>
{
private:
    using Next = Signature<args...>;
public:
    static constexpr auto Parameters = Next::Parameters.Cons<Constants<visibility, reg, space, count>>;
    static constexpr D3D12_ROOT_SIGNATURE_DESC1 Desc{
        .NumParameters     = Parameters.Size             ,
        .pParameters       = Parameters.Data             ,
        .NumStaticSamplers = Next::Desc.NumStaticSamplers,
        .pStaticSamplers   = Next::Desc.pStaticSamplers  ,
        .Flags             = Next::Desc.Flags            ,
    };

    operator D3D12_ROOT_SIGNATURE_DESC1 const&() const { return Desc; }
};

template < D3D12_ROOT_SIGNATURE_FLAGS flags, typename... args > struct Signature<Flags<flags>, args...>
{
private:
    using Next = Signature<args...>;
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
inline ComPtr<ID3D12RootSignature> RenderDevice::CreateRootSignature() const
{
    return Create(Root::Signature<args...>{});
}



struct Shader
{
    wchar_t const* pFilename;
    char const*    pFunction;
    char const*    pTarget;
    UINT           Flags = 0;
};

ComPtr<ID3DBlob> Compile(Shader const&);



struct SV_Position { static constexpr char Name[] = "SV_Position"; static constexpr uint32_t Index = 0; };

namespace Vertex
{

template < typename Semantic, DXGI_FORMAT Format, uint32_t OffsetInBytes >
struct Input
{
    static constexpr D3D12_INPUT_ELEMENT_DESC Desc
    {
        .SemanticName      = Semantic::Name,
        .SemanticIndex     = Semantic::Index,
        .Format            = Format,
        .AlignedByteOffset = OffsetInBytes,
        .InputSlotClass    = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
    };
};

template < typename... Inputs >
struct InputLayout
{
    static constexpr std::array<D3D12_INPUT_ELEMENT_DESC, sizeof...(Inputs)> Descs { Inputs::Desc... };

    operator std::span<D3D12_INPUT_ELEMENT_DESC const>() const { return Descs; }
};

}
// namespace Vertex

using ShaderCode = std::variant<std::span<BYTE const>, ComPtr<ID3DBlob>>;

namespace Pipeline
{

struct MultisamplingState
{
    UINT Count   = 1;
    UINT Quality = 0;
    UINT Mask    = UINT_MAX;
};

constexpr MultisamplingState NoMultisampling{};

template < uint8_t msaa > constexpr MultisamplingState Multisampling{ .Count = msaa };

constexpr D3D12_DEPTH_STENCIL_DESC NoDepthStencil{ .DepthEnable = FALSE, .StencilEnable = FALSE };

struct State
{
    ComRef<ID3D12RootSignature>   RootSignature;

    D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    std::span<D3D12_INPUT_ELEMENT_DESC const> InputLayout{};
    ShaderCode                                VS;
    ShaderCode                                DS;
    ShaderCode                                HS;
    ShaderCode                                GS;

    ShaderCode                    PS;
    D3D12_RASTERIZER_DESC         RasterizerState   = defaultRasterizerDesc;
    D3D12_BLEND_DESC              BlendState        = defaultBlendDesc;
    D3D12_DEPTH_STENCIL_DESC      DepthStencilState = NoDepthStencil;
    MultisamplingState            Multisampling     = NoMultisampling;
    DXGI_FORMAT                   RTVFormats[8]     {};
    DXGI_FORMAT                   DSVFormat         = DXGI_FORMAT_UNKNOWN;
};

}
// namespace Pipeline

namespace Compute
{

struct State
{
    ComRef<ID3D12RootSignature> RootSignature;
    ShaderCode                  CS;
};

}
// namespace Compute


class AssetUploader;

struct MipGenerationPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    MipGenerationPass(RenderDevice const&);

    void Render(ComRef<ID3D12Device>, ComRef<ID3D12GraphicsCommandList>, AssetUploader&, ComRef<ID3D12Resource> uavTexture) const;
};

class AssetUploader
{
public:
    AssetUploader(RenderDevice const& renderDevice);
    ~AssetUploader();

    AssetUploader           (AssetUploader&&) = delete;
    AssetUploader& operator=(AssetUploader&&) = delete;

    void HoldPendingObject(ComPtr<IUnknown>);

    ComPtr<ID3D12Resource> CreateStaticBuffer (uint32_t size, D3D12_RESOURCE_STATES initialState, std::function<void(void*)> dataFill);
    ComPtr<ID3D12Resource> CreateStaticTexture(uint32_t width, uint32_t height, uint32_t mips, D3D12_RESOURCE_STATES initialState, std::function<void(uint32_t x, uint32_t y, uint32_t w, void*)> dataFill);

    void GenerateMips(ComRef<ID3D12Resource> uavTexture)
    {
        m_mipGenerator.Render(m_device, m_commandList, *this, uavTexture);
    }

private:
    ComPtr<ID3D12Device>               m_device;
    ComPtr<ID3D12CommandQueue>         m_queue;
    ComPtr<ID3D12CommandAllocator>     m_allocator;
    ComPtr<ID3D12GraphicsCommandList>  m_commandList;
    GpuSignalingFence                  m_fence;

    struct PendingObject
    {
        UINT64           Fence;
        ComPtr<IUnknown> Object;
    };

    std::vector<PendingObject>         m_pendingObjects;

    MipGenerationPass                  m_mipGenerator;
};



struct IntermediateRenderTarget
{
    UINT                   Width              = 0;
    UINT                   Height             = 0;
    std::array<float, 4>   ClearColor         {};
    ComPtr<ID3D12Resource> Resource           {};
    UINT                   RtvDescriptorIndex = 0;
    UINT                   SrvDescriptorIndex = 0;
};

IntermediateRenderTarget CreateIntermediateRenderTarget(
    RenderDevice&         ,
    UINT                  width,
    UINT                  height,
    DXGI_FORMAT           ,
    std::array<float, 4>  clearColor,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    D3D12_RESOURCE_FLAGS  = D3D12_RESOURCE_FLAG_NONE,
    uint32_t              msaa = 1
);

struct IntermediateDepthBuffer
{
    UINT                   Width              = 0;
    UINT                   Height             = 0;
    float                  ClearValue         = 0;
    ComPtr<ID3D12Resource> Resource           {};
    UINT                   DsvDescriptorIndex = 0;
    UINT                   SrvDescriptorIndex = 0;
};

IntermediateDepthBuffer CreateIntermediateDepthBuffer(
    RenderDevice&         ,
    UINT                  width,
    UINT                  height,
    float                 clearValue,
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
    uint32_t              msaa = 1
);



template < typename T >
struct CpuAccessibleBuffer
{
    ComPtr<ID3D12Resource> Buffer{};
    T*                     pData = nullptr;

    CpuAccessibleBuffer() = default;

    explicit CpuAccessibleBuffer(RenderDevice& renderDevice);

    explicit operator bool() const { return pData != nullptr; }
    operator ComRef<ID3D12Resource>() const { return Buffer; }
    operator T*  () const { return pData; }
    T& operator* () const { return *pData; }
    T* operator->() const { return pData; }
};

template < typename T >
CpuAccessibleBuffer<T>::CpuAccessibleBuffer(RenderDevice& renderDevice)
    : Buffer{ CreateUploadBuffer(renderDevice.device, sizeof(*pData)) }
{
    D3D12_RANGE const readRange{ .Begin = 0, .End = 0 }; // We do not intend to read from this resource on the CPU.
    ThrowIfFailed(Buffer->Map(0, &readRange, reinterpret_cast<void**>(&pData)));
}

