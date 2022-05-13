
// Shared with the shader.
#define MAX_MODEL_DEFINITIONS    100
#define MAX_INSTANCE_DEFINITIONS 100

struct InstanceAttributes
{
    float4x4 View;
    float4x4 Projection;
    uint     ModelIndex;
    uint3    Padding;
};

cbuffer RootConstants : register(b0)
{
    uint instanceIndex;
};

StructuredBuffer<InstanceAttributes> instanceList : register(t0, space0);

float4 VisibilityVS(float3 position : SV_Position) : SV_Position
{
    return mul(float4(position, 1), instanceList[instanceIndex].Projection);
}

uint VisibilityPS(uint triangleId : SV_PrimitiveID) : SV_Target
{
    return triangleId | ((instanceIndex + 1) << 20);
}





struct VertexAttributes
{
    float3 Normal;
    uint   Color;
};

struct TriangleParamseters
{
    uint Color;
    uint Patch;
};


struct SceneConstants
{
    float4x4 InverseProjection;
};

ConstantBuffer<SceneConstants> sceneConstants : register(b0);

Texture2DMS<uint>  visibilityMap : register(t1, space0);
Texture2DMS<float> depthMap      : register(t2, space0);

Buffer<uint3>                         indices         [MAX_MODEL_DEFINITIONS] : register(t1, space1);
StructuredBuffer<TriangleParamseters> triangleParams  [MAX_MODEL_DEFINITIONS] : register(t1, space2);
StructuredBuffer<float3>              vertexPositions [MAX_MODEL_DEFINITIONS] : register(t1, space3);
StructuredBuffer<VertexAttributes>    vertexAttributes[MAX_MODEL_DEFINITIONS] : register(t1, space4);

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

// ViewPos.xy / z projected into [0..1280, 0..720] screen space
static const float2 ProjectionScale = float2(  720.0f / 2.0f, -720.0f / 2.0f);
static const float2 ProjectionBias  = float2( 1280.0f / 2.0f,  720.0f / 2.0f);

static const float2 InverseProjectionScale = float2(    2.0f / 720.0f, -2.0f / 720.0f);
static const float2 InverseProjectionBias  = float2(-1280.0f / 720.0f,  1.0f);

// ViewPos.xy / z projected into [0..1, 0..1] normalized space
static const float2 NormalizedProjectionScale = float2( 720.0f / (1280.0f * 2.0f), -1.0f / 2.0f);
static const float2 NormalizedProjectionBias  = float2( 1.0f / 2.0f,  1.0f / 2.0f);

static const float2 InverseNormalizedProjectionScale = float2( (1280.0f * 2.0f) / 720.0f, -2.0f);
static const float2 InverseNormalizedProjectionBias  = float2(-1280.0f          / 720.0f,  1.0f);

static const float2 InverseAspectRatio = float2(720.0f / 1280.0f, -1.0f);
static const float2 AspectRatio        = float2(1280.0f / 720.0f, -1.0f);

