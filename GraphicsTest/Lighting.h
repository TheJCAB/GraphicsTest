#pragma once

#if HLSL

#include "HLSL.h"

#include "ViewSpace.h"

float3 SampleSkymap(ViewSpaceConstants viewSpace, Texture2D<float4> map, float3 viewDir)
{
    float3 worldDir = mul(viewDir, (float3x3)viewSpace.WorldToView);

    float x = atan2(worldDir.x, -worldDir.z) / (2.0f * 3.141592f) + 0.5f;
    float y = asin(worldDir.y)               /         3.141592f  + 0.5f;

    return map.SampleLevel(wrapSampler, float2(x, y), 0).xyz;
}

float3 SampleSkymapGrad(ViewSpaceConstants viewSpace, Texture2D<float4> map, float3 viewDir, float2 ddx, float2 ddy)
{
    float3 worldDir = mul(viewDir, (float3x3)viewSpace.WorldToView);

    float x = atan2(worldDir.x, -worldDir.z) / (2.0f * 3.141592f) + 0.5f;
    float y = asin(worldDir.y)               /         3.141592f  + 0.5f;

    return map.SampleGrad(wrapSampler, float2(x, y), ddx, ddy).xyz;
}

struct GBufferSample
{
    float3            ViewPos;
    BarycentricCoords Barycentric;
    float3            ViewNormal;
    float3            DiffuseColor;
    float3            SpecularColor;
    float             Alpha2;
};

GBufferSample GetGBufferSample(ViewSpaceConstants viewSpace, uint visibility, float3 viewPos)
{
    GBufferSample result;

    uint     tri         = visibility & 0xFFFFF;
    uint     instanceId  = (visibility >> 20) - 1;
    uint     modelIndex  = instanceList[instanceId].ModelIndex;
    float4x4 modelToView = instanceList[instanceId].View;

    uint3 i             = indices[modelIndex].Load(tri);
    uint  materialIndex = triangleParams[modelIndex][tri].MaterialIndex;

    result.DiffuseColor    = materials[materialIndex].DiffuseColor;
    result.SpecularColor   = materials[materialIndex].SpecularColor;
    result.Alpha2          = 2.0f / (materials[materialIndex].SpecularExponent + 2.0f);
    uint diffuseTextureId  = materials[materialIndex].DiffuseTextureId;
    uint bumpTextureId     = materials[materialIndex].BumpTextureId;
    uint specularTextureId = materials[materialIndex].SpecularTextureId;

    result.Barycentric = GetBarycentric(viewSpace, vertexPositions[modelIndex], i, modelToView, viewPos);

    float3 normal0 = vertexAttributes[modelIndex][i[0]].Normal;
    float3 normal1 = vertexAttributes[modelIndex][i[1]].Normal;
    float3 normal2 = vertexAttributes[modelIndex][i[2]].Normal;

    result.ViewPos    = viewPos;
    result.ViewNormal = normalize(mul(Interpolate(normal0, normal1, normal2, result.Barycentric.B), (float3x3)modelToView));

    if ((diffuseTextureId | specularTextureId | bumpTextureId) != 0)
    {
        float2 uv0 = vertexAttributes[modelIndex][i[0]].UV;
        float2 uv1 = vertexAttributes[modelIndex][i[1]].UV;
        float2 uv2 = vertexAttributes[modelIndex][i[2]].UV;
        float2 uv  = Interpolate(uv0, uv1, uv2, result.Barycentric.B);
        float2 ddx = Interpolate(uv0, uv1, uv2, result.Barycentric.dB_dx);
        float2 ddy = Interpolate(uv0, uv1, uv2, result.Barycentric.dB_dy);

        if (diffuseTextureId != 0)
        {
            result.DiffuseColor = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv, ddx, ddy).rgb;
        }

        if (specularTextureId != 0)
        {
            result.SpecularColor = textures[specularTextureId - 1].SampleGrad(wrapSampler, uv, ddx, ddy).rgb;
        }

        if (bumpTextureId != 0)
        {
            float2 bump = textures[bumpTextureId - 1].SampleGrad(wrapSampler, uv, ddx, ddy).xy - 0.5f;
            float du_dSx = ddx.x;
            float du_dSy = ddy.x;
            float dv_dSx = ddx.y;
            float dv_dSy = ddy.y;

            //float dN_du = bump.x;
            //float dN_dv = bump.y;
            //float2 dN_dS = float2(dN_du * du_dSx + dN_dv * dv_dSx, dN_du * du_dSy + dN_dv * dv_dSy);

            float3 dViewPos_dx = result.Barycentric.dViewPos_dx;
            float3 dViewPos_dy = result.Barycentric.dViewPos_dy;

            float3 tangent   = normalize(dViewPos_dx * du_dSx + dViewPos_dy * du_dSy);
            //float3 cotangent = normalize(dViewPos_dx / abs(dv_dSx) + dViewPos_dy / abs(dv_dSy));
            float3 cotangent = cross(result.ViewNormal, tangent);
            //tangent   = cross(cotangent, result.ViewNormal);

            //float3 dN = / dN_dS.x + result.Barycentric.dViewPos_dy / dN_dS.y;

            result.ViewNormal = normalize(result.ViewNormal * sqrt(1.0f - dot(bump.xy, bump.xy)) + tangent * bump.x + cotangent * bump.y);
        }
    }

    return result;
}

