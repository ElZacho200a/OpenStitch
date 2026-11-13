// SPDX-License-Identifier: Apache-2.0
#include "simulation_nav.hpp"

#include <algorithm>

#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {

Result<SimulationMarks> simulationMarks(const document::Project& project) {
    auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    return simulationMarks(*sequence);
}

SimulationMarks simulationMarks(const stitch::StitchSequence& sequence) {
    SimulationMarks marks;
    marks.total = sequence.commands.size();
    ObjectId current{};
    bool first = true;

    // Collecter les sauts et coupes en une seule passe
    std::vector<std::size_t> jump_positions;
    std::vector<std::size_t> trim_positions;

    for (std::size_t i = 0; i < sequence.commands.size(); ++i) {
        const auto& c = sequence.commands[i];

        // Collecter les sauts et coupes
        if (c.type == stitch::CommandType::Jump) {
            jump_positions.push_back(i + 1);
        } else if (c.type == stitch::CommandType::Trim) {
            trim_positions.push_back(i + 1);
        }

        if (c.type == stitch::CommandType::ColorChange) {
            marks.colours.push_back(i + 1);
            continue;
        }
        if (c.type != stitch::CommandType::Stitch && c.type != stitch::CommandType::Jump) {
            continue; // une coupe ne démarre pas un objet
        }
        if (first || c.source != current) {
            if (!first && i > 0) {
                marks.objects.push_back(i);
            }
            current = c.source;
            first = false;
        }
    }

    // Sauvegarder les positions de sauts et coupes
    marks.jumps = std::move(jump_positions);
    marks.trims = std::move(trim_positions);

    return marks;
}

std::size_t nextMark(const std::vector<std::size_t>& marks, std::size_t position,
                     std::size_t total) {
    const auto it = std::upper_bound(marks.begin(), marks.end(), position);
    return it == marks.end() ? total : *it;
}

std::size_t previousMark(const std::vector<std::size_t>& marks, std::size_t position) {
    const auto it = std::lower_bound(marks.begin(), marks.end(), position);
    return it == marks.begin() ? 0 : *(it - 1);
}

} // namespace openstitch::desktop_v2
