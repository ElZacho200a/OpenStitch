// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_render/thread_scene.hpp"

#include <format>

namespace openstitch::stitch_render {

std::string thread_scene_json(const std::vector<ThreadSegment>& segments, double thread_width_mm) {
    std::string out =
        std::format("{{\"format\": \"openstitch-thread-scene\", \"version\": 1, \"units\": \"mm\", "
                    "\"y_axis\": \"down\", \"thread_width_mm\": {},\n\"segments\": [",
                    thread_width_mm);
    out.reserve(out.size() + segments.size() * 56);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& g = segments[i];
        std::format_to(std::back_inserter(out), "{}\n[{:.4f}, {:.4f}, {:.4f}, {:.4f}, {}, {}, {}]",
                       i == 0 ? "" : ",", g.ax, g.ay, g.bx, g.by, g.rgb[0], g.rgb[1], g.rgb[2]);
    }
    out += "\n]}\n";
    return out;
}

} // namespace openstitch::stitch_render