float3 LightPixel(ViewSpaceConstants viewSpace, GBufferSample gSample)
{
    float3 albedo = gSample.DiffuseColor;

    //albedo *= gSample.Barycentric;

    float3 normalizedViewPos = normalize(gSample.ViewPos);

    float3 H = gSample.ViewNormal;
    float reflectionCosine = max(0.0f, dot(H, -normalizedViewPos));

    float3 F0 = gSample.SpecularColor; //float3(0.03f, 0.03f, 0.03f);
    float3 F = F0 + (1.0 - F0) * pow(1.0 - reflectionCosine, 5.0);

    float3 reflectViewPos = normalizedViewPos - 2.0f * dot(normalizedViewPos, gSample.ViewNormal) * gSample.ViewNormal;

    float roughness = sqrt(sqrt(gSample.Alpha2));
    float2 specularBrdf = textures[3].SampleLevel(clampSampler, float2(reflectionCosine, roughness), 0).xy;

    float3 specularLight = (F * specularBrdf.x + specularBrdf.y) * SampleSkymapGrad(viewSpace, textures[2], reflectViewPos, float2(0.5f, 0.25f) * gSample.Alpha2, float2(0.5f, 0.25f) * gSample.Alpha2).xyz;

    float3 diffuseLight = albedo * SampleSkymap(viewSpace, textures[1], gSample.ViewNormal).xyz;

    return diffuseLight * (1.0f - F) + specularLight;
}


struct GBufferSample4
{
    float3            ViewPos;
    BarycentricCoords Barycentric;
    float3            ViewNormal;
    float4x3          DiffuseColor;
    float3            SpecularColor;
    float             Alpha2;
};

