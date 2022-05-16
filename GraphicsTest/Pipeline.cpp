// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "Pipeline.h"


RenderPipelineAssets::RenderPipelineAssets(RenderDevice& renderDevice)
{
    VisibilityBuffer = CreateIntermediateRenderTarget(renderDevice, 1280, 720, DXGI_FORMAT_R32_UINT, {}, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_NONE, 8);
    DepthBuffer      = CreateIntermediateDepthBuffer (renderDevice, 1280, 720, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 8);

    MsaaSampleBuffer = CreateGpuBuffer(renderDevice.device, (1280 * 720 / 4) * MaxSamplesPerQuad * sizeof(MsaaSample));
    MsaaSampleBufferUav = renderDevice.AllocateUavDescriptor(MsaaSampleBuffer,
        {
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer{
                .NumElements = (1280 * 720 / 4) * MaxSamplesPerQuad,
                .StructureByteStride = sizeof(MsaaSample),
            },
        }
    );

    MsaaSampleCountBuffer = CreateGpuBuffer(renderDevice.device, (1280 * 720 / 4) * sizeof(uint32_t));
    MsaaSampleCountBufferUav = renderDevice.AllocateUavDescriptor(MsaaSampleCountBuffer,
        {
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer{
                .NumElements = 1280 * 720 / 4,
                .StructureByteStride = sizeof(uint32_t),
            },
        }
    );

    ComputableSampleBuffer = CreateGpuBuffer(renderDevice.device, (1280 * 720 / 4) * MaxSamplesPerQuad * sizeof(ComputableSample));
    ComputableSampleBufferUav = renderDevice.AllocateUavDescriptor(ComputableSampleBuffer,
        {
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer{
                .NumElements = (1280 * 720 / 4) * MaxSamplesPerQuad,
                .StructureByteStride = sizeof(ComputableSample),
            },
        }
    );

    ColorBuffer = CreateIntermediateRenderTarget(renderDevice, 1280, 720, DXGI_FORMAT_R16G16B16A16_UNORM, {}, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ColorBufferUav = renderDevice.AllocateUavDescriptor(ColorBuffer.Resource,
        {
            .Format        = DXGI_FORMAT_R16G16B16A16_UNORM,
            .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
        }
    );

    for (auto& buffer : SceneConstantsBuffer)
    {
        buffer = CpuAccessibleBuffer<SceneConstants>{renderDevice};
    }
}


VisibilityPass::VisibilityPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Constants<D3D12_SHADER_VISIBILITY_ALL   ,                0,    0, 1>, // Constant buffer
        Root::Table    <D3D12_SHADER_VISIBILITY_VERTEX, Root::SrvRange<0, 1, 0>>,   // Instance buffer
        Root::Flags    <D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC const inputElementDescs[] =
        {
            { .SemanticName = "SV_Position", .SemanticIndex = 0, .Format = DXGI_FORMAT_R32G32B32_FLOAT, .InputSlot = 0, .AlignedByteOffset =  0, .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, .InstanceDataStepRate = 0 },
        };

        PipelineState = renderDevice.Create(Pipeline::State
            {
                .RootSignature         { RootSignature },
                .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                .InputLayout           { inputElementDescs, _countof(inputElementDescs) },
                .VS                    = Compile(Shader{ L"model.hlsl", "VisibilityVS", "vs_5_1" }),
                .PS                    = Compile(Shader{ L"model.hlsl", "VisibilityPS", "ps_5_1" }),
                .DepthStencilState     = DefaultDepthStencilDesc,
                .Multisampling         = Pipeline::Multisampling<8>,
                .RTVFormats            { DXGI_FORMAT_R32_UINT, },
                .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
            }
        );
    }
}

