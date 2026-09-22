// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/metrics.hpp"

#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace openstitch::stitch_analysis {

SequenceMetrics sequence_metrics(const stitch::StitchSequence& sequence,
                                 const SequenceMetricsOptions& options) {
    using stitch::CommandType;
    SequenceMetrics m;
    const auto& cmds = sequence.commands;

    // Positions des points cousus, pour la reconnaissance des allers-retours.
    std::vector<std::size_t> stitchIdx;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        if (cmds[i].type == CommandType::Stitch) {
            stitchIdx.push_back(i);
        }
    }
    const auto lockLike = [&](std::size_t k) {
        if (cmds[stitchIdx[k]].pass == stitch::StitchPass::Lock) {
            return true;
        }
        if (!options.infer_locks) {
            return false;
        }
        const Vec2um p = cmds[stitchIdx[k]].pos;
        return (k >= 2 && cmds[stitchIdx[k - 2]].pos == p) ||
               (k + 2 < stitchIdx.size() && cmds[stitchIdx[k + 2]].pos == p);
    };

    std::optional<Vec2um> needle;     // position courante de l'aiguille
    std::optional<Vec2um> lastStitch; // dernière piqûre
    bool inMove = false;
    bool trimmed = false;
    std::size_t k = 0;
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const auto& c = cmds[i];
        switch (c.type) {
        case CommandType::Jump:
            inMove = true;
            break;
        case CommandType::Trim:
            ++m.trims;
            trimmed = true;
            break;
        case CommandType::ColorChange:
        case CommandType::Stop:
            ++m.color_changes;
            break;
        case CommandType::End:
            break;
        case CommandType::Stitch: {
            ++m.stitches;
            if (inMove && lastStitch) {
                ++m.moves;
                if (!trimmed && length_um(c.pos - *lastStitch) >
                                    static_cast<double>(options.trim_threshold.value)) {
                    ++m.long_moves_without_trim;
                }
            }
            if (needle) {
                const double len = length_um(c.pos - *needle);
                if (len < static_cast<double>(options.short_stitch.value)) {
                    if (lockLike(k)) {
                        ++m.short_lock_stitches;
                    } else {
                        ++m.short_stitches;
                    }
                }
                if (!inMove && len >= static_cast<double>(options.direction_min_length.value)) {
                    double deg = std::atan2(static_cast<double>(c.pos.y.value - needle->y.value),
                                            static_cast<double>(c.pos.x.value - needle->x.value)) *
                                 180.0 / std::numbers::pi;
                    deg = std::fmod(deg + 360.0, 180.0);
                    const auto bin = static_cast<std::size_t>(std::lround(deg / 5.0)) % 36;
                    ++m.direction_histogram[bin];
                }
            }
            lastStitch = c.pos;
            inMove = false;
            trimmed = false;
            ++k;
            break;
        }
        }
        needle = c.pos;
    }
    return m;
}

} // namespace openstitch::stitch_analysis
