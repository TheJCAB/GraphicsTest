
#include "Pipeline.h"
#include "Lighting.h"

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

float2 GetSamplePos(uint2 pos, int s)
{
    return GetVisibilityPos(pos, s) + 0.5 + visibilityMap.GetSamplePosition(GetVisibilitySample(s));
}

float3 GetSampleViewPos(uint2 pos, int s, float depth)
{
    float2 spos = GetSamplePos(pos, s);
    return ScreenPosToViewPos(spos, depth);
}

float3 GetSampleViewPos(uint2 pos, int s)
{
    return GetSampleViewPos(pos, s, -0.0001f / GetDepth(pos, s));
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

        viewPos += GetSampleViewPos(quadPos, s);
        posWeight += 1;
        ++s;
        sampleMask >>= 1;
    }

    return viewPos / posWeight;
}

float3 CalculateViewDirForQuad(uint2 quadPos, uint sampleMask)
{
    float2 viewDir = float2(0, 0);
    uint s = 0;
    float posWeight = 0;
    while (sampleMask != 0)
    {
        while ((sampleMask & 1) == 0)
        {
            ++s;
            sampleMask >>= 1;
        }

        viewDir += GetSamplePos(quadPos, s);
        posWeight += 1;
        ++s;
        sampleMask >>= 1;
    }

    return normalize(ScreenPosToViewPos(viewDir / posWeight, -1.0f));
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
                float3 viewDir = CalculateViewDirForQuad(pos.xy, msample.SampleMask);
                float3 skyMapColor = SampleSkymap(sceneConstants.ViewSpace, textures[0], viewDir).xyz;

                color[0] += weights[0] * float4(skyMapColor, 1);
                color[1] += weights[1] * float4(skyMapColor, 1);
                color[2] += weights[2] * float4(skyMapColor, 1);
                color[3] += weights[3] * float4(skyMapColor, 1);
            }
            else
            {
                float3 viewPos = CalculateViewPosForQuad(pos.xy, msample.SampleMask);
                GBufferSample gSample = GetGBufferSample(sceneConstants.ViewSpace, v, viewPos);

                float3 lightedColor = LightPixel(sceneConstants.ViewSpace, gSample);

                color[0] += weights[0] * float4(lightedColor, 1);
                color[1] += weights[1] * float4(lightedColor, 1);
                color[2] += weights[2] * float4(lightedColor, 1);
                color[3] += weights[3] * float4(lightedColor, 1);
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
