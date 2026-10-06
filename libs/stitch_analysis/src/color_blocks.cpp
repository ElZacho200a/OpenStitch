// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/color_blocks.hpp"

namespace openstitch::stitch_analysis {

std::vector<stitch::ColorBlock> color_blocks(const document::Project& project,
                                             const stitch::StitchSequence& sequence) {
    std::vector<stitch::ColorBlock> blocks;
    const auto& cmds = sequence.commands;
    std::size_t start = 0;

    const auto closeBlock = [&](std::size_t end) {
        if (end > start) {
            stitch::ColorBlock block;
            block.start = start;
            block.end = end;
            if (const auto* emb = project.findEmbroidery(cmds[start].source)) {
                block.rgb = emb->rgb;
            } else {
                // Source inconnue (design importé, DST sans vraie couleur --
                // roadmap §2 FMT-002) : couleur par défaut honnête, jamais
                // une supposition.
                block.rgb = {0, 0, 0};
            }
            blocks.push_back(block);
        }
        start = end + 1;
    };

    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const auto type = cmds[i].type;
        if (type == stitch::CommandType::ColorChange || type == stitch::CommandType::Stop ||
            type == stitch::CommandType::End) {
            closeBlock(i);
        }
    }
    // Défensif : une séquence qui ne se terminerait pas par `End` (ne devrait
    // pas arriver, cf. invariants de `generate_sequence`/`decode_dst`) ne
    // perd pas silencieusement son dernier bloc.
    closeBlock(cmds.size());

    return blocks;
}

} // namespace openstitch::stitch_analysis
