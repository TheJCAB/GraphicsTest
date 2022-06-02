
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

groupshared uint2 visibilityListGS[8][8][8];

uint2 BringTopSampleToFront(uint2 tid, inout uint numSamples, bool overwriteFirst = false)
{
    if (numSamples == 1)
    {
        // By definition.
        return visibilityListGS[tid.x][tid.y][0];
    }

    uint foundIndex = overwriteFirst ? 1 : 0;
    uint2 found = visibilityListGS[tid.x][tid.y][foundIndex];
    uint foundBits  = countbits(found.y);
    for (uint i = foundIndex + 1; i < numSamples; ++i)
    {
        uint2 s = visibilityListGS[tid.x][tid.y][i];
        uint sBits = countbits(s.y);
        if (foundBits < sBits)
        {
            found      = s;
            foundIndex = i;
            foundBits  = sBits;
        }
    }
    if (foundIndex != 0)
    {
        if (overwriteFirst)
        {
            --numSamples;
            if (foundIndex < numSamples)
            {
                visibilityListGS[tid.x][tid.y][foundIndex] = visibilityListGS[tid.x][tid.y][numSamples];
            }
            visibilityListGS[tid.x][tid.y][numSamples] = 0;
        }
        else
        {
            visibilityListGS[tid.x][tid.y][foundIndex] = visibilityListGS[tid.x][tid.y][0];
        }
        visibilityListGS[tid.x][tid.y][0] = found;
    }
    return found;
}

