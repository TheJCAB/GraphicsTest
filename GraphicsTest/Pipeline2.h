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
namespace VisDefPipeline
{

inline bool g_forgetCurrentFrame = false;
inline bool g_markMagenta        = false;

#endif // !HLSL

struct SceneConstants
{
    ViewSpaceConstants ViewSpace;

    XMFLOAT2 PixelOffset;
    XMFLOAT2 NormalizedPixelOffset;

    XMFLOAT2 OffsetedProjectionScale;
    XMFLOAT2 OffsetedProjectionBias ;

    XMFLOAT2 InverseOffsetedProjectionScale;
    XMFLOAT2 InverseOffsetedProjectionBias ;

    XMFLOAT2 LastFrameProjectionScale;
    XMFLOAT2 LastFrameProjectionBias ;

    XMFLOAT2 LastFrameInverseProjectionScale;
    XMFLOAT2 LastFrameInverseProjectionBias ;

    uint32_t ForgetCurrentFrame;
    uint32_t MarkMagenta;
};


#if !HLSL

struct RenderPipelineAssets
{
    IntermediateRenderTarget    VisibilityBuffer[2];
    IntermediateDepthBuffer     DepthBuffer[2];

    IntermediateRenderTarget TaaColorBuffer[2];
    UINT                     TaaColorBufferUav[2]{};

    IntermediateRenderTarget ColorBuffer;
    UINT                     ColorBufferUav{};

    CpuAccessibleBuffer<SceneConstants> SceneConstantsBuffer[FrameCount];

    RenderPipelineAssets(RenderDevice&);
};

struct FrameParameters
{
    uint32_t previousFlipIndex = 0;
    uint32_t flipIndex         = 0;
};

struct ColorPass
{
    Root::Signature RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ColorPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, FrameParameters const&, SceneGraphAssets const&, RenderPipelineAssets const&);
};

struct PostprocessPass
{
    Root::Signature RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    PostprocessPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, FrameParameters const&, InstanceManager const&, RenderPipelineAssets const&);
};

struct ResolvePass
{
    Root::Signature RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ResolvePass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, FrameParameters const&, RenderPipelineAssets const&);
};


struct RenderPipeline : IRenderPipeline
{
    RenderPipelineAssets assets;

    VisibilityPass       visibilityPass;
    ColorPass            colorPass;
    PostprocessPass      postprocessPass;
    ResolvePass          resolvePass;

    FrameParameters currentFrameParams;

    RenderPipeline(RenderDevice&);

    void Render(RenderDevice&, SceneGraphAssets const&);
};

#endif // !HLSL


#if !HLSL
}
// namespace VisDefPipeline
#endif // !HLSL
