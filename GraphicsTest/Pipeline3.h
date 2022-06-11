// GraphicsTest.cpp : Defines the entry point for the application.
//
#pragma once

#if HLSL

#include "HLSL.h"

#else // !HLSL

#include "JcabD3D.h"
#include "IRenderPipeline.h"

#include <shlobj.h>

#include <array>
#include <span>
#include <vector>
#include <map>
#include <filesystem>

#endif // !HLSL

#include "Model.h"
#include "ViewSpace.h"

#if !HLSL
namespace VisDefMsaa3Pipeline
{
#endif // !HLSL

struct SceneConstants
{
    ViewSpaceConstants ViewSpace;

    // ViewPos.xy / z projected into [0..1, 0..1] normalized space
    XMFLOAT2 NormalizedProjectionScale;
    XMFLOAT2 NormalizedProjectionBias ;

    XMFLOAT2 InverseNormalizedProjectionScale;
    XMFLOAT2 InverseNormalizedProjectionBias ;
};


#if !HLSL

struct RenderPipelineAssets
{
    IntermediateRenderTarget    VisibilityBuffer;
    IntermediateDepthBuffer     DepthBuffer;

    static constexpr uint8_t MaxSamplesPerQuad = 8;

    struct MsaaSample
    {
        uint32_t Visibility; // 32 bits instance + tri
        uint32_t SampleMask; // 32 samples in a quad
    };
    ComPtr<ID3D12Resource> MsaaSampleBuffer;
    UINT                   MsaaSampleBufferUav;

    IntermediateRenderTarget ColorBuffer;
    UINT                     ColorBufferUav = 0;

    CpuAccessibleBuffer<SceneConstants> SceneConstantsBuffer[FrameCount];

    RenderPipelineAssets(RenderDevice&);
};


struct MsaaGatherPass
{
    Root::Signature RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    MsaaGatherPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, SceneGraphAssets const&, RenderPipelineAssets const&);
};

struct ColorPass
{
    Root::Signature RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ColorPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, SceneGraphAssets const&, RenderPipelineAssets const&);
};

struct PostprocessPass
{
    Root::Signature RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    PostprocessPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, RenderPipelineAssets const&);
};

struct ResolvePass
{
    Root::Signature RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ResolvePass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, RenderPipelineAssets const&);
};


struct RenderPipeline : IRenderPipeline
{
    RenderPipelineAssets assets;

    VisibilityPass       visibilityPass;
    MsaaGatherPass       msaaGatherPass;
    ColorPass            colorPass;
    PostprocessPass      postprocessPass;
    ResolvePass          resolvePass;

    RenderPipeline(RenderDevice&);

    void Render(RenderDevice&, SceneGraphAssets const&);
};

#endif // !HLSL


#if !HLSL
}
// namespace VisDefMsaa3Pipeline
#endif // !HLSL
