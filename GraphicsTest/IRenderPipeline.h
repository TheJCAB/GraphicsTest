#pragma once

struct RenderDevice;
struct TextureManager;
struct MaterialManager;
struct ModelManager;
struct InstanceManager;

struct IRenderPipeline
{
    virtual ~IRenderPipeline() = default;

    virtual void Render(RenderDevice&, TextureManager const&, MaterialManager const&, ModelManager const&, InstanceManager const&) = 0;
};
