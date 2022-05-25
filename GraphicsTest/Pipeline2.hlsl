
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

uint NumPixelsX() { return sceneConstants.ViewSpace.Size.x                                  ; }
uint NumPixelsY() { return sceneConstants.ViewSpace.Size.y                                  ; }
uint NumPixels () { return sceneConstants.ViewSpace.Size.x * sceneConstants.ViewSpace.Size.y; }



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

    float2 sPos    = (viewPos.xy / viewPos.z) * sceneConstants.OffsetedProjectionScale + sceneConstants.OffsetedProjectionBias;
    float2 sPos_dx = sPos + float2(1.0f, 0.0f);
    float2 sPos_dy = sPos + float2(0.0f, 1.0f);

    float3 zViewPos_dx = float3((sPos_dx * sceneConstants.InverseOffsetedProjectionScale + sceneConstants.InverseOffsetedProjectionBias) * viewPos.z, viewPos.z);
    float3 zViewPos_dy = float3((sPos_dy * sceneConstants.InverseOffsetedProjectionScale + sceneConstants.InverseOffsetedProjectionBias) * viewPos.z, viewPos.z);

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
    uint visibility = GetVisibility(pos.xy);
    float4 color = colorSrv[pos.xy];
    if (visibility >> 20 == 0)
    {
        // Nothing.
        taaColorUav[pos.xy] = color;
        return;
    }
    else
    {
        uint instanceIndex  = (visibility >> 20) - 1;

        float3 lastFrameViewPos = mul(float4(GetCenterViewPos(pos.xy), 1), instanceList[instanceIndex].InverseViewAndLastFrameView).xyz;
        float2 lastFramePos = (lastFrameViewPos.xy / lastFrameViewPos.z) * (-0.5f * sceneConstants.ViewSpace.InverseFlipAndAspectRatio) + 0.5f; //CenterProjectionScale + sceneConstants.CenterProjectionBias;
        if (lastFramePos.x < 0 || lastFramePos.y < 0 || lastFramePos.x >= 1 || lastFramePos.y >= 1) //lastFramePos.x >= sceneConstants.Size.x || lastFramePos.y >= sceneConstants.Size.y)
        {
            // Don't sample from outside the frame.
            taaColorUav[pos.xy] = color;
            return;
        }
        //lastFramePos = floor((lastFrameViewPos.xy / lastFrameViewPos.z) * sceneConstants.ProjectionScale + sceneConstants.ProjectionBias);
        //lastFramePos = floor((lastFrameViewPos.xy / lastFrameViewPos.z) * sceneConstants.LastFrameProjectionScale + sceneConstants.LastFrameProjectionBias);

        //float depth    = depthMap[pos.xy              ].x;
        //float depth0   = depthMap[pos.xy + uint2(1, 0)].x;
        //float depth1   = depthMap[pos.xy - uint2(1, 0)].x;
        //float depth2   = depthMap[pos.xy + uint2(0, 1)].x;
        //float depth3   = depthMap[pos.xy - uint2(0, 1)].x;
        //float depthMin = min(min(depth, depth0), min(min(depth1, depth2), depth3));
        //float depthMax = max(max(depth, depth0), max(max(depth1, depth2), depth3));
        //float previousDepth = previousDepthSrv.SampleLevel(defaultSampler, lastFramePos, 0).x;
        //if (previousDepth < depthMin || previousDepth > depthMax)
        //{
        //    // Reject if the depth doesn't correspond.
        //    taaColorUav[pos.xy] = color;
        //    return;
        //}

        float3 colorYCog  = mul(color.xyz           , RgbToYCog);
        float3 colorYCog0 = mul(colorSrv[pos.xy + uint2(1, 0)].xyz, RgbToYCog);
        float3 colorYCog1 = mul(colorSrv[pos.xy - uint2(1, 0)].xyz, RgbToYCog);
        float3 colorYCog2 = mul(colorSrv[pos.xy + uint2(0, 1)].xyz, RgbToYCog);
        float3 colorYCog3 = mul(colorSrv[pos.xy - uint2(0, 1)].xyz, RgbToYCog);
        float3 colorYCog4 = mul(colorSrv[pos.xy + uint2(1, 1)].xyz, RgbToYCog);
        float3 colorYCog5 = mul(colorSrv[pos.xy - uint2(1, 1)].xyz, RgbToYCog);
        float3 colorYCog6 = mul(colorSrv[pos.xy + uint2(-1, 1)].xyz, RgbToYCog);
        float3 colorYCog7 = mul(colorSrv[pos.xy - uint2(-1, 1)].xyz, RgbToYCog);
        float3 colorYCogMin = min(min(colorYCog, colorYCog0), min(min(colorYCog1, colorYCog2), min(min(colorYCog3, min(colorYCog4, colorYCog5)), min(colorYCog6, colorYCog7))));
        float3 colorYCogMax = max(max(colorYCog, colorYCog0), max(max(colorYCog1, colorYCog2), min(max(colorYCog3, max(colorYCog4, colorYCog5)), max(colorYCog6, colorYCog7))));

        float3 previousColor = SampleTextureCatmullRom(previousTaaColorSrv, defaultSampler, lastFramePos, sceneConstants.ViewSpace.Size).xyz;
        //float3 previousColor = previousTaaColorSrv.SampleLevel(defaultSampler, lastFramePos, 0).xyz;
        float3 previousColorYCog = mul(previousColor, RgbToYCog);
        if (previousColorYCog.x < colorYCogMin.x)
        {
            previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMin.x - colorYCog.x) / (previousColorYCog.x - colorYCog.x);
        }
        if (previousColorYCog.y < colorYCogMin.y)
        {
            previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMin.y - colorYCog.y) / (previousColorYCog.y - colorYCog.y);
        }
        if (previousColorYCog.z < colorYCogMin.z)
        {
            previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMin.z - colorYCog.z) / (previousColorYCog.z - colorYCog.z);
        }

        if (previousColorYCog.x > colorYCogMax.x)
        {
            previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMax.x - colorYCog.x) / (previousColorYCog.x - colorYCog.x);
        }
        if (previousColorYCog.y > colorYCogMax.y)
        {
            previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMax.y - colorYCog.y) / (previousColorYCog.y - colorYCog.y);
        }
        if (previousColorYCog.z > colorYCogMax.z)
        {
            previousColorYCog = colorYCog + (previousColorYCog - colorYCog) * (colorYCogMax.z - colorYCog.z) / (previousColorYCog.z - colorYCog.z);
        }
        previousColor = mul(previousColorYCog, YCogToRgb);

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
            taaColorUav[pos.xy] = float4(previousColor, 1);
        }
        else
        {
            taaColorUav[pos.xy] = float4(previousColor * TaaBlendFactor + color.xyz * (1.0f - TaaBlendFactor), 1);
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

    return pixelColor;
}
