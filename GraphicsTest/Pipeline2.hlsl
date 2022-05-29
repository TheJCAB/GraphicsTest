
#include "ViewSpace.lib.hlsl"

#include "Pipeline2.h"

ConstantBuffer<SceneConstants> sceneConstants : register(b0);

Texture2D<uint>   visibilityMap : register(t1, space0);
Texture2D<float>  depthMap      : register(t2, space0);

RWTexture2D<float4> colorUav    : register(u0, space0);
RWTexture2D<float4> taaColorUav : register(u0, space0);

Texture2D<float4> colorSrv            : register(t3, space0);
Texture2D<float4> previousTaaColorSrv : register(t4, space0);
Texture2D<float4> previousDepthSrv    : register(t5, space0);



uint GetVisibility(uint2 pos)
{
    return visibilityMap[pos];
}

float GetDepth(uint2 pos)
{
    return depthMap[pos];
}

float DepthToViewDepth(float depth)
{
    return -0.0001f / depth;
}

float GetViewDepth(uint2 pos)
{
    return DepthToViewDepth(GetDepth(pos));
}

float3 ScreenPosToViewPos(float2 sPos, float viewPosZ)
{
    return float3((sPos * sceneConstants.ViewSpace.InverseProjectionScale + sceneConstants.ViewSpace.InverseProjectionBias) * viewPosZ, viewPosZ);
}

float3 GetViewPos(uint2 pos)
{
    float depth = GetViewDepth(pos);
    return float3(((pos + 0.5f) * sceneConstants.InverseOffsetedProjectionScale + sceneConstants.InverseOffsetedProjectionBias) * depth, depth);
}

float3 GetCenterViewPos(uint2 pos)
{
    float depth = GetViewDepth(pos);
    return GetPixelCenterViewPos(sceneConstants.ViewSpace, pos, depth);
}

uint NumPixelsX() { return sceneConstants.ViewSpace.Size.x                                  ; }
uint NumPixelsY() { return sceneConstants.ViewSpace.Size.y                                  ; }
uint NumPixels () { return sceneConstants.ViewSpace.Size.x * sceneConstants.ViewSpace.Size.y; }



struct GBufferSample
{
    float3            ViewPos;
    BarycentricCoords Barycentric;
    float3            ViewNormal;
    float3            DiffuseColor;
    float3            SpecularColor;
    float             Alpha2;
};

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
    result.SpecularColor = materials[materialIndex].SpecularColor;
    result.Alpha2 = materials[materialIndex].SpecularExponent;
    uint diffuseTextureId = materials[materialIndex].DiffuseTextureId;
    uint bumpTextureId = materials[materialIndex].BumpTextureId;
    uint specularTextureId = materials[materialIndex].SpecularTextureId;
    //float3 AmbientColor;
    //float3 DiffuseColor;
    //float3 SpecularColor;
    //float  SpecularExponent;
    //uint   DiffuseTextureId;

    result.Alpha2 = 2.0f / (result.Alpha2 + 2.0f);

    result.Barycentric = GetBarycentric(sceneConstants.ViewSpace, vertexPositions[modelIndex], i, modelToView, viewPos);

    float3 normal0 = vertexAttributes[modelIndex][i[0]].Normal;
    float3 normal1 = vertexAttributes[modelIndex][i[1]].Normal;
    float3 normal2 = vertexAttributes[modelIndex][i[2]].Normal;

    result.ViewPos    = viewPos;
    result.ViewNormal = normalize(mul(Interpolate(normal0, normal1, normal2, result.Barycentric.B), (float3x3)modelToView));

    float3 Tangent;

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

    //uint color = triangleParams[tri].Color;
    //return float4(
    //    ( color        & 0xFF) / 255.0f,
    //    ((color >>  8) & 0xFF) / 255.0f,
    //    ((color >> 16) & 0xFF) / 255.0f,
    //    ((color >> 24) & 0xFF) / 255.0f);

float3 SampleSkymap(Texture2D<float4> map, float3 viewDir)
{
    float3 worldDir = mul(viewDir, (float3x3)sceneConstants.ViewSpace.WorldToView);

    float x = atan2(worldDir.x, -worldDir.z) / (2.0f * 3.141592f) + 0.5f;
    float y = asin(worldDir.y)               /         3.141592f  + 0.5f;

    return map.SampleLevel(wrapSampler, float2(x, y), 0).xyz;
}

float3 SampleSkymapGrad(Texture2D<float4> map, float3 viewDir, float2 ddx, float2 ddy)
{
    float3 worldDir = mul(viewDir, (float3x3)sceneConstants.ViewSpace.WorldToView);

    float x = atan2(worldDir.x, -worldDir.z) / (2.0f * 3.141592f) + 0.5f;
    float y = asin(worldDir.y)               /         3.141592f  + 0.5f;

    return map.SampleGrad(wrapSampler, float2(x, y), ddx, ddy).xyz;
}

