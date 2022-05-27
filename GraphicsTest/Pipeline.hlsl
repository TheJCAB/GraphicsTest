
#include "Pipeline.h"

ConstantBuffer<SceneConstants> sceneConstants : register(b0);

Texture2DMS<uint>  visibilityMap : register(t1, space0);
Texture2DMS<float> depthMap      : register(t2, space0);


RWTexture2D<float4> colorUav : register(u0, space0);




#define PSamplesX 2
#define PSamplesY 2
#define PSamples  (PSamplesX * PSamplesY)
#define PSampleXY uint2(PSamplesX, PSamplesY)


uint2 GetSamplePixel(uint s)
{
    uint sPixel  = s / 8;
    return uint2(sPixel & (PSamplesX - 1), (sPixel / PSamplesX) & (PSamplesY - 1));
}

uint2 GetVisibilityPos(uint2 pos, uint s)
{
    uint sPixel  = s / 8;
    return pos * PSampleXY + uint2(sPixel & (PSamplesX - 1), (sPixel / PSamplesX) & (PSamplesY - 1));
}

uint GetVisibilitySample(uint s)
{
    return s & 7;
}

uint GetVisibility(uint2 pos, uint s)
{
    return visibilityMap.sample[GetVisibilitySample(s)][GetVisibilityPos(pos, s)];
}

float GetDepth(uint2 pos, uint s)
{
    return depthMap.sample[GetVisibilitySample(s)][GetVisibilityPos(pos, s)];
}

float3 ScreenPosToViewPos(float2 sPos, float viewPosZ)
{
    return float3((sPos * sceneConstants.ViewSpace.InverseProjectionScale + sceneConstants.ViewSpace.InverseProjectionBias) * viewPosZ, viewPosZ);
}

float3 GetSampleViewPos(uint2 pos, int s)
{
    float depth = -0.0001f / GetDepth(pos, s);
    float2 spos = GetVisibilityPos(pos, s) + 0.5 + visibilityMap.GetSamplePosition(GetVisibilitySample(s));
    return ScreenPosToViewPos(spos, depth);
}

uint NumQuadsX() { return sceneConstants.ViewSpace.Size.x                                   / PSamplesX; }
uint NumQuadsY() { return sceneConstants.ViewSpace.Size.y                                   / PSamplesY; }
uint NumQuads () { return sceneConstants.ViewSpace.Size.x * sceneConstants.ViewSpace.Size.y / PSamples ; }

struct MsaaSample
{
    uint  Visibility; // 32 bits instance + tri
    uint  SampleMask; // 32 bits (1 per sample)
};

RWStructuredBuffer<MsaaSample> msaaBuffer : register(u2, space0);
RWStructuredBuffer<uint> msaaSampleCountBuffer : register(u3, space0);

groupshared uint2 visibilityListGS[(32 / PSamplesX) * (16 / PSamplesY)][8 * PSamples];

