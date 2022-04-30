
#include "JcabD3D.h"

ComPtr<IDXGIAdapter1> FindHardwareAdapter(ComRef<IDXGIFactory1> pFactory, bool requestHighPerformanceAdapter)
{
    auto const gpuPreference = requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED;

    ComPtr<IDXGIFactory6> factory6;
    if (!SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        return {};
    }

    for (UINT adapterIndex = 0; ; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (!SUCCEEDED(factory6->EnumAdapterByGpuPreference(adapterIndex, gpuPreference, IID_PPV_ARGS(&adapter))))
        {
            return {};
        }

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            // Don't select the Basic Render Driver adapter.
            continue;
        }

        // Check to see whether the adapter supports Direct3D 12, but don't create the
        // actual device yet.
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
        {
            return adapter;
        }
    }
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
ComPtr<IDXGIAdapter1> GetPreferredAdapter(ComRef<IDXGIFactory1> pFactory, bool requestHighPerformanceAdapter)
{
    auto adapter = FindHardwareAdapter(pFactory, requestHighPerformanceAdapter);

    if(adapter.Get() != nullptr)
    {
        return adapter;
    }

    // Just find something.
    for (UINT adapterIndex = 0; SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            // Don't select the Basic Render Driver adapter.
            // If you want a software adapter, pass in "/warp" on the command line.
            continue;
        }

        // Check to see whether the adapter supports Direct3D 12, but don't create the
        // actual device yet.
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
        {
            break;
        }
    }
    
    return adapter;
}




template < D3D12_DESCRIPTOR_HEAP_TYPE type >
UINT DescriptorHeap<type>::AllocateDescriptors(UINT count)
{
    auto result = descriptorsUsed;
    descriptorsUsed += count;
    return result;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap<type>::GetCpuDescriptorHandle(UINT index) const
{
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += descriptorSize * index;
    return handle;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap<type>::NextDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const
{
    handle.ptr += descriptorSize;
    return handle;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap<type>::GetGpuDescriptorHandle(UINT index) const
{
    auto handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += descriptorSize * index;
    return handle;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap<type>::NextDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) const
{
    handle.ptr += descriptorSize;
    return handle;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
DescriptorHeap<type> DescriptorHeap<type>::Create(ComRef<ID3D12Device> device, UINT count, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
    DescriptorHeap<type> result;
    result.descriptorSize  = device->GetDescriptorHandleIncrementSize(type);
    result.descriptorCount = count;

    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC const heapDesc
    {
        .Type           = type,
        .NumDescriptors = count,
        .Flags          = flags,
    };
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&result.heap)));

    return result;
}

// Instantiate.
template class DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV>;
template class DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER    >;
template class DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_RTV        >;
template class DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_DSV        >;



// Load the rendering pipeline dependencies.
RenderDevice RenderDevice::Create(HWND const hwnd)
{
    RenderDevice pipeline{};

    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after device creation will invalidate the active device.
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> hardwareAdapter = GetPreferredAdapter(factory.Get());

    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pipeline.device)));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc{
        .Type  = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };

    ThrowIfFailed(pipeline.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&pipeline.commandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{
        .Width       = 1280,
        .Height      = 720,
        .Format      = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc  { .Count = 1 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = FrameCount,
        .SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    };

    {
        ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
            pipeline.commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain1
        ));

        ThrowIfFailed(swapChain1.As(&pipeline.swapChain));
    }

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    pipeline.viewport    = D3D12_VIEWPORT{ 0, 0, 1280, 720, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    pipeline.scissorRect = D3D12_RECT    { 0, 0, 1280, 720 };

    // Create descriptor heaps.
    pipeline.rtvDescriptorHeap = pipeline.rtvDescriptorHeap    .Create(pipeline.device.Get(), 10);

    // Create frame resources.
    {
        auto rtvIndex = pipeline.rtvDescriptorHeap.AllocateDescriptors(2);
        assert(rtvIndex == 0);
        auto rtvHandle = pipeline.rtvDescriptorHeap.GetCpuDescriptorHandle(0);

        // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(pipeline.swapChain->GetBuffer(n, IID_PPV_ARGS(&pipeline.renderTargets[n])));
            pipeline.device->CreateRenderTargetView(pipeline.renderTargets[n].Get(), nullptr, rtvHandle);

            rtvHandle = pipeline.rtvDescriptorHeap.NextDescriptorHandle(rtvHandle);

            ThrowIfFailed(pipeline.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&pipeline.commandAllocators[n])));
        }
    }

    pipeline.frameIndex = pipeline.swapChain->GetCurrentBackBufferIndex();

    // Create synchronization objects.
    pipeline.fence = pipeline.fence.Create(pipeline.device, pipeline.commandQueue);

    return pipeline;
}

ComPtr<ID3D12GraphicsCommandList> RenderDevice::CreateDirectCommandList(ComRef<ID3D12CommandAllocator> allocator, ComRef<ID3D12PipelineState> pipelineState)
{
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipelineState, IID_PPV_ARGS(&commandList)));
    return commandList;
}

UINT64 GpuSignalingFence::SignalOnGpu()
{
    auto const value = m_fenceValue;
    ++m_fenceValue;
    ThrowIfFailed(m_queue->Signal(m_fence.Get(), value));
    return value;
}

void GpuSignalingFence::WaitOnCpu(UINT64 const value)
{
    static thread_local HANDLE const eventHandle = []{
        auto const handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (handle == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
        return handle;
    }();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (m_fence->GetCompletedValue() < value)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(value, eventHandle));
        WaitForSingleObjectEx(eventHandle, INFINITE, FALSE);
    }
}

GpuSignalingFence GpuSignalingFence::Create(ComRef<ID3D12Device> const device, ComPtr<ID3D12CommandQueue> const queue)
{
    GpuSignalingFence fence{};
    ThrowIfFailed(device->CreateFence(fence.m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.m_fence)));
    fence.m_queue = std::move(queue);
    return fence;
}

// Prepare to render the next frame.
void RenderDevice::PresentFrame()
{
    // Present the frame.
    ThrowIfFailed(swapChain->Present(1, 0));

    // Signal the end of the frame's command lists.
    auto const currentFenceValue = frameFenceValues[frameIndex] = fence.SignalOnGpu();

    // Update the frame index.
    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    fence.WaitOnCpu(frameFenceValues[frameIndex]);
}




ComPtr<ID3D12Resource> CreateUploadConstantBuffer(
    ComRef<ID3D12Device>  device,
    UINT                  size,
    D3D12_RESOURCE_STATES initState
)
{
    ComPtr<ID3D12Resource> resource;

    UINT const alignedSize = CalculateConstantBufferByteSize(size);

    D3D12_HEAP_PROPERTIES const uploadHeapProperties{
        .Type                 = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask     = 1,
        .VisibleNodeMask      = 1,
    };

    D3D12_RESOURCE_DESC const bufferDesc{
        .Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment        = 0,
        .Width            = alignedSize,
        .Height           = 1,
        .DepthOrArraySize = 1,
        .MipLevels        = 1,
        .Format           = DXGI_FORMAT_UNKNOWN,
        .SampleDesc       { .Count = 1, .Quality = 0 },
        .Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags            = D3D12_RESOURCE_FLAG_NONE,
    };

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        initState,
        nullptr,
        IID_PPV_ARGS(&resource)));

    return resource;
}