[numthreads(64, 16, 1)]
void ColorPass(uint3 pos : SV_DispatchThreadID)
{
    float3 color = 0;

    float3 viewDir = normalize(ScreenPosToViewPos(pos.xy + 0.5f, -1.0f));

    uint v = GetVisibility(pos.xy);
    if (v == 0)
    {
        color = SampleSkymap(textures[0], viewDir).xyz;
    }
    else
    {
        float3 viewPos = GetViewPos(pos.xy);

        GBufferSample gSample = GetGBufferSample(v, viewPos);

        float3 albedo = gSample.DiffuseColor;

        //albedo *= gSample.Barycentric;

        float3 normalizedViewPos = normalize(viewPos);

        float3 H = gSample.ViewNormal;
        float reflectionCosine = max(0.0f, dot(H, -normalizedViewPos));

        float3 F0 = gSample.SpecularColor; //float3(0.03f, 0.03f, 0.03f);
        float3 F = F0 + (1.0 - F0) * pow(1.0 - reflectionCosine, 5.0);

        float3 reflectViewPos = normalizedViewPos - 2.0f * dot(normalizedViewPos, gSample.ViewNormal) * gSample.ViewNormal;

        float roughness = sqrt(sqrt(gSample.Alpha2));
        float2 specularBrdf = textures[3].SampleLevel(clampSampler, float2(reflectionCosine, roughness), 0).xy;

        float3 specularLight = (F * specularBrdf.x + specularBrdf.y) * SampleSkymapGrad(textures[2], reflectViewPos, float2(0.5f, 0.25f) * roughness, float2(0.5f, 0.25f) * roughness).xyz;


        float3 diffuseLight = albedo * (1.0f - F) * SampleSkymap(textures[1], gSample.ViewNormal).xyz;


        color = diffuseLight + specularLight;

        //color = albedo * light; //dot(gSample.ViewNormal, normalize(float3(1, 1, 1))));
    }

    uint2 spos = pos.xy;
    float4 posColor = 1;//float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);
    //float4 posColor = float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);

    colorUav[spos.xy] = posColor * float4(color, 1);
}

static const float TaaBlendFactor = 0.9f;

static const float3x3 RgbToYCog = float3x3(
    float3( 0.25f, 0.5f,  0.25f),
    float3( 0.5f , 0.0f, -0.5f ),
    float3(-0.25f, 0.5f, -0.25f)
);

static const float3x3 YCogToRgb = float3x3(
    float3( 1.0f,  1.0f, -1.0f),
    float3( 1.0f,  0.0f,  1.0f),
    float3( 1.0f, -1.0f, -1.0f)
);