[numthreads(32 / PSamplesX, 16 / PSamplesY, 1)]
void MsaaGatherPass(uint3 pos : SV_DispatchThreadID, uint tindex : SV_GroupIndex)
{
    // Register arrays are more expensive, apparently, so we use GDS.
    //uint2 visibilityList[8 * PSamples];
    #undef visibilityList
    #define visibilityList visibilityListGS[tindex]
    uint numSamples = 0;
    {
        uint sampleBit = 1;
        for (uint s = 0; s != 8 * PSamples; ++s, sampleBit <<= 1)
        {
            uint v = GetVisibility(pos.xy, s);
            uint i = 0;
            for (; i < numSamples; ++i)
            {
                if (visibilityList[i].x == v)
                {
                    visibilityList[i].y |= sampleBit;
                    break;
                }
            }
            if (i == numSamples)
            {
                visibilityList[i] = uint2(v, sampleBit);
                ++numSamples;
            }
        }
    }

    uint i = 0;

    // Pick the top samples, aiming to cover 7/8 of all pixels, but of course we need at least one.
    if (numSamples > 1)
    {
        uint maxSamples = min(numSamples, 8);
        uint4 samplesLeft = uint4(8, 8, 8, 8);
        for (i = 0; i < maxSamples; ++i)
        {
            uint foundIndex = i;
            uint4 foundBits = countbits(uint4(
                visibilityList[i].y & 0xFF,
                visibilityList[i].y & 0xFF00,
                visibilityList[i].y & 0xFF0000,
                visibilityList[i].y & 0xFF000000
            ));
            uint foundBitsCount = dot(samplesLeft, foundBits);

            for (uint s = i + 1; s < numSamples; ++s)
            {
                uint4 sBits = countbits(uint4(
                    visibilityList[s].y & 0xFF,
                    visibilityList[s].y & 0xFF00,
                    visibilityList[s].y & 0xFF0000,
                    visibilityList[s].y & 0xFF000000
                ));
                uint sBitsCount = dot(samplesLeft, sBits);
                if (foundBitsCount < sBitsCount)
                {
                    foundIndex     = s;
                    foundBits      = sBits;
                    foundBitsCount = sBitsCount;
                }
            }
            if (foundIndex != i)
            {
                uint2 tmp = visibilityList[i];
                visibilityList[i] = visibilityList[foundIndex];
                visibilityList[foundIndex] = tmp;
            }
            samplesLeft -= foundBits;
            if (dot(min(samplesLeft, 1), uint4(1, 8, 16, 24)) == dot(samplesLeft, uint4(1, 8, 16, 24)))
            {
                ++i;
                break;
            }
        }
        numSamples = i;
    }

    uint quadIndex = pos.x + pos.y * NumQuadsX();

    msaaSampleCountBuffer[quadIndex] = numSamples;

    for (i = 0; i < numSamples; ++i)
    {
        MsaaSample result;
        result.Visibility = visibilityList[i].x;
        result.SampleMask = visibilityList[i].y;
        msaaBuffer[i * NumQuads() + quadIndex] = result;
    }
}

float3 CalculateViewPosForQuad(uint2 quadPos, uint sampleMask)
{
    float3 viewPos = float3(0, 0, 0);
    uint s = 0;
    float posWeight = 0;
    while (sampleMask != 0)
    {
        while ((sampleMask & 1) == 0)
        {
            ++s;
            sampleMask >>= 1;
        }

        uint2 samplePixel = GetSamplePixel(s);
        viewPos += GetSampleViewPos(quadPos, s);
        posWeight += 1;
        ++s;
        sampleMask >>= 1;
    }

    return viewPos / posWeight;
}

float3 DecodeViewPos(uint pos, float depth)
{
    float2 spos = ((float2)(uint2(pos, pos >> 16) & 0xFFFF) / 65535.5f) * sceneConstants.InverseNormalizedProjectionScale + sceneConstants.InverseNormalizedProjectionBias;
    return float3(spos * depth, depth);
}

uint2 DecodeGroupPos(uint pos)
{
    uint2 npos = uint2(pos, pos >> 16) & 0xFFFF;
    return npos * uint2(640, 360) / 0x10000;
}

uint4 DecodeWeights(uint mask)
{
    return uint4(
        countbits(mask & 0xFF),
        countbits(mask & 0xFF00),
        countbits(mask & 0xFF0000),
        countbits(mask & 0xFF000000)
    );
}

struct GBufferSample
{
    float3            ViewPos;
    BarycentricCoords Barycentric;
    float3            ViewNormal;
    float3            DiffuseColor[4];
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
    result.DiffuseColor[0] =
    result.DiffuseColor[1] =
    result.DiffuseColor[2] =
    result.DiffuseColor[3] = materials[materialIndex].DiffuseColor;
    uint diffuseTextureId = materials[materialIndex].DiffuseTextureId;
    //float3 AmbientColor;
    //float3 DiffuseColor;
    //float3 SpecularColor;
    //float  SpecularExponent;
    //uint   DiffuseTextureId;


