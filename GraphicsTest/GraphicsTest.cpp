// GraphicsTest.cpp : Defines the entry point for the application.
//

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

using DirectX::XMFLOAT3;
using DirectX::XMFLOAT4;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw hr;
    }
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
_Use_decl_annotations_
ComPtr<IDXGIAdapter1> GetHardwareAdapter(IDXGIFactory1* pFactory, bool requestHighPerformanceAdapter = true)
{
    ComPtr<IDXGIAdapter1> adapter;

    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (
            UINT adapterIndex = 0;
            SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                adapterIndex,
                requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                IID_PPV_ARGS(&adapter)));
            ++adapterIndex)
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
    }

    if(adapter.Get() == nullptr)
    {
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
    }
    
    return adapter;
}

constexpr UINT FrameCount = 2;

struct PipelineResources
{
    ComPtr<ID3D12Device>         device;
    ComPtr<ID3D12CommandQueue>   commandQueue;
    ComPtr<IDXGISwapChain3>      swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;

    UINT                         rtvDescriptorSize;

    D3D12_VIEWPORT               viewport;
    D3D12_RECT                   scissorRect;

    std::array<ComPtr<ID3D12Resource>        , FrameCount> renderTargets;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> commandAllocators;
};

// Load the rendering pipeline dependencies.
PipelineResources LoadPipeline(HWND const hwnd)
{
    PipelineResources result{};

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

    ComPtr<IDXGIAdapter1> hardwareAdapter = GetHardwareAdapter(factory.Get());

    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&result.device)));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc{
        .Type  = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };

    ThrowIfFailed(result.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&result.commandQueue)));

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
            result.commandQueue.Get(),        // Swap chain needs the queue so that it can force a flush on it.
            hwnd,
            &swapChainDesc,
            nullptr,
            nullptr,
            &swapChain1
        ));

        ThrowIfFailed(swapChain1.As(&result.swapChain));
    }

    // This sample does not support fullscreen transitions.
    ThrowIfFailed(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

    result.viewport    = D3D12_VIEWPORT{ 0, 0, 1280, 720, D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    result.scissorRect = D3D12_RECT    { 0, 0, 1280, 720 };

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{
            .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
            .NumDescriptors = FrameCount,
            .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        };
        ThrowIfFailed(result.device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&result.rtvHeap)));
    }
    result.rtvDescriptorSize = result.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create frame resources.
    {
        auto rtvHandle = result.rtvHeap->GetCPUDescriptorHandleForHeapStart();

        // Create a RTV and a command allocator for each frame.
        for (UINT n = 0; n < FrameCount; n++)
        {
            ThrowIfFailed(result.swapChain->GetBuffer(n, IID_PPV_ARGS(&result.renderTargets[n])));
            result.device->CreateRenderTargetView(result.renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += result.rtvDescriptorSize;

            ThrowIfFailed(result.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&result.commandAllocators[n])));
        }
    }

    return result;
}

struct Synchronization
{
    UINT                frameIndex;
    UINT64              fenceValues[FrameCount];
    ComPtr<ID3D12Fence> fence;
    HANDLE              fenceEvent;
};

Synchronization LoadSynchronization(PipelineResources const& pipeline)
{
    Synchronization result{};

    result.frameIndex = pipeline.swapChain->GetCurrentBackBufferIndex();

    // Create synchronization objects and wait until assets have been uploaded to the GPU.
    {
        ThrowIfFailed(pipeline.device->CreateFence(result.fenceValues[result.frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&result.fence)));
        result.fenceValues[result.frameIndex]++;

        // Create an event handle to use for frame synchronization.
        result.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (result.fenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
    }

    return result;
}

void WaitForGpu(PipelineResources const& pipeline, Synchronization& synchronization)
{
    // Schedule a Signal command in the queue.
    ThrowIfFailed(pipeline.commandQueue->Signal(synchronization.fence.Get(), synchronization.fenceValues[synchronization.frameIndex]));

    // Wait until the fence has been processed.
    ThrowIfFailed(synchronization.fence->SetEventOnCompletion(synchronization.fenceValues[synchronization.frameIndex], synchronization.fenceEvent));
    WaitForSingleObjectEx(synchronization.fenceEvent, INFINITE, FALSE);

    // Increment the fence value for the current frame.
    synchronization.fenceValues[synchronization.frameIndex]++;
}

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

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT4 color;
};

// Load the sample assets.
struct Assets
{
    ComPtr<ID3D12RootSignature>       rootSignature;
    ComPtr<ID3D12Resource>            objectTarget;
    ComPtr<ID3D12DescriptorHeap>      objectTargetDescriptorHeap;
    ComPtr<ID3D12PipelineState>       pipelineState;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Resource>            vertexBuffer[FrameCount];
    D3D12_VERTEX_BUFFER_VIEW          vertexBufferView[FrameCount];
};

