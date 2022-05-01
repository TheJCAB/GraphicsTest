// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "JcabD3D.h"

#include <shlobj.h>

#include <array>
#include <filesystem>

// Load the sample assets.
struct ModelVertex
{
    XMFLOAT3 position;
    uint32_t color;

};

struct SceneConstantBuffer
{
    XMFLOAT4X4 projection;
};

struct Model
{
    ComPtr<ID3D12RootSignature>         RootSignature   ;
    ComPtr<ID3D12PipelineState>         PipelineState   ;
    ConstantBuffer<SceneConstantBuffer> ConstantBuffer  ;
    ComPtr<ID3D12Resource>              VertexBuffer    ;
    D3D12_VERTEX_BUFFER_VIEW            VertexBufferView;
};

Model LoadModel(AssetUploader& uploader, RenderDevice& renderDevice)
{
    Model model{};

    // Create the rendering root signature.
    model.RootSignature = renderDevice.CreateRootSignature<
        Root::Cbv<D3D12_SHADER_VISIBILITY_VERTEX, 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC const inputElementDescs[] =
        {
            { .SemanticName = "POSITION", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset =  0, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
            { .SemanticName = "COLOR"   , .SemanticIndex = 0, .Format = DXGI_FORMAT_R8G8B8A8_UNORM , .InputSlot = 0, .AlignedByteOffset = 12, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
        };

        model.PipelineState = renderDevice.Create(Pipeline::State
            {
                .RootSignature         { model.RootSignature },
                .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                .InputLayout           { inputElementDescs, _countof(inputElementDescs) },
                .VS                    = Compile(Shader{ L"model.hlsl", "VSModel", "vs_5_0" }),
                .PS                    = Compile(Shader{ L"model.hlsl", "PSModel", "ps_5_0" }),
                .DepthStencilState     = Pipeline::NoDepthStencil,
                .Multisampling         = Pipeline::NoMultisampling,
                .RTVFormats            { DXGI_FORMAT_R8G8B8A8_UNORM, },
            }
        );
    }

    model.ConstantBuffer = model.ConstantBuffer.Create(renderDevice.device.Get());

    // Create the vertex buffer.
    {
        constexpr auto aspectRatio = 1280.0f / 712.0f;

        // Define the geometry for a triangle.
        static constexpr ModelVertex triangleVertices[] =
        {
            { {  1.0f,  1.0f,  1.0f }, 0xFF'FF'00'00 },
            { {  1.0f,  1.0f, -1.0f }, 0xFF'FF'00'00 },
            { {  1.0f, -1.0f,  1.0f }, 0xFF'FF'00'00 },
            { {  1.0f, -1.0f,  1.0f }, 0xFF'FF'00'00 },
            { {  1.0f,  1.0f, -1.0f }, 0xFF'FF'00'00 },
            { {  1.0f, -1.0f, -1.0f }, 0xFF'FF'00'00 },

            { { -1.0f,  1.0f,  1.0f }, 0xFF'00'FF'00 },
            { { -1.0f, -1.0f,  1.0f }, 0xFF'00'FF'00 },
            { { -1.0f,  1.0f, -1.0f }, 0xFF'00'FF'00 },
            { { -1.0f,  1.0f, -1.0f }, 0xFF'00'FF'00 },
            { { -1.0f, -1.0f,  1.0f }, 0xFF'00'FF'00 },
            { { -1.0f, -1.0f, -1.0f }, 0xFF'00'FF'00 },

            { {  1.0f,  1.0f,  1.0f }, 0xFF'00'00'FF },
            { {  1.0f, -1.0f,  1.0f }, 0xFF'00'00'FF },
            { { -1.0f,  1.0f,  1.0f }, 0xFF'00'00'FF },
            { { -1.0f,  1.0f,  1.0f }, 0xFF'00'00'FF },
            { {  1.0f, -1.0f,  1.0f }, 0xFF'00'00'FF },
            { { -1.0f, -1.0f,  1.0f }, 0xFF'00'00'FF },

            { {  1.0f,  1.0f, -1.0f }, 0xFF'FF'FF'00 },
            { { -1.0f,  1.0f, -1.0f }, 0xFF'FF'FF'00 },
            { {  1.0f, -1.0f, -1.0f }, 0xFF'FF'FF'00 },
            { {  1.0f, -1.0f, -1.0f }, 0xFF'FF'FF'00 },
            { { -1.0f,  1.0f, -1.0f }, 0xFF'FF'FF'00 },
            { { -1.0f, -1.0f, -1.0f }, 0xFF'FF'FF'00 },

            { {  1.0f,  1.0f,  1.0f }, 0xFF'FF'00'FF },
            { { -1.0f,  1.0f,  1.0f }, 0xFF'FF'00'FF },
            { {  1.0f,  1.0f, -1.0f }, 0xFF'FF'00'FF },
            { {  1.0f,  1.0f, -1.0f }, 0xFF'FF'00'FF },
            { { -1.0f,  1.0f,  1.0f }, 0xFF'FF'00'FF },
            { { -1.0f,  1.0f, -1.0f }, 0xFF'FF'00'FF },

            { {  1.0f, -1.0f,  1.0f }, 0xFF'00'FF'FF },
            { {  1.0f, -1.0f, -1.0f }, 0xFF'00'FF'FF },
            { { -1.0f, -1.0f,  1.0f }, 0xFF'00'FF'FF },
            { { -1.0f, -1.0f,  1.0f }, 0xFF'00'FF'FF },
            { {  1.0f, -1.0f, -1.0f }, 0xFF'00'FF'FF },
            { { -1.0f, -1.0f, -1.0f }, 0xFF'00'FF'FF },
        };

        model.VertexBuffer = uploader.CreateStaticBuffer(sizeof(triangleVertices), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
            [&](void* p)
            {
                memcpy(p, triangleVertices, sizeof(triangleVertices));
            }
        );

        // Initialize the vertex buffer view.
        model.VertexBufferView.BufferLocation = model.VertexBuffer->GetGPUVirtualAddress();
        model.VertexBufferView.StrideInBytes  = sizeof(triangleVertices[0]);
        model.VertexBufferView.SizeInBytes    = sizeof(triangleVertices);
    }

    return model;
}


struct Assets
{
    DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV> srvDescriptorHeap;
    DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER    > samplerDescriptorHeap;

    ComPtr<ID3D12RootSignature>       rootSignature;

    ComPtr<ID3D12Resource>            objectTarget;
    UINT                              objectTargetRtvDescriptorIndex;
    UINT                              objectTargetSrvDescriptorIndex;
    ComPtr<ID3D12PipelineState>       objectTargetPipelineState;

    UINT                              objectTargetSampleWrapDescriptorIndex;

    Model                             model;

    ComPtr<ID3D12RootSignature>       renderSignature;
    ComPtr<ID3D12PipelineState>       pipelineState;
    ComPtr<ID3D12Resource>            vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW          vertexBufferView;
};

Assets LoadAssets(RenderDevice& renderDevice)
{
    Assets assets{};

    AssetUploader uploader{ renderDevice };

    assets.srvDescriptorHeap     = assets.srvDescriptorHeap    .Create(renderDevice.device.Get(), 10, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    assets.samplerDescriptorHeap = assets.samplerDescriptorHeap.Create(renderDevice.device.Get(), 10, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

    // Create an empty root signature.
    assets.rootSignature = renderDevice.CreateRootSignature<
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

    // Create the rendering root signature.
    assets.renderSignature = renderDevice.CreateRootSignature<
        Root::Table<D3D12_SHADER_VISIBILITY_PIXEL, Root::SrvRange    <0, 1>>,
        Root::Table<D3D12_SHADER_VISIBILITY_PIXEL, Root::SamplerRange<0, 1>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

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
        D3D12_CLEAR_VALUE const clear
        {
            .Format = DXGI_FORMAT_R32_UINT,
        };
        ThrowIfFailed(renderDevice.device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear,
            IID_PPV_ARGS(&assets.objectTarget)
        ));

        // Create frame resources.
        {
            assets.objectTargetRtvDescriptorIndex = renderDevice.rtvDescriptorHeap.AllocateDescriptors(1);
            auto handle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(assets.objectTargetRtvDescriptorIndex);

            // Create a RTV and a command allocator for each frame.
            renderDevice.device->CreateRenderTargetView(assets.objectTarget.Get(), nullptr, handle);
        }

        {
            assets.objectTargetSrvDescriptorIndex = assets.srvDescriptorHeap.AllocateDescriptors(1);
            auto handle = assets.srvDescriptorHeap.GetCpuDescriptorHandle(assets.objectTargetSrvDescriptorIndex);

            D3D12_SHADER_RESOURCE_VIEW_DESC const srvDesc{
                .Format                  = DXGI_FORMAT_R32_UINT,
                .ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D,
                .Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 0),
                .Texture2D               { .MipLevels = 1 },
            };
            renderDevice.device->CreateShaderResourceView(assets.objectTarget.Get(), &srvDesc, handle);
        }
    }

    {
        assets.objectTargetSampleWrapDescriptorIndex = assets.samplerDescriptorHeap.AllocateDescriptors(1);
        auto handle = assets.samplerDescriptorHeap.GetCpuDescriptorHandle(assets.objectTargetSampleWrapDescriptorIndex);

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

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC const inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT   , 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR"   , 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        assets.pipelineState = renderDevice.Create(Pipeline::State
            {
                .RootSignature         { assets.renderSignature },
                .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                .InputLayout           { inputElementDescs, _countof(inputElementDescs) },
                .VS                    = Compile(Shader{ L"shaders.hlsl", "VSMain", "vs_5_0" }),
                .PS                    = Compile(Shader{ L"shaders.hlsl", "PSMain", "ps_5_0" }),
                .DepthStencilState     = Pipeline::NoDepthStencil,
                .Multisampling         = Pipeline::NoMultisampling,
                .RTVFormats            { DXGI_FORMAT_R8G8B8A8_UNORM, },
            }
        );
    }

    // Create the fullscreen pipeline state, which includes compiling and loading shaders.
    assets.objectTargetPipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { assets.rootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .VS                    = Compile(Shader{ L"shaders.hlsl", "VSFullScreen", "vs_5_0" }),
            .PS                    = Compile(Shader{ L"shaders.hlsl", "PSFullScreen", "ps_5_0" }),
            .DepthStencilState     = Pipeline::NoDepthStencil,
            .Multisampling         = Pipeline::NoMultisampling,
            .RTVFormats            { DXGI_FORMAT_R32_UINT, },
        }
    );

    // Create the vertex buffer.
    {
        constexpr auto aspectRatio = 1280.0f / 712.0f;

        struct Vertex
        {
            XMFLOAT3 position;
            XMFLOAT4 color;
        };

        // Define the geometry for a triangle.
        Vertex triangleVertices[] =
        {
            { {  0.5f ,  0.25f * aspectRatio, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
            { {  0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
            { { -0.25f, -0.25f * aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
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
        ThrowIfFailed(renderDevice.device->CreateCommittedResource(
            &uploadHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &vertexBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&assets.vertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        D3D12_RANGE const nullReadRange{ 0, 0 };        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(assets.vertexBuffer->Map(0, &nullReadRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        assets.vertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        assets.vertexBufferView.BufferLocation = assets.vertexBuffer->GetGPUVirtualAddress();
        assets.vertexBufferView.StrideInBytes  = sizeof(Vertex);
        assets.vertexBufferView.SizeInBytes    = vertexBufferSize;
    }

    assets.model = LoadModel(uploader, renderDevice);

    return assets;
}

void PopulateCommandList(RenderDevice const& renderDevice, Assets const& assets)
{
    auto const& commandAllocator = renderDevice.commandAllocators[renderDevice.frameIndex];

    // Command list allocators can only be reset when the associated 
    // command lists have finished execution on the GPU; apps should use 
    // fences to determine GPU execution progress.
    ThrowIfFailed(commandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command 
    // list, that command list can then be reset at any time and must be before 
    // re-recording.
    ThrowIfFailed(renderDevice.commandList->Reset(commandAllocator.Get(), nullptr));


    {
        renderDevice.commandList->SetPipelineState(assets.objectTargetPipelineState.Get());
        renderDevice.commandList->SetGraphicsRootSignature(assets.rootSignature.Get());
        TransitionResource(renderDevice.commandList, assets.objectTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto const rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(assets.objectTargetRtvDescriptorIndex);
        renderDevice.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        renderDevice.commandList->RSSetViewports    (1, &renderDevice.viewport);
        renderDevice.commandList->RSSetScissorRects (1, &renderDevice.scissorRect);

        float const clearColor[4]{};
        renderDevice.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        renderDevice.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        renderDevice.commandList->DrawInstanced(3, 1, 0, 0);

        TransitionResource(renderDevice.commandList, assets.objectTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // Indicate that the back buffer will be used as a render target.
    TransitionResource(renderDevice.commandList, renderDevice.renderTargets[renderDevice.frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    renderDevice.commandList->SetGraphicsRootSignature(assets.renderSignature.Get());
    renderDevice.commandList->SetPipelineState(assets.pipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ assets.srvDescriptorHeap, assets.samplerDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(2, heaps);
    renderDevice.commandList->SetGraphicsRootDescriptorTable(0, assets.srvDescriptorHeap    .GetGpuDescriptorHandle(assets.objectTargetSrvDescriptorIndex));
    renderDevice.commandList->SetGraphicsRootDescriptorTable(1, assets.samplerDescriptorHeap.GetGpuDescriptorHandle(assets.objectTargetSampleWrapDescriptorIndex));
    auto const rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(renderDevice.frameIndex);
    renderDevice.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    renderDevice.commandList->RSSetViewports    (1, &renderDevice.viewport);
    renderDevice.commandList->RSSetScissorRects (1, &renderDevice.scissorRect);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    renderDevice.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    renderDevice.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderDevice.commandList->IASetVertexBuffers(0, 1, &assets.vertexBufferView);
    renderDevice.commandList->DrawInstanced(3, 1, 0, 0);




    renderDevice.commandList->SetGraphicsRootSignature(assets.model.RootSignature.Get());
    renderDevice.commandList->SetPipelineState(assets.model.PipelineState.Get());
    renderDevice.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderDevice.commandList->IASetVertexBuffers(0, 1, &assets.model.VertexBufferView);
    renderDevice.commandList->SetGraphicsRootConstantBufferView(0, assets.model.ConstantBuffer);
    renderDevice.commandList->DrawInstanced(3 * 2 * 6, 1, 0, 0);



    TransitionResource(renderDevice.commandList, renderDevice.renderTargets[renderDevice.frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    ThrowIfFailed(renderDevice.commandList->Close());
}

// Prepare to render the next frame.
void AnimateAssets(Assets const& assets)
{
    static uint32_t roll = 0;
    static uint32_t yaw  = 0;

    XMMATRIX model = XMMatrixRotationRollPitchYaw(0, (yaw & 0xFF) * 2 * XM_PI / 256.0f, (roll & 0xFFF) * 2 * XM_PI / 4096.0);

    ++roll;
    ++yaw;

    XMFLOAT3 const eye{ 0, 0, -3 };
    XMFLOAT3 const at { 0, 0,  0 };
    XMFLOAT3 const up { 0, 1,  0 };

    XMMATRIX view = XMMatrixLookAtRH(XMLoadFloat3(&eye), XMLoadFloat3(&at), XMLoadFloat3(&up));

    float const fovAngleY = 90.0f;
    float const fovRadiansY = fovAngleY * XM_PI / 180.0f;
    XMMATRIX proj = XMMatrixPerspectiveFovRH(fovRadiansY, 1280.0 / 720.0, 0.01f, 125.0f);

    XMStoreFloat4x4(&assets.model.ConstantBuffer->projection, XMMatrixTranspose(model * view * proj));
}

RenderDevice* pRenderDevice;
Assets const* pAssets      ;

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
        PopulateCommandList(*pRenderDevice, *pAssets);

        // Execute the command list.
        pRenderDevice->ExecuteCommandList(pRenderDevice->commandList);

        pRenderDevice->PresentFrame();

        AnimateAssets(*pAssets);

        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}

static std::wstring GetLatestWinPixGpuCapturerPath_Cpp17()
{
    LPWSTR programFilesPath = nullptr;
    SHGetKnownFolderPath(FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, NULL, &programFilesPath);

    std::filesystem::path pixInstallationPath = programFilesPath;
    pixInstallationPath /= "Microsoft PIX";

    std::wstring newestVersionFound;

    for (auto const& directory_entry : std::filesystem::directory_iterator(pixInstallationPath))
    {
        if (directory_entry.is_directory())
        {
            if (newestVersionFound.empty() || newestVersionFound < directory_entry.path().filename().c_str())
            {
                newestVersionFound = directory_entry.path().filename().c_str();
            }
        }
    }

    if (newestVersionFound.empty())
    {
        // TODO: Error, no PIX installation found
    }

    return pixInstallationPath / newestVersionFound / L"WinPixGpuCapturer.dll";
}

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    LoadLibraryW(GetLatestWinPixGpuCapturerPath_Cpp17().c_str());

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

    auto       renderDevice = RenderDevice::Create(hwnd);
    auto const assets       = LoadAssets(renderDevice);

    pRenderDevice = &renderDevice;
    pAssets       = &assets      ;

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

    renderDevice.WaitForGpuIdle();

    return 0;
}
