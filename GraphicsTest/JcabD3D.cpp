
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
    RenderDevice renderDevice{};

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

    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&renderDevice.device)));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc{
        .Type  = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };

    ThrowIfFailed(renderDevice.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&renderDevice.commandQueue)));

    ThrowIfFailed(renderDevice.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&renderDevice.assetsCommandAllocator)));
    renderDevice.commandList = renderDevice.CreateDirectCommandList(renderDevice.assetsCommandAllocator);
    ThrowIfFailed(renderDevice.commandList->Close());

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{
        .Width       = 1280,
        .Height      = 720,
        .Format      = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc  { .Count = 1 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT,
        .BufferCount = FrameCount,
        .SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    };

    {
        ComPtr<IDXGISwapChain1> swapChain1;
        ThrowIfFailed(factory->CreateSwapChainForHwnd(
            renderDevice.commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain1
        ));

        ThrowIfFailed(swapChain1.As(&renderDevice.swapChain));
    }

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    renderDevice.viewport    = D3D12_VIEWPORT{ 0, 0, 1280, 720, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    renderDevice.scissorRect = D3D12_RECT    { 0, 0, 1280, 720 };

    // Create descriptor heaps.
    renderDevice.rtvDescriptorHeap     = renderDevice.rtvDescriptorHeap    .Create(renderDevice.device.Get(),    10);
    renderDevice.dsvDescriptorHeap     = renderDevice.dsvDescriptorHeap    .Create(renderDevice.device.Get(),    10);
    renderDevice.srvDescriptorHeap     = renderDevice.srvDescriptorHeap    .Create(renderDevice.device.Get(), 10000, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    renderDevice.samplerDescriptorHeap = renderDevice.samplerDescriptorHeap.Create(renderDevice.device.Get(),    10, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

    {
        auto const defaultSamplerIndex = renderDevice.samplerDescriptorHeap.AllocateDescriptors(1);
        assert(defaultSamplerIndex == 0);
        auto handle = renderDevice.samplerDescriptorHeap.GetCpuDescriptorHandle(defaultSamplerIndex);
    
        D3D12_SAMPLER_DESC const wrapSamplerDesc
        {
            .Filter         = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            .MipLODBias     = 0.0f,
            .MaxAnisotropy  = 1,
            .ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS,
            .BorderColor    {},
            .MinLOD         = 0,
            .MaxLOD         = D3D12_FLOAT32_MAX,
        };
        renderDevice.device->CreateSampler(&wrapSamplerDesc, handle);
    }

    // Create frame resources.
    {
        auto rtvIndex = renderDevice.rtvDescriptorHeap.AllocateDescriptors(FrameCount);
        assert(rtvIndex == 0);
        auto rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(0);

         // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(renderDevice.swapChain->GetBuffer(n, IID_PPV_ARGS(&renderDevice.renderTargets[n])));
            renderDevice.device->CreateRenderTargetView(renderDevice.renderTargets[n].Get(), nullptr, rtvHandle);

            rtvHandle = renderDevice.rtvDescriptorHeap.NextDescriptorHandle(rtvHandle);

            ThrowIfFailed(renderDevice.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&renderDevice.commandAllocators[n])));
        }
    }
    {
        auto srvIndex = renderDevice.srvDescriptorHeap.AllocateDescriptors(FrameCount);
        assert(srvIndex == 0);
        auto srvHandle = renderDevice.srvDescriptorHeap.GetCpuDescriptorHandle(0);

        D3D12_SHADER_RESOURCE_VIEW_DESC const srvDesc{
            .Format                  = swapChainDesc.Format,
            .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D               { .MipLevels = 1 },
        };

        // Create a SRV descriptor for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            renderDevice.device->CreateShaderResourceView(renderDevice.renderTargets[n].Get(), &srvDesc, srvHandle);
            srvHandle = renderDevice.rtvDescriptorHeap.NextDescriptorHandle(srvHandle);
        }
    }

    renderDevice.frameIndex = renderDevice.swapChain->GetCurrentBackBufferIndex();

    // Create synchronization objects.
    renderDevice.fence = std::make_shared<GpuSignalingFence>(renderDevice.device, renderDevice.commandQueue);

    return renderDevice;
}

ComPtr<ID3D12CommandAllocator> RenderDevice::CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE const type) const
{
    ComPtr<ID3D12CommandAllocator> allocator;
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    return allocator;
}

