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

#if !HLSL
namespace VisDefPipeline
{

inline bool g_forgetCurrentFrame = false;

#endif // !HLSL

struct SceneConstants
{
    XMUINT2  Size;

    XMFLOAT2 PixelOffset;
    XMFLOAT2 NormalizedPixelOffset;

    // ViewPos.xy / z projected into [0..width, 0..height] screen space
    XMFLOAT2 ProjectionScale;
    XMFLOAT2 ProjectionBias ;

    XMFLOAT2 InverseProjectionScale;
    XMFLOAT2 InverseProjectionBias ;

    XMFLOAT2 CenterProjectionScale;
    XMFLOAT2 CenterProjectionBias ;

    XMFLOAT2 InverseCenterProjectionScale;
    XMFLOAT2 InverseCenterProjectionBias ;

    XMFLOAT2 LastFrameProjectionScale;
    XMFLOAT2 LastFrameProjectionBias ;

    XMFLOAT2 LastFrameInverseProjectionScale;
    XMFLOAT2 LastFrameInverseProjectionBias ;

    XMFLOAT2 InverseAspectRatio;
    XMFLOAT2 AspectRatio       ;

    uint32_t ForgetCurrentFrame;
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

struct VisibilityPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> SolidPipelineState;
    ComPtr<ID3D12PipelineState> AlphaTestPipelineState;

    VisibilityPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, FrameParameters const&, TextureManager const&, MaterialManager const&, ModelManager const&, InstanceManager const&, RenderPipelineAssets const&) const;
};

struct ColorPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ColorPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, FrameParameters const&, TextureManager const&, MaterialManager const&, ModelManager const&, InstanceManager const&, RenderPipelineAssets const&) const;
};

struct PostprocessPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    PostprocessPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, FrameParameters const&, InstanceManager const&, RenderPipelineAssets const&) const;
};

struct ResolvePass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ResolvePass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, FrameParameters const&, RenderPipelineAssets const&) const;
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

    void Render(RenderDevice&, TextureManager const&, MaterialManager const&, ModelManager const&, InstanceManager const&);
};

#endif // !HLSL


#if !HLSL
}
// namespace VisDefPipeline
#endif // !HLSL
