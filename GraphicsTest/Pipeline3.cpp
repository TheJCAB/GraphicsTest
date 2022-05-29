// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "Pipeline3.h"

#include "VisDefMsaa3Pipeline_MsaaGatherPassShader.h"
#include "VisDefMsaa3Pipeline_ColorPassShader.h"
#include "VisDefMsaa3Pipeline_PostprocessShader.h"
#include "VisDefMsaa3Pipeline_ResolveVSShader.h"
#include "VisDefMsaa3Pipeline_ResolvePSShader.h"


namespace VisDefMsaa3Pipeline
{

RenderPipelineAssets::RenderPipelineAssets(RenderDevice& renderDevice)
{
    VisibilityBuffer = CreateIntermediateRenderTarget(renderDevice, renderDevice.Width, renderDevice.Height, DXGI_FORMAT_R32_UINT, {}, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_NONE, 8);
    DepthBuffer      = CreateIntermediateDepthBuffer (renderDevice, renderDevice.Width, renderDevice.Height, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 8);

    MsaaSampleBuffer = CreateGpuBuffer(renderDevice.device, (renderDevice.Width * renderDevice.Height / 4) * MaxSamplesPerQuad * sizeof(MsaaSample));
    MsaaSampleBufferUav = renderDevice.AllocateUavDescriptor(MsaaSampleBuffer,
        {
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer{
                .NumElements = (renderDevice.Width * renderDevice.Height / 4) * MaxSamplesPerQuad,
                .StructureByteStride = sizeof(MsaaSample),
            },
        }
    );

    MsaaSampleCountBuffer = CreateGpuBuffer(renderDevice.device, (renderDevice.Width * renderDevice.Height / 4) * sizeof(uint32_t));
    MsaaSampleCountBufferUav = renderDevice.AllocateUavDescriptor(MsaaSampleCountBuffer,
        {
            .Format        = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer{
                .NumElements = renderDevice.Width * renderDevice.Height / 4,
                .StructureByteStride = sizeof(uint32_t),
            },
        }
    );

    ColorBuffer = CreateIntermediateRenderTarget(renderDevice, renderDevice.Width, renderDevice.Height, DXGI_FORMAT_R16G16B16A16_UNORM, {}, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
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

MsaaGatherPass::MsaaGatherPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,                        0 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1,                     0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<2, 2,                     0>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_NONE>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = std::span{VisDefMsaa3Pipeline_MsaaGatherPassShader},
        }
    );
}

void MsaaGatherPass::Render(RenderDevice const& renderDevice, SceneGraphAssets const& sceneGraphAssets, RenderPipelineAssets const& assets) const
{
    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetComputeRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.DepthBuffer     .SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));

    renderDevice.commandList->Dispatch((renderDevice.Width + 7) / 8, (renderDevice.Height + 7) / 8, 1);

    UavBarrier(renderDevice.commandList, assets.MsaaSampleCountBuffer);
    UavBarrier(renderDevice.commandList, assets.MsaaSampleBuffer);
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
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<2, 2,                        0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 1>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 2>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 3>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 4>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MATERIAL_DEFINITIONS, 5>>,
        Root::Srv  <D3D12_SHADER_VISIBILITY_ALL,                0,                           5 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SamplerRange<0, 2, 0>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           { std::span{VisDefMsaa3Pipeline_ColorPassShader} },
        }
    );
}

void ColorPass::Render(RenderDevice const& renderDevice, SceneGraphAssets const& sceneGraphAssets, RenderPipelineAssets const& assets) const
{
    TransitionResource(renderDevice.commandList, assets.ColorBuffer.Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap, renderDevice.samplerDescriptorHeap  };
    renderDevice.commandList->SetDescriptorHeaps(2, heaps);

    renderDevice.commandList->SetComputeRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.InstanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.DepthBuffer     .SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (4, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBufferUav));
    renderDevice.commandList->SetComputeRootDescriptorTable   (5, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));

    renderDevice.commandList->SetComputeRootDescriptorTable( 6, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager  .GetTriangleIndicesDescriptorTable   ()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 7, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager  .GetTriangleParametersDescriptorTable()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 8, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager  .GetVertexPositionsDescriptorTable   ()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 9, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager  .GetVertexAttributesDescriptorTable  ()));
    renderDevice.commandList->SetComputeRootDescriptorTable(10, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.TextureManager.GetTexturesDescriptorTable          ()));

    renderDevice.commandList->SetComputeRootShaderResourceView(11, sceneGraphAssets.MaterialManager.MaterialList.pResource->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable(12, renderDevice.samplerDescriptorHeap.GetGpuDescriptorHandle(0)); // TODO Samplers

    renderDevice.commandList->Dispatch((renderDevice.Width + 7) / 8, (renderDevice.Height + 7) / 8, 1);

    UavBarrier(renderDevice.commandList, assets.ColorBuffer.Resource);
}



