// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace openstitch::compute_experiment::d3d11_kernels {

inline constexpr char morphology[] = R"hlsl(
cbuffer Parameters : register(b0) {
    uint width;
    uint height;
    uint offsetCount;
    uint hasZero;
};
StructuredBuffer<uint> source : register(t0);
StructuredBuffer<int2> offsets : register(t1);
RWStructuredBuffer<uint> target : register(u0);

uint applyOffsetKernel(uint2 xy, bool erode) {
    uint neutral = erode ? 255u : 0u;
    uint bound = erode ? 0u : 255u;
    uint result = neutral;
    [loop]
    for (uint i = 0; i < offsetCount; ++i) {
        int2 at = int2(xy) + offsets[i];
        if (at.x >= 0 && at.y >= 0 && at.x < int(width) && at.y < int(height)) {
            uint value = source[uint(at.y) * width + uint(at.x)];
            result = erode ? min(result, value) : max(result, value);
            if (result == bound) {
                break;
            }
        }
    }
    return result;
}

[numthreads(16, 16, 1)]
void erode(uint3 tid : SV_DispatchThreadID) {
    if (tid.x < width && tid.y < height) {
        target[tid.y * width + tid.x] = applyOffsetKernel(tid.xy, true);
    }
}

[numthreads(16, 16, 1)]
void dilate(uint3 tid : SV_DispatchThreadID) {
    if (tid.x < width && tid.y < height) {
        target[tid.y * width + tid.x] = applyOffsetKernel(tid.xy, false);
    }
}
)hlsl";

inline constexpr char distance[] = R"hlsl(
cbuffer Parameters : register(b0) {
    uint width;
    uint height;
    uint offsetCount;
    uint hasZero;
};
StructuredBuffer<uint> source : register(t0);
RWStructuredBuffer<uint> target : register(u0);

// The same Q16 chamfer weights and saturation limit as OpenCV's 3x3 path.
static const uint axial = 62587u;
static const uint diagonal = 89738u;
static const uint farDistance = 0xffffffffu - diagonal;

[numthreads(64, 1, 1)]
void nearestInRow(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= height) {
        return;
    }
    uint first = tid.x * width;
    uint dx = width;
    [loop]
    for (uint x = 0; x < width; ++x) {
        uint index = first + x;
        dx = source[index] == 0u ? 0u : min(dx + 1u, width);
        target[index] = dx;
    }
    dx = width;
    [loop]
    for (uint x = width; x > 0; --x) {
        uint index = first + x - 1u;
        dx = source[index] == 0u ? 0u : min(dx + 1u, width);
        target[index] = min(target[index], dx);
    }
}

uint chamferCost(uint dx, uint dy) {
    return axial * max(dx, dy) + (diagonal - axial) * min(dx, dy);
}

uint considerRow(uint x, uint row, uint dy, uint best) {
    uint dx = source[row * width + x];
    return dx < width ? min(best, chamferCost(dx, dy)) : best;
}

[numthreads(16, 16, 1)]
void nearestAcrossRows(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= width || tid.y >= height) {
        return;
    }
    uint index = tid.y * width + tid.x;
    if (hasZero == 0u) {
        target[index] = farDistance;
        return;
    }
    uint best = considerRow(tid.x, tid.y, 0u, farDistance);
    [loop]
    for (uint dy = 1u; dy < height && axial * dy < best; ++dy) {
        if (dy <= tid.y) {
            best = considerRow(tid.x, tid.y - dy, dy, best);
        }
        if (dy < height - tid.y) {
            best = considerRow(tid.x, tid.y + dy, dy, best);
        }
    }
    target[index] = best;
}
)hlsl";

} // namespace openstitch::compute_experiment::d3d11_kernels
