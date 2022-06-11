#pragma once

#include <JcabD3D/Base.h>
#include <JcabD3D/ParameterId.h>
#include <JcabD3D/RootSignature.h>

#include <unordered_map>
#include <span>
#include <utility>
#include <compare>
#include <stdexcept>

class RenderDevice;


namespace Root
{

struct Constants
{
    ParameterId             Id;
    D3D12_SHADER_VISIBILITY Visibility = D3D12_SHADER_VISIBILITY_ALL;
    UINT                    Reg        = 0;
    UINT                    Count      = 1;
    UINT                    Space      = 0;
};

template < D3D12_DESCRIPTOR_RANGE_TYPE type >
struct DescriptorTableParameter
{
    ParameterId                  Id;
    D3D12_SHADER_VISIBILITY      Visibility = D3D12_SHADER_VISIBILITY_ALL;
    UINT                         Reg        = 0;
    UINT                         Count      = 1;
    UINT                         Space      = 0;
    D3D12_DESCRIPTOR_RANGE_FLAGS Flags      = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
};

template < D3D12_ROOT_PARAMETER_TYPE type >
struct DescriptorParameter
{
    ParameterId                 Id;
    D3D12_SHADER_VISIBILITY     Visibility = D3D12_SHADER_VISIBILITY_ALL;
    UINT                        Reg        = 0;
    UINT                        Space      = 0;
    D3D12_ROOT_DESCRIPTOR_FLAGS Flags      = D3D12_ROOT_DESCRIPTOR_FLAG_NONE;
};

template < typename T >
concept RootParameter = requires(T const a, Signature::ParameterDb& db, uint32_t index)
{
    { AddId(db, a, index) };
};

template < typename T >
concept RootTableParameter = RootParameter<T> && requires(T const a)
{
    { GetTableParameter(a) } -> std::convertible_to<std::tuple<D3D12_ROOT_PARAMETER1, D3D12_DESCRIPTOR_RANGE1>>;

};

template < typename T >
concept RootNonTableParameter = RootParameter<T> && requires(T const a)
{
    { GetParameter(a) } -> std::convertible_to<D3D12_ROOT_PARAMETER1>;
};

constexpr D3D12_ROOT_SIGNATURE_FLAGS GatherFlags() noexcept
{
    return D3D12_ROOT_SIGNATURE_FLAG_NONE;
}

template < typename Arg, typename... Args >
constexpr D3D12_ROOT_SIGNATURE_FLAGS GatherFlags(Arg&& arg, Args&&... args) noexcept
{
    auto const rest = GatherFlags(std::forward<Args>(args)...);
    if constexpr (std::is_same_v<std::decay_t<Arg>, D3D12_ROOT_SIGNATURE_FLAGS>)
    {
        return arg | rest;
    }
    else
    {
        return rest;
    }
}

template <                    typename... Args > constexpr size_t ParameterCount;
template <>                                      constexpr size_t ParameterCount<>             = 0;
template < RootParameter Arg, typename... Args > constexpr size_t ParameterCount<Arg, Args...> = 1 + ParameterCount<Args...>;
template < typename      Arg, typename... Args > constexpr size_t ParameterCount<Arg, Args...> =     ParameterCount<Args...>;

template <                         typename... Args > constexpr size_t DescriptorRangeCount;
template <>                                           constexpr size_t DescriptorRangeCount<>             = 0;
template < RootTableParameter Arg, typename... Args > constexpr size_t DescriptorRangeCount<Arg, Args...> = 1 + DescriptorRangeCount<Args...>;
template < typename           Arg, typename... Args > constexpr size_t DescriptorRangeCount<Arg, Args...> =     DescriptorRangeCount<Args...>;

struct RootSignatureDescriptionBuilder
{
    size_t NextDescriptorRangeIndex = 0;
    size_t NextParameterIndex       = 0;
        
    std::span<D3D12_DESCRIPTOR_RANGE1> DescriptorRanges;
    std::span<D3D12_ROOT_PARAMETER1>   Parameters;
        
