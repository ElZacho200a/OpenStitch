// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_render/segments.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace openstitch::stitch_render {

std::vector<ThreadSegment> build_thread_segments(const stitch::StitchSequence& sequence,
                                                 std::size_t last_index,
                                                 const ThreadColorFn& color_of) {
    std::vector<ThreadSegment> out;
    const auto& commands = sequence.commands;
    const std::size_t count = last_index >= commands.size() ? commands.size() : last_index + 1;
    out.reserve(count);

    bool has_pos = false;
    float last_x = 0.0F;
    float last_y = 0.0F;
    std::uint64_t last_source = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& cmd = commands[i];
        const bool sews = cmd.type == stitch::CommandType::Stitch;
        const bool jumps = cmd.type == stitch::CommandType::Jump;
        if (!sews && !jumps) {
            // ColorChange / Trim / Stop / End : rompt la continuité.
            has_pos = false;
            continue;
        }
        const float x = static_cast<float>(static_cast<double>(cmd.pos.x.value) / 1000.0);
        const float y = static_cast<float>(-static_cast<double>(cmd.pos.y.value) / 1000.0);
        const std::uint64_t src = cmd.source.value;
        if (sews && has_pos && last_source == src && (x != last_x || y != last_y)) {
            if (const auto rgb = color_of(cmd.source)) {
                out.push_back(ThreadSegment{last_x, last_y, x, y, *rgb});
            }
        }
        last_x = x;
        last_y = y;
        has_pos = true;
        last_source = src;
    }
    return out;
}

RectMm segments_bounds(const std::vector<ThreadSegment>& segments) {
    if (segments.empty()) {
        return {};
    }
    float x0 = std::numeric_limits<float>::max();
    float y0 = x0;
    float x1 = std::numeric_limits<float>::lowest();
    float y1 = x1;
    for (const auto& s : segments) {
        x0 = std::min({x0, s.ax, s.bx});
        x1 = std::max({x1, s.ax, s.bx});
        y0 = std::min({y0, s.ay, s.by});
        y1 = std::max({y1, s.ay, s.by});
    }
    return RectMm{x0, y0, x1, y1};
}

std::uint64_t hash_segments(const std::vector<ThreadSegment>& segments) {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](std::uint64_t v) { h = (h ^ v) * kPrime; };
    mix(segments.size());
    for (const auto& s : segments) {
        mix(std::bit_cast<std::uint32_t>(s.ax));
        mix(std::bit_cast<std::uint32_t>(s.ay));
        mix(std::bit_cast<std::uint32_t>(s.bx));
        mix(std::bit_cast<std::uint32_t>(s.by));
        mix((static_cast<std::uint64_t>(s.rgb[0]) << 16) |
            (static_cast<std::uint64_t>(s.rgb[1]) << 8) | s.rgb[2]);
    }
    return h;
}

} // namespace openstitch::stitch_render
