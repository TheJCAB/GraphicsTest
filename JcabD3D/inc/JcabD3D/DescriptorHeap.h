#pragma once

#include <JcabD3D/Base.h>

#include <utility>

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
class DescriptorHeap
{
public:
    DescriptorHeap() = default;
    DescriptorHeap(ComRef<ID3D12Device> device, UINT count, D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE);

    UINT AllocateDescriptors(UINT count);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuDescriptorHandle(UINT index) const;

    D3D12_CPU_DESCRIPTOR_HANDLE NextDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const;
    D3D12_GPU_DESCRIPTOR_HANDLE NextDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) const;

    ID3D12DescriptorHeap* Get            () const { return heap.Get(); }
    ID3D12DescriptorHeap* operator->     () const { return heap.Get(); }
    explicit operator ID3D12DescriptorHeap*       () const { return heap.Get(); }
    operator ComRef<ID3D12DescriptorHeap>() const { return heap.Get(); }
    explicit operator bool               () const { return heap; }

    ComPtr<IUnknown> Extract() && { return std::move(heap); }

private:
    ComPtr<ID3D12DescriptorHeap> heap;
    UINT                         descriptorSize  = 0;
    UINT                         descriptorCount = 0;
    UINT                         descriptorsUsed = 0;
};

class RtvDescriptorHeap : public DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_RTV>
{
public:
    using DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_RTV>::DescriptorHeap;
};

class DsvDescriptorHeap : public DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_DSV>
{
public:
    using DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_DSV>::DescriptorHeap;
};

class CsuvDescriptorHeap : public DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV>
{
public:
    using DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV>::DescriptorHeap;
};

class SamplerDescriptorHeap : public DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER>
{
public:
    using DescriptorHeap<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER>::DescriptorHeap;
};

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
UINT DescriptorHeap<type>::AllocateDescriptors(UINT count)
{
    auto result = descriptorsUsed;
    descriptorsUsed += count;
    return result;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap<type>::GetCpuDescriptorHandle(UINT index) const
{
    auto handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += descriptorSize * index;
    return handle;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap<type>::NextDescriptorHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) const
{
    handle.ptr += descriptorSize;
    return handle;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap<type>::GetGpuDescriptorHandle(UINT index) const
{
    auto handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += descriptorSize * index;
    return handle;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap<type>::NextDescriptorHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) const
{
    handle.ptr += descriptorSize;
    return handle;
}

template < D3D12_DESCRIPTOR_HEAP_TYPE type >
DescriptorHeap<type>::DescriptorHeap(ComRef<ID3D12Device> device, UINT count, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
{
    descriptorSize  = device->GetDescriptorHandleIncrementSize(type);
    descriptorCount = count;

    // Describe and create a render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC const heapDesc
    {
        .Type           = type,
        .NumDescriptors = count,
        .Flags          = flags,
    };
    ThrowIfFailed(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)));
}
