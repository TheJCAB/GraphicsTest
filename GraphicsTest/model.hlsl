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
    float4x4 Projection;
    uint     ModelIndex;
    uint3    Padding;
};

cbuffer RootConstants
{
    uint instanceId;
};

StructuredBuffer<InstanceAttributes> instanceList : register(t0, space0);

float4 VisibilityVS(float3 position : SV_Position) : SV_Position
{
    return mul(float4(position, 1), instanceList[instanceId].Projection);
}

uint VisibilityPS(VisibilityPsInput input, uint triangleId : SV_PrimitiveID) : SV_Target
{
    return triangleId | ((instanceId + 1) << 20);
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

Texture2D<uint> visibilityMap : register(t1, space0);

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
    uint v = visibilityMap.Load(int3(floor(position.xy), 0));
    uint tri  = v & 0xFFFFF;
    uint instanceId = v >> 20;

    if (instanceId == 0)
    {
        clip(-1);
    }
    --instanceId;

    uint modelId = instanceList[instanceId].ModelIndex;

    uint3 i = indices[modelId].Load(tri);

    float3 normal0 = vertexAttributes[modelId][i[0]].Normal;
    //float3 normal1 = vertexPositions[modelId][i[1]].Normal;
    //float3 normal2 = vertexPositions[modelId][i[2]].Normal;

    return float4(abs(normal0), 1);

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
