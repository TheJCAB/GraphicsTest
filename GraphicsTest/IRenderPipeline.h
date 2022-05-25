#pragma once

struct RenderDevice;
struct SceneGraphAssets;

struct IRenderPipeline
{
    virtual ~IRenderPipeline() = default;

    virtual void Render(RenderDevice&, SceneGraphAssets const&) = 0;
};
