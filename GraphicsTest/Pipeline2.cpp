// GraphicsTest.cpp : Defines the entry point for the application.
//

#include "Pipeline2.h"

#include "VisDefPipeline_VisibilitySolidVSShader.h"
#include "VisDefPipeline_VisibilitySolidPSShader.h"
#include "VisDefPipeline_VisibilityAlphaTestVSShader.h"
#include "VisDefPipeline_VisibilityAlphaTestPSShader.h"
#include "VisDefPipeline_ColorPassShader.h"
#include "VisDefPipeline_PostprocessShader.h"
#include "VisDefPipeline_ResolveVSShader.h"
#include "VisDefPipeline_ResolvePSShader.h"


namespace VisDefPipeline
{

RenderPipelineAssets::RenderPipelineAssets(RenderDevice& renderDevice)
{
    for (uint32_t i = 0; i < 2; ++i)
    {
        VisibilityBuffer[i] = CreateIntermediateRenderTarget(renderDevice, renderDevice.Width, renderDevice.Height, DXGI_FORMAT_R32_UINT, {}, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_NONE, 1);
        DepthBuffer     [i] = CreateIntermediateDepthBuffer (renderDevice, renderDevice.Width, renderDevice.Height, 0, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, 1);

        TaaColorBuffer   [i] = CreateIntermediateRenderTarget(renderDevice, renderDevice.Width, renderDevice.Height, DXGI_FORMAT_R16G16B16A16_UNORM, {}, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        TaaColorBufferUav[i] = renderDevice.AllocateUavDescriptor(TaaColorBuffer[i].Resource,
            {
                .Format        = DXGI_FORMAT_R16G16B16A16_UNORM,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
            }
        );
    }

    ColorBuffer = CreateIntermediateRenderTarget(renderDevice, renderDevice.Width, renderDevice.Height, DXGI_FORMAT_R16G16B16A16_UNORM, {}, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
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
    RootSignature = renderDevice.Create(
        Root::Signature<
            Root::Cbv<      D3D12_SHADER_VISIBILITY_ALL,                   0,    0 >,
            Root::Constants<D3D12_SHADER_VISIBILITY_ALL   ,                0,    1, 1 >, // Instance constant
            Root::Table    <D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<0, 1, 0>>,    // Instance buffer
            Root::Table    <D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 2>>, // Triangle paramters
            Root::Table    <D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_TEXTURES            , 5>>, // Textures
            Root::Srv      <D3D12_SHADER_VISIBILITY_ALL,                0,                           5 >, // Materials
            Root::Table    <D3D12_SHADER_VISIBILITY_ALL, Root::SamplerRange<0, 1, 0>>,
            Root::Flags    <D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
        >{}
    );

    // Create the pipeline state, which includes compiling and loading shaders.
    SolidPipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .InputLayout           = Vertex::InputLayout<Vertex::Input<Semantic::SV_Position, DXGI_FORMAT_R32G32B32_FLOAT, 0>>{},
            .VS                    = std::span{VisDefPipeline_VisibilitySolidVSShader},
            .PS                    = std::span{VisDefPipeline_VisibilitySolidPSShader},
            .DepthStencilState     = DefaultDepthStencilDesc,
            .Multisampling         = Pipeline::NoMultisampling,
            .RTVFormats            { DXGI_FORMAT_R32_UINT, },
            .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
        }
    );

    // Create the pipeline state, which includes compiling and loading shaders.
    AlphaTestPipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .InputLayout           = Vertex::InputLayout<
                                        Vertex::Input<Semantic::SV_Position, DXGI_FORMAT_R32G32B32_FLOAT, 0                             , 0>,
                                        Vertex::Input<Semantic::UV         , DXGI_FORMAT_R32G32_FLOAT   , offsetof(VertexAttributes, UV), 1>
                                    >{},
            .VS                    = std::span{VisDefPipeline_VisibilityAlphaTestVSShader},
            .PS                    = std::span{VisDefPipeline_VisibilityAlphaTestPSShader},
            .DepthStencilState     = DefaultDepthStencilDesc,
            .Multisampling         = Pipeline::NoMultisampling,
            .RTVFormats            { DXGI_FORMAT_R32_UINT, },
            .DSVFormat             = DXGI_FORMAT_D32_FLOAT,
        }
    );
}

