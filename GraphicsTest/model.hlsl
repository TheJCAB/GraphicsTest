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

cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 Projection;
    uint     PrimitiveIndex;
};

float4 VisibilityVS(float4 position : POSITION) : SV_POSITION
{
    return mul(position, Projection);
}

uint VisibilityPS(uint tri : SV_PrimitiveID) : SV_TARGET
{
    return tri | PrimitiveIndex;
}

Texture2D<uint> visibilityMap : register(t0);

float4 ColorVS(uint index : SV_VertexID) : SV_POSITION
{
    float4 position;

    position.x = index == 0 ? -3 : 1;
    position.y = index == 2 ? -3 : 1;
    position.z = 0.5;
    position.w = 1;

    return position;
}

float4 ColorPS(float4 position : SV_POSITION) : SV_TARGET
{
    uint v = visibilityMap.Load(int3(floor(position.xy), 0));
    uint tri  = v & 0xFFFFF;
    uint prim = v >> 20;

    if (prim == 0)
    {
        clip(-1);
    }

    return float4(
        (tri & 1) ? 1 : 0,
        (tri & 2) ? 1 : 0,
        (tri & 4) ? 1 : 0,
        1
    );
}




struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

PSInput VSCube(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = mul(position, Projection);
    result.color = color;

    return result;
}

float4 PSCube(PSInput input, uint tri : SV_PrimitiveID) : SV_TARGET
{
    if (tri & 1)
    {
        return input.color;
    }
    else
    {
        return float4(1, 1, 1, 1);
    }
}
