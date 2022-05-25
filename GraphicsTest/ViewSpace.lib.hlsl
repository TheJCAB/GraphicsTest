
#include "ViewSpace.h"

float3 GetPixelCenterViewPos(ViewSpaceConstants view, uint2 pos, float depth)
{
    return float3(((pos + 0.5f) * view.InverseProjectionScale + view.InverseProjectionBias) * depth, depth);
}
