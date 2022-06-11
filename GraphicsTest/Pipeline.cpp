// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "Pipeline.h"

#include <JcabD3D/RootSignatureBuilder.h>

#include "VisDefMsaaPipeline_MsaaGatherPassShader.h"
#include "VisDefMsaaPipeline_ColorPassShader.h"
#include "VisDefMsaaPipeline_PostprocessShader.h"
#include "VisDefMsaaPipeline_ResolveVSShader.h"
#include "VisDefMsaaPipeline_ResolvePSShader.h"


namespace VisDefMsaaPipeline
{

constexpr Root::Cbv      SceneConstantBuffer{ .Reg = 0 };
constexpr Root::SrvTable VisibilityBufferSrv{ .Reg = 1 };
constexpr Root::SrvTable DepthBufferSrv     { .Reg = 2 };
constexpr Root::SrvTable ColorBufferSrv     { .Reg = 2 };
constexpr Root::UavTable MsaaBufferUavs     { .Reg = 2, .Count = 2 };

constexpr Root::SrvTable     InstanceAttributesSrv { .Reg = 0,                                            };
constexpr Root::UavTable     ColorBufferUav        { .Reg = 0,                                            };
constexpr Root::SrvTable     TriangleIndicesSrvs   { .Reg = 1, .Count = MAX_MODEL_DEFINITIONS, .Space = 1 };
constexpr Root::SrvTable     TriangleParametersSrvs{ .Reg = 1, .Count = MAX_MODEL_DEFINITIONS, .Space = 2 };
constexpr Root::SrvTable     VertexPositionsSrvs   { .Reg = 1, .Count = MAX_MODEL_DEFINITIONS, .Space = 3 };
constexpr Root::SrvTable     VertexAttributesSrvs  { .Reg = 1, .Count = MAX_MODEL_DEFINITIONS, .Space = 4 };
constexpr Root::SrvTable     TextureSrvs           { .Reg = 1, .Count = MAX_TEXTURES         , .Space = 5 };
constexpr Root::Srv          MaterialsBufferSrv    { .Reg = 0                                , .Space = 5 };
constexpr Root::SamplerTable DefaultSamplers       { .Reg = 0, .Count = 2                                 };

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
    RootSignature = Root::CreateSignature(renderDevice, 
        SceneConstantBuffer,
        VisibilityBufferSrv,
        DepthBufferSrv     ,
        MsaaBufferUavs     
    );

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = std::span{VisDefMsaaPipeline_MsaaGatherPassShader},
        }
    );
}

void MsaaGatherPass::Render(RenderDevice const& renderDevice, SceneGraphAssets const& sceneGraphAssets, RenderPipelineAssets const& assets)
{
    RootSignature.SetForCompute(renderDevice.commandList);

    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    renderDevice.commandList.SetDescriptorHeap(renderDevice.srvDescriptorHeap);

    RootSignature.SetCbv     (renderDevice.commandList, SceneConstantBuffer, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    RootSignature.SetSrvTable(renderDevice.commandList, VisibilityBufferSrv, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    RootSignature.SetSrvTable(renderDevice.commandList, DepthBufferSrv     , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.DepthBuffer     .SrvDescriptorIndex));
    RootSignature.SetUavTable(renderDevice.commandList, MsaaBufferUavs     , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));

    renderDevice.commandList->Dispatch((renderDevice.Width + 31) / 32, (renderDevice.Height + 15) / 16, 1);

    UavBarrier(renderDevice.commandList, assets.MsaaSampleCountBuffer);
    UavBarrier(renderDevice.commandList, assets.MsaaSampleBuffer);
}



ColorPass::ColorPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = Root::CreateSignature(renderDevice, 
        SceneConstantBuffer   ,
        InstanceAttributesSrv ,
        VisibilityBufferSrv   ,
        DepthBufferSrv        ,
        ColorBufferUav        ,
        MsaaBufferUavs        ,
        TriangleIndicesSrvs   ,
        TriangleParametersSrvs,
        VertexPositionsSrvs   ,
        VertexAttributesSrvs  ,
        TextureSrvs           ,
        MaterialsBufferSrv    ,
        DefaultSamplers       
    );

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           { std::span{VisDefMsaaPipeline_ColorPassShader} },
        }
    );
}