void VisibilityPass::Render(RenderDevice const& renderDevice, FrameParameters const& frameParameters, TextureManager const& textureManager, MaterialManager const&materialManager, ModelManager const& modelManager, InstanceManager const& instanceManager, RenderPipelineAssets const& assets) const
{
    renderDevice.commandList->SetGraphicsRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState        (SolidPipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap, renderDevice.samplerDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(2, heaps);

    auto& visibilityBuffer = assets.VisibilityBuffer[frameParameters.flipIndex];
    auto& depthBuffer      = assets.DepthBuffer     [frameParameters.flipIndex];

    TransitionResource(renderDevice.commandList, visibilityBuffer.Resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionResource(renderDevice.commandList, depthBuffer     .Resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    auto const rtvHandle = renderDevice.rtvDescriptorHeap.GetCpuDescriptorHandle(visibilityBuffer.RtvDescriptorIndex);
    auto const dsvHandle = renderDevice.dsvDescriptorHeap.GetCpuDescriptorHandle(depthBuffer     .DsvDescriptorIndex);
    renderDevice.commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    D3D12_VIEWPORT const viewport   { 0, 0, static_cast<float>(visibilityBuffer.Width), static_cast<float>(visibilityBuffer.Height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    D3D12_RECT     const scissorRect{ 0, 0, static_cast<LONG >(depthBuffer     .Width), static_cast<LONG >(depthBuffer     .Height) };

    renderDevice.commandList->RSSetViewports    (1, &viewport);
    renderDevice.commandList->RSSetScissorRects (1, &scissorRect);

    renderDevice.commandList->ClearRenderTargetView(rtvHandle, visibilityBuffer.ClearColor.data(), 0, nullptr);
    renderDevice.commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, depthBuffer.ClearValue, 0, 0, nullptr);
    renderDevice.commandList->SetGraphicsRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetGraphicsRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(instanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));

    renderDevice.commandList->SetGraphicsRootDescriptorTable( 3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetTriangleParametersDescriptorTable()));
    renderDevice.commandList->SetGraphicsRootDescriptorTable( 4, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(textureManager.GetTexturesDescriptorTable          ()));

    renderDevice.commandList->SetGraphicsRootShaderResourceView(5, materialManager.MaterialList.pResource->GetGPUVirtualAddress());
    renderDevice.commandList->SetGraphicsRootDescriptorTable(6, renderDevice.samplerDescriptorHeap.GetGpuDescriptorHandle(0)); // TODO Samplers

    for (auto instanceId : instanceManager)
    {
        auto& instance = instanceManager.GetInstance(instanceId);
        if (instance.ModelId == Model::Id::Invalid)
        {
            continue;
        }
        auto& model = modelManager.GetModel(instance.ModelId);
        if (!model.UsesAlphaTest)
        {
            renderDevice.commandList->IASetPrimitiveTopology      (D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            renderDevice.commandList->IASetIndexBuffer            (&model.TriangleIndicesView);
            renderDevice.commandList->IASetVertexBuffers          (0, 1, &model.VertexPositionsBufferView);
            renderDevice.commandList->SetGraphicsRoot32BitConstant(1, instanceManager.GetInstanceIndex(instanceId), 0);
            renderDevice.commandList->DrawIndexedInstanced        (model.TriangleCount * 3, 1, 0, 0, 0);
        }
    }

    renderDevice.commandList->SetPipelineState(AlphaTestPipelineState.Get());

    for (auto instanceId : instanceManager)
    {
        auto& instance = instanceManager.GetInstance(instanceId);
        if (instance.ModelId == Model::Id::Invalid)
        {
            continue;
        }
        auto& model = modelManager.GetModel(instance.ModelId);
        if (model.UsesAlphaTest)
        {
            renderDevice.commandList->IASetPrimitiveTopology      (D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            renderDevice.commandList->IASetIndexBuffer            (&model.TriangleIndicesView);
            renderDevice.commandList->IASetVertexBuffers          (0, 1, &model.VertexPositionsBufferView);
            renderDevice.commandList->IASetVertexBuffers          (1, 1, &model.VertexAttributesBufferView);
            renderDevice.commandList->SetGraphicsRoot32BitConstant(1, instanceManager.GetInstanceIndex(instanceId), 0);
            renderDevice.commandList->DrawIndexedInstanced        (model.TriangleCount * 3, 1, 0, 0, 0);
        }
    }

    TransitionResource(renderDevice.commandList, visibilityBuffer.Resource, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    TransitionResource(renderDevice.commandList, depthBuffer     .Resource, D3D12_RESOURCE_STATE_DEPTH_WRITE  , D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
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
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 1>>, // Triangle indices
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 2>>, // Triangle paramters
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 3>>, // Vertex positions
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_MODEL_DEFINITIONS   , 4>>, // Vertex attributes
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, MAX_TEXTURES            , 5>>, // Textures
        Root::Srv  <D3D12_SHADER_VISIBILITY_ALL,                0,                           5 >, // Materials
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SamplerRange<0, 1, 0>>,
        Root::Flags<D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           { std::span{VisDefPipeline_ColorPassShader} },
        }
    );
}

void ColorPass::Render(RenderDevice const& renderDevice, FrameParameters const& frameParameters, TextureManager const& textureManager, MaterialManager const& materialManager, ModelManager const& modelManager, InstanceManager const& instanceManager, RenderPipelineAssets const& assets) const
{
    auto& visibilityBuffer = assets.VisibilityBuffer[frameParameters.flipIndex];
    auto& depthBuffer      = assets.DepthBuffer     [frameParameters.flipIndex];
    auto& colorBuffer      = assets.ColorBuffer;

    TransitionResource(renderDevice.commandList, colorBuffer.Resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap, renderDevice.samplerDescriptorHeap  };
    renderDevice.commandList->SetDescriptorHeaps(2, heaps);

    renderDevice.commandList->SetComputeRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(instanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(visibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(depthBuffer     .SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (4, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.ColorBufferUav));

    renderDevice.commandList->SetComputeRootDescriptorTable( 5, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetTriangleIndicesDescriptorTable   ()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 6, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetTriangleParametersDescriptorTable()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 7, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetVertexPositionsDescriptorTable   ()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 8, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(modelManager  .GetVertexAttributesDescriptorTable  ()));
    renderDevice.commandList->SetComputeRootDescriptorTable( 9, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(textureManager.GetTexturesDescriptorTable          ()));

    renderDevice.commandList->SetComputeRootShaderResourceView(10, materialManager.MaterialList.pResource->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable(11, renderDevice.samplerDescriptorHeap.GetGpuDescriptorHandle(0)); // TODO Samplers

    renderDevice.commandList->Dispatch((renderDevice.Width + 63) / 64, (renderDevice.Height + 15) / 16, 1);

    TransitionResource(renderDevice.commandList, colorBuffer.Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}



PostprocessPass::PostprocessPass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,    0 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<0, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<1, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<3, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<4, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<5, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::UavRange<0, 1, 0>>,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SamplerRange<0, 1, 0>>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Compute::State
        {
            .RootSignature{ RootSignature },
            .CS           = std::span{VisDefPipeline_PostprocessShader},
        }
    );
}

void PostprocessPass::Render(RenderDevice const& renderDevice, FrameParameters const& frameParameters, InstanceManager const& instanceManager, RenderPipelineAssets const& assets) const
{
    auto& visibilityBuffer       = assets.VisibilityBuffer[frameParameters.flipIndex];
    auto& depthBuffer            = assets.DepthBuffer     [frameParameters.flipIndex];
    auto& colorBuffer            = assets.ColorBuffer;
    auto& taaColorBuffer         = assets.TaaColorBuffer  [frameParameters.flipIndex];
    auto& previousTaaColorBuffer = assets.TaaColorBuffer  [frameParameters.previousFlipIndex];
    auto& previousDepthBuffer    = assets.DepthBuffer     [frameParameters.previousFlipIndex];

    TransitionResource(renderDevice.commandList, taaColorBuffer.Resource, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    renderDevice.commandList->SetComputeRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState       (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap, renderDevice.samplerDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(2, heaps);

    renderDevice.commandList->SetComputeRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetComputeRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(instanceManager.GetCurrentFrameInstanceAttributes(renderDevice).SrvIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (2, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(visibilityBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (3, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(depthBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (4, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(colorBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (5, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(previousTaaColorBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (6, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(previousDepthBuffer.SrvDescriptorIndex));
    renderDevice.commandList->SetComputeRootDescriptorTable   (7, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(assets.TaaColorBufferUav[frameParameters.flipIndex]));
    renderDevice.commandList->SetComputeRootDescriptorTable   (8, renderDevice.samplerDescriptorHeap.GetGpuDescriptorHandle(0)); // TODO Samplers

    renderDevice.commandList->Dispatch((renderDevice.Width + 7) / 8, (renderDevice.Height + 7) / 8, 1);

    TransitionResource(renderDevice.commandList, taaColorBuffer.Resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
}



ResolvePass::ResolvePass(RenderDevice& renderDevice)
{
    // Create the rendering root signature.
    RootSignature = renderDevice.CreateRootSignature<
        Root::Cbv<  D3D12_SHADER_VISIBILITY_ALL,                0,    0 >,
        Root::Table<D3D12_SHADER_VISIBILITY_ALL, Root::SrvRange<2, 1, 0>>
    >();

    // Create the pipeline state, which includes compiling and loading shaders.
    PipelineState = renderDevice.Create(Pipeline::State
        {
            .RootSignature         { RootSignature },
            .TopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .VS                    = std::span{VisDefPipeline_ResolveVSShader},
            .PS                    = std::span{VisDefPipeline_ResolvePSShader},
            .DepthStencilState     = Pipeline::NoDepthStencil,
            .Multisampling         = Pipeline::NoMultisampling,
            .RTVFormats            { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, },
        }
    );
}

void ResolvePass::Render(RenderDevice const& renderDevice, FrameParameters const& frameParameters, RenderPipelineAssets const& assets) const
{
    auto& taaColorBuffer = assets.TaaColorBuffer[frameParameters.flipIndex];

    renderDevice.commandList->SetGraphicsRootSignature(RootSignature.Get());
    renderDevice.commandList->SetPipelineState        (PipelineState.Get());

    ID3D12DescriptorHeap* const heaps[]{ renderDevice.srvDescriptorHeap };
    renderDevice.commandList->SetDescriptorHeaps(1, heaps);

    renderDevice.commandList->SetGraphicsRootConstantBufferView(0, assets.SceneConstantsBuffer[renderDevice.frameIndex].Buffer->GetGPUVirtualAddress());
    renderDevice.commandList->SetGraphicsRootDescriptorTable   (1, renderDevice.srvDescriptorHeap.GetGpuDescriptorHandle(taaColorBuffer.SrvDescriptorIndex));

    renderDevice.commandList->DrawInstanced(3, 1, 0, 0);
}



RenderPipeline::RenderPipeline(RenderDevice& renderDevice)
    : assets              { renderDevice }
    , visibilityPass      { renderDevice }
    , colorPass           { renderDevice }
    , postprocessPass     { renderDevice }
    , resolvePass         { renderDevice }
{
}

constexpr XMFLOAT2 PixelOffsets[8]
{
    { 1 /  2.0f, 1 / 3.0f },
    { 1 /  4.0f, 2 / 3.0f },
    { 3 /  4.0f, 1 / 9.0f },
    { 1 /  8.0f, 4 / 9.0f },
    { 5 /  8.0f, 7 / 9.0f },
    { 3 /  8.0f, 2 / 9.0f },
    { 7 /  8.0f, 5 / 9.0f },
    { 7 / 16.0f, 8 / 9.0f },
};

void RenderPipeline::Render(RenderDevice& renderDevice, TextureManager const& textureManager, MaterialManager const& materialManager, ModelManager const& modelManager, InstanceManager const& instanceManager)
{
    auto& sceneConstants = assets.SceneConstantsBuffer[renderDevice.frameIndex];

    float const width  = static_cast<float>(renderDevice.Width );
    float const height = static_cast<float>(renderDevice.Height);

    sceneConstants->Size = XMUINT2(renderDevice.Width, renderDevice.Height);

    static size_t offsetCount = 0;

    // ViewPos.xy / z projected into [0..renderDevice.Width, 0..renderDevice.Height] screen space
    sceneConstants->LastFrameProjectionScale = XMFLOAT2( -height / 2.0f,  height / 2.0f);
    sceneConstants->LastFrameProjectionBias  = XMFLOAT2(  width  / 2.0f + PixelOffsets[offsetCount].x,  height / 2.0f + PixelOffsets[offsetCount].y);

    sceneConstants->LastFrameInverseProjectionScale = XMFLOAT2( -2.0f / height,  2.0f / height);
    sceneConstants->LastFrameInverseProjectionBias  = XMFLOAT2( width / height + PixelOffsets[offsetCount].x * 2.0f / height, -1.0f - 2.0f * PixelOffsets[offsetCount].y / height);

    offsetCount = (offsetCount + 1) % std::size(PixelOffsets);

    sceneConstants->PixelOffset           = XMFLOAT2{ PixelOffsets[offsetCount].x - 0.5f, PixelOffsets[offsetCount].y - 0.5f };
    sceneConstants->NormalizedPixelOffset = XMFLOAT2{ (PixelOffsets[offsetCount].x - 0.5f) / width, (PixelOffsets[offsetCount].y - 0.5f) / height };

    // ViewPos.xy / z projected into [0..renderDevice.Width, 0..renderDevice.Height] screen space
    sceneConstants->ProjectionScale = XMFLOAT2( -height / 2.0f,  height / 2.0f);
    sceneConstants->ProjectionBias  = XMFLOAT2(  width  / 2.0f + PixelOffsets[offsetCount].x,  height / 2.0f + PixelOffsets[offsetCount].y);

    sceneConstants->InverseProjectionScale = XMFLOAT2( -2.0f / height,  2.0f / height);
    sceneConstants->InverseProjectionBias  = XMFLOAT2( width / height + PixelOffsets[offsetCount].x * 2.0f / height, -1.0f - 2.0f * PixelOffsets[offsetCount].y / height);

    // ViewPos.xy / z projected into [0..renderDevice.Width, 0..renderDevice.Height] screen space
    sceneConstants->CenterProjectionScale = XMFLOAT2( -height / 2.0f,  height / 2.0f);
    sceneConstants->CenterProjectionBias  = XMFLOAT2(  width  / 2.0f,  height / 2.0f);

    sceneConstants->InverseCenterProjectionScale = XMFLOAT2( -2.0f / height,  2.0f / height);
    sceneConstants->InverseCenterProjectionBias  = XMFLOAT2( width / height, -1.0f);

    sceneConstants->InverseAspectRatio = XMFLOAT2(height / width, -1.0f);
    sceneConstants->AspectRatio        = XMFLOAT2(width / height, -1.0f);

    sceneConstants->ForgetCurrentFrame = g_forgetCurrentFrame ? 1 : 0;

    currentFrameParams.previousFlipIndex = currentFrameParams.flipIndex;
    currentFrameParams.flipIndex = (currentFrameParams.flipIndex + 1) % 2;

    visibilityPass      .Render(renderDevice, currentFrameParams, textureManager, materialManager, modelManager, instanceManager, assets);
    colorPass           .Render(renderDevice, currentFrameParams, textureManager, materialManager, modelManager, instanceManager, assets);

    postprocessPass.Render(renderDevice, currentFrameParams, instanceManager, assets);

    renderDevice.StartFrame();

    resolvePass.Render(renderDevice, currentFrameParams, assets);
}

}
// namespace VisDefPipeline