    result.Barycentric = GetBarycentric(sceneConstants.ViewSpace, vertexPositions[modelIndex], i, modelToView, viewPos);

    float3 normal0 = vertexAttributes[modelIndex][i[0]].Normal;
    float3 normal1 = vertexAttributes[modelIndex][i[1]].Normal;
    float3 normal2 = vertexAttributes[modelIndex][i[2]].Normal;

    result.ViewPos    = viewPos;
    result.ViewNormal = normalize(mul(normalize(normal0 * result.Barycentric.B[0] + normal1 * result.Barycentric.B[1] + normal2 * result.Barycentric.B[2]), (float3x3)modelToView));

    if (diffuseTextureId != 0)
    {
        float2 uv0 = vertexAttributes[modelIndex][i[0]].UV;
        float2 uv1 = vertexAttributes[modelIndex][i[1]].UV;
        float2 uv2 = vertexAttributes[modelIndex][i[2]].UV;
        float2 uv =
            uv0 * result.Barycentric.B[0] +
            uv1 * result.Barycentric.B[1] +
            uv2 * result.Barycentric.B[2];
        float2 ddx =
            uv0 * result.Barycentric.dB_dx[0] +
            uv1 * result.Barycentric.dB_dx[1] +
            uv2 * result.Barycentric.dB_dx[2];
        float2 ddy =
            uv0 * result.Barycentric.dB_dy[0] +
            uv1 * result.Barycentric.dB_dy[1] +
            uv2 * result.Barycentric.dB_dy[2];

        result.DiffuseColor[0] *= textures[diffuseTextureId - 1].SampleGrad(defaultSampler, uv - ddx * 0.5f - ddy * 0.5f, ddx, ddy).rgb;
        result.DiffuseColor[1] *= textures[diffuseTextureId - 1].SampleGrad(defaultSampler, uv + ddx * 0.5f - ddy * 0.5f, ddx, ddy).rgb;
        result.DiffuseColor[2] *= textures[diffuseTextureId - 1].SampleGrad(defaultSampler, uv - ddx * 0.5f + ddy * 0.5f, ddx, ddy).rgb;
        result.DiffuseColor[3] *= textures[diffuseTextureId - 1].SampleGrad(defaultSampler, uv + ddx * 0.5f + ddy * 0.5f, ddx, ddy).rgb;
    }

    return result;
}

    //uint color = triangleParams[tri].Color;
    //return float4(
    //    ( color        & 0xFF) / 255.0f,
    //    ((color >>  8) & 0xFF) / 255.0f,
    //    ((color >> 16) & 0xFF) / 255.0f,
    //    ((color >> 24) & 0xFF) / 255.0f);


float4 LinearToGamma(float4 color)
{
    return float4(pow(color.xyz, 1.0f / 2.2f), color.w);
}