void ColorPass::Render(RenderDevice const& renderDevice, SceneGraphAssets const& sceneGraphAssets, RenderPipelineAssets const& assets)
{
    TransitionResource(renderDevice.commandList, assets.ColorBuffer.Resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    RootSignature.SetForCompute(renderDevice.commandList);
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    renderDevice.commandList.SetDescriptorHeaps(renderDevice.srvDescriptorHeap, renderDevice.samplerDescriptorHeap);

    RootSignature.SetCbv         (renderDevice.commandList, SceneConstantBuffer   , assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    RootSignature.SetSrvTable    (renderDevice.commandList, InstanceAttributesSrv , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.InstanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));
    RootSignature.SetSrvTable    (renderDevice.commandList, VisibilityBufferSrv   , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    RootSignature.SetSrvTable    (renderDevice.commandList, DepthBufferSrv        , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.DepthBuffer     .SrvDescriptorIndex));
    RootSignature.SetUavTable    (renderDevice.commandList, ColorBufferUav        , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBufferUav));
    RootSignature.SetUavTable    (renderDevice.commandList, MsaaBufferUavs        , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));
    RootSignature.SetSrvTable    (renderDevice.commandList, TriangleIndicesSrvs   , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager  .GetTriangleIndicesDescriptorTable   ()));
    RootSignature.SetSrvTable    (renderDevice.commandList, TriangleParametersSrvs, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager  .GetTriangleParametersDescriptorTable()));
    RootSignature.SetSrvTable    (renderDevice.commandList, VertexPositionsSrvs   , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager  .GetVertexPositionsDescriptorTable   ()));
    RootSignature.SetSrvTable    (renderDevice.commandList, VertexAttributesSrvs  , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.ModelManager  .GetVertexAttributesDescriptorTable  ()));
    RootSignature.SetSrvTable    (renderDevice.commandList, TextureSrvs           , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(sceneGraphAssets.TextureManager.GetTexturesDescriptorTable          ()));
    RootSignature.SetSrv         (renderDevice.commandList, MaterialsBufferSrv    , sceneGraphAssets.MaterialManager.MaterialList.pResource->GetGPUVirtualAddress());
    RootSignature.SetSamplerTable(renderDevice.commandList, DefaultSamplers       , renderDevice.samplerDescriptorHeap.GetGpuDescriptorHandle(0)); // TODO Samplers

    renderDevice.commandList->Dispatch((renderDevice.Width + 63) / 64, (renderDevice.Height + 15) / 16, 1);

    UavBarrier(renderDevice.commandList, assets.ColorBuffer.Resource);
}



PostprocessPass::PostprocessPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = Root::CreateSignature(renderDevice, 
        SceneConstantBuffer,
        VisibilityBufferSrv,
        ColorBufferUav     
    );

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = std::span{VisDefMsaaPipeline_PostprocessShader},
        }
    );
}

void PostprocessPass::Render(RenderDevice const& renderDevice, RenderPipelineAssets const& assets)
{
    //TransitionResource(renderDevice.commandList, colorPass.ColorBuffer.Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    RootSignature.SetForCompute(renderDevice.commandList);
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    renderDevice.commandList.SetDescriptorHeap(renderDevice.srvDescriptorHeap);

    RootSignature.SetCbv     (renderDevice.commandList, SceneConstantBuffer, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    RootSignature.SetSrvTable(renderDevice.commandList, VisibilityBufferSrv, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.VisibilityBuffer.SrvDescriptorIndex));
    RootSignature.SetUavTable(renderDevice.commandList, ColorBufferUav     , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBufferUav));

    renderDevice.commandList->Dispatch((renderDevice.Width + 7) / 8, (renderDevice.Height + 7) / 8, 1);

    UavBarrier(renderDevice.commandList, assets.ColorBuffer.Resource);
}


ResolvePass::ResolvePass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = Root::CreateSignature(renderDevice, 
        SceneConstantBuffer,
        ColorBufferSrv     ,
        MsaaBufferUavs     
    );

    //    <
    //    Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,    0 >,
    //    Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1, 0>>,
    //    Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<2, 2, 0>>
    //>();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .VS                    = std::span{VisDefMsaaPipeline_ResolveVSShader},
            .PS                    = std::span{VisDefMsaaPipeline_ResolvePSShader},
            .DepthStencilState     = Pipeline::NoDepthStencil,
            .Multisampling         = Pipeline::NoMultisampling,
            .RTVFormats            { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, },
        }
    );
}

void ResolvePass::Render(RenderDevice const& renderDevice, RenderPipelineAssets const& assets)
{
    TransitionResource(renderDevice.commandList, assets.ColorBuffer.Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    RootSignature.SetForGraphics(renderDevice.commandList);

    renderDevice.commandList->SetPipelineState        (PipelineState.Get());

    renderDevice.commandList.SetDescriptorHeap(renderDevice.srvDescriptorHeap);

    renderDevice.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    RootSignature.SetCbv     (renderDevice.commandList, SceneConstantBuffer, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    RootSignature.SetSrvTable(renderDevice.commandList, ColorBufferSrv     , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBuffer.SrvDescriptorIndex));
    RootSignature.SetUavTable(renderDevice.commandList, MsaaBufferUavs     , renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.MsaaSampleBufferUav));

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
// namespace VisDefMsaaPipeline
