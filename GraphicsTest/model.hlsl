//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

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

struct TriangleParams
{
    uint Color;
};


struct SceneConstants
{
    float4x4 InverseProjection;
};

ConstantBuffer<SceneConstants> sceneConstants : register(b0);

Texture2D<uint>  visibilityMap : register(t1, space0);
Texture2D<float> depthMap      : register(t2, space0);

Buffer<uint3>                      indices         [MAX_MODEL_DEFINITIONS] : register(t1, space1);
StructuredBuffer<TriangleParams>   triangleParams  [MAX_MODEL_DEFINITIONS] : register(t1, space2);
StructuredBuffer<float3>           vertexPositions [MAX_MODEL_DEFINITIONS] : register(t1, space3);
StructuredBuffer<VertexAttributes> vertexAttributes[MAX_MODEL_DEFINITIONS] : register(t1, space4);

float4 ColorVS(uint index : SV_VertexID) : SV_Position
{
    float4 position;

    position.x = index == 0 ? -3 : 1;
    position.y = index == 2 ? -3 : 1;
    position.z = 0.5;
    position.w = 1;

    return position;
}

float4 ColorPS(float4 position : SV_Position) : SV_Target
{
    uint v = visibilityMap[floor(position.xy)];
    uint tri  = v & 0xFFFFF;
    uint instanceId = v >> 20;

    if (instanceId == 0)
    {
        clip(-1);
    }
    --instanceId;

    uint     modelIndex  = instanceList[instanceId].ModelIndex;
    float4x4 modelToView = instanceList[instanceId].View;

    uint3 i = indices[modelIndex].Load(tri);

    float3 normal0 = mul(vertexAttributes[modelIndex][i[0]].Normal, (float3x3)modelToView);
    float3 normal1 = mul(vertexAttributes[modelIndex][i[1]].Normal, (float3x3)modelToView);
    float3 normal2 = mul(vertexAttributes[modelIndex][i[2]].Normal, (float3x3)modelToView);

    float3 viewPos0 = mul(float4(vertexPositions[modelIndex][i[0]], 1), modelToView).xyz;
    float3 viewPos1 = mul(float4(vertexPositions[modelIndex][i[1]], 1), modelToView).xyz;
    float3 viewPos2 = mul(float4(vertexPositions[modelIndex][i[2]], 1), modelToView).xyz;

    float3 N = cross(viewPos1 - viewPos0, viewPos2 - viewPos0);
    float N2 = dot(N, N);

    float depth = 0.0001f / depthMap[floor(position.xy)];
    float2 normPos = float2(
               position.x * (2 / 1280.0f) - 1.0f,
        1.0f - position.y * (2 /  720.0f)
    );
    float3 viewPos = depth * float3(
        normPos.x * (1280.0 / 720.0f),
        normPos.y                    ,
        -1
    );

    float3 bary = clamp(float3(
        dot(cross(viewPos1 - viewPos, viewPos2 - viewPos), N),
        dot(cross(viewPos2 - viewPos, viewPos0 - viewPos), N),
        dot(cross(viewPos0 - viewPos, viewPos1 - viewPos), N)
    ) / N2, 0, 1);

    bary = bary / dot(bary, 1);

    float3 normal = normal0 * bary[0] + normal1 * bary[1] + normal2 * bary[2];

    return float4(1, 1, 1, 1) * dot(normal, normalize(float3(1, 1, 1)));

    //uint color = triangleParams[tri].Color;
    //return float4(
    //    ( color        & 0xFF) / 255.0f,
    //    ((color >>  8) & 0xFF) / 255.0f,
    //    ((color >> 16) & 0xFF) / 255.0f,
    //    ((color >> 24) & 0xFF) / 255.0f);

    //return float4(
    //    (tri & 1) ? 1 : 0,
    //    (tri & 2) ? 1 : 0,
    //    (tri & 4) ? 1 : 0,
    //    1
    //);
}


RWTexture2D<float4> colorUav : register(u0, space0);

[numthreads(8, 8, 1)]
void Postprocess(uint3 pos : SV_DispatchThreadID)
{
    //uint3 pos = uint2(instance / 720, instance % 720, 0);

    uint v = visibilityMap[pos.xy];

    if ((v >> 20) == 0)
    {
        colorUav[pos.xy] = float4(0.3, 0, 0, 1);
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
    return colorMap[floor(position.xy)];
}