Assets LoadAssets(PipelineResources const& pipeline, Synchronization& synchronization)
{
    Assets result{};

    // Create an empty root signature.
    {
        D3D12_ROOT_SIGNATURE_DESC const rootSignatureDesc{
            .NumParameters     = 0,
            .pParameters       = nullptr,
            .NumStaticSamplers = 0,
            .pStaticSamplers   = nullptr,
            .Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
        };

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(pipeline.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&result.rootSignature)));
    }

    // Create rendering surfaces.
    {
        
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
            .Width            = 1280,
            .Height           = 720,
            .DepthOrArraySize = 1,
            .MipLevels        = 1,
            .Format           = DXGI_FORMAT_R32_UINT,
            .SampleDesc       { .Count = 1, .Quality = 0 },
            .Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN,
            .Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
        };
        ThrowIfFailed(pipeline.device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            nullptr,
            IID_PPV_ARGS(&result.objectTarget)
        ));

        // Create descriptor heaps.
        {
            // Describe and create a render target view (RTV) descriptor heap.
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
                .Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                .NumDescriptors = 1,
                .Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            };
            ThrowIfFailed(pipeline.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&result.objectTargetDescriptorHeap)));
        }

        // Create frame resources.
        {
            auto handle = result.objectTargetDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

            // Create a RTV and a command allocator for each frame.
            for (UINT n = 0; n < FrameCount; n++)
            {
                pipeline.device->CreateRenderTargetView(result.objectTarget.Get(), nullptr, handle);
                handle.ptr += pipeline.rtvDescriptorSize;
            }
        }
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
        ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC const inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT   , 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR"   , 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature        = result.rootSignature.Get(),
            .VS                    { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() },
            .PS                    { pixelShader ->GetBufferPointer(), pixelShader ->GetBufferSize() },
            .BlendState            = defaultBlendDesc,
            .SampleMask            = UINT_MAX,
            .RasterizerState       = defaultRasterizerDesc,
            .DepthStencilState     { .DepthEnable = FALSE, .StencilEnable = FALSE },
            .InputLayout           { inputElementDescs, _countof(inputElementDescs) },
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .NumRenderTargets      = 1,
            .RTVFormats            { DXGI_FORMAT_R8G8B8A8_UNORM, },
            .SampleDesc            { .Count = 1 },
        };
        ThrowIfFailed(pipeline.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&result.pipelineState)));
    }

    // Create the command list.
    ThrowIfFailed(pipeline.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pipeline.commandAllocators[synchronization.frameIndex].Get(), result.pipelineState.Get(), IID_PPV_ARGS(&result.commandList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(result.commandList->Close());

    // Create the vertex buffer.
    {
        constexpr auto aspectRatio = 1280.0f / 712.0f;

        // Define the geometry for a triangle.
        Vertex triangleVertices[] =
        {
            { {  0.0f ,  0.25f * aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
            { {  0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
            { { -0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
            { { -0.25f,  0.0f  * aspectRatio, 0.0f }, { 0.0f, 1.0f, 1.0f, 1.0f } },
        };

        const UINT vertexBufferSize = sizeof(triangleVertices);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        D3D12_HEAP_PROPERTIES const uploadHeapProperties{
            .Type                 = D3D12_HEAP_TYPE_UPLOAD,
            .CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask     = 1,
            .VisibleNodeMask      = 1,
        };
        D3D12_RESOURCE_DESC const vertexBufferDesc{
            .Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER,
            .Alignment        = 0,
            .Width            = vertexBufferSize,
            .Height           = 1,
            .DepthOrArraySize = 1,
            .MipLevels        = 1,
            .Format           = DXGI_FORMAT_UNKNOWN,
            .SampleDesc       { .Count = 1, .Quality = 0 },
            .Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            .Flags            = D3D12_RESOURCE_FLAG_NONE,
        };
        ThrowIfFailed(pipeline.device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &vertexBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&result.vertexBuffer[0])));
        ThrowIfFailed(pipeline.device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &vertexBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&result.vertexBuffer[1])));

        // Copy the triangle data to the vertex buffer.
        for (UINT i = 0; i < FrameCount; ++i)
        {
            UINT8* pVertexDataBegin;
            D3D12_RANGE const nullReadRange{ 0, 0 };        // We do not intend to read from this resource on the CPU.
            ThrowIfFailed(result.vertexBuffer[i]->Map(0, &nullReadRange, reinterpret_cast<void**>(&pVertexDataBegin)));
            memcpy(pVertexDataBegin, triangleVertices + i, sizeof(triangleVertices[0]) * 3);
            result.vertexBuffer[i]->Unmap(0, nullptr);

            // Initialize the vertex buffer view.
            result.vertexBufferView[i].BufferLocation = result.vertexBuffer[i]->GetGPUVirtualAddress();
            result.vertexBufferView[i].StrideInBytes  = sizeof(Vertex);
            result.vertexBufferView[i].SizeInBytes    = vertexBufferSize;
        }
    }

    // We need to wait until assets have been uploaded to the GPU.

    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.
    WaitForGpu(pipeline, synchronization);

    return result;
}

void PopulateCommandList(PipelineResources const& pipeline, Assets const& assets, Synchronization& synchronization)
{
    auto const& commandAllocator = pipeline.commandAllocators[synchronization.frameIndex];

    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(assets.commandList->Reset(commandAllocator.Get(), assets.pipelineState.Get()));

    // Set necessary state.
    assets.commandList->SetGraphicsRootSignature(assets.rootSignature.Get());
    assets.commandList->RSSetViewports   (1, &pipeline.viewport);
    assets.commandList->RSSetScissorRects(1, &pipeline.scissorRect);

    // Indicate that the back buffer will be used as a render target.
    {
        D3D12_RESOURCE_BARRIER const barrier{
            .Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition{
                .pResource   = pipeline.renderTargets[synchronization.frameIndex].Get(),
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                .StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET,
            },
        };
        assets.commandList->ResourceBarrier(1, &barrier);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE const rtvHandle{
        pipeline.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + synchronization.frameIndex * pipeline.rtvDescriptorSize
    };
    assets.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    assets.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    assets.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    assets.commandList->IASetVertexBuffers(0, 1, &assets.vertexBufferView[synchronization.frameIndex]);
    assets.commandList->DrawInstanced(3, 1, 0, 0);

    // Indicate that the back buffer will now be used to present.
    {
        D3D12_RESOURCE_BARRIER const barrier{
            .Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition{
                .pResource   = pipeline.renderTargets[synchronization.frameIndex].Get(),
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
                .StateAfter  = D3D12_RESOURCE_STATE_PRESENT,
            },
        };
        assets.commandList->ResourceBarrier(1, &barrier);
    }

    ThrowIfFailed(assets.commandList->Close());
}

// Prepare to render the next frame.
void MoveToNextFrame(PipelineResources const& pipeline, Synchronization& synchronization)
{
    // Schedule a Signal command in the queue.
    auto const currentFenceValue = synchronization.fenceValues[synchronization.frameIndex];
    ThrowIfFailed(pipeline.commandQueue->Signal(synchronization.fence.Get(), currentFenceValue));

    // Update the frame index.
    synchronization.frameIndex = pipeline.swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is ready.
    if (synchronization.fence->GetCompletedValue() < synchronization.fenceValues[synchronization.frameIndex])
    {
        ThrowIfFailed(synchronization.fence->SetEventOnCompletion(synchronization.fenceValues[synchronization.frameIndex], synchronization.fenceEvent));
        WaitForSingleObjectEx(synchronization.fenceEvent, INFINITE, FALSE);
    }

    // Set the fence value for the next frame.
    synchronization.fenceValues[synchronization.frameIndex] = currentFenceValue + 1;
}

PipelineResources const* pPipeline       ;
Assets const*            pAssets         ;
Synchronization*         pSynchronization;

// Main message handler for the sample.
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            // Save the DXSample* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        }
        return 0;

    case WM_KEYDOWN:
        // OnKeyDown(static_cast<UINT8>(wParam));
        return 0;

    case WM_KEYUP:
        // OnKeyUp(static_cast<UINT8>(wParam));
        return 0;

    case WM_PAINT:
        // Record all the commands we need to render the scene into the command list.
        PopulateCommandList(*pPipeline, *pAssets, *pSynchronization);

        // Execute the command list.
        {
            ID3D12CommandList* const commandList = pAssets->commandList.Get();
            pPipeline->commandQueue->ExecuteCommandLists(1, &commandList);
        }

        // Present the frame.
        ThrowIfFailed(pPipeline->swapChain->Present(1, 0));

        MoveToNextFrame(*pPipeline, *pSynchronization);
        break;//return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Initialize the window class.
    WNDCLASSEX windowClass{
        .cbSize        = sizeof(WNDCLASSEX),
        .style         = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc   = WindowProc,
        .hInstance     = hInstance,
        .hCursor       = LoadCursor(NULL, IDC_ARROW),
        .lpszClassName = L"GraphicsTestClass",
    };
    RegisterClassEx(&windowClass);

    RECT windowRect{ 0, 0, 1280, 720 };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // Create the window and store a handle to it.
    auto const hwnd = CreateWindow(
        windowClass.lpszClassName,
        L"Graphics test",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,        // We have no parent window.
        nullptr,        // We aren't using menus.
        hInstance,
        nullptr         // lParam
    );

    auto const pipeline        = LoadPipeline(hwnd);
    auto       synchronization = LoadSynchronization(pipeline);
    auto const assets          = LoadAssets(pipeline, synchronization);

    pPipeline        = &pipeline       ;
    pAssets          = &assets         ;
    pSynchronization = &synchronization;

    ShowWindow(hwnd, nCmdShow);

    // Main sample loop.
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // Process any messages in the queue.
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else if (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    WaitForGpu(pipeline, synchronization);

    return 0;
}