// Updates numSamples and topSample.
void MergeSamplesAndSync(inout uint numSamples, inout uint2 topSample, uint2 tid, uint2 otherTid, bool thisIsFirstSample)
{
    // Figure out what we're going to do with the top samples from this pixel and the other pixel.

    bool removeSample = false;
    bool mergeRemove  = false;

    uint2 otherSample = visibilityListGS[otherTid.x][otherTid.y][0];
    bool topsMatch = otherSample.x == topSample.x;
    if (topsMatch)
    {
        uint2 firstTid  = thisIsFirstSample ? tid : otherTid;
        uint2 secondTid = thisIsFirstSample ? otherTid : tid;

        uint2 firstSample  = thisIsFirstSample ? topSample : otherSample;
        uint2 secondSample = thisIsFirstSample ? otherSample : topSample;

        if (visibilityListGS[firstTid.x][firstTid.y][1].x != 0 && countbits(secondSample.y) >= countbits(firstSample.y))
        {
            // First has another sample and second has more samples, so merge first into second.
            removeSample =  thisIsFirstSample;
            mergeRemove  = true;
        }
        else if (visibilityListGS[secondTid.x][secondTid.y][1].x != 0 && countbits(secondSample.y) <= countbits(firstSample.y))
        {
            // Second has another sample and first has more samples, so merge second into first.
            removeSample = !thisIsFirstSample;
            mergeRemove  = true;
        }

        GroupMemoryBarrierWithGroupSync();

        // Both top samples were the same triangle, so either we merge them or we leave them alone.

        if (mergeRemove)
        {
            // Merge the samples.
            if (removeSample)
            {
                // This sample merged into the other, so it's now gone.
                // We need to find a next best one.
                // Note that we're guaranteed to have at least another sample by the checks above.
                topSample = BringTopSampleToFront(tid, numSamples, true);
            }
            else
            {
                // The other sample merged into this one. We won!
                topSample.y |= otherSample.y;
                visibilityListGS[tid.x][tid.y][0].y = topSample.y;
            }
        }
    }
    else
    {
        for (int i = 1; i < numSamples; ++i)
        {
            uint2 curSample = visibilityListGS[tid.x][tid.y][i];
            if (curSample.x == otherSample.x)
            {
                // Note: here we modify the other pixel's top sample, and the current pixel's tail of samples.
                // The other pixel's thread will do the same, so we'll need to reacquire
                // the potentially updated topSample.y below, after the barrier.
                otherSample.y |= curSample.y;
                visibilityListGS[otherTid.x][otherTid.y][0].y = otherSample.y;
                if (i + 1 < numSamples)
                {
                    // We've given our "i" sample to the other thread, so replace it with our last sample.
                    visibilityListGS[tid.x][tid.y][i] = visibilityListGS[tid.x][tid.y][numSamples - 1];
                    visibilityListGS[tid.x][tid.y][numSamples - 1] = 0;
                }
                else
                {
                    // We've given our last sample to the other thread, so just neuter it.
                    visibilityListGS[tid.x][tid.y][i] = 0;
                }
                --numSamples;
                break;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // The other thread might have merged some samples into our top, so refresh it.
    topSample.y = visibilityListGS[tid.x][tid.y][0].y;

}

[numthreads(8, 8, 1)]
void MsaaGatherPass(uint3 pos : SV_DispatchThreadID, uint3 tid : SV_GroupThreadId)
{
    const uint2 qpos = pos.xy >> 1;
    const uint2 qid  = pos.xy & 1;

    const uint maskBit = qid.x * 8 + qid.y * 16;

    #define visibilityList visibilityListGS[tid.x][tid.y]

    uint numSamples = 0;
    {
        uint sampleBit = 1u << maskBit;
        [unroll]
        for (uint s = 0; s != 8; ++s, sampleBit <<= 1)
        {
            uint v = visibilityMap.sample[s][pos.xy];
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

    if (numSamples < 8)
    {
        visibilityList[numSamples] = 0;
    }

    // Pick the top sample.
    uint2 topSample = BringTopSampleToFront(tid.xy, numSamples);

    GroupMemoryBarrierWithGroupSync();

    // Do a cooperative merge of this pixel and another one in the same quad.
    // We do this three times: along the X coordinate, then along the Y coordinate, and lastly along the diagonals.
    // The idea is that each pixel in the quad will end up owning all the samples
    // for each of the 4 most populated triangles across the quad, if there are 4 triangles or more.
    // Each pixel in general will end up with a mask covering any samples across the entire quad.
    // If there are less than 4 triangles across the quad, then multiple pixels will split the sample mask,
    // keeping the shading and texturing resolution constant.
    MergeSamplesAndSync(numSamples, topSample, tid.xy, tid.xy ^ uint2(1, 0), (tid.x & 1) == 0);
    MergeSamplesAndSync(numSamples, topSample, tid.xy, tid.xy ^ uint2(0, 1), (tid.y & 1) == 0);
    MergeSamplesAndSync(numSamples, topSample, tid.xy, tid.xy ^ uint2(1, 1), (tid.x & 1) == 0);

    uint quadIndex = 4 * (qpos.x + qpos.y * NumQuadsX());
    uint pIndex = qid.x + qid.y * 2;

    MsaaSample result;
    result.Visibility = topSample.x;
    result.SampleMask = topSample.y;
    msaaBuffer[pIndex + quadIndex] = result;
}

float3 CalculateViewPosForQuad(uint2 quadPos, uint sampleMask)
{
    float3 viewPos = float3(0, 0, 0);
    uint s = 0;
    float posWeight = 0;
    [unroll(8)]
    while (sampleMask != 0)
    {
        uint skipSamples = firstbitlow(sampleMask);
        s += skipSamples;
        sampleMask >>= skipSamples;

        uint2 samplePixel = GetSamplePixel(s);
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
    [unroll(8)]
    while (sampleMask != 0)
    {
        uint skipSamples = firstbitlow(sampleMask);
        s += skipSamples;
        sampleMask >>= skipSamples;

        viewDir += GetSamplePos(quadPos, s);
        posWeight += 1;
        ++s;
        sampleMask >>= 1;
    }

    return normalize(ScreenPosToViewPos(viewDir / posWeight, -1.0f));
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

    //uint color = triangleParams[tri].Color;
    //return float4(
    //    ( color        & 0xFF) / 255.0f,
    //    ((color >>  8) & 0xFF) / 255.0f,
    //    ((color >> 16) & 0xFF) / 255.0f,
    //    ((color >> 24) & 0xFF) / 255.0f);


groupshared float4 AccumColors[8][8];

[numthreads(8, 8, 1)]
void ColorPass(uint3 pos : SV_DispatchThreadID, uint3 tid : SV_GroupThreadId)
{
    // Consider this for thread locality:
    // const uint2 gSize = sceneConstants.ViewSpace.Size / 8;
    // uint gindex = (pos.x / 8) + (pos.y / 8) * gSize.x;
    // uint2 gid = uint2(gindex % 8 + 8 * ((gindex / 8) / gSize.y), (gindex / 8) % gSize.y);
    // pos.xy = gid * 8 + tid.xy;

    const uint2 qpos = pos.xy >> 1;
    const uint2 qid  = pos.xy & 1;

    uint quadIndex = 4 * (qpos.x + qpos.y * NumQuadsX());
    uint pIndex = qid.x + qid.y * 2;

    float4 color = 0;

    MsaaSample msample = msaaBuffer[pIndex + quadIndex];
    if (msample.Visibility == 0)
    {
        float3 viewDir = CalculateViewDirForQuad(qpos.xy, msample.SampleMask);
        float3 skyMapColor = SampleSkymap(sceneConstants.ViewSpace, textures[0], viewDir).xyz;

        color = float4(skyMapColor, 1.0f);
    }
    else
    {
        float3 viewPos = CalculateViewPosForQuad(qpos.xy, msample.SampleMask);
        GBufferSample gSample = GetGBufferSample(sceneConstants.ViewSpace, msample.Visibility, viewPos);

        float3 lightedColor = LightPixel(sceneConstants.ViewSpace, gSample);

        color = float4(lightedColor, 1.0f);
    }

    AccumColors[tid.x][tid.y] = color;

    uint maskBits = 0xFFu << (pIndex * 8);
    color *= countbits(msample.SampleMask & maskBits);

    GroupMemoryBarrierWithGroupSync();

    color += AccumColors[tid.x ^ 1u][tid.y     ] * countbits(msaaBuffer[(pIndex ^ 1) + quadIndex].SampleMask & maskBits);
    color += AccumColors[tid.x     ][tid.y ^ 1u] * countbits(msaaBuffer[(pIndex ^ 2) + quadIndex].SampleMask & maskBits);
    color += AccumColors[tid.x ^ 1u][tid.y ^ 1u] * countbits(msaaBuffer[(pIndex ^ 3) + quadIndex].SampleMask & maskBits);

    uint2 spos = pos.xy;
    float4 posColor = 1;//float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);
    //float4 posColor = float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);

    colorUav[pos.xy] = posColor * float4(color.xyz / color.w, 1);
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

    return pixelColor;
}