GBufferSample4 GetGBufferSample4(ViewSpaceConstants viewSpace, uint visibility, float3 viewPos, float4x2 screenSpacePixelOffsets)
{
    GBufferSample4 result;

    uint     tri         = visibility & 0xFFFFF;
    uint     instanceId  = (visibility >> 20) - 1;
    uint     modelIndex  = instanceList[instanceId].ModelIndex;
    float4x4 modelToView = instanceList[instanceId].View;

    uint3 i             = indices[modelIndex].Load(tri);
    uint  materialIndex = triangleParams[modelIndex][tri].MaterialIndex;

    result.DiffuseColor[0] =
    result.DiffuseColor[1] =
    result.DiffuseColor[2] =
    result.DiffuseColor[3] = materials[materialIndex].DiffuseColor;
    result.SpecularColor   = materials[materialIndex].SpecularColor;
    result.Alpha2          = 2.0f / (materials[materialIndex].SpecularExponent + 2.0f);
    uint diffuseTextureId  = materials[materialIndex].DiffuseTextureId;
    uint bumpTextureId     = materials[materialIndex].BumpTextureId;
    uint specularTextureId = materials[materialIndex].SpecularTextureId;

    result.Barycentric = GetBarycentric(viewSpace, vertexPositions[modelIndex], i, modelToView, viewPos);

    float3 normal0 = vertexAttributes[modelIndex][i[0]].Normal;
    float3 normal1 = vertexAttributes[modelIndex][i[1]].Normal;
    float3 normal2 = vertexAttributes[modelIndex][i[2]].Normal;

    result.ViewPos    = viewPos;
    result.ViewNormal = normalize(mul(Interpolate(normal0, normal1, normal2, result.Barycentric.B), (float3x3)modelToView));

    if ((diffuseTextureId | specularTextureId | bumpTextureId) != 0)
    {
        float2 uv0 = vertexAttributes[modelIndex][i[0]].UV;
        float2 uv1 = vertexAttributes[modelIndex][i[1]].UV;
        float2 uv2 = vertexAttributes[modelIndex][i[2]].UV;
        float2 uv  = Interpolate(uv0, uv1, uv2, result.Barycentric.B);
        float2 ddx = Interpolate(uv0, uv1, uv2, result.Barycentric.dB_dx);
        float2 ddy = Interpolate(uv0, uv1, uv2, result.Barycentric.dB_dy);

        if (diffuseTextureId != 0)
        {
            result.DiffuseColor[0] = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv + ddx * screenSpacePixelOffsets[0].x + ddy * screenSpacePixelOffsets[0].y, ddx, ddy).rgb;
            result.DiffuseColor[1] = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv + ddx * screenSpacePixelOffsets[1].x + ddy * screenSpacePixelOffsets[1].y, ddx, ddy).rgb;
            result.DiffuseColor[2] = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv + ddx * screenSpacePixelOffsets[2].x + ddy * screenSpacePixelOffsets[2].y, ddx, ddy).rgb;
            result.DiffuseColor[3] = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv + ddx * screenSpacePixelOffsets[3].x + ddy * screenSpacePixelOffsets[3].y, ddx, ddy).rgb;
        }

        if (specularTextureId != 0)
        {
            result.SpecularColor = textures[specularTextureId - 1].SampleGrad(wrapSampler, uv, ddx, ddy).rgb;
        }

        if (bumpTextureId != 0)
        {
            float2 bump = textures[bumpTextureId - 1].SampleGrad(wrapSampler, uv, ddx, ddy).xy - 0.5f;
            float du_dSx = ddx.x;
            float du_dSy = ddy.x;
            float dv_dSx = ddx.y;
            float dv_dSy = ddy.y;

            //float dN_du = bump.x;
            //float dN_dv = bump.y;
            //float2 dN_dS = float2(dN_du * du_dSx + dN_dv * dv_dSx, dN_du * du_dSy + dN_dv * dv_dSy);

            float3 dViewPos_dx = result.Barycentric.dViewPos_dx;
            float3 dViewPos_dy = result.Barycentric.dViewPos_dy;

            float3 tangent   = normalize(dViewPos_dx * du_dSx + dViewPos_dy * du_dSy);
            //float3 cotangent = normalize(dViewPos_dx / abs(dv_dSx) + dViewPos_dy / abs(dv_dSy));
            float3 cotangent = cross(result.ViewNormal, tangent);
            //tangent   = cross(cotangent, result.ViewNormal);

            //float3 dN = / dN_dS.x + result.Barycentric.dViewPos_dy / dN_dS.y;

            result.ViewNormal = normalize(result.ViewNormal * sqrt(1.0f - dot(bump.xy, bump.xy)) + tangent * bump.x + cotangent * bump.y);
        }
    }

    return result;
}

float4x3 LightPixel4(ViewSpaceConstants viewSpace, GBufferSample4 gSample)
{
    float3 normalizedViewPos = normalize(gSample.ViewPos);

    float3 H = gSample.ViewNormal;
    float reflectionCosine = max(0.0f, dot(H, -normalizedViewPos));

    float3 F0 = gSample.SpecularColor; //float3(0.03f, 0.03f, 0.03f);
    float3 F = F0 + (1.0 - F0) * pow(1.0 - reflectionCosine, 5.0);

    float3 reflectViewPos = normalizedViewPos - 2.0f * dot(normalizedViewPos, gSample.ViewNormal) * gSample.ViewNormal;

    float roughness = sqrt(sqrt(gSample.Alpha2));
    float2 specularBrdf = textures[3].SampleLevel(clampSampler, float2(reflectionCosine, roughness), 0).xy;

    float3 specularLight = (F * specularBrdf.x + specularBrdf.y) * SampleSkymapGrad(viewSpace, textures[2], reflectViewPos, float2(0.5f, 0.25f) * gSample.Alpha2, float2(0.5f, 0.25f) * gSample.Alpha2).xyz;

    float3 diffuseLight = SampleSkymap(viewSpace, textures[1], gSample.ViewNormal).xyz * (1.0f - F);

    return float4x3(
        gSample.DiffuseColor[0] * diffuseLight + specularLight,
        gSample.DiffuseColor[1] * diffuseLight + specularLight,
        gSample.DiffuseColor[2] * diffuseLight + specularLight,
        gSample.DiffuseColor[3] * diffuseLight + specularLight
    );
}

#endif // HLSL
