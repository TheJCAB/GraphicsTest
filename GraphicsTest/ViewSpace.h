#pragma once

#include "HLSL.h"
#include "Model.h"

struct ViewSpaceConstants
{
    // The viewport's width and height
    XMUINT2  Size;

    // Normalizes screen coordinates into alignment with view space
    XMFLOAT2 FlipAndAspectRatio       ; // Width / height, -1
    XMFLOAT2 InverseFlipAndAspectRatio; // Height / width, -1

    // View space's xy/z projected into [0..width, 0..height] screen space
    // Includes the aspect ratio, FOV and size.
    XMFLOAT2 ProjectionScale;
    XMFLOAT2 ProjectionBias ;

    // Screen position [0..width, 0..height] projected back into view space's xy/z
    // Includes the aspect ratio, FOV and size.
    XMFLOAT2 InverseProjectionScale;
    XMFLOAT2 InverseProjectionBias ;

    XMUINT2 Padding;

    XMFLOAT3X4 WorldToView;
    XMFLOAT3X4 ViewToWorld;
};

#if HLSL

// Barycentric coordinates and their deltas, used to sample textures inside a triangle.

struct BarycentricCoords
{
    float3 B;
    float3 dB_dx;
    float3 dB_dy;
    float3 dViewPos_dx;
    float3 dViewPos_dy;
};

// Get the barycentric coordinates and their deltas, given the triangle's geometry and a position in view space.
BarycentricCoords GetBarycentric(ViewSpaceConstants viewSpace, StructuredBuffer<VertexPosition> vertexPositions, uint3 indices, float4x4 modelToView, float3 viewPos)
{
    float3 viewPos0 = mul(float4(vertexPositions[indices[0]], 1), modelToView).xyz;
    float3 viewPos1 = mul(float4(vertexPositions[indices[1]], 1), modelToView).xyz;
    float3 viewPos2 = mul(float4(vertexPositions[indices[2]], 1), modelToView).xyz;

    // N is the normal to the triangle, and its length is the area.
    float3 N = cross(viewPos1 - viewPos0, viewPos2 - viewPos0);
    float3 normalizedN = normalize(N);

    BarycentricCoords result;

    float3 N_N2 = N / dot(N, N);

    result.dViewPos_dx = viewSpace.InverseProjectionScale.x * viewPos.z * (float3(1.0f, 0.0f, 0.0f) - normalizedN.x * viewPos / max(dot(viewPos, normalizedN), 1e-20f));
    result.dViewPos_dy = viewSpace.InverseProjectionScale.y * viewPos.z * (float3(0.0f, 1.0f, 0.0f) - normalizedN.y * viewPos / max(dot(viewPos, normalizedN), 1e-20f));

    float3 viewPos_dx = viewPos + result.dViewPos_dx;
    float3 viewPos_dy = viewPos + result.dViewPos_dy;

    result.B    [1] = dot(cross(viewPos2 - viewPos   , viewPos0 - viewPos   ), N_N2);
    result.B    [2] = dot(cross(viewPos0 - viewPos   , viewPos1 - viewPos   ), N_N2);
    result.dB_dx[1] = dot(cross(viewPos2 - viewPos_dx, viewPos0 - viewPos_dx), N_N2) - result.B[1];
    result.dB_dx[2] = dot(cross(viewPos0 - viewPos_dx, viewPos1 - viewPos_dx), N_N2) - result.B[2];
    result.dB_dy[1] = dot(cross(viewPos2 - viewPos_dy, viewPos0 - viewPos_dy), N_N2) - result.B[1];
    result.dB_dy[2] = dot(cross(viewPos0 - viewPos_dy, viewPos1 - viewPos_dy), N_N2) - result.B[2];

    result. B   [0] = 1.0f - (result.B    [1] + result.B    [2]);
    result.dB_dx[0] =      - (result.dB_dx[1] + result.dB_dx[2]); // Note: barycentric deltas add up to zero, not one.
    result.dB_dy[0] =      - (result.dB_dy[1] + result.dB_dy[2]);

    return result;
}

float Interpolate(float a, float b, float c, float3 barycentric)
{
    return a * barycentric[0] + b * barycentric[1] + c * barycentric[2];
}

float2 Interpolate(float2 a, float2 b, float2 c, float3 barycentric)
{
    return a * barycentric[0] + b * barycentric[1] + c * barycentric[2];
}

float3 Interpolate(float3 a, float3 b, float3 c, float3 barycentric)
{
    return a * barycentric[0] + b * barycentric[1] + c * barycentric[2];
}

float4 Interpolate(float4 a, float4 b, float4 c, float3 barycentric)
{
    return a * barycentric[0] + b * barycentric[1] + c * barycentric[2];
}

#else // !HLSL

namespace ViewSpace
{

inline
ViewSpaceConstants CalculateConstants(float const width, float const height, XMMATRIX worldToView)
{
    ViewSpaceConstants result
    {
        .Size = XMUINT2(width, height),

        .FlipAndAspectRatio        = XMFLOAT2(width / height, -1.0f),
        .InverseFlipAndAspectRatio = XMFLOAT2(height / width, -1.0f),

        // ViewPos.xy / z projected into [0..renderDevice.Width, 0..renderDevice.Height] screen space
        .ProjectionScale = XMFLOAT2( -height / 2.0f,  height / 2.0f),
        .ProjectionBias  = XMFLOAT2(  width  / 2.0f,  height / 2.0f),

        .InverseProjectionScale = XMFLOAT2( -2.0f / height,  2.0f / height),
        .InverseProjectionBias  = XMFLOAT2( width / height, -1.0f),
    };

    XMStoreFloat3x4(&result.WorldToView, XMMatrixTranspose(worldToView));
    XMStoreFloat3x4(&result.ViewToWorld, XMMatrixTranspose(XMMatrixInverse(nullptr, worldToView)));

    return result;
}

}
// namespace ViewSpace

#endif // !HLSL
