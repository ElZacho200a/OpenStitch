// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/predicted_pull.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>
#include <vector>

#include "openstitch/stitch_generation/pull_map.hpp"

namespace openstitch::stitch_generation {

namespace {

bool is_predicted(const document::StitchParams& params) {
    return std::visit(
        [](const auto& p) {
            if constexpr (requires { p.pull_mode; }) {
                return p.pull_mode == document::PullMode::Predicted;
            } else {
                return false;
            }
        },
        params);
}

} // namespace

void apply_predicted_pull(stitch::StitchSequence& sequence, const document::Project& project,
                          const FabricResponse& fabric) {
    std::vector<ObjectId> predicted;
    for (const document::EmbroideryObject& object : project.embroidery_objects) {
        if (is_predicted(object.params)) {
            predicted.push_back(object.id);
        }
    }
    if (predicted.empty()) {
        return; // aucun objet concerné : séquence strictement inchangée
    }
    std::sort(predicted.begin(), predicted.end());

    const auto& cmds = sequence.commands;
    std::vector<PullSource> sources = collect_pull_sources(sequence);
    if (sources.empty()) {
        return;
    }
    const PullField field(std::move(sources), fabric);

    // Positions d'origine échantillonnées d'abord : le champ ne voit jamais un point déjà déplacé.
    std::vector<Vec2um> shifted(cmds.size());
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const stitch::StitchCommand& c = cmds[i];
        shifted[i] = c.pos;
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::TopStitch &&
            std::binary_search(predicted.begin(), predicted.end(), c.source)) {
            shifted[i] = c.pos + field.compensation_at(c.pos);
        }
    }
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        sequence.commands[i].pos = shifted[i];
    }
}

} // namespace openstitch::stitch_generation
