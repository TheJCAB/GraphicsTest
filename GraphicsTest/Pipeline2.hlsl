
#include "ViewSpace.lib.hlsl"

#include "Pipeline2.h"
#include "Lighting.h"

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
        float3 viewDir = normalize(ScreenPosToViewPos(pos.xy + 0.5f, -1.0f));
        color = SampleSkymap(sceneConstants.ViewSpace, textures[0], viewDir).xyz;
    }
    else
    {
        float3 viewPos = GetViewPos(pos.xy);

        GBufferSample gSample = GetGBufferSample(sceneConstants.ViewSpace, v, viewPos);

        color = LightPixel(sceneConstants.ViewSpace, gSample);
    }

    uint2 spos = pos.xy;
    float4 posColor = 1;//float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);
    //float4 posColor = float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);

    colorUav[spos.xy] = posColor * float4(color, 1);
}

static const float TaaBlendFactor = 0.9f;

[numthreads(8, 8, 1)]
void Postprocess(uint3 pos : SV_DispatchThreadID)
{
    float4 color = float4(colorSrv[pos.xy].xyz, 0.0f);

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

        float4 previousColor = SampleTextureCatmullRom(previousTaaColorSrv, wrapSampler, lastFramePos, sceneConstants.ViewSpace.Size);
        //float3 previousColor = previousTaaColorSrv.SampleLevel(wrapSampler, lastFramePos, 0).xyz;

//#if 0
        uint2 pos0 = pos.xy + uint2( 1, 0);
        uint2 pos1 = pos.xy - uint2( 1, 0);
        uint2 pos2 = pos.xy + uint2( 0, 1);
        uint2 pos3 = pos.xy - uint2( 0, 1);
        uint2 pos4 = pos.xy + uint2( 1, 1);
        uint2 pos5 = pos.xy - uint2( 1, 1);
        uint2 pos6 = pos.xy + uint2(-1, 1);
        uint2 pos7 = pos.xy - uint2(-1, 1);

        // Reject history when the depth just doesn't match.
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

        // Adjust history's color so it matches the current frame's neighborhood.
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

        float3 previousColorYCog = mul(previousColor.xyz, RgbToYCog);
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
        taaBlendFactor *= 1.0f - clamp(alpha * previousColor.a, 0.0f, 1.0f); // Reduce the blend factor to forget longer history when colors differ.
        previousColor.xyz = mul(previousColorYCog, YCogToRgb);

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
            taaColorUav[pos.xy] = float4(previousColor.xyz, alpha);
        }
        else
        {
            taaColorUav[pos.xy] = float4(previousColor * taaBlendFactor + float4(color.xyz * (1.0f - taaBlendFactor), 1.0f));
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
    }
    else
    {
        return float4(pixelColor.rgb, 1);
    }
}
