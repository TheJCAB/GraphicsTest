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

Model LoadModel(ID3D12GraphicsCommandList* pCommandList, RenderDevice& renderDevice)
{
    Model model{};

    {
        D3D12_ROOT_PARAMETER1 const rootParameter{ // Performance tip: Order root parameters from most frequently accessed to least frequently accessed.
            .ParameterType    = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor{
                .ShaderRegister = 0,
                .RegisterSpace  = 0,
                .Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC,
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX,
        };

        D3D12_ROOT_SIGNATURE_DESC const rootSignatureDesc{
            .NumParameters     = 1,
            .pParameters       = reinterpret_cast<D3D12_ROOT_PARAMETER const*>(&rootParameter),
            .NumStaticSamplers = 0,
            .pStaticSamplers   = nullptr,
            .Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
        };

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(renderDevice.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&model.RootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> errors;
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSModel", "vs_5_0", compileFlags, 0, &vertexShader, &errors));
        ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSModel", "ps_5_0", compileFlags, 0, &pixelShader, &errors));

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC const inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR"   , 0, DXGI_FORMAT_R8G8B8A8_UNORM , 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature        = model.RootSignature.Get(),
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
        ThrowIfFailed(renderDevice.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&model.PipelineState)));
    }

    model.ConstantBuffer = model.ConstantBuffer.Create(renderDevice.device.Get());

    // Create the vertex buffer.
    {
        constexpr auto aspectRatio = 1280.0f / 712.0f;

        // Define the geometry for a triangle.
        ModelVertex triangleVertices[] =
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

        const UINT vertexBufferSize = sizeof(triangleVertices);

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        ComPtr<ID3D12Resource> uploadBuffer;
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
            IID_PPV_ARGS(&uploadBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        D3D12_RANGE const nullReadRange{ 0, 0 };        // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(uploadBuffer->Map(0, &nullReadRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, triangleVertices, vertexBufferSize);
        uploadBuffer->Unmap(0, nullptr);

        D3D12_HEAP_PROPERTIES const heap{
            .Type                 = D3D12_HEAP_TYPE_DEFAULT,
            .CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
            .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
            .CreationNodeMask     = 1,
            .VisibleNodeMask      = 1,
        };
        ThrowIfFailed(renderDevice.device->CreateCommittedResource(
            &heap,
            D3D12_HEAP_FLAG_NONE,
            &vertexBufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&model.VertexBuffer)));

        pCommandList->CopyBufferRegion(model.VertexBuffer.Get(), 0, uploadBuffer.Get(), 0, vertexBufferSize);

        TransitionResource(pCommandList, model.VertexBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

        // Initialize the vertex buffer view.
        model.VertexBufferView.BufferLocation = model.VertexBuffer->GetGPUVirtualAddress();
        model.VertexBufferView.StrideInBytes  = sizeof(triangleVertices[0]);
        model.VertexBufferView.SizeInBytes    = vertexBufferSize;

        // Command lists are created in the recording state, but there is nothing
        // to record yet. The main loop expects it to be closed, so close it now.
        {
            ThrowIfFailed(pCommandList->Close());
            ID3D12CommandList* const pList = pCommandList;
            renderDevice.commandQueue->ExecuteCommandLists(1, &pList);
            ThrowIfFailed(pCommandList->Reset(renderDevice.commandAllocators[renderDevice.frameIndex].Get(), nullptr));

            renderDevice.fence.SignalAndDrainQueue();
        }

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
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Resource>            vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW          vertexBufferView;
};

Assets LoadAssets(RenderDevice& renderDevice)
{
    Assets assets{};

    assets.srvDescriptorHeap     = assets.srvDescriptorHeap    .Create(renderDevice.device.Get(), 10, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    assets.samplerDescriptorHeap = assets.samplerDescriptorHeap.Create(renderDevice.device.Get(), 10, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);

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
        ThrowIfFailed(renderDevice.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&assets.rootSignature)));
    }

    // Create the rendering root signature.
    {
        D3D12_DESCRIPTOR_RANGE1 const srvRange     { .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV    , .NumDescriptors = 1, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
        D3D12_DESCRIPTOR_RANGE1 const samplerRanges{ .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, .NumDescriptors = 1, .BaseShaderRegister = 0, .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };

        // Performance tip: Order root parameters from most frequently accessed to least frequently accessed.
        D3D12_ROOT_PARAMETER1 const rootParameters[]
        {
            {
                .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                .DescriptorTable
                {
                    .NumDescriptorRanges = 1,
                    .pDescriptorRanges   = &srvRange,
                },
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
            },
            {
                .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
                .DescriptorTable
                {
                    .NumDescriptorRanges = 1,
                    .pDescriptorRanges   = &samplerRanges,
                },
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL,
            },
        };

        D3D12_ROOT_SIGNATURE_DESC1 const rootSignatureDesc{
            .NumParameters     = std::size(rootParameters),
            .pParameters       = rootParameters,
            .NumStaticSamplers = 0,
            .pStaticSamplers   = nullptr,
            .Flags             = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
        };

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(reinterpret_cast<D3D12_ROOT_SIGNATURE_DESC const*>(&rootSignatureDesc), D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(renderDevice.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&assets.renderSignature)));
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
        ComPtr<ID3DBlob> errors;
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, &errors));
        ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, &errors));

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC const inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT   , 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR"   , 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature        = assets.renderSignature.Get(),
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
        ThrowIfFailed(renderDevice.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&assets.pipelineState)));
    }

    // Create the fullscreen pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> errors;
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSFullScreen", "vs_5_0", compileFlags, 0, &vertexShader, &errors));
        ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSFullScreen", "ps_5_0", compileFlags, 0, &pixelShader, &errors));

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature        = assets.rootSignature.Get(),
            .VS                    { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() },
            .PS                    { pixelShader ->GetBufferPointer(), pixelShader ->GetBufferSize() },
            .BlendState            = defaultBlendDesc,
            .SampleMask            = UINT_MAX,
            .RasterizerState       = defaultRasterizerDesc,
            .DepthStencilState     { .DepthEnable = FALSE, .StencilEnable = FALSE },
            .InputLayout           { nullptr, 0 },
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .NumRenderTargets      = 1,
            .RTVFormats            { DXGI_FORMAT_R32_UINT, },
            .SampleDesc            { .Count = 1 },
        };
        ThrowIfFailed(renderDevice.device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&assets.objectTargetPipelineState)));
    }

    // Create the command list.
    assets.commandList = renderDevice.CreateDirectCommandList(renderDevice.commandAllocators[renderDevice.frameIndex], assets.pipelineState);

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

    assets.model = LoadModel(assets.commandList.Get(), renderDevice);

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    ThrowIfFailed(assets.commandList->Close());

    {
        ID3D12CommandList* const commandList = assets.commandList.Get();
        renderDevice.commandQueue->ExecuteCommandLists(1, &commandList);
    }

    // We need to wait until assets have been uploaded to the GPU.

    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.
    renderDevice.fence.SignalAndDrainQueue();

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
    ThrowIfFailed(assets.commandList->Reset(commandAllocator.Get(), nullptr));


    {
        assets.commandList->SetPipelineState(assets.objectTargetPipelineState.Get());
        assets.commandList->SetGraphicsRootSignature(assets.rootSignature.Get());
        TransitionResource(assets.commandList, assets.objectTarget, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto const rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(assets.objectTargetRtvDescriptorIndex);
        assets.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        assets.commandList->RSSetViewports    (1, &renderDevice.viewport);
        assets.commandList->RSSetScissorRects (1, &renderDevice.scissorRect);

        float const clearColor[4]{};
        assets.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        assets.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        assets.commandList->DrawInstanced(3, 1, 0, 0);

        TransitionResource(assets.commandList, assets.objectTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // Indicate that the back buffer will be used as a render target.
    TransitionResource(assets.commandList, renderDevice.renderTargets[renderDevice.frameIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

    assets.commandList->SetGraphicsRootSignature(assets.renderSignature.Get());
    assets.commandList->SetPipelineState(assets.pipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ assets.srvDescriptorHeap, assets.samplerDescriptorHeap };
    assets.commandList->SetDescriptorHeaps(2, heaps);
    assets.commandList->SetGraphicsRootDescriptorTable(0, assets.srvDescriptorHeap    .GetGpuDescriptorHandle(assets.objectTargetSrvDescriptorIndex));
    assets.commandList->SetGraphicsRootDescriptorTable(1, assets.samplerDescriptorHeap.GetGpuDescriptorHandle(assets.objectTargetSampleWrapDescriptorIndex));
    auto const rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(renderDevice.frameIndex);
    assets.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    assets.commandList->RSSetViewports    (1, &renderDevice.viewport);
    assets.commandList->RSSetScissorRects (1, &renderDevice.scissorRect);

    // Record commands.
    const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    assets.commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    assets.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    assets.commandList->IASetVertexBuffers(0, 1, &assets.vertexBufferView);
    assets.commandList->DrawInstanced(3, 1, 0, 0);




    assets.commandList->SetGraphicsRootSignature(assets.model.RootSignature.Get());
    assets.commandList->SetPipelineState(assets.model.PipelineState.Get());
    assets.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    assets.commandList->IASetVertexBuffers(0, 1, &assets.model.VertexBufferView);
    assets.commandList->SetGraphicsRootConstantBufferView(0, assets.model.ConstantBuffer);
    assets.commandList->DrawInstanced(3 * 2 * 6, 1, 0, 0);



    TransitionResource(assets.commandList, renderDevice.renderTargets[renderDevice.frameIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    ThrowIfFailed(assets.commandList->Close());
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
        {
            ID3D12CommandList* const commandList = pAssets->commandList.Get();
            pRenderDevice->commandQueue->ExecuteCommandLists(1, &commandList);
        }

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

    renderDevice.fence.SignalAndDrainQueue();

    return 0;
}
