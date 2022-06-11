#pragma once

#include <JcabD3D/Base.h>

#include <utility>

class RenderDevice;

class CsuvDescriptorHeap;
class SamplerDescriptorHeap;

class GraphicsCommandList : public ComWrapper<ID3D12GraphicsCommandList>
{
    friend RenderDevice;

    explicit GraphicsCommandList(ComPtr<ID3D12GraphicsCommandList> p) : ComWrapper{ std::move(p) } {}

public:
    GraphicsCommandList() = default;

    void SetDescriptorHeaps(CsuvDescriptorHeap const&, SamplerDescriptorHeap const&) const;
    void SetDescriptorHeap(CsuvDescriptorHeap const&) const;
};

inline
void GraphicsCommandList::SetDescriptorHeaps(CsuvDescriptorHeap const& csuvs, SamplerDescriptorHeap const& samplers) const
{
    ID3D12DescriptorHeap* const heaps[]{ csuvs.Get(), samplers.Get() };
    Get()->SetDescriptorHeaps(2, heaps);
}

inline
void GraphicsCommandList::SetDescriptorHeap(CsuvDescriptorHeap const& csuvs) const
{
    ID3D12DescriptorHeap* const heap{ csuvs.Get() };
    Get()->SetDescriptorHeaps(1, &heap);
}