[numthreads(64 / PSamplesX, 16 / PSamplesY, 1)]
void ColorPass(uint3 pos : SV_DispatchThreadID)
{
    float4x4 color = 0;

    uint computableIndex = pos.x + pos.y * NumQuadsX();

    uint numSamples = msaaSampleCountBuffer[computableIndex];


    uint4 totalWeights = 0;
    [unroll(4)] for (uint k = 0; k < numSamples; ++k)
    {
        totalWeights += DecodeWeights(msaaBuffer[k * NumQuads() + computableIndex].SampleMask);
    }

    {
        for (uint i = 0; i < numSamples; ++i)
        {
            MsaaSample msample = msaaBuffer[i * NumQuads() + computableIndex];
            float4 weights = (float4)DecodeWeights(msample.SampleMask) + 0.01f;
            uint v = msample.Visibility;
            if (v == 0)
            {
                color[0] += weights[0] * float4(0.1, 0, 0, 1);
                color[1] += weights[1] * float4(0.1, 0, 0, 1);
                color[2] += weights[2] * float4(0.1, 0, 0, 1);
                color[3] += weights[3] * float4(0.1, 0, 0, 1);
            }
            else
            {
                float3 viewPos = CalculateViewPosForQuad(pos.xy, msample.SampleMask);

                GBufferSample gSample = GetGBufferSample(v, viewPos);

                float3 albedo0 = gSample.DiffuseColor[0];
                float3 albedo1 = gSample.DiffuseColor[1];
                float3 albedo2 = gSample.DiffuseColor[2];
                float3 albedo3 = gSample.DiffuseColor[3];

                //albedo0.y *= totalWeights[0] / 8.0f;
                //albedo1.y *= totalWeights[1] / 8.0f;
                //albedo2.y *= totalWeights[2] / 8.0f;
                //albedo3.y *= totalWeights[3] / 8.0f;

                //albedo0 *= gSample.Barycentric * weights[0] / totalWeights[0];
                //albedo1 *= gSample.Barycentric * weights[1] / totalWeights[1];
                //albedo2 *= gSample.Barycentric * weights[2] / totalWeights[2];
                //albedo3 *= gSample.Barycentric * weights[3] / totalWeights[3];

                color[0] += weights[0] * float4(albedo0 * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))), 1);
                color[1] += weights[1] * float4(albedo1 * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))), 1);
                color[2] += weights[2] * float4(albedo2 * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))), 1);
                color[3] += weights[3] * float4(albedo3 * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))), 1);
            }
        }
    }

    //color[0] +=  * (totalWeights[0] - color[0].w);
    //color[1] +=  * (totalWeights[1] - color[1].w);
    //color[2] +=  * (totalWeights[2] - color[2].w);
    //color[3] +=  * (totalWeights[3] - color[3].w);

