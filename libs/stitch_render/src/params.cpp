// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_render/params.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace openstitch::stitch_render {

namespace {

double clamp01(double v) {
    // NaN -> 0 (la comparaison échoue) : jamais de valeur non finie en aval.
    if (!(v > 0.0)) {
        return 0.0;
    }
    return std::min(v, 1.0);
}

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void mix(std::uint64_t& h, std::uint64_t v) {
    h = (h ^ v) * kFnvPrime;
}

} // namespace

RenderParams sanitized(RenderParams params) {
    if (!std::isfinite(params.thread_width_mm)) {
        params.thread_width_mm = RenderParams{}.thread_width_mm;
    }
    params.thread_width_mm =
        std::clamp(params.thread_width_mm, kMinThreadWidthMm, kMaxThreadWidthMm);
    params.relief = clamp01(params.relief);
    params.sheen = clamp01(params.sheen);
    params.twist = clamp01(params.twist);
    params.shadow = clamp01(params.shadow);
    params.fabric_relief = clamp01(params.fabric_relief);
    return params;
}

std::uint64_t hash_params(const RenderParams& params) {
    std::uint64_t h = kFnvOffset;
    mix(h, std::bit_cast<std::uint64_t>(params.thread_width_mm));
    mix(h, std::bit_cast<std::uint64_t>(params.relief));
    mix(h, std::bit_cast<std::uint64_t>(params.sheen));
    mix(h, std::bit_cast<std::uint64_t>(params.twist));
    mix(h, std::bit_cast<std::uint64_t>(params.shadow));
    for (const std::uint8_t c : params.fabric_rgb) {
        mix(h, c);
    }
    mix(h, params.fabric_opaque ? 1U : 0U);
    mix(h, static_cast<std::uint64_t>(params.fabric_texture));
    mix(h, std::bit_cast<std::uint64_t>(params.fabric_relief));
    mix(h, static_cast<std::uint64_t>(params.quality));
    return h;
}

} // namespace openstitch::stitch_render
