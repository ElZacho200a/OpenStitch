// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace openstitch::compute_experiment::tatami_d3d11_shader {

inline constexpr char source[] = R"hlsl(
struct Job {
    uint2 x;
    uint2 y;
    uint2 dx;
    uint2 dy;
    uint firstRow;
    uint rowCount;
    uint outputOffset;
    uint padding;
};

StructuredBuffer<uint2> rows : register(t0);
StructuredBuffer<Job> jobs : register(t1);
RWStructuredBuffer<uint2> output : register(u0);

double decode(uint2 bits) {
    return asdouble(bits.x, bits.y);
}

[numthreads(64, 1, 1)]
void intersectRows(uint3 group : SV_GroupID, uint3 lane : SV_GroupThreadID) {
    Job job = jobs[group.x];
    double x = decode(job.x);
    double y = decode(job.y);
    double dx = decode(job.dx);
    double dy = decode(job.dy);
    [loop]
    for (uint i = lane.x; i < job.rowCount; i += 64u) {
        double scanY = decode(rows[job.firstRow + i]);
        precise double delta = scanY - y;
        precise double t = delta / dy;
        precise double product = t * dx;
        precise double projected = x + product;
        uint low;
        uint high;
        asuint(projected, low, high);
        output[job.outputOffset + i] = uint2(low, high);
    }
}
)hlsl";

} // namespace openstitch::compute_experiment::tatami_d3d11_shader
