// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch/sequence.hpp"

#include <algorithm>
#include <cstdint>
#include <map>

namespace openstitch::stitch {

StitchStats compute_stats(const StitchSequence& sequence) {
    StitchStats stats;
    bool hasPrev = false;
    bool hasBounds = false;
    Vec2um prev{};

    for (const StitchCommand& cmd : sequence.commands) {
        switch (cmd.type) {
        case CommandType::Stitch:
            ++stats.stitches;
            if (hasPrev) {
                stats.thread_length_um += length_um(cmd.pos - prev);
            }
            if (!hasBounds) {
                stats.bounds.min = stats.bounds.max = cmd.pos;
                hasBounds = true;
            } else {
                stats.bounds.min.x = std::min(stats.bounds.min.x, cmd.pos.x);
                stats.bounds.min.y = std::min(stats.bounds.min.y, cmd.pos.y);
                stats.bounds.max.x = std::max(stats.bounds.max.x, cmd.pos.x);
                stats.bounds.max.y = std::max(stats.bounds.max.y, cmd.pos.y);
            }
            prev = cmd.pos;
            hasPrev = true;
            break;
        case CommandType::Jump:
            ++stats.jumps;
            prev = cmd.pos; // le fil ne coud pas mais l'aiguille se déplace
            hasPrev = true;
            break;
        case CommandType::Trim:
            ++stats.trims;
            break;
        case CommandType::ColorChange:
            ++stats.color_changes;
            break;
        case CommandType::Stop:
        case CommandType::End:
            break;
        }
    }
    return stats;
}

std::vector<ObjectEndpoints> object_endpoints(const StitchSequence& sequence) {
    std::vector<ObjectEndpoints> out;
    std::map<std::uint64_t, std::size_t> index;
    for (const StitchCommand& cmd : sequence.commands) {
        if (cmd.type != CommandType::Stitch || cmd.source.value == 0) {
            continue;
        }
        const auto [it, inserted] = index.try_emplace(cmd.source.value, out.size());
        if (inserted) {
            out.push_back({cmd.source, cmd.pos, cmd.pos});
        } else {
            out[it->second].last = cmd.pos;
        }
    }
    return out;
}

} // namespace openstitch::stitch
