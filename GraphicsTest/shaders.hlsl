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

Texture2D<uint> visibilityMap : register(t0);
SamplerState sampleWrap : register(s0);

cbuffer SceneConstantBuffer : register(b0)
{
    float4x4 projection;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = position;
    result.color = color;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    uint v = visibilityMap.Load(int3(floor(input.position.xy), 0));

    return (v & 0x800) == 0 ? input.color : float4(1, 0, 1, 1);
}



PSInput VSModel(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = mul(position, projection);
    result.color = color;

    return result;
}

float4 PSModel(PSInput input) : SV_TARGET
{
    return input.color;
}



PSInput VSFullScreen(uint index : SV_VertexID)
{
    PSInput result;

    result.position.x = index == 0 ? -3 : 1;
    result.position.y = index == 2 ? -3 : 1;
    result.position.z = 0.5;
    result.position.w = 1;
    result.color.x = index * (0xFFFFFu / 2);
    result.color.y = 0;
    result.color.z = 0;
    result.color.w = 0;

    return result;
}

uint PSFullScreen(PSInput input) : SV_TARGET
{
    return floor(input.color.x + input.position.x * 17 * 0x1000u + input.position.y * 11 * 0x1000u);
}
