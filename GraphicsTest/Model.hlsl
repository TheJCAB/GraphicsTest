
#include "ViewSpace.lib.hlsl"

#include "Model.h"

struct VisibilityPassRootConstants
{
    uint32_t InstanceIndex;
    //XMFLOAT2 NormalizedPixelOffset;
};

ConstantBuffer<VisibilityPassRootConstants> RootConstants : register(b0, space1);



float4 VisibilitySolidVS(float3 position : SV_Position) : SV_Position
{
    float4 result = mul(float4(position, 1), instanceList[RootConstants.InstanceIndex].Projection);
    //result.xy += RootConstants.NormalizedPixelOffset * result.w;
    return result;
}

uint VisibilitySolidPS(uint triangleId : SV_PrimitiveID) : SV_Target
{
    uint instanceId = RootConstants.InstanceIndex + 1;
    return triangleId | (instanceId << 20);
}




struct AlphaTestVertexOutput
{
    float4 Position : SV_Position;
    float2 Uv       : UV;
};

AlphaTestVertexOutput VisibilityAlphaTestVS(float3 position : SV_Position, float2 uv : UV)
{
    AlphaTestVertexOutput result;
    result.Position = mul(float4(position, 1), instanceList[RootConstants.InstanceIndex].Projection);
    //result.Position.xy += RootConstants.NormalizedPixelOffset * result.Position.w;
    result.Uv = uv;
    return result;
}

uint VisibilityAlphaTestPS(AlphaTestVertexOutput data, uint triangleId : SV_PrimitiveID) : SV_Target
{
    uint instanceId = RootConstants.InstanceIndex + 1;

    // TODO: Individual Draw calls so the texture ID can just be a constant.
    uint modelIndex       = instanceList  [RootConstants.InstanceIndex].ModelIndex;
    uint materialIndex    = triangleParams[modelIndex][triangleId]     .MaterialIndex;
    uint diffuseTextureId = materials     [materialIndex]              .DiffuseTextureId;

    clip(textures[diffuseTextureId - 1].Sample(wrapSampler, data.Uv).a - 0.5f);

    return triangleId | (instanceId << 20);
}

