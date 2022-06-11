#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <type_traits>

// Note that while ComPtr is used to manage the lifetime of resources on the CPU,
// it has no understanding of the lifetime of resources on the GPU. Apps must account
// for the GPU lifetime of resources to avoid destroying objects that may still be
// referenced by the GPU.
using Microsoft::WRL::ComPtr;

template < typename Interface >
class ComRef
{
public:
    ComRef(Interface* pInterface              ) : m_pInterface{ pInterface       } {}
    ComRef(ComPtr<Interface> const& pInterface) : m_pInterface{ pInterface.Get() } {}

    Interface* Get        () const { return m_pInterface; }
    Interface* operator-> () const { return m_pInterface; }
    operator Interface*   () const { return m_pInterface; }
    explicit operator bool() const { return m_pInterface != nullptr; }

private:
    Interface* m_pInterface;
};

template < typename Interface >
class ComWrapper
{
protected:
    explicit ComWrapper(ComPtr<Interface> p) : m_obj{ std::move(p) } {}

public:
    ComWrapper() = default;

    Interface* Get              () const { return m_obj.Get(); }
    Interface* operator->       () const { return m_obj.Get(); }
    explicit operator Interface*() const { return m_obj.Get(); }
    operator ComRef<Interface>  () const { return m_obj.Get(); }
    explicit operator bool      () const { return m_obj; }

    ComPtr<IUnknown> Extract() && { return std::move(m_obj); }

private:
    ComPtr<Interface> m_obj;
};

using namespace DirectX;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw hr;
    }
}



class RenderDevice;

class CsuvDescriptorHeap;
class SamplerDescriptorHeap;

class GraphicsCommandList;
class ComputeCommandList;

template < typename CommandList >
concept CommandListType = std::is_same_v<CommandList, GraphicsCommandList> || std::is_same_v<CommandList, ComputeCommandList>;

using GpuVirtualAddress = D3D12_GPU_VIRTUAL_ADDRESS;

namespace Root
{

struct Constants;

template < D3D12_DESCRIPTOR_RANGE_TYPE type > struct DescriptorTableParameter;

using CbvTable     = DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_CBV>;
using SrvTable     = DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_SRV>;
using UavTable     = DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_UAV>;
using SamplerTable = DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER>;

template < D3D12_ROOT_PARAMETER_TYPE type > struct DescriptorParameter;

using Cbv = DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_CBV>;
using Srv = DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_SRV>;
using Uav = DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_UAV>;

struct Signature;

}
// namespace Root
