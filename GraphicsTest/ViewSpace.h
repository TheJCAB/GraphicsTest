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
};

// Get the barycentric coordinates and their deltas, given the triangle's geometry and a position in view space.
BarycentricCoords GetBarycentric(ViewSpaceConstants viewSpace, StructuredBuffer<VertexPosition> vertexPositions, uint3 indices, float4x4 modelToView, float3 viewPos)
{
    float3 viewPos0 = mul(float4(vertexPositions[indices[0]], 1), modelToView).xyz;
    float3 viewPos1 = mul(float4(vertexPositions[indices[1]], 1), modelToView).xyz;
    float3 viewPos2 = mul(float4(vertexPositions[indices[2]], 1), modelToView).xyz;

    // N is the normal to the triangle, and its length is the area.
    float3 N = cross(viewPos1 - viewPos0, viewPos2 - viewPos0);

    // Back-project the viewPos deltas of the pixel deltas, along each axis, the Z plane of viewPos.
    float dViewPosX_dx = viewSpace.InverseProjectionScale.x * viewPos.z;
    float dViewPosY_dy = viewSpace.InverseProjectionScale.y * viewPos.z;

    float viewPosDotN = dot(viewPos, N);
    float dViewPosX_dx_Nx = dViewPosX_dx * N.x;
    float dViewPosY_dy_Ny = dViewPosY_dy * N.y;

    // viewPosDotN + dViewPos*N will be used as denominator below, so for numerical stability
    // we choose between that and viewPosDotN - dViewPos*N, on each axis.
    // We do that by inverting the deltas using sign multiplers, and then un-inverting later below the same way.
    float signX = abs(viewPosDotN + dViewPosX_dx_Nx) > abs(viewPosDotN - dViewPosX_dx_Nx) ? 1.0f : -1.0f;
    float signY = abs(viewPosDotN + dViewPosY_dy_Ny) > abs(viewPosDotN - dViewPosY_dy_Ny) ? 1.0f : -1.0f;

    // We'll find the view positions, in the triangle, one pixel over on each axis, so we can calculate derivatives.
    float3 zViewPos_dx = viewPos;
    float3 zViewPos_dy = viewPos;

    // Add the back-projection of the pixel deltas, into the Z plane of viewPos.
    zViewPos_dx.x += dViewPosX_dx * signX;
    zViewPos_dy.y += dViewPosY_dy * signY;

    // Adjust for the triangle's orientation, by pushing/pulling the Z-plane view positions into the triangle's plane.
    float3 viewPos_dx = zViewPos_dx * viewPosDotN / (viewPosDotN + dViewPosX_dx_Nx * signX);
    float3 viewPos_dy = zViewPos_dy * viewPosDotN / (viewPosDotN + dViewPosY_dy_Ny * signY);

    BarycentricCoords result;

    float3 N_N2 = N / dot(N, N);

    result.B    [1] =  dot(cross(viewPos2 - viewPos   , viewPos0 - viewPos   ), N_N2);
    result.B    [2] =  dot(cross(viewPos0 - viewPos   , viewPos1 - viewPos   ), N_N2);
    result.dB_dx[1] = (dot(cross(viewPos2 - viewPos_dx, viewPos0 - viewPos_dx), N_N2) - result.B[1]) * signX;
    result.dB_dx[2] = (dot(cross(viewPos0 - viewPos_dx, viewPos1 - viewPos_dx), N_N2) - result.B[2]) * signX;
    result.dB_dy[1] = (dot(cross(viewPos2 - viewPos_dy, viewPos0 - viewPos_dy), N_N2) - result.B[1]) * signY;
    result.dB_dy[2] = (dot(cross(viewPos0 - viewPos_dy, viewPos1 - viewPos_dy), N_N2) - result.B[2]) * signY;

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
