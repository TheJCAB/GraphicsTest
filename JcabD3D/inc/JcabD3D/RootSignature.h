#pragma once

#include <JcabD3D/Base.h>
#include <JcabD3D/ParameterId.h>

#include <unordered_map>
#include <span>
#include <utility>
#include <compare>
#include <stdexcept>

class RenderDevice;


namespace Root
{

class Signature : public ComWrapper<ID3D12RootSignature>
{
public:
    Signature() = default;

    void SetForGraphics(GraphicsCommandList const&) noexcept;
    void SetForCompute (GraphicsCommandList const&) noexcept;
    void SetForCompute (ComputeCommandList  const&) noexcept;

    template <CommandListType CommandList> void SetConstants         (CommandList const&, Constants    const&, std::span<uint32_t const>, uint32_t offset = 0) const;
    template <CommandListType CommandList> void SetConstant          (CommandList const&, Constants    const&, uint32_t, uint32_t offset = 0) const;
    template <CommandListType CommandList> void SetCbv               (CommandList const&, Cbv          const&, GpuVirtualAddress) const;
    template <CommandListType CommandList> void SetSrv               (CommandList const&, Srv          const&, GpuVirtualAddress) const;
    template <CommandListType CommandList> void SetUav               (CommandList const&, Uav          const&, GpuVirtualAddress) const;
    template <CommandListType CommandList> void SetCbvTable          (CommandList const&, CbvTable     const&, D3D12_GPU_DESCRIPTOR_HANDLE) const;
    template <CommandListType CommandList> void SetSrvTable          (CommandList const&, SrvTable     const&, D3D12_GPU_DESCRIPTOR_HANDLE) const;
    template <CommandListType CommandList> void SetUavTable          (CommandList const&, UavTable     const&, D3D12_GPU_DESCRIPTOR_HANDLE) const;
    template <CommandListType CommandList> void SetSamplerTable      (CommandList const&, SamplerTable const&, D3D12_GPU_DESCRIPTOR_HANDLE) const;

    struct ParameterDb
    {
        std::unordered_map<ParameterId, uint32_t> Constants;
        std::unordered_map<ParameterId, uint32_t> Cbv;
        std::unordered_map<ParameterId, uint32_t> Srv;
        std::unordered_map<ParameterId, uint32_t> Uav;
        std::unordered_map<ParameterId, uint32_t> CbvTable;
        std::unordered_map<ParameterId, uint32_t> SrvTable;
        std::unordered_map<ParameterId, uint32_t> UavTable;
        std::unordered_map<ParameterId, uint32_t> SamplerTable;
    };

private:
    friend RenderDevice;

    friend Signature Create(RenderDevice const&, D3D12_ROOT_SIGNATURE_DESC  const&, ParameterDb&&);
    friend Signature Create(RenderDevice const&, D3D12_ROOT_SIGNATURE_DESC1 const&, ParameterDb&&);

    explicit Signature(ComPtr<ID3D12RootSignature> p, ParameterDb&& db) : ComWrapper{ std::move(p) }, m_parameterDb{ std::move(db) } {}

private:
    ParameterDb m_parameterDb;
    bool        m_isCompute = false;
};

}
// namespace Root
