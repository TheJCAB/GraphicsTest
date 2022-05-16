// GraphicsTest.cpp : Defines the entry point for the application.
//
#pragma once

#include "JcabD3D.h"
#include "Model.h"

#include <shlobj.h>

#include <array>
#include <span>
#include <vector>
#include <map>
#include <filesystem>



struct RenderPipelineAssets
{
    IntermediateRenderTarget    VisibilityBuffer;
    IntermediateDepthBuffer     DepthBuffer;

    static constexpr uint8_t MaxSamplesPerQuad = 8;

    struct MsaaSample
    {
        uint32_t Visibility; // 32 bits instance + tri
        uint32_t SampleMask; // UNORM  4 * 5 = 20 bits compressed
    };
    ComPtr<ID3D12Resource> MsaaSampleBuffer;
    UINT                   MsaaSampleBufferUav;
    ComPtr<ID3D12Resource> MsaaSampleCountBuffer;
    UINT                   MsaaSampleCountBufferUav;

    struct ComputableSample
    {
        uint32_t ScreenPos;  // 32 bits
        float    Depth;      // As is, 32 bits
    };
    ComPtr<ID3D12Resource> ComputableSampleBuffer;
    UINT                   ComputableSampleBufferUav;

    IntermediateRenderTarget ColorBuffer;
    UINT                     ColorBufferUav = 0;

    struct SceneConstants
    {
        XMFLOAT4X4 InverseProjection;
    };
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

struct MsaaGatherPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    MsaaGatherPass(RenderDevice& renderDevice);

    void Render(RenderDevice const&, ModelManager const&, InstanceManager const&, RenderPipelineAssets const&) const;
};

struct ComputableGatherPass
{
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PipelineState;

    ComputableGatherPass(RenderDevice& renderDevice);

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


struct RenderPipeline
{
    RenderPipelineAssets assets;

    VisibilityPass       visibilityPass;
    MsaaGatherPass       msaaGatherPass;
    ComputableGatherPass computableGatherPass;
    ColorPass            colorPass;
    PostprocessPass      postprocessPass;
    ResolvePass          resolvePass;

    RenderPipeline(RenderDevice&);

    void Render(RenderDevice&, TextureManager const&, MaterialManager const&, ModelManager const&, InstanceManager const&);
};
