#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

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

using namespace DirectX;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw hr;
    }
}