void VisibilityPass::Render(RenderDevice const& renderDevice, ModelManager const& modelManager, InstanceManager const& instanceManager, RenderPipelineAssets const& assets) const
{
    renderDevice.commandList->SetGraphicsRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState        (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    TransitionResource(renderDevice.commandList, assets.VisibilityBuffer.Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionResource(renderDevice.commandList, assets.DepthBuffer     .Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    auto const rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(assets.VisibilityBuffer.RtvDescriptorIndex);
    auto const dsvHandle = renderDevice.dsvDescriptorHeap.GetCpuDescriptorHandle(assets.DepthBuffer     .DsvDescriptorIndex);
    renderDevice.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    D3D12_VIEWPORT const viewport   { 0, 0, static_cast<float>(assets.VisibilityBuffer.Width), static_cast<float>(assets.VisibilityBuffer.Height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    D3D12_RECT     const scissorRect{ 0, 0, static_cast<LONG >(assets.VisibilityBuffer.Width), static_cast<LONG >(assets.VisibilityBuffer.Height) };

    renderDevice.commandList->RSSetViewports    (1, &viewport);
    renderDevice.commandList->RSSetScissorRects (1, &scissorRect);

    renderDevice.commandList->ClearRenderTargetView(rtvHandle, assets.VisibilityBuffer.ClearColor.data(), 0, nullptr);
    renderDevice.commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, assets.DepthBuffer.ClearValue, 0, 0, nullptr);
    renderDevice.commandList->SetGraphicsRootDescriptorTable(1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(instanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));

    for (auto instanceId : instanceManager)
    {
        auto& instance = instanceManager.GetInstance(instanceId);
        if (instance.ModelId == Model::Id::Invalid)
        {
            continue;
        }
        auto& model = modelManager.GetModel(instance.ModelId);
        renderDevice.commandList->IASetPrimitiveTopology      (D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        renderDevice.commandList->IASetIndexBuffer            (&model.TriangleIndicesView);
        renderDevice.commandList->IASetVertexBuffers          (0, 1, &model.VertexPositionsBufferView);
        renderDevice.commandList->SetGraphicsRoot32BitConstant(0, instanceManager.GetInstanceIndex(instanceId), 0);
        renderDevice.commandList->DrawIndexedInstanced        (model.TriangleCount * 3, 1, 0, 0, 0);
    }

    TransitionResource(renderDevice.commandList, assets.VisibilityBuffer.Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionResource(renderDevice.commandList, assets.DepthBuffer     .Resource, D3D12_RESOURCE_STATE_DEPTH_WRITE  , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}



MsaaGatherPass::MsaaGatherPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<2, 2,                     0>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_NONE>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = Compile(Shader{ L"model.hlsl", "MsaaGatherPass", "cs_5_1" }),
        }
    );
}

void MsaaGatherPass::Render(RenderDevice const& renderDevice, ModelManager const& modelManager, InstanceManager const& instanceManager, RenderPipelineAssets const& assets) const
{
    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetComputeRootDescriptorTable   (0, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.DepthBuffer     .SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));

    renderDevice.commandList->Dispatch(1280 / 32, 720 / 16, 1);
}



ComputableGatherPass::ComputableGatherPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<1, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<2, 2,                     0>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_NONE>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = Compile(Shader{ L"model.hlsl", "ComputableGatherPass", "cs_5_1" }),
        }
    );
}

void ComputableGatherPass::Render(RenderDevice const& renderDevice, ModelManager const& modelManager, InstanceManager const& instanceManager, RenderPipelineAssets const& assets) const
{
    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetComputeRootDescriptorTable   (0, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.DepthBuffer     .SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ComputableSampleBufferUav));
    renderDevice.commandList->SetComputeRootDescriptorTable   (3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));

    renderDevice.commandList->Dispatch(1280 / 32, 720 / 16, 1);
}



ColorPass::ColorPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,                           0 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<0, 1,                        0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1,                        0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1,                        0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<0, 1,                        0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<1, 1,                        0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<2, 2,                        0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 1>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 2>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 3>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 4>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MATERIAL_DEFINITIONS, 5>>,
        Root::Srv  <D3D12_SHADER_VISIBILITY_ALL,                0,                           5 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SamplerRange<0, 1, 0>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = Compile(Shader{ L"model.hlsl", "ColorPass", "cs_5_1" }),
        }
    );
}