    Signature::ParameterDb& ParameterDb;
};

constexpr void StoreParameters(RootSignatureDescriptionBuilder&) noexcept
{
}

template < typename Arg, typename... Args >
constexpr void StoreParameters(RootSignatureDescriptionBuilder& builder, Arg&& arg, Args&&... args) noexcept
{
    if constexpr (RootParameter<Arg>)
    {
        assert(builder.NextParameterIndex < builder.Parameters.size());

        if constexpr(RootTableParameter<Arg>)
        {
            assert(builder.NextDescriptorRangeIndex < builder.DescriptorRanges.size());

            auto& range     = builder.DescriptorRanges[builder.NextDescriptorRangeIndex];
            auto& parameter = builder.Parameters      [builder.NextParameterIndex      ];
            std::tie(parameter, range) = GetTableParameter(arg);
            parameter.DescriptorTable.pDescriptorRanges = &range;
            ++builder.NextDescriptorRangeIndex;
        }
        else
        {
            auto const parameter = GetParameter(arg);
            builder.Parameters[builder.NextParameterIndex] = parameter;
        }

        AddId(builder.ParameterDb, arg, builder.NextParameterIndex);

        ++builder.NextParameterIndex;
    }
    return StoreParameters(builder, std::forward<Args>(args)...);
}

template < size_t parameterCount, size_t descriptorRangeCount >
struct ParametersData
{
    template < typename... Args >
    ParametersData(Args&&... args)
    {
        static_assert(DescriptorRangeCount<Args...> == descriptorRangeCount);
        static_assert(ParameterCount      <Args...> == parameterCount      );

        RootSignatureDescriptionBuilder builder
        {
            .DescriptorRanges{ DescriptorRanges },
            .Parameters      { Parameters       },
            .ParameterDb     { ParameterDb      },
        };
        StoreParameters(builder, std::forward<Args>(args)...);
        assert(builder.NextDescriptorRangeIndex == DescriptorRanges.size());
        assert(builder.NextParameterIndex       == Parameters      .size());
    }

    ParametersData           ()                 = default;
    ParametersData           (ParametersData&&) = delete;
    ParametersData& operator=(ParametersData&&) = delete;

    std::array<D3D12_ROOT_PARAMETER1  , parameterCount      > Parameters      {};
    std::array<D3D12_DESCRIPTOR_RANGE1, descriptorRangeCount> DescriptorRanges{};
    Signature::ParameterDb                                    ParameterDb     {};
};

template < typename... Args >
constexpr auto GatherParameters(Args&&... args) noexcept
{
    return ParametersData<ParameterCount<Args...>, DescriptorRangeCount<Args...>>{ std::forward<Args>(args)... };
}

D3D12_ROOT_PARAMETER1 GetParameter(Constants const& p) noexcept;

void AddId(Signature::ParameterDb& db, Constants const& p, uint32_t index) noexcept;

template < D3D12_DESCRIPTOR_RANGE_TYPE type >
std::tuple<D3D12_ROOT_PARAMETER1, D3D12_DESCRIPTOR_RANGE1> GetTableParameter(DescriptorTableParameter<type> const& p) noexcept;

template < D3D12_DESCRIPTOR_RANGE_TYPE type >
void AddId(Signature::ParameterDb& db, DescriptorTableParameter<type> const& p, uint32_t index);

template < D3D12_ROOT_PARAMETER_TYPE type >
D3D12_ROOT_PARAMETER1 GetParameter(DescriptorParameter<type> const& p) noexcept;

template < D3D12_ROOT_PARAMETER_TYPE type >
void AddId(Signature::ParameterDb& db, DescriptorParameter<type> const& p, uint32_t index);

template < typename... Args >
inline Signature CreateSignature(RenderDevice const& renderDevice, Args&&... args)
{
    auto parameterData = GatherParameters(std::forward<Args>(args)...);

    D3D12_ROOT_SIGNATURE_DESC1 const desc{
        .NumParameters     = static_cast<UINT>(parameterData.Parameters.size()),
        .pParameters       = parameterData.Parameters.data(),
        .NumStaticSamplers = 0,
        .pStaticSamplers   = nullptr,
        .Flags             = GatherFlags(std::forward<Args>(args)...),
    };

    return Create(renderDevice, desc, std::move(parameterData.ParameterDb));
}

}
// namespace Root