float3 GetSampleViewPos(uint2 pos, int s)
{
    float depth = -0.0001f / GetDepth(pos, s);
    float2 spos = GetVisibilityPos(pos, s) + 0.5 + visibilityMap.GetSamplePosition(GetVisibilitySample(s));
    return float3((spos * InverseProjectionScale + InverseProjectionBias) * depth, depth);
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

struct ComputableSample
{
    uint  ScreenPos;  // 32 bits
    uint  Visibility; // 32 bits instance + tri
    uint  Weights;    // UNORM  4 * 5 = 20 bits compressed
    float Depth;      // As is, 32 bits
};
// 128 bits compressed.

struct ComputableQuad
{
    uint             SampleCount;
    ComputableSample SampleList[8];
};

RWStructuredBuffer<ComputableQuad> computableBuffer : register(u1, space0);

groupshared uint2 visibilityListGS[(32 / PSamplesX) * (16 / PSamplesY)][8 * PSamples];
#define visibilityList visibilityListGS[tindex]

[numthreads(32 / PSamplesX, 16 / PSamplesY, 1)]
void ComputableGatherPass(uint3 pos : SV_DispatchThreadID, uint tindex : SV_GroupIndex)
{
    //uint2 visibilityList[8 * PSamples];
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

    // Pick the top samples
    uint maxSamples = 8;
    if (numSamples > maxSamples)
    {
        uint4 samplesLeft = uint4(8, 8, 8, 8);
        for (i = 0; i < maxSamples; ++i)
        {
            uint4 foundBits = uint4(
                countbits(visibilityList[i].y & 0xFF),
                countbits(visibilityList[i].y & 0xFF00),
                countbits(visibilityList[i].y & 0xFF0000),
                countbits(visibilityList[i].y & 0xFF000000)
            );
            uint foundBitsCount = dot(samplesLeft, foundBits);

            for (uint s = i + 1; s < numSamples; ++s)
            {
                uint4 sBits = uint4(
                    countbits(visibilityList[s].y & 0xFF),
                    countbits(visibilityList[s].y & 0xFF00),
                    countbits(visibilityList[s].y & 0xFF0000),
                    countbits(visibilityList[s].y & 0xFF000000)
                );
                uint sBitsCount = dot(samplesLeft, sBits);
                if (foundBitsCount < sBitsCount)
                {
                    uint2 tmp = visibilityList[i];
                    visibilityList[i] = visibilityList[s];
                    visibilityList[s] = tmp;
                    foundBits = sBits;
                    foundBitsCount = sBitsCount;
                }
            }
            samplesLeft -= foundBits;
        }
        numSamples = maxSamples;
    }

    uint computableIndex = pos.x + pos.y * (1280 / PSamplesX);

    for (i = 0; i < numSamples; ++i)
    {
        ComputableSample result;

        uint weight[PSamplesX][PSamplesY];
        {
            [unroll] for (int x = 0; x < PSamplesX; ++x)
                [unroll] for (int y = 0; y < PSamplesY; ++y)
                    weight[x][y] = 0;
        }

        uint v    = visibilityList[i].x;
        uint mask = visibilityList[i].y;
        if (v == 0)
        {
            result.ScreenPos = 0; // TODO: calculate from pos.xy
            result.Depth     = 1e20;

            weight[0][0] = countbits(mask & 0xFF);
            weight[1][0] = countbits(mask & 0xFF00);
            weight[0][1] = countbits(mask & 0xFF0000);
            weight[1][1] = countbits(mask & 0xFF000000);
        }
        else
        {
            float3 viewPos = float3(0, 0, 0);
            uint s = 0;
            while (mask != 0)
            {
                while ((mask & 1) == 0)
                {
                    ++s;
                    mask >>= 1;
                }

                uint2 samplePixel = GetSamplePixel(s);
                viewPos += GetSampleViewPos(pos.xy, s);
                weight[samplePixel.x][samplePixel.y] += 1;
                ++s;
                mask >>= 1;
            }

            // screen coordinates with 4 bits of fractional: uint2 normalizedScreenPos = floor(((viewPos.xy / viewPos.z) * ProjectionScale + ProjectionBias) * 16),
            uint2 normalizedScreenPos = floor(((viewPos.xy / viewPos.z) * NormalizedProjectionScale + NormalizedProjectionBias) * 65535.0f); // Normalized screen coordinates (resolution independent)

            result.ScreenPos  = normalizedScreenPos.x | (normalizedScreenPos.y << 16);
            result.Depth      = viewPos.z;
        }

        uint intWeight00 = weight[0][0];
        uint intWeight10 = weight[1][0];
        uint intWeight01 = weight[0][1];
        uint intWeight11 = weight[1][1];

        result.Visibility = v;
        result.Weights    = intWeight00 | (intWeight10 << 8) | (intWeight01 << 16) | (intWeight11 << 24);

        computableBuffer[computableIndex].SampleList[i] = result;
    }

    computableBuffer[computableIndex].SampleCount = numSamples;
}

float3 DecodeViewPos(uint pos, float depth)
{
    float2 spos = ((float2)(uint2(pos, pos >> 16) & 0xFFFF) / 65535.5f) * InverseNormalizedProjectionScale + InverseNormalizedProjectionBias;
    return float3(spos * depth, depth);
}

uint2 DecodeGroupPos(uint pos)
{
    uint2 npos = uint2(pos, pos >> 16) & 0xFFFF;
    return npos * uint2(640, 360) / 0x10000;
}

uint4 DecodeWeights(uint encodedWeights)
{
    return uint4(encodedWeights, encodedWeights >> 8, encodedWeights >> 16, encodedWeights >> 24) & 0xFF;
}

struct GBufferSample
{
    float3 ViewPos;         // As is, or... say 48-64 bits (plus instance index for constants)
    float3 Barycentric;     // UNORM, 32 bits compressed, possibly less
    float3 ViewNormal;      // UNORM, 2 * 15 + 2 = 32 bits compressed
    //float4 Weight;          // UNORM  4 * 5      = 20 bits compressed
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

    result.Barycentric = GetBarycentric(modelIndex, i, modelToView, viewPos);

    float3 normal0 = vertexAttributes[modelIndex][i[0]].Normal;
    float3 normal1 = vertexAttributes[modelIndex][i[1]].Normal;
    float3 normal2 = vertexAttributes[modelIndex][i[2]].Normal;

    result.ViewPos    = viewPos;
    result.ViewNormal = mul(normalize(normal0 * result.Barycentric[0] + normal1 * result.Barycentric[1] + normal2 * result.Barycentric[2]), (float3x3)modelToView);

    return result;
}

    //uint color = triangleParams[tri].Color;
    //return float4(
    //    ( color        & 0xFF) / 255.0f,
    //    ((color >>  8) & 0xFF) / 255.0f,
    //    ((color >> 16) & 0xFF) / 255.0f,
    //    ((color >> 24) & 0xFF) / 255.0f);


[numthreads(64 / PSamplesX, 16 / PSamplesY, 1)]
void ColorPass(uint3 pos : SV_DispatchThreadID)
{
    float4x4 color = 0;

    uint computableIndex = pos.x + pos.y * (1280 / PSamplesX);

    uint numSamples = computableBuffer[computableIndex].SampleCount;

    float4 totalWeights = 0;
    [unroll(4)] for (uint k = 0; k < numSamples; ++k)
    {
        totalWeights += (float4)DecodeWeights(computableBuffer[computableIndex].SampleList[k].Weights);
    }

    {
        for (int i = 0; i < numSamples; ++i)
        {
            ComputableSample csample = computableBuffer[computableIndex].SampleList[i];
            float4 weights = (float4)DecodeWeights(csample.Weights);// / totalWeights;
            uint v = csample.Visibility;
            if (v == 0)
            {
                color[0] += weights[0] * float4(0.3, 0, 0, 1);
                color[1] += weights[1] * float4(0.3, 0, 0, 1);
                color[2] += weights[2] * float4(0.3, 0, 0, 1);
                color[3] += weights[3] * float4(0.3, 0, 0, 1);
            }
            else
            {
                float3 viewPos = DecodeViewPos(csample.ScreenPos, csample.Depth);

                GBufferSample gSample = GetGBufferSample(v, viewPos);

                color[0] += weights[0] * float4(float3(1, 1 /*totalWeights[0] / 8*/, 1) * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))), 1);
                color[1] += weights[1] * float4(float3(1, 1 /*totalWeights[1] / 8*/, 1) * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))), 1);
                color[2] += weights[2] * float4(float3(1, 1 /*totalWeights[2] / 8*/, 1) * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))), 1);
                color[3] += weights[3] * float4(float3(1, 1 /*totalWeights[3] / 8*/, 1) * dot(gSample.ViewNormal, normalize(float3(1, 1, 1))), 1);
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

    uint2 spos = pos.xy; //DecodeGroupPos(computableBuffer[computableIndex].SampleList[0].ScreenPos);
    float4 posColor = 1;//float4(spos.x == pos.x ? 1.0f: 0.5f, 1.0f, spos.y == pos.y ? 1.0f: 0.5f, 1.0f);

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
    return colorMap[floor(position.xy)];
}
