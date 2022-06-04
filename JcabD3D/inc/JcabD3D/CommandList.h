#pragma once

#include <JcabD3D/Base.h>

#include <utility>

class RenderDevice;

class CsuvDescriptorHeap;
class SamplerDescriptorHeap;

class GraphicsCommandList
{
    friend RenderDevice;

    explicit GraphicsCommandList(ComPtr<ID3D12GraphicsCommandList> p) : commandList{ std::move(p) } {}

public:
    GraphicsCommandList() = default;

    ID3D12GraphicsCommandList* Get            () const { return commandList.Get(); }
    ID3D12GraphicsCommandList* operator->     () const { return commandList.Get(); }
    /* explicit */ operator ID3D12GraphicsCommandList*       () const { return commandList.Get(); }
    operator ComRef<ID3D12GraphicsCommandList>() const { return commandList.Get(); }
    explicit operator bool               () const { return commandList; }

    ComPtr<IUnknown> Extract() && { return std::move(commandList); }

    void SetDescriptorHeaps(CsuvDescriptorHeap const&, SamplerDescriptorHeap const&) const;
    void SetDescriptorHeap(CsuvDescriptorHeap const&) const;

private:
    ComPtr<ID3D12GraphicsCommandList> commandList;
};

inline
void GraphicsCommandList::SetDescriptorHeaps(CsuvDescriptorHeap const& csuvs, SamplerDescriptorHeap const& samplers) const
{
    ID3D12DescriptorHeap* const heaps[]{ csuvs.Get(), samplers.Get() };
    commandList->SetDescriptorHeaps(2, heaps);
}

inline
void GraphicsCommandList::SetDescriptorHeap(CsuvDescriptorHeap const& csuvs) const
{
    ID3D12DescriptorHeap* const heap{ csuvs.Get() };
    commandList->SetDescriptorHeaps(1, &heap);
}
