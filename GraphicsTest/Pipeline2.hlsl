
#include "Pipeline.h"

StructuredBuffer<TriangleIndices>     indices         [MAX_MODEL_DEFINITIONS] : register(t1, space1);
StructuredBuffer<TriangleParameters>  triangleParams  [MAX_MODEL_DEFINITIONS] : register(t1, space2);
StructuredBuffer<VertexPosition>      vertexPositions [MAX_MODEL_DEFINITIONS] : register(t1, space3);
StructuredBuffer<VertexAttributes>    vertexAttributes[MAX_MODEL_DEFINITIONS] : register(t1, space4);
Texture2D<float4>                     textures        [MAX_TEXTURES         ] : register(t1, space5);
StructuredBuffer<MaterialParameters>  materials                               : register(t0, space5);

ConstantBuffer<SceneConstants> sceneConstants : register(b0);

cbuffer VisibilityPassRootConstants : register(b0, space1)
{
    uint InstanceIndex;
};

StructuredBuffer<InstanceAttributes> instanceList : register(t0, space0);





float4 VisibilityVS(float3 position : SV_Position) : SV_Position
{
    return mul(float4(position, 1), instanceList[InstanceIndex].Projection);
}

uint VisibilityPS(uint triangleId : SV_PrimitiveID) : SV_Target
{
    return triangleId | ((InstanceIndex + 1) << 20);
}






Texture2DMS<uint>  visibilityMap : register(t1, space0);
Texture2DMS<float> depthMap      : register(t2, space0);


RWTexture2D<float4> colorUav : register(u0, space0);

sampler defaultSampler : register(s0, space0);



uint GetVisibility(uint2 pos)
{
    return visibilityMap[pos];
}

float GetDepth(uint2 pos)
{
    return depthMap[pos];
}

float3 GetViewPos(uint2 pos)
{
    float depth = -0.0001f / GetDepth(pos);
    return float3((pos * sceneConstants.InverseProjectionScale + sceneConstants.InverseProjectionBias) * depth, depth);
}

float3 GetBarycentric(uint modelIndex, uint3 indices, float4x4 modelToView, float3 viewPos)
{
    float3 viewPos0 = mul(float4(vertexPositions[modelIndex][indices[0]], 1), modelToView).xyz;
    float3 viewPos1 = mul(float4(vertexPositions[modelIndex][indices[1]], 1), modelToView).xyz;
    float3 viewPos2 = mul(float4(vertexPositions[modelIndex][indices[2]], 1), modelToView).xyz;

    float3 N = cross(viewPos1 - viewPos0, viewPos2 - viewPos0);
    float N2 = dot(N, N);

    float3 bary = clamp(float3(
        dot(cross(viewPos1 - viewPos, viewPos2 - viewPos), N),
        dot(cross(viewPos2 - viewPos, viewPos0 - viewPos), N),
        dot(cross(viewPos0 - viewPos, viewPos1 - viewPos), N)
    ) / N2, 0, 1);

    return bary / dot(bary, 1);
}

uint NumPixelsX() { return sceneConstants.Size.x                        ; }
uint NumPixelsY() { return sceneConstants.Size.y                        ; }
uint NumPixels () { return sceneConstants.Size.x * sceneConstants.Size.y; }

struct GBufferSample
{
    float3 ViewPos;         // As is, or... say 48-64 bits (plus instance index for constants)
    float3 Barycentric;     // UNORM, 32 bits compressed, possibly less
    float3 dBarycentric_dx;
    float3 dBarycentric_dy;
    float3 ViewNormal;      // UNORM, 2 * 15 + 2 = 32 bits compressed
    float3 DiffuseColor;
};
// Say, 172 bits compressed.

