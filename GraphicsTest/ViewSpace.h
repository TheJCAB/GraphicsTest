#pragma once

#include "HLSL.h"

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
};

#if !HLSL

namespace ViewSpace
{

inline
ViewSpaceConstants CalculateConstants(float const width, float const height)
{
    return ViewSpaceConstants
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
}

}
// namespace ViewSpace

#endif // !HLSL
