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
    float4x4 projection;
};

float4 VisibilityVS(float4 position : POSITION) : SV_POSITION
{
    return mul(position, projection);
}

uint VisibilityPS(uint tri : SV_PrimitiveID) : SV_TARGET
{
    return tri | 0x00100000;
}




struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR;
};

PSInput VSModel(float4 position : POSITION, float4 color : COLOR)
{
    PSInput result;

    result.position = mul(position, projection);
    result.color = color;

    return result;
}

float4 PSModel(PSInput input, uint tri : SV_PrimitiveID) : SV_TARGET
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
