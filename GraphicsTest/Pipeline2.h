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
#endif // !HLSL

struct SceneConstants
{
    XMUINT2  Size;

    // ViewPos.xy / z projected into [0..width, 0..height] screen space
    XMFLOAT2 ProjectionScale;
    XMFLOAT2 ProjectionBias ;

    XMFLOAT2 InverseProjectionScale;
    XMFLOAT2 InverseProjectionBias ;

    // ViewPos.xy / z projected into [0..1, 0..1] normalized space
    XMFLOAT2 NormalizedProjectionScale;
    XMFLOAT2 NormalizedProjectionBias ;

    XMFLOAT2 InverseNormalizedProjectionScale;
    XMFLOAT2 InverseNormalizedProjectionBias ;

    XMFLOAT2 InverseAspectRatio;
    XMFLOAT2 AspectRatio       ;
};


#if !HLSL

struct RenderPipelineAssets
{
    IntermediateRenderTarget    VisibilityBuffer;
    IntermediateDepthBuffer     DepthBuffer;

    IntermediateRenderTarget ColorBuffer;
    UINT                     ColorBufferUav = 0;

    CpuAccessibleBuffer<SceneConstants> SceneConstantsBuffer[FrameCount];

    RenderPipelineAssets(RenderDevice&);
};



struct VisibilityPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    VisibilityPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, ModelManager const&, InstanceManager const&, RenderPipelineAssets const&) const;
};

struct ColorPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ColorPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, TextureManager const&, MaterialManager const&, ModelManager const&, InstanceManager const&, RenderPipelineAssets const&) const;
};

struct PostprocessPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    PostprocessPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, RenderPipelineAssets const&) const;
};

struct ResolvePass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ResolvePass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, RenderPipelineAssets const&) const;
};


struct RenderPipeline : IRenderPipeline
{
    RenderPipelineAssets assets;

    VisibilityPass       visibilityPass;
    ColorPass            colorPass;
    PostprocessPass      postprocessPass;
    ResolvePass          resolvePass;

    RenderPipeline(RenderDevice&);

    void Render(RenderDevice&, TextureManager const&, MaterialManager const&, ModelManager const&, InstanceManager const&);
};

#endif // !HLSL


#if !HLSL
}
// namespace VisDefPipeline
#endif // !HLSL