PostprocessPass::PostprocessPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,    0 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<0, 1, 0>>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = std::span{VisDefMsaa3Pipeline_PostprocessShader},
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

    renderDevice.commandList->SetComputeRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBufferUav));

    renderDevice.commandList->Dispatch((renderDevice.Width + 7) / 8, (renderDevice.Height + 7) / 8, 1);

    UavBarrier(renderDevice.commandList, assets.ColorBuffer.Resource);
}



ResolvePass::ResolvePass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,    0 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<2, 2, 0>>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .VS                    = std::span{VisDefMsaa3Pipeline_ResolveVSShader},
            .PS                    = std::span{VisDefMsaa3Pipeline_ResolvePSShader},
            .DepthStencilState     = Pipeline::NoDepthStencil,
            .Multisampling         = Pipeline::NoMultisampling,
            .RTVFormats            { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, },
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

    renderDevice.commandList->IASetPrimitiveTopology      (D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderDevice.commandList->SetGraphicsRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetGraphicsRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetGraphicsRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));

    renderDevice.commandList->DrawInstanced(3, 1, 0, 0);
}



RenderPipeline::RenderPipeline(RenderDevice& renderDevice)
    : assets              { renderDevice }
    , visibilityPass      { renderDevice, 8 }
    , msaaGatherPass      { renderDevice }
    , colorPass           { renderDevice }
    , postprocessPass     { renderDevice }
    , resolvePass         { renderDevice }
{
}

void RenderPipeline::Render(RenderDevice& renderDevice, SceneGraphAssets const& sceneGraphAssets)
{
    auto& sceneConstants = assets.SceneConstantsBuffer[renderDevice.frameIndex];

    float const width  = static_cast<float>(renderDevice.Width );
    float const height = static_cast<float>(renderDevice.Height);

    sceneConstants->ViewSpace = ViewSpace::CalculateConstants(width, height, View);

    // ViewPos.xy / z projected into [0..1, 0..1] normalized space
    sceneConstants->NormalizedProjectionScale = XMFLOAT2( height / (width * 2.0f), -1.0f / 2.0f);
    sceneConstants->NormalizedProjectionBias  = XMFLOAT2(   1.0f /          2.0f ,  1.0f / 2.0f);

    sceneConstants->InverseNormalizedProjectionScale = XMFLOAT2((width * 2.0f) / height, -2.0f);
    sceneConstants->InverseNormalizedProjectionBias  = XMFLOAT2(-width         / height,  1.0f);

    TransitionResource(renderDevice.commandList, assets.VisibilityBuffer.Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionResource(renderDevice.commandList, assets.DepthBuffer     .Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    visibilityPass.Render(renderDevice, sceneGraphAssets, XMFLOAT2{0, 0}, assets.VisibilityBuffer, assets.DepthBuffer);

    TransitionResource(renderDevice.commandList, assets.VisibilityBuffer.Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionResource(renderDevice.commandList, assets.DepthBuffer     .Resource, D3D12_RESOURCE_STATE_DEPTH_WRITE  , D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    msaaGatherPass      .Render(renderDevice, sceneGraphAssets, assets);
    colorPass           .Render(renderDevice, sceneGraphAssets, assets);

    postprocessPass.Render(renderDevice, assets);

    renderDevice.StartFrame();

    resolvePass.Render(renderDevice, assets);
}

}
// namespace VisDefMsaa3Pipeline