void ColorPass::Render(RenderDevice const& renderDevice, TextureManager const& textureManager, MaterialManager const& materialManager, ModelManager const& modelManager, InstanceManager const& instanceManager, RenderPipelineAssets const& assets) const
{
    TransitionResource(renderDevice.commandList, assets.ColorBuffer.Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    auto& sceneConstants = assets.SceneConstantsBuffer[renderDevice.frameIndex];
    XMStoreFloat4x4(&sceneConstants->InverseProjection, XMMatrixTranspose(InverseProjection));

    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap, renderDevice.samplerDescriptorHeap  };
    renderDevice.commandList->SetDescriptorHeaps(2, heaps);

    renderDevice.commandList->SetComputeRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(instanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.DepthBuffer     .SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (4, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBufferUav));
    renderDevice.commandList->SetComputeRootDescriptorTable   (5, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ComputableSampleBufferUav));
    renderDevice.commandList->SetComputeRootDescriptorTable   (6, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));

    renderDevice.commandList->SetComputeRootDescriptorTable( 7, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetTriangleIndicesDescriptorTable   ()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 8, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetTriangleParametersDescriptorTable()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 9, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetVertexPositionsDescriptorTable   ()));
    renderDevice.commandList->SetComputeRootDescriptorTable(10, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetVertexAttributesDescriptorTable  ()));
    renderDevice.commandList->SetComputeRootDescriptorTable(11, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(textureManager.GetTexturesDescriptorTable          ()));

    renderDevice.commandList->SetComputeRootShaderResourceView(12, materialManager.MaterialList.pResource->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable(13, renderDevice.samplerDescriptorHeap.GetGpuDescriptorHandle(0)); // TODO Samplers

    renderDevice.commandList->Dispatch(1280 / 64, 720 / 16, 1);
}



PostprocessPass::PostprocessPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<0, 1, 0>>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = Compile(Shader{ L"model.hlsl", "Postprocess", "cs_5_1" }),
        }
    );
}

void PostprocessPass::Render(RenderDevice const& renderDevice, RenderPipelineAssets const& assets) const
{
    //TransitionResource(renderDevice.commandList, colorPass.ColorBuffer.Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetComputeRootDescriptorTable(0, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable(1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBufferUav));

    renderDevice.commandList->Dispatch(1280/8, 720/8, 1);
}



ResolvePass::ResolvePass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1, 0>>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .VS                    = Compile(Shader{ L"model.hlsl", "ResolveVS", "vs_5_1" }),
            .PS                    = Compile(Shader{ L"model.hlsl", "ResolvePS", "ps_5_1" }),
            .DepthStencilState     = Pipeline::NoDepthStencil,
            .Multisampling         = Pipeline::NoMultisampling,
            .RTVFormats            { DXGI_FORMAT_R8G8B8A8_UNORM, },
        }
    );
}

void ResolvePass::Render(RenderDevice const& renderDevice, RenderPipelineAssets const& assets) const
{
    TransitionResource(renderDevice.commandList, assets.ColorBuffer.Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    renderDevice.commandList->SetGraphicsRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState        (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetGraphicsRootDescriptorTable(0, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBuffer.SrvDescriptorIndex));

    renderDevice.commandList->DrawInstanced(3, 1, 0, 0);
}



RenderPipeline::RenderPipeline(RenderDevice& renderDevice)
    : assets              { renderDevice }
    , visibilityPass      { renderDevice }
    , msaaGatherPass      { renderDevice }
    , computableGatherPass{ renderDevice }
    , colorPass           { renderDevice }
    , postprocessPass     { renderDevice }
    , resolvePass         { renderDevice }
{
}

void RenderPipeline::Render(RenderDevice& renderDevice, TextureManager const& textureManager, MaterialManager const& materialManager, ModelManager const& modelManager, InstanceManager const& instanceManager)
{
    visibilityPass      .Render(renderDevice,                                  modelManager, instanceManager, assets);
    msaaGatherPass      .Render(renderDevice,                                  modelManager, instanceManager, assets);
    computableGatherPass.Render(renderDevice,                                  modelManager, instanceManager, assets);
    colorPass           .Render(renderDevice, textureManager, materialManager, modelManager, instanceManager, assets);

    postprocessPass.Render(renderDevice, assets);

    renderDevice.StartFrame();

    resolvePass.Render(renderDevice, assets);
}