ComPtr<ID3D12RootSignature> RenderDevice::CreateRootSignature(D3D12_ROOT_SIGNATURE_DESC  const& desc) const
{
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(reinterpret_cast<D3D12_ROOT_SIGNATURE_DESC const*>(&desc), D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    return rootSignature;
}

ComPtr<ID3D12RootSignature> RenderDevice::CreateRootSignature(D3D12_ROOT_SIGNATURE_DESC1 const& desc) const
{
    return CreateRootSignature(reinterpret_cast<D3D12_ROOT_SIGNATURE_DESC const&>(desc));
}

ComPtr<ID3D12GraphicsCommandList> RenderDevice::CreateDirectCommandList(ComRef<ID3D12CommandAllocator> allocator, ComRef<ID3D12PipelineState> pipelineState) const
{
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, pipelineState, IID_PPV_ARGS(&commandList)));
    return commandList;
}

void RenderDevice::UploadSrvDescriptor(UINT index, ComRef<ID3D12Resource> const resource)
{
    auto handle = srvDescriptorHeap.GetCpuDescriptorHandle(index);
    device->CreateShaderResourceView(resource, nullptr, handle);
}

void RenderDevice::UploadSrvDescriptor(UINT index, ComRef<ID3D12Resource> const resource, D3D12_SHADER_RESOURCE_VIEW_DESC const& desc)
{
    auto handle = srvDescriptorHeap.GetCpuDescriptorHandle(index);
    device->CreateShaderResourceView(resource, &desc, handle);
}

void RenderDevice::UploadUavDescriptor(UINT index, ComRef<ID3D12Resource> const resource, D3D12_UNORDERED_ACCESS_VIEW_DESC const& desc)
{
    auto handle = srvDescriptorHeap.GetCpuDescriptorHandle(index);
    device->CreateUnorderedAccessView(resource, nullptr, &desc, handle);
}

UINT RenderDevice::AllocateSrvDescriptor(ComRef<ID3D12Resource> const resource, D3D12_SHADER_RESOURCE_VIEW_DESC const& desc)
{
    auto const index = srvDescriptorHeap.AllocateDescriptors(1);
    UploadSrvDescriptor(index, resource, desc);
    return index;
}