// Taken from: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
// The following code is licensed under the MIT license: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
// Samples a texture with Catmull-Rom filtering, using 9 texture fetches instead of 16.
// See http://vec3.ca/bicubic-filtering-in-fewer-taps/ for more details
float4 SampleTextureCatmullRom(in Texture2D<float4> tex, in SamplerState linearSampler, in float2 uv, in float2 texSize)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0 = texPos1 - 1;
    float2 texPos3 = texPos1 + 2;
    float2 texPos12 = texPos1 + offset12;

    texPos0 /= texSize;
    texPos3 /= texSize;
    texPos12 /= texSize;

    float4 result = 0.0f;
    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

    result += tex.SampleLevel(linearSampler, float2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
    result += tex.SampleLevel(linearSampler, float2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

    return result;
}

[numthreads(8, 8, 1)]
void Postprocess(uint3 pos : SV_DispatchThreadID)
{
    float4 color = colorSrv[pos.xy];

#if 0
    taaColorUav[pos.xy] = color;

    if (sceneConstants.ForgetCurrentFrame != 0)
    {
        uint visibility = GetVisibility(pos.xy);
        if (visibility >> 20 == 0)
        {
            // Nothing.
            taaColorUav[pos.xy] = color;
            return;
        }
        uint instanceIndex  = (visibility >> 20) - 1;
        float3 viewPos = GetViewPos(pos.xy);
        float3 lastFrameViewPos = mul(float4(viewPos, 1), instanceList[instanceIndex].InverseViewAndLastFrameView).xyz;
        float2 lastFramePos = (lastFrameViewPos.xy / lastFrameViewPos.z) * (-0.5f * sceneConstants.ViewSpace.InverseFlipAndAspectRatio) + 0.5f - sceneConstants.NormalizedPixelOffset;
        if (lastFramePos.x < 0 || lastFramePos.y < 0 || lastFramePos.x >= 1 || lastFramePos.y >= 1) //lastFramePos.x >= sceneConstants.Size.x || lastFramePos.y >= sceneConstants.Size.y)
        {
            // Don't sample from outside the frame.
            taaColorUav[pos.xy] = color;
            return;
        }

        taaColorUav[pos.xy] = SampleTextureCatmullRom(previousTaaColorSrv, wrapSampler, lastFramePos, sceneConstants.ViewSpace.Size);
    }
#endif

    uint visibility = GetVisibility(pos.xy);
    if (visibility >> 20 == 0)
    {
        // Nothing.
        taaColorUav[pos.xy] = color;
        return;
    }
    else
    {
        uint instanceIndex  = (visibility >> 20) - 1;

        float3 viewPos = GetViewPos(pos.xy);
        float3 lastFrameViewPos = mul(float4(viewPos, 1), instanceList[instanceIndex].InverseViewAndLastFrameView).xyz;
        if (length(lastFrameViewPos - viewPos) > length(viewPos) * 0.1f)
        {
            // Don't sample things that have moved too far in view space.
            taaColorUav[pos.xy] = color;
            return;
        }
        float2 lastFramePos = (lastFrameViewPos.xy / lastFrameViewPos.z) * (-0.5f * sceneConstants.ViewSpace.InverseFlipAndAspectRatio) + 0.5f + sceneConstants.NormalizedPixelOffset;
        if (lastFramePos.x < 0 || lastFramePos.y < 0 || lastFramePos.x >= 1 || lastFramePos.y >= 1) //lastFramePos.x >= sceneConstants.Size.x || lastFramePos.y >= sceneConstants.Size.y)
        {
            // Don't sample from outside the frame.
            taaColorUav[pos.xy] = color;
            return;
        }

        float normalizedDistanceInPixels = length(lastFramePos * sceneConstants.ViewSpace.Size - pos.xy) / 2.0f;
        if (normalizedDistanceInPixels > 1.0f)
        {
            // Don't sample too far.
            taaColorUav[pos.xy] = color;
            return;
        }

        float taaBlendFactor = TaaBlendFactor * (1.0f - (normalizedDistanceInPixels * normalizedDistanceInPixels) * (normalizedDistanceInPixels * normalizedDistanceInPixels));

        float3 previousColor = SampleTextureCatmullRom(previousTaaColorSrv, wrapSampler, lastFramePos, sceneConstants.ViewSpace.Size).xyz;
        //float3 previousColor = previousTaaColorSrv.SampleLevel(wrapSampler, lastFramePos, 0).xyz;

#if 0
        uint2 pos0 = pos.xy + uint2( 1, 0);
        uint2 pos1 = pos.xy - uint2( 1, 0);
        uint2 pos2 = pos.xy + uint2( 0, 1);
        uint2 pos3 = pos.xy - uint2( 0, 1);
        uint2 pos4 = pos.xy + uint2( 1, 1);
        uint2 pos5 = pos.xy - uint2( 1, 1);
        uint2 pos6 = pos.xy + uint2(-1, 1);
        uint2 pos7 = pos.xy - uint2(-1, 1);

        float lastFrameViewPosZ0 = mul(float4(GetViewPos(pos0), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).z;
        float lastFrameViewPosZ1 = mul(float4(GetViewPos(pos1), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).z;
        float lastFrameViewPosZ2 = mul(float4(GetViewPos(pos2), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).z;
        float lastFrameViewPosZ3 = mul(float4(GetViewPos(pos3), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).z;
        float lastFrameViewPosZ4 = mul(float4(GetViewPos(pos4), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).z;
        float lastFrameViewPosZ5 = mul(float4(GetViewPos(pos5), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).z;
        float lastFrameViewPosZ6 = mul(float4(GetViewPos(pos6), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).z;
        float lastFrameViewPosZ7 = mul(float4(GetViewPos(pos7), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).z;
        float lastFrameViewPosZMin = min(min(lastFrameViewPos.z, lastFrameViewPosZ0), min(min(lastFrameViewPosZ1, lastFrameViewPosZ2), min(min(lastFrameViewPosZ3, min(lastFrameViewPosZ4, lastFrameViewPosZ5)), min(lastFrameViewPosZ6, lastFrameViewPosZ7))));
        float lastFrameViewPosZMax = max(max(lastFrameViewPos.z, lastFrameViewPosZ0), max(max(lastFrameViewPosZ1, lastFrameViewPosZ2), min(max(lastFrameViewPosZ3, max(lastFrameViewPosZ4, lastFrameViewPosZ5)), max(lastFrameViewPosZ6, lastFrameViewPosZ7))));

        float halfDepthRange = (lastFrameViewPosZMax - lastFrameViewPosZMin) / 2.0f;
        float previousDepth = DepthToViewDepth(previousDepthSrv.SampleLevel(wrapSampler, lastFramePos, 0).x);
        if (previousDepth + halfDepthRange < lastFrameViewPosZMin || previousDepth - halfDepthRange > lastFrameViewPosZMax)
        {
            // Reject if the depth doesn't correspond.
            taaColorUav[pos.xy] = color;
            return;
        }

        float3 colorYCog  = mul(color.xyz         , RgbToYCog);
        float3 colorYCog0 = mul(colorSrv[pos0].xyz, RgbToYCog);
        float3 colorYCog1 = mul(colorSrv[pos1].xyz, RgbToYCog);
        float3 colorYCog2 = mul(colorSrv[pos2].xyz, RgbToYCog);
        float3 colorYCog3 = mul(colorSrv[pos3].xyz, RgbToYCog);
        float3 colorYCog4 = mul(colorSrv[pos4].xyz, RgbToYCog);
        float3 colorYCog5 = mul(colorSrv[pos5].xyz, RgbToYCog);
        float3 colorYCog6 = mul(colorSrv[pos6].xyz, RgbToYCog);
        float3 colorYCog7 = mul(colorSrv[pos7].xyz, RgbToYCog);
        float3 colorYCogMin = min(min(colorYCog, colorYCog0), min(min(colorYCog1, colorYCog2), min(min(colorYCog3, min(colorYCog4, colorYCog5)), min(colorYCog6, colorYCog7))));
        float3 colorYCogMax = max(max(colorYCog, colorYCog0), max(max(colorYCog1, colorYCog2), min(max(colorYCog3, max(colorYCog4, colorYCog5)), max(colorYCog6, colorYCog7))));

        float3 previousColorYCog = mul(previousColor, RgbToYCog);
        float3 savedPreviousColorYCog = previousColorYCog;
        //previousColorYCog.yz = clamp(previousColorYCog.yz, colorYCogMin.yz, colorYCogMax.yz);
        //previousColorYCog = clamp(previousColorYCog, colorYCogMin, colorYCogMax);
        //if (previousColorYCog.x < colorYCogMin.x)
        //{
        //    previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMin.x - colorYCog.x) / (previousColorYCog.x - colorYCog.x);
        //}
        //if (previousColorYCog.y < colorYCogMin.y)
        //{
        //    previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMin.y - colorYCog.y) / (previousColorYCog.y - colorYCog.y);
        //}
        //if (previousColorYCog.z < colorYCogMin.z)
        //{
        //    previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMin.z - colorYCog.z) / (previousColorYCog.z - colorYCog.z);
        //}
        //
        //if (previousColorYCog.x > colorYCogMax.x)
        //{
        //    previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMax.x - colorYCog.x) / (previousColorYCog.x - colorYCog.x);
        //}
        //if (previousColorYCog.y > colorYCogMax.y)
        //{
        //    previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMax.y - colorYCog.y) / (previousColorYCog.y - colorYCog.y);
        //}
        //if (previousColorYCog.z > colorYCogMax.z)
        //{
        //    previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMax.z - colorYCog.z) / (previousColorYCog.z - colorYCog.z);
        //}
        float alpha = length(previousColorYCog - savedPreviousColorYCog);
        previousColor = mul(previousColorYCog, YCogToRgb);
#else
        float alpha = 0;
#endif
        //if (previousColorYCog.x < colorYCogMin.x || previousColorYCog.x > colorYCogMax.x ||
        //    previousColorYCog.y < colorYCogMin.y || previousColorYCog.y > colorYCogMax.y ||
        //    previousColorYCog.z < colorYCogMin.z || previousColorYCog.z > colorYCogMax.z)
        //{
        //    // Reject.
        //    taaColorUav[pos.xy] = color;
        //    return;
        //}

        if (sceneConstants.ForgetCurrentFrame != 0)
        {
            taaColorUav[pos.xy] = float4(previousColor, alpha);
        }
        else
        {
            taaColorUav[pos.xy] = float4(previousColor * taaBlendFactor + color.xyz * (1.0f - taaBlendFactor), alpha);
        }
    }
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

    if (sceneConstants.MarkMagenta != 0)
    {
        return pixelColor.a < 0.01f ? pixelColor : float4(1, 0, 1, 1);
        //return float4(float3(1, 0, 1) * pixelColor.a + colorMap[pos].rgb * (1.0f - pixelColor.a), 1);
    }
    else
    {
        return float4(pixelColor.rgb, 1);
    }
}
