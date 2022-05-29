
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
    bool removeSample = false;
    bool mergeRemove  = false;

    uint2 otherSample = visibilityListGS[otherTid.x][otherTid.y][0];
    if (otherSample.x == topSample.x)
    {
        uint2 firstTid  = thisIsFirstSample ? tid : otherTid;
        uint2 secondTid = thisIsFirstSample ? otherTid : tid;

        uint2 firstSample  = thisIsFirstSample ? topSample : otherSample;
        uint2 secondSample = thisIsFirstSample ? otherSample : topSample;

        if (visibilityListGS[firstTid.x][firstTid.y][1].x != 0 && countbits(secondSample.y) > countbits(firstSample.y))
        {
            // Merge to the second
            removeSample =  thisIsFirstSample;
            mergeRemove  = true;
        }
        else if (visibilityListGS[secondTid.x][secondTid.y][1].x != 0 && countbits(secondSample.y) < countbits(firstSample.y))
        {
            // Merge to the first
            removeSample = !thisIsFirstSample;
            mergeRemove  = true;
        }
    }

    AllMemoryBarrierWithGroupSync();

    if (mergeRemove)
    {
        if (removeSample)
        {
            topSample = BringTopSampleToFront(tid, numSamples, true);
        }
        else
        {
            topSample.y |= otherSample.y;
            visibilityListGS[tid.x][tid.y][0].y = topSample.y;
        }
    }
    else
    {
        uint otherRemoved = 8;
        for (int i = 1; i < 8; ++i)
        {
            otherSample = visibilityListGS[otherTid.x][otherTid.y][i];
            if (otherSample.x == topSample.x)
            {
                topSample.y |= otherSample.y;
                visibilityListGS[tid.x][tid.y][0].y = topSample.y;
                otherRemoved = i;
            }
            else if (otherSample.x == 0)
            {
                if (otherRemoved < i - 1)
                {
                    visibilityListGS[otherTid.x][otherTid.y][otherRemoved] = visibilityListGS[otherTid.x][otherTid.y][i - 1];
                    visibilityListGS[otherTid.x][otherTid.y][i - 1] = 0;
                }
                break;
            }
        }
    }

    AllMemoryBarrierWithGroupSync();

    if (!mergeRemove)
    {
        if (visibilityListGS[tid.x][tid.y][numSamples - 1].x == 0)
        {
            --numSamples;
        }
    }

    AllMemoryBarrierWithGroupSync();
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

    AllMemoryBarrierWithGroupSync();

    MergeSamplesAndSync(numSamples, topSample, tid.xy, tid.xy ^ uint2(1, 0), (tid.x & 1) == 0);
    MergeSamplesAndSync(numSamples, topSample, tid.xy, tid.xy ^ uint2(0, 1), (tid.y & 1) == 0);
    MergeSamplesAndSync(numSamples, topSample, tid.xy, tid.xy ^ uint2(1, 1), (tid.x & 1) == 0);

    uint quadIndex = qpos.x + qpos.y * NumQuadsX();
    uint pIndex = qid.x + qid.y * 2;

    if (qid.x + qid.y == 0)
    {
        msaaSampleCountBuffer[quadIndex] = 4;
    }

    MsaaSample result;
    result.Visibility = topSample.x;
    result.SampleMask = topSample.y;
    msaaBuffer[pIndex * NumQuads() + quadIndex] = result;
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

        result.DiffuseColor[0] = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv - ddx * 0.5f - ddy * 0.5f, ddx, ddy).rgb;
        result.DiffuseColor[1] = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv + ddx * 0.5f - ddy * 0.5f, ddx, ddy).rgb;
        result.DiffuseColor[2] = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv - ddx * 0.5f + ddy * 0.5f, ddx, ddy).rgb;
        result.DiffuseColor[3] = textures[diffuseTextureId - 1].SampleGrad(wrapSampler, uv + ddx * 0.5f + ddy * 0.5f, ddx, ddy).rgb;
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

groupshared float4 AccumColors[8][8][4];

[numthreads(8, 8, 1)]
void ColorPass(uint3 pos : SV_DispatchThreadID, uint3 tid : SV_GroupThreadId)
{
    const uint2 qpos = pos.xy >> 1;
    const uint2 qid  = pos.xy & 1;

    const uint thisMask = 0xFFu << (qid.x * 8 + qid.y * 16);

    uint quadIndex = qpos.x + qpos.y * NumQuadsX();
    uint pIndex = qid.x + qid.y * 2;

    MsaaSample msample = msaaBuffer[pIndex * NumQuads() + quadIndex];
    float4 weights = (float4)DecodeWeights(msample.SampleMask) + 0.01f;
    uint v = msample.Visibility;
    if (v == 0)
    {
        AccumColors[tid.x][tid.y][0] = weights[0] * float4(0.1, 0, 0, 1);
        AccumColors[tid.x][tid.y][1] = weights[1] * float4(0.1, 0, 0, 1);
        AccumColors[tid.x][tid.y][2] = weights[2] * float4(0.1, 0, 0, 1);
        AccumColors[tid.x][tid.y][3] = weights[3] * float4(0.1, 0, 0, 1);
    }
    else
    {
        float3 viewPos = CalculateViewPosForQuad(qpos.xy, msample.SampleMask);

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

        float3 finalColor = albedo0 * dot(gSample.ViewNormal, normalize(float3(1, 1, 1)));

        AccumColors[tid.x][tid.y][0] = weights[0] * float4(finalColor, 1);
        AccumColors[tid.x][tid.y][1] = weights[1] * float4(finalColor, 1);
        AccumColors[tid.x][tid.y][2] = weights[2] * float4(finalColor, 1);
        AccumColors[tid.x][tid.y][3] = weights[3] * float4(finalColor, 1);
    }

    AllMemoryBarrierWithGroupSync();

    float4 color =
        AccumColors[tid.x    ][tid.y    ][pIndex] +
        AccumColors[tid.x ^ 1][tid.y    ][pIndex] +
        AccumColors[tid.x    ][tid.y ^ 1][pIndex] +
        AccumColors[tid.x ^ 1][tid.y ^ 1][pIndex];

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
