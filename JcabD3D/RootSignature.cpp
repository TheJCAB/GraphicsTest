
#include <JcabD3D/RootSignature.h>
#include <JcabD3D/RootSignatureBuilder.h>
#include <JcabD3D.h>

namespace Root
{

Signature Create(RenderDevice const& renderDevice, D3D12_ROOT_SIGNATURE_DESC const& desc, Signature::ParameterDb&& db)
{
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(reinterpret_cast<D3D12_ROOT_SIGNATURE_DESC const*>(&desc), D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(renderDevice.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    return Signature{ std::move(rootSignature), std::move(db) };
}

Signature Create(RenderDevice const& renderDevice, D3D12_ROOT_SIGNATURE_DESC1 const& desc, Signature::ParameterDb&& db)
{
    return Create(renderDevice, reinterpret_cast<D3D12_ROOT_SIGNATURE_DESC const&>(desc), std::move(db));
}

void Signature::SetForGraphics(GraphicsCommandList const& commandList) noexcept
{
    m_isCompute = false;
    commandList->SetGraphicsRootSignature(Get());
}

void Signature::SetForCompute(GraphicsCommandList const& commandList) noexcept
{
    m_isCompute = true;
    commandList->SetComputeRootSignature(Get());
}

// TODO
//void Signature::SetForCompute(ComputeCommandList const& commandList) noexcept
//{
//    m_isCompute = true;
//    commandList->SetComputeRootSignature(Get());
//}

template <CommandListType CommandList>
void Signature::SetConstants(CommandList const& commandList, Root::Constants const& id, std::span<uint32_t const> const data, uint32_t const offset) const
{
    auto const it = m_parameterDb.Constants.find(id.Id);
    if (it == m_parameterDb.Constants.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRoot32BitConstants(index, data.size(), data.data(), offset);
    }
    else
    {
        commandList->SetGraphicsRoot32BitConstants(index, data.size(), data.data(), offset);
    }
}

template <CommandListType CommandList>
void Signature::SetConstant(CommandList const& commandList, Root::Constants const& id, uint32_t const data, uint32_t const offset) const
{
    auto const it = m_parameterDb.Constants.find(id.Id);
    if (it == m_parameterDb.Constants.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRoot32BitConstant(index, data, offset);
    }
    else
    {
        commandList->SetGraphicsRoot32BitConstant(index, data, offset);
    }
}

template <CommandListType CommandList>
void Signature::SetCbv(CommandList const& commandList, Root::Cbv const& id, GpuVirtualAddress const address) const
{
    auto const it = m_parameterDb.Cbv.find(id.Id);
    if (it == m_parameterDb.Cbv.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRootConstantBufferView(index, address);
    }
    else
    {
        commandList->SetGraphicsRootConstantBufferView(index, address);
    }
}
template <CommandListType CommandList>
void Signature::SetSrv(CommandList const& commandList, Root::Srv const& id, GpuVirtualAddress const address) const
{
    auto const it = m_parameterDb.Srv.find(id.Id);
    if (it == m_parameterDb.Srv.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRootShaderResourceView(index, address);
    }
    else
    {
        commandList->SetGraphicsRootShaderResourceView(index, address);
    }
}

template <CommandListType CommandList>
void Signature::SetUav(CommandList const& commandList, Root::Uav const& id, GpuVirtualAddress const address) const
{
    auto const it = m_parameterDb.Uav.find(id.Id);
    if (it == m_parameterDb.Uav.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRootUnorderedAccessView(index, address);
    }
    else
    {
        commandList->SetGraphicsRootUnorderedAccessView(index, address);
    }
}


template <CommandListType CommandList>
void Signature::SetCbvTable(CommandList const& commandList, Root::CbvTable const& id, D3D12_GPU_DESCRIPTOR_HANDLE const address) const
{
    auto const it = m_parameterDb.CbvTable.find(id.Id);
    if (it == m_parameterDb.CbvTable.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRootDescriptorTable(index, address);
    }
    else
    {
        commandList->SetGraphicsRootDescriptorTable(index, address);
    }
}

template <CommandListType CommandList>
void Signature::SetSrvTable(CommandList const& commandList, Root::SrvTable const& id, D3D12_GPU_DESCRIPTOR_HANDLE const address) const
{
    auto const it = m_parameterDb.SrvTable.find(id.Id);
    if (it == m_parameterDb.SrvTable.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRootDescriptorTable(index, address);
    }
    else
    {
        commandList->SetGraphicsRootDescriptorTable(index, address);
    }
}

template <CommandListType CommandList>
void Signature::SetUavTable(CommandList const& commandList, Root::UavTable const& id, D3D12_GPU_DESCRIPTOR_HANDLE const address) const
{
    auto const it = m_parameterDb.UavTable.find(id.Id);
    if (it == m_parameterDb.UavTable.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRootDescriptorTable(index, address);
    }
    else
    {
        commandList->SetGraphicsRootDescriptorTable(index, address);
    }
}

template <CommandListType CommandList>
void Signature::SetSamplerTable(CommandList const& commandList, Root::SamplerTable const& id, D3D12_GPU_DESCRIPTOR_HANDLE const address) const
{
    auto const it = m_parameterDb.SamplerTable.find(id.Id);
    if (it == m_parameterDb.SamplerTable.end())
    {
        throw std::invalid_argument{"Invalid root parameter ID"};
    }
    uint32_t const index = it->second;
    if (m_isCompute)
    {
        commandList->SetComputeRootDescriptorTable(index, address);
    }
    else
    {
        commandList->SetGraphicsRootDescriptorTable(index, address);
    }
}

template void Signature::SetConstants   (GraphicsCommandList const&, Root::Constants    const&, std::span<uint32_t const>, uint32_t offset) const;
template void Signature::SetConstant    (GraphicsCommandList const&, Root::Constants    const&, uint32_t, uint32_t offset) const;
template void Signature::SetCbv         (GraphicsCommandList const&, Root::Cbv          const&, GpuVirtualAddress) const;
template void Signature::SetSrv         (GraphicsCommandList const&, Root::Srv          const&, GpuVirtualAddress) const;
template void Signature::SetUav         (GraphicsCommandList const&, Root::Uav          const&, GpuVirtualAddress) const;
template void Signature::SetCbvTable    (GraphicsCommandList const&, Root::CbvTable     const&, D3D12_GPU_DESCRIPTOR_HANDLE) const;
template void Signature::SetSrvTable    (GraphicsCommandList const&, Root::SrvTable     const&, D3D12_GPU_DESCRIPTOR_HANDLE) const;
template void Signature::SetUavTable    (GraphicsCommandList const&, Root::UavTable     const&, D3D12_GPU_DESCRIPTOR_HANDLE) const;
template void Signature::SetSamplerTable(GraphicsCommandList const&, Root::SamplerTable const&, D3D12_GPU_DESCRIPTOR_HANDLE) const;

D3D12_ROOT_PARAMETER1 GetParameter(Constants const& p) noexcept
{
    return D3D12_ROOT_PARAMETER1
    {
        .ParameterType    = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
        .Constants
        {
            .ShaderRegister = p.Reg,
            .RegisterSpace  = p.Space,
            .Num32BitValues = p.Count,
        },
        .ShaderVisibility = p.Visibility,
    };
}

void AddId(Signature::ParameterDb& db, Constants const& p, uint32_t index) noexcept
{
    assert(db.Constants.find(p.Id) == db.Constants.end());
    db.Constants[p.Id] = index;
}

template < D3D12_DESCRIPTOR_RANGE_TYPE type >
std::tuple<D3D12_ROOT_PARAMETER1, D3D12_DESCRIPTOR_RANGE1> GetTableParameter(DescriptorTableParameter<type> const& p) noexcept
{
    return {
        D3D12_ROOT_PARAMETER1
        {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable{
                .NumDescriptorRanges = 1,
                .pDescriptorRanges   = nullptr,
            },
            .ShaderVisibility = p.Visibility,
        },
        D3D12_DESCRIPTOR_RANGE1
        {
            .RangeType          = type,
            .NumDescriptors     = p.Count,
            .BaseShaderRegister = p.Reg,
            .RegisterSpace      = p.Space,
            .Flags              = p.Flags,
            .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
        }
    };
}

template < D3D12_DESCRIPTOR_RANGE_TYPE type >
void AddId(Signature::ParameterDb& db, DescriptorTableParameter<type> const& p, uint32_t index)
{
    if constexpr (type == D3D12_DESCRIPTOR_RANGE_TYPE_CBV)
    {
        assert(db.CbvTable.find(p.Id) == db.CbvTable.end());
        db.CbvTable[p.Id] = index;
    }
    else if constexpr (type == D3D12_DESCRIPTOR_RANGE_TYPE_SRV)
    {
        assert(db.SrvTable.find(p.Id) == db.SrvTable.end());
        db.SrvTable[p.Id] = index;
    }
    else if constexpr (type == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
    {
        assert(db.UavTable.find(p.Id) == db.UavTable.end());
        db.UavTable[p.Id] = index;
    }
    else if constexpr (type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
    {
        assert(db.SamplerTable.find(p.Id) == db.SamplerTable.end());
        db.SamplerTable[p.Id] = index;
    }
}

template < D3D12_ROOT_PARAMETER_TYPE type >
D3D12_ROOT_PARAMETER1 GetParameter(DescriptorParameter<type> const& p) noexcept
{
    return D3D12_ROOT_PARAMETER1
    {
        .ParameterType = type,
        .Descriptor{
            .ShaderRegister = p.Reg,
            .RegisterSpace  = p.Space,
            .Flags          = p.Flags,
        },
        .ShaderVisibility = p.Visibility,
    };
}

template < D3D12_ROOT_PARAMETER_TYPE type >
void AddId(Signature::ParameterDb& db, DescriptorParameter<type> const& p, uint32_t index)
{
    if constexpr (type == D3D12_ROOT_PARAMETER_TYPE_CBV)
    {
        assert(db.Cbv.find(p.Id) == db.Cbv.end());
        db.Cbv[p.Id] = index;
    }
    else if constexpr (type == D3D12_ROOT_PARAMETER_TYPE_SRV)
    {
        assert(db.Srv.find(p.Id) == db.Srv.end());
        db.Srv[p.Id] = index;
    }
    else if constexpr (type == D3D12_ROOT_PARAMETER_TYPE_UAV)
    {
        assert(db.Uav.find(p.Id) == db.Uav.end());
        db.Uav[p.Id] = index;
    }
}

template std::tuple<D3D12_ROOT_PARAMETER1, D3D12_DESCRIPTOR_RANGE1> GetTableParameter(DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_CBV    > const&) noexcept;
template std::tuple<D3D12_ROOT_PARAMETER1, D3D12_DESCRIPTOR_RANGE1> GetTableParameter(DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_SRV    > const&) noexcept;
template std::tuple<D3D12_ROOT_PARAMETER1, D3D12_DESCRIPTOR_RANGE1> GetTableParameter(DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_UAV    > const&) noexcept;
template std::tuple<D3D12_ROOT_PARAMETER1, D3D12_DESCRIPTOR_RANGE1> GetTableParameter(DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER> const&) noexcept;

template void AddId(Signature::ParameterDb&, DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_CBV    > const&, uint32_t index);
template void AddId(Signature::ParameterDb&, DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_SRV    > const&, uint32_t index);
template void AddId(Signature::ParameterDb&, DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_UAV    > const&, uint32_t index);
template void AddId(Signature::ParameterDb&, DescriptorTableParameter<D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER> const&, uint32_t index);

template D3D12_ROOT_PARAMETER1 GetParameter(DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS> const&) noexcept;
template D3D12_ROOT_PARAMETER1 GetParameter(DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_CBV            > const&) noexcept;
template D3D12_ROOT_PARAMETER1 GetParameter(DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_SRV            > const&) noexcept;
template D3D12_ROOT_PARAMETER1 GetParameter(DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_UAV            > const&) noexcept;

template void AddId(Signature::ParameterDb&, DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS> const&, uint32_t index);
template void AddId(Signature::ParameterDb&, DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_CBV            > const&, uint32_t index);
template void AddId(Signature::ParameterDb&, DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_SRV            > const&, uint32_t index);
template void AddId(Signature::ParameterDb&, DescriptorParameter<D3D12_ROOT_PARAMETER_TYPE_UAV            > const&, uint32_t index);

}
// namespace Root