GBufferSample GetGBufferSample(uint visibility, float3 viewPos)
{
    GBufferSample result;

    uint     tri         = visibility & 0xFFFFF;
    uint     instanceId  = (visibility >> 20) - 1;
    uint     modelIndex  = instanceList[instanceId].ModelIndex;
    float4x4 modelToView = instanceList[instanceId].View;

    uint3 i = indices[modelIndex].Load(tri);
    uint materialIndex = triangleParams[modelIndex][tri].MaterialIndex;
    result.DiffuseColor = materials[materialIndex].DiffuseColor;
    uint diffuseTextureId = materials[materialIndex].DiffuseTextureId;
    //float3 AmbientColor;
    //float3 DiffuseColor;
    //float3 SpecularColor;
    //float  SpecularExponent;
    //uint   DiffuseTextureId;


    result.Barycentric = GetBarycentric(modelIndex, i, modelToView, viewPos);

    float3 normal0 = vertexAttributes[modelIndex][i[0]].Normal;
    float3 normal1 = vertexAttributes[modelIndex][i[1]].Normal;
    float3 normal2 = vertexAttributes[modelIndex][i[2]].Normal;

    result.ViewPos    = viewPos;
    result.ViewNormal = normalize(mul(normalize(normal0 * result.Barycentric[0] + normal1 * result.Barycentric[1] + normal2 * result.Barycentric[2]), (float3x3)modelToView));

    float2 sPos    = (viewPos.xy / viewPos.z) * sceneConstants.ProjectionScale + sceneConstants.ProjectionBias;
    float2 sPos_dx = sPos + float2(1.0f, 0.0f);
    float2 sPos_dy = sPos + float2(0.0f, 1.0f);

    float3 zViewPos_dx = float3((sPos_dx * sceneConstants.InverseProjectionScale + sceneConstants.InverseProjectionBias) * viewPos.z, viewPos.z);
    float3 zViewPos_dy = float3((sPos_dy * sceneConstants.InverseProjectionScale + sceneConstants.InverseProjectionBias) * viewPos.z, viewPos.z);

    float3 viewPos_dx = zViewPos_dx * (dot(viewPos, result.ViewNormal) / dot(zViewPos_dx, result.ViewNormal));
    float3 viewPos_dy = zViewPos_dy * (dot(viewPos, result.ViewNormal) / dot(zViewPos_dy, result.ViewNormal));

    float3 barycentric_dx = GetBarycentric(modelIndex, i, modelToView, viewPos_dx);
    float3 barycentric_dy = GetBarycentric(modelIndex, i, modelToView, viewPos_dy);

    result.dBarycentric_dx = barycentric_dx - result.Barycentric;
    result.dBarycentric_dy = barycentric_dy - result.Barycentric;

    if (diffuseTextureId != 0)
    {
        float2 uv0 = vertexAttributes[modelIndex][i[0]].UV;
        float2 uv1 = vertexAttributes[modelIndex][i[1]].UV;
        float2 uv2 = vertexAttributes[modelIndex][i[2]].UV;
        float2 uv =
            uv0 * result.Barycentric[0] +
            uv1 * result.Barycentric[1] +
            uv2 * result.Barycentric[2];
        float2 ddx =
            uv0 * result.dBarycentric_dx[0] +
            uv1 * result.dBarycentric_dx[1] +
            uv2 * result.dBarycentric_dx[2];
        float2 ddy =
            uv0 * result.dBarycentric_dy[0] +
            uv1 * result.dBarycentric_dy[1] +
            uv2 * result.dBarycentric_dy[2];

        result.DiffuseColor *= textures[diffuseTextureId - 1].SampleGrad(defaultSampler, uv, ddx, ddy).rgb;
    }

    return result;
}

    //uint color = triangleParams[tri].Color;
    //return float4(
    //    ( color        & 0xFF) / 255.0f,
    //    ((color >>  8) & 0xFF) / 255.0f,
    //    ((color >> 16) & 0xFF) / 255.0f,
    //    ((color >> 24) & 0xFF) / 255.0f);


[numthreads(64, 16, 1)]
void ColorPass(uint3 pos : SV_DispatchThreadID)
{
    float3 color = 0;

    uint v = GetVisibility(pos.xy);
    if (v == 0)
    {
        color = float3(0.1, 0, 0);
    }
    else
    {
        float3 viewPos = GetViewPos(pos.xy);

        GBufferSample gSample = GetGBufferSample(v, viewPos);

        float3 albedo = gSample.DiffuseColor;

        //albedo *= gSample.Barycentric;

        color = float3(albedo * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))));
    }

    uint2 spos = pos.xy;
    float4 posColor = 1;//float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);
    //float4 posColor = float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);

    colorUav[spos.xy] = posColor * float4(color, 1);
}


[numthreads(8, 8, 1)]
void Postprocess(uint3 pos : SV_DispatchThreadID)
{
    //uint v = visibilityMap[pos.xy];
    //
    //if ((v >> 20) == 0)
    //{
    //    colorUav[pos.xy] = float4(0.3, 0, 0, 1);
    //}
}




Texture2D<float4> colorMap : register(t2, space0);

float4 ResolveVS(uint index : SV_VertexID) : SV_Position
{
    float4 position;

    position.x = index == 0 ? -3 : 1;
    position.y = index == 2 ? -3 : 1;
    position.z = 0.5;
    position.w = 1;

    return position;
}

float4 ResolvePS(float4 position : SV_Position) : SV_Target
{
    uint2 pos = floor(position.xy);

    float4 pixelColor = colorMap[pos];

    return pixelColor;
}