/*
    uint mask = 0xFFFFFFFF >> (32 - (8 * PSamples));

    for (int s = 0; mask != 0; ++s, mask >>= 1)
    {
        while ((mask & 1) == 0 && mask != 0)
        {
            ++s;
            mask >>= 1;
        }
        if (mask == 0)
        {
            break;
        }

        uint v = GetVisibility(pos.xy, s);
        uint tri  = v & 0xFFFFF;
        uint instanceId = v >> 20;
        if (instanceId == 0)
        {
            continue;
        }
        --instanceId;

        uint2 samplePixel = GetSamplePixel(s);

        float3 viewPos = GetSampleViewPos(pos.xy, s);
        float  totalWeight = 1;
        float  weight[PSamplesX][PSamplesY];
        {
            [unroll] for (int x = 0; x < PSamplesX; ++x)
                [unroll] for (int y = 0; y < PSamplesY; ++y)
                    weight[x][y] = 0;
        }
        weight[samplePixel.x][samplePixel.y] = 1;

        uint s2mask = 2;
        for (int s2 = s + 1; s2 < (8 * PSamples); ++s2, s2mask <<= 1)
        {
            if ((mask & s2mask) == 0)
            {
                continue;
            }
            uint v2 = GetVisibility(pos.xy, s2);
            if (v2 == v)
            {
                uint2 sample2Pixel = GetSamplePixel(s2);
                viewPos += GetSampleViewPos(pos.xy, s2);
                weight[sample2Pixel.x][sample2Pixel.y] += 1;
                totalWeight  += 1;
                mask &= ~s2mask;
            }
            else if ((v2 >> 20) == 0)
            {
                mask &= ~s2mask;
            }
        }

        uint     modelIndex  = instanceList[instanceId].ModelIndex;
        float4x4 modelToView = instanceList[instanceId].View;

        uint3 i = indices[modelIndex].Load(tri);

        float3 bary = GetBarycentric(modelIndex, i, modelToView, viewPos / totalWeight);

        float3 normal0 = mul(vertexAttributes[modelIndex][i[0]].Normal, (float3x3)modelToView);
        float3 normal1 = mul(vertexAttributes[modelIndex][i[1]].Normal, (float3x3)modelToView);
        float3 normal2 = mul(vertexAttributes[modelIndex][i[2]].Normal, (float3x3)modelToView);

        float3 normal = normalize(normal0 * bary[0] + normal1 * bary[1] + normal2 * bary[2]);

        {
            [unroll] for (int x = 0; x < PSamplesX; ++x)
                [unroll] for (int y = 0; y < PSamplesY; ++y)
                {
                    color[x][y] += weight[x][y] * float4(float3(1, 1, 1) * dot(normal, normalize(float3(1, 1, 1))), 1);
                }
        }
    }
*/

    uint2 spos = pos.xy;
    float4 posColor = 1;//float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);
    //float4 posColor = float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);

    colorUav[spos.xy * PSampleXY + uint2(0, 0)] = posColor * float4(color[0].xyz / color[0].w, 1);
    colorUav[spos.xy * PSampleXY + uint2(1, 0)] = posColor * float4(color[1].xyz / color[1].w, 1);
    colorUav[spos.xy * PSampleXY + uint2(0, 1)] = posColor * float4(color[2].xyz / color[2].w, 1);
    colorUav[spos.xy * PSampleXY + uint2(1, 1)] = posColor * float4(color[3].xyz / color[3].w, 1);
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
    uint2 posNeighborX = uint2(pos.x & 1 ? pos.x + 1 : pos.x - 1, pos.y);
    uint2 posNeighborY = uint2(pos.x, pos.y & 1 ? pos.y + 1 : pos.y - 1);

    // Since we're rendering in quads, we can see some pixelation on quad boundaries, so we apply a small filter across the quad boundary.
    // If the difference within the quad is much smaller than the difference with the next quad,
    // we'll push the color a tad towards the next quad's color.
    // We do this in both cartesian directions.
    uint2 qpos = pos / 2;
    uint2 qposX = posNeighborX / 2;
    uint2 qposY = posNeighborY / 2;
    uint computableIndex  = qpos .x + qpos .y * NumQuadsX();
    uint computableIndexX = qposX.x + qposX.y * NumQuadsX();
    uint computableIndexY = qposY.x + qposY.y * NumQuadsX();

    float4 pixelColor = colorMap[pos];

    //if (msaaSampleCountBuffer[computableIndex] == 1)
    //{
    //    uint visibility = msaaBuffer[computableIndex].Visibility;
    //    float4 mixedColor = pixelColor;
    //    if (msaaSampleCountBuffer[computableIndexX] == 1)
    //    {
    //        if (visibility == msaaBuffer[computableIndexX].Visibility)
    //        {
    //            float4 oppositeColorX = colorMap[pos ^ uint2(1, 0)];
    //            float4 neighborColorX = colorMap[posNeighborX];
    //            mixedColor += (oppositeColorX + neighborColorX - 2.0f * pixelColor) / 3.0f;
    //        }
    //    }
    //    if (msaaSampleCountBuffer[computableIndexY] == 1)
    //    {
    //        if (visibility == msaaBuffer[computableIndexY].Visibility)
    //        {
    //            float4 oppositeColorY = colorMap[pos ^ uint2(0, 1)];
    //            float4 neighborColorY = colorMap[posNeighborY];
    //            mixedColor += (oppositeColorY + neighborColorY - 2.0f * pixelColor) / 3.0f;
    //        }
    //    }
    //    pixelColor = clamp(mixedColor, 0, 1);
    //}

    return //LinearToGamma(
        pixelColor;
        /*
        * 0.4f +
        colorMap[floor(position.xy) + uint2(-1, 0)] * 0.15f +
        colorMap[floor(position.xy) + uint2( 1, 0)] * 0.15f +
        colorMap[floor(position.xy) + uint2(0, -1)] * 0.15f +
        colorMap[floor(position.xy) + uint2(0,  1)] * 0.15f
        */
    //);
}