UINT RenderDevice::AllocateUavDescriptor(ComRef<ID3D12Resource> const resource, D3D12_UNORDERED_ACCESS_VIEW_DESC const& desc)
{
    auto const index = srvDescriptorHeap.AllocateDescriptors(1);
    UploadUavDescriptor(index, resource, desc);
    return index;
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

GpuSignalingFence::GpuSignalingFence(ComRef<ID3D12Device> const device, ComPtr<ID3D12CommandQueue> queue)
{
    ThrowIfFailed(device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_queue = std::move(queue);
}

void ExecuteCommandList(ComRef<ID3D12CommandQueue> const queue, ComRef<ID3D12GraphicsCommandList> const commandList)
{
    ID3D12CommandList* const list = commandList;
    queue->ExecuteCommandLists(1, &list);
}

void RenderDevice::ExecuteCommandList(ComRef<ID3D12GraphicsCommandList> const commandList)
{
    ID3D12CommandList* const list = commandList;
    commandQueue->ExecuteCommandLists(1, &list);
}

// Prepare to render the next frame.
void RenderDevice::StartFrame()
{
    TransitionResource(commandList, renderTargets[frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    auto const rtvHandle = rtvDescriptorHeap.GetCpuDescriptorHandle(frameIndex);
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    commandList->RSSetViewports    (1, &viewport);
    commandList->RSSetScissorRects (1, &scissorRect);

    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
}

// Prepare to render the next frame.
void RenderDevice::PresentFrame()
{
    TransitionResource(commandList, renderTargets[frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    ThrowIfFailed(commandList->Close());

    // Execute the command list.
    ExecuteCommandList(commandList);

    // Present the frame.
    ThrowIfFailed(swapChain->Present(1, 0));

    // Signal the end of the frame's command lists.
    auto const currentFenceValue = frameFenceValues[frameIndex] = fence->SignalOnGpu();

    // Update the frame index.
    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    fence->WaitOnCpu(frameFenceValues[frameIndex]);
}



ComPtr<ID3D12Resource> CreateUploadConstantBuffer(
    ComRef<ID3D12Device>  device,
    UINT                  size,
    D3D12_RESOURCE_STATES initState
)
{
    ComPtr<ID3D12Resource> resource;

    UINT const alignedSize = CalculateConstantBufferByteSize(size);

    BufferDesc bufferDesc{ alignedSize };

    ThrowIfFailed(device->CreateCommittedResource(
        &UploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        initState,
        nullptr,
        IID_PPV_ARGS(&resource)));

    return resource;
}




ComPtr<ID3DBlob> Compile(Shader const& shader)
{
    ComPtr<ID3DBlob> compiledShader;
#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    auto const compileFlags = shader.Flags | D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    auto const compileFlags = shader.Flags | D3DCOMPILE_DEBUG;
#endif
    ComPtr<ID3DBlob> errors;
    auto const hr = D3DCompileFromFile(shader.pFilename, nullptr, nullptr, shader.pFunction, shader.pTarget, compileFlags, 0, &compiledShader, &errors);
    if (errors)
    {
        std::string const errorString{ static_cast<char const*>(errors->GetBufferPointer()), errors->GetBufferSize() };
        OutputDebugStringA(errorString.c_str());
    }
    ThrowIfFailed(hr);
    return compiledShader;
}

D3D12_SHADER_BYTECODE GetShaderBytecode(ComRef<ID3DBlob> const shader)
{
    if (shader)
    {
        return { shader->GetBufferPointer(), shader->GetBufferSize() };
    }
    else
    {
        return {};
    }
}

ComPtr<ID3D12PipelineState> RenderDevice::Create(Pipeline::State const& state)
{
    ComPtr<ID3D12PipelineState> result;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC const desc
    {
        .pRootSignature        = state.RootSignature,
        .VS                    = GetShaderBytecode(state.VS),
        .PS                    = GetShaderBytecode(state.PS),
        .DS                    = GetShaderBytecode(state.DS),
        .HS                    = GetShaderBytecode(state.HS),
        .GS                    = GetShaderBytecode(state.GS),
        .BlendState            = state.BlendState,
        .SampleMask            = state.Multisampling.Mask,
        .RasterizerState       = state.RasterizerState,
        .DepthStencilState     = state.DepthStencilState,
        .InputLayout           = state.InputLayout,
        .PrimitiveTopologyType = state.TopologyType,
        .NumRenderTargets      = state.RTVFormats[0] == DXGI_FORMAT_UNKNOWN ? 0u :
                                 state.RTVFormats[1] == DXGI_FORMAT_UNKNOWN ? 1u :
                                 state.RTVFormats[2] == DXGI_FORMAT_UNKNOWN ? 2u :
                                 state.RTVFormats[3] == DXGI_FORMAT_UNKNOWN ? 3u :
                                 state.RTVFormats[4] == DXGI_FORMAT_UNKNOWN ? 4u :
                                 state.RTVFormats[5] == DXGI_FORMAT_UNKNOWN ? 5u :
                                 state.RTVFormats[6] == DXGI_FORMAT_UNKNOWN ? 6u :
                                 state.RTVFormats[7] == DXGI_FORMAT_UNKNOWN ? 7u :
                                                                              8u,
        .RTVFormats            { state.RTVFormats[0],
                                 state.RTVFormats[1],
                                 state.RTVFormats[2],
                                 state.RTVFormats[3],
                                 state.RTVFormats[4],
                                 state.RTVFormats[5],
                                 state.RTVFormats[6],
                                 state.RTVFormats[7] },
        .DSVFormat             = state.DSVFormat,
        .SampleDesc            { .Count = state.Multisampling.Count, .Quality = state.Multisampling.Quality },
    };
    ThrowIfFailed(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&result)));
    return result;
}

ComPtr<ID3D12PipelineState> RenderDevice::Create(Compute::State const& state)
{
    ComPtr<ID3D12PipelineState> result;

    D3D12_COMPUTE_PIPELINE_STATE_DESC const desc
    {
        .pRootSignature = state.RootSignature,
        .CS             = GetShaderBytecode(state.CS),
    };
    ThrowIfFailed(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&result)));
    return result;
}

ComPtr<ID3D12Resource> CreateUploadBuffer(ComRef<ID3D12Device> const device, UINT const size)
{
    ComPtr<ID3D12Resource> uploadBuffer;
    BufferDesc const bufferDesc{ size };
    ThrowIfFailed(device->CreateCommittedResource(
        &UploadHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    return uploadBuffer;
}

ComPtr<ID3D12Resource> CreateGpuBuffer(ComRef<ID3D12Device> const device, UINT const size)
{
    ComPtr<ID3D12Resource> uploadBuffer;
    BufferDesc const bufferDesc{ size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS };
    ThrowIfFailed(device->CreateCommittedResource(
        &StaticBufferHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    return uploadBuffer;
}

ComPtr<ID3D12Resource> AssetUploader::CreateStaticBuffer(
    uint32_t                   const size,
    D3D12_RESOURCE_STATES      const initialState,
    std::function<void(void*)> const dataFill
)
{
    ComPtr<ID3D12Resource> buffer;

    auto uploadBuffer = CreateUploadBuffer(m_device, size);

    // Copy the triangle data to the vertex buffer.
    void* p;
    ThrowIfFailed(uploadBuffer->Map(0, &NullRange, &p));
    dataFill(p);
    uploadBuffer->Unmap(0, nullptr);

    BufferDesc const bufferDesc{ size };
    ThrowIfFailed(m_device->CreateCommittedResource(
        &StaticBufferHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&buffer)));

    m_commandList->CopyBufferRegion(buffer.Get(), 0, uploadBuffer.Get(), 0, size);

    TransitionResource(m_commandList, buffer, D3D12_RESOURCE_STATE_COPY_DEST, initialState);

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());
    ExecuteCommandList(m_queue, m_commandList);
    HoldPendingObject(std::move(uploadBuffer));
    ThrowIfFailed(m_commandList->Reset(m_allocator.Get(), nullptr));

    return buffer;
}

ComPtr<ID3D12Resource> AssetUploader::CreateStaticTexture(
    uint32_t                                                       width,
    uint32_t                                                       height,
    D3D12_RESOURCE_STATES                                          initialState,
    std::function<void(uint32_t x, uint32_t y, uint32_t w, void*)> dataFill
)
{
    ComPtr<ID3D12Resource> texture;

    Texture2DDesc desc{ width, height, DXGI_FORMAT_R8G8B8A8_UNORM }; // _SRGB

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT   numRows        = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes     = 0;
    m_device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

    auto uploadBuffer = CreateUploadBuffer(m_device, totalBytes);

    // Copy the triangle data to the vertex buffer.
    std::byte* p;
    ThrowIfFailed(uploadBuffer->Map(0, &NullRange, reinterpret_cast<void**>(&p)));
    for (UINT y = 0; y < numRows; ++y)
    {
        dataFill(0, y, footprint.Footprint.Width, p + footprint.Offset + y * footprint.Footprint.RowPitch);
    }
    uploadBuffer->Unmap(0, nullptr);

    ThrowIfFailed(m_device->CreateCommittedResource(
        &StaticBufferHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)));

    D3D12_TEXTURE_COPY_LOCATION const destLocation
    {
        .pResource = texture.Get(),
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        .SubresourceIndex = 0,
    };
    D3D12_TEXTURE_COPY_LOCATION const srcLocation
    {
        .pResource = uploadBuffer.Get(),
        .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint = footprint,
    };
    m_commandList->CopyTextureRegion(&destLocation, 0, 0, 0, &srcLocation, nullptr);

    TransitionResource(m_commandList, texture, D3D12_RESOURCE_STATE_COPY_DEST, initialState);

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(m_commandList->Close());
    ExecuteCommandList(m_queue, m_commandList);
    HoldPendingObject(std::move(uploadBuffer));
    ThrowIfFailed(m_commandList->Reset(m_allocator.Get(), nullptr));

    return texture;
}




AssetUploader::AssetUploader(RenderDevice const& renderDevice)
    : m_device     { renderDevice.device }
    , m_queue      { renderDevice.commandQueue }
    , m_allocator  { renderDevice.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT) }
    , m_commandList{ renderDevice.CreateDirectCommandList(m_allocator) }
    , m_fence      { renderDevice.device, renderDevice.commandQueue }
{}

AssetUploader::~AssetUploader()
{
    m_fence.WaitForGpuIdle();
    // Now the pending objects can be deleted.
}

void AssetUploader::HoldPendingObject(ComPtr<IUnknown> o)
{
    m_pendingObjects.emplace_back(m_fence.SignalOnGpu(), std::move(o));
    // TODO: Maybe look to see if we can delete some objects now?
}





IntermediateRenderTarget CreateIntermediateRenderTarget(
    RenderDevice&               renderDevice,
    UINT                  const width,
    UINT                  const height,
    DXGI_FORMAT           const format,
    std::array<float, 4>  const clearColor,
    D3D12_RESOURCE_STATES const initialState,
    D3D12_RESOURCE_FLAGS  const flags,
    uint32_t              const msaa
)
{
    IntermediateRenderTarget renderTarget
    {
        .Width      = width ,
        .Height     = height,
        .ClearColor = clearColor,
    };
        
    D3D12_HEAP_PROPERTIES const heap{
        .Type                 = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask     = 1,
        .VisibleNodeMask      = 1,
    };
    D3D12_RESOURCE_DESC const desc{
        .Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment        = 0,
        .Width            = width,
        .Height           = height,
        .DepthOrArraySize = 1,
        .MipLevels        = 1,
        .Format           = format,
        .SampleDesc       { .Count = msaa, .Quality = 0 },
        .Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | flags,
    };
    D3D12_CLEAR_VALUE const clear
    {
        .Format = format,
        .Color{ clearColor[0], clearColor[1], clearColor[2], clearColor[3] },
    };
    ThrowIfFailed(renderDevice.device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        &clear,
        IID_PPV_ARGS(&renderTarget.Resource)
    ));

    // Create frame resources.
    {
        renderTarget.RtvDescriptorIndex = renderDevice.rtvDescriptorHeap.AllocateDescriptors(1);
        auto handle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(renderTarget.RtvDescriptorIndex);

        // Create a RTV and a command allocator for each frame.
        renderDevice.device->CreateRenderTargetView(renderTarget.Resource.Get(), nullptr, handle);
    }

    renderTarget.SrvDescriptorIndex = renderDevice.AllocateSrvDescriptor(renderTarget.Resource,
        {
            .Format                  = format,
            .ViewDimension           = msaa > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D               { .MipLevels = 1 },
        }
    );

    return renderTarget;
}

IntermediateDepthBuffer CreateIntermediateDepthBuffer(
    RenderDevice&               renderDevice,
    UINT                  const width,
    UINT                  const height,
    float                 const clearValue,
    D3D12_RESOURCE_STATES const initialState,
    uint32_t              const msaa
)
{
    IntermediateDepthBuffer depthBuffer
    {
        .Width      = width ,
        .Height     = height,
        .ClearValue = clearValue,
    };
        
    D3D12_HEAP_PROPERTIES const heap{
        .Type                 = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask     = 1,
        .VisibleNodeMask      = 1,
    };
    D3D12_RESOURCE_DESC const desc{
        .Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment        = 0,
        .Width            = width,
        .Height           = height,
        .DepthOrArraySize = 1,
        .MipLevels        = 1,
        .Format           = DXGI_FORMAT_D32_FLOAT,
        .SampleDesc       { .Count = msaa, .Quality = 0 },
        .Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
    };
    D3D12_CLEAR_VALUE const clear
    {
        .Format = DXGI_FORMAT_D32_FLOAT,
        .Color  = clearValue,
    };
    ThrowIfFailed(renderDevice.device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        &clear,
        IID_PPV_ARGS(&depthBuffer.Resource)
    ));

    // Create frame resources.
    {
        depthBuffer.DsvDescriptorIndex = renderDevice.dsvDescriptorHeap.AllocateDescriptors(1);
        auto handle = renderDevice.dsvDescriptorHeap.GetCpuDescriptorHandle(depthBuffer.DsvDescriptorIndex);

        // Create a RTV and a command allocator for each frame.
        renderDevice.device->CreateDepthStencilView(depthBuffer.Resource.Get(), nullptr, handle);
    }

    {
        depthBuffer.SrvDescriptorIndex = renderDevice.srvDescriptorHeap.AllocateDescriptors(1);
        auto handle = renderDevice.srvDescriptorHeap.GetCpuDescriptorHandle(depthBuffer.SrvDescriptorIndex);

        D3D12_SHADER_RESOURCE_VIEW_DESC const srvDesc{
            .Format                  = DXGI_FORMAT_R32_FLOAT,
            .ViewDimension           = msaa > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D               { .MipLevels = 1 },
        };
        renderDevice.device->CreateShaderResourceView(depthBuffer.Resource.Get(), &srvDesc, handle);
    }

    return depthBuffer;
}
