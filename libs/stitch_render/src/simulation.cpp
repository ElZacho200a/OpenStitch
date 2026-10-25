// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_render/simulation.hpp"

#include <algorithm>

namespace openstitch::stitch_render {

SimulationIndex build_simulation_index(const stitch::StitchSequence& sequence) {
    SimulationIndex idx;
    idx.color_starts.push_back(0);
    ObjectId current{};
    bool have = false;
    for (std::size_t i = 0; i < sequence.commands.size(); ++i) {
        const auto& c = sequence.commands[i];
        if (c.type == stitch::CommandType::ColorChange) {
            idx.color_starts.push_back(static_cast<int>(i));
            have = false;
        } else if (c.type == stitch::CommandType::Stitch) {
            if (!have || c.source != current) {
                idx.object_starts.push_back(static_cast<int>(i));
                current = c.source;
                have = true;
            }
        }
    }
    if (idx.object_starts.empty()) {
        idx.object_starts.push_back(0);
    }
    return idx;
}

int simulation_step_target(const SimulationIndex& index, std::size_t command_count, int current,
                           SimulationUnit unit, int direction) {
    if (command_count == 0) {
        return 0;
    }
    const int last = static_cast<int>(command_count) - 1;
    current = std::clamp(current, 0, last);
    if (unit == SimulationUnit::Stitch) {
        return std::clamp(current + (direction >= 0 ? 1 : -1), 0, last);
    }
    const std::vector<int>& starts =
        unit == SimulationUnit::Color ? index.color_starts : index.object_starts;
    if (direction >= 0) {
        const auto it = std::upper_bound(starts.begin(), starts.end(), current);
        return it == starts.end() ? last : std::min(*it, last);
    }
    const auto it = std::lower_bound(starts.begin(), starts.end(), current);
    return it == starts.begin() ? 0 : *(it - 1);
}

double simulation_elapsed_minutes(const stitch::StitchSequence& sequence, int step,
                                  const SimulationTiming& timing) {
    if (sequence.commands.empty() || step < 0) {
        return 0.0;
    }
    const std::size_t end = std::min(sequence.commands.size(), static_cast<std::size_t>(step) + 1);
    double stitches = 0.0;
    double seconds = 0.0;
    for (std::size_t i = 0; i < end; ++i) {
        switch (sequence.commands[i].type) {
        case stitch::CommandType::Stitch:
            stitches += 1.0;
            break;
        case stitch::CommandType::ColorChange:
            seconds += timing.color_change_seconds;
            break;
        case stitch::CommandType::Trim:
            seconds += timing.trim_seconds;
            break;
        default:
            break;
        }
    }
    const double spm = timing.stitches_per_minute > 0.0 ? timing.stitches_per_minute : 700.0;
    return stitches / spm + seconds / 60.0;
}

int simulation_color_rank(const SimulationIndex& index, int step) {
    const auto it = std::upper_bound(index.color_starts.begin(), index.color_starts.end(), step);
    return std::max<int>(1, static_cast<int>(it - index.color_starts.begin()));
}

double simulation_machine_steps_per_second(const SimulationTiming& timing) {
    return (timing.stitches_per_minute > 0.0 ? timing.stitches_per_minute : 700.0) / 60.0;
}

} // namespace openstitch::stitch_render
