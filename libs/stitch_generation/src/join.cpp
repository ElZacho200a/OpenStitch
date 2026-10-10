// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/join.hpp"

#include <algorithm>

namespace openstitch::stitch_generation {

namespace {

using stitch::CommandType;
using stitch::StitchCommand;
using stitch::StitchPass;

// Un tracé : un Jump suivi de ses points cousus (éventuellement aucun).
struct TracePath {
    std::vector<StitchCommand> cmds;
    bool underlay{false};
};

// Découpe `chunk` en tracés. Faux si le tronçon n'a pas la forme attendue.
bool split_paths(const std::vector<StitchCommand>& chunk, std::vector<TracePath>& out) {
    if (chunk.empty() || chunk.front().type != CommandType::Jump) {
        return false;
    }
    for (const StitchCommand& c : chunk) {
        if (c.type == CommandType::Jump) {
            out.emplace_back();
        } else if (c.type != CommandType::Stitch) {
            return false;
        }
        out.back().cmds.push_back(c);
    }
    bool previousUnderlay = false;
    bool seenTop = false;
    for (TracePath& p : out) {
        bool u = false;
        bool t = false;
        for (const StitchCommand& c : p.cmds) {
            if (c.type != CommandType::Stitch) {
                continue;
            }
            (c.pass == StitchPass::Underlay ? u : t) = true;
        }
        if (u && t) {
            return false; // tracé enchaîné sous-couche -> couche supérieure : on n'y touche pas
        }
        p.underlay = (u || t) ? u : previousUnderlay; // tracé sans point : suit le précédent
        if (!p.underlay) {
            seenTop = true;
        } else if (seenTop) {
            return false; // sous-couche après la couche supérieure
        }
        previousUnderlay = p.underlay;
    }
    return true;
}

// Même tracé parcouru en sens inverse : mêmes pénétrations, ordre inversé.
TracePath reverse_path(const TracePath& path) {
    std::vector<const StitchCommand*> stitches;
    for (const StitchCommand& c : path.cmds) {
        if (c.type == CommandType::Stitch) {
            stitches.push_back(&c);
        }
    }
    if (stitches.empty()) {
        return path;
    }
    TracePath out;
    out.underlay = path.underlay;
    const std::size_t n = stitches.size();
    const StitchCommand& jump = path.cmds.front();
    out.cmds.push_back({stitches[n - 1]->pos, CommandType::Jump, jump.source, StitchPass::Travel});
    // Première pénétration : atterrissage sur l'ancien dernier point.
    out.cmds.push_back({stitches[n - 1]->pos, CommandType::Stitch, stitches[n - 1]->source,
                        stitches[n - 1]->pass});
    for (std::size_t j = 1; j < n; ++j) {
        // Le segment S'[j-1] -> S'[j] est l'ancien segment S[n-j-1] -> S[n-j] : sa passe.
        out.cmds.push_back({stitches[n - 1 - j]->pos, CommandType::Stitch,
                            stitches[n - 1 - j]->source, stitches[n - j]->pass});
    }
    return out;
}

// Une couche : tracés inversés (ordre des tracés et sens de chacun) ou conservés.
std::vector<TracePath> layer(const std::vector<TracePath>& paths, bool reversed) {
    if (!reversed) {
        return paths;
    }
    std::vector<TracePath> out;
    out.reserve(paths.size());
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
        out.push_back(reverse_path(*it));
    }
    return out;
}

std::vector<StitchCommand> assemble(const std::vector<TracePath>& under,
                                    const std::vector<TracePath>& top) {
    std::vector<StitchCommand> out;
    for (const auto* layerPaths : {&under, &top}) {
        for (const TracePath& p : *layerPaths) {
            out.insert(out.end(), p.cmds.begin(), p.cmds.end());
        }
    }
    return out;
}

double internal_jumps(const std::vector<StitchCommand>& cmds) {
    double total = 0.0;
    for (std::size_t i = 1; i < cmds.size(); ++i) {
        if (cmds[i].type == CommandType::Jump) {
            total += length_um(cmds[i].pos - cmds[i - 1].pos);
        }
    }
    return total;
}

} // namespace

bool join_active(const document::Project& project, const document::EmbroideryObject& object) {
    switch (object.join) {
    case document::JoinMode::Auto:
        return true;
    case document::JoinMode::Off:
        return false;
    case document::JoinMode::Inherit:
        break;
    }
    return project.finishing.auto_join;
}

std::vector<StitchCommand> orient_chunk(const std::vector<StitchCommand>& chunk,
                                        std::optional<Vec2um> previous_end,
                                        std::optional<Vec2um> next_start) {
    if (!previous_end && !next_start) {
        return chunk;
    }
    std::vector<TracePath> paths;
    if (!split_paths(chunk, paths)) {
        return chunk;
    }
    std::vector<TracePath> under;
    std::vector<TracePath> top;
    for (TracePath& p : paths) {
        (p.underlay ? under : top).push_back(std::move(p));
    }
    const auto cost = [&](const std::vector<StitchCommand>& c) {
        double d = internal_jumps(c);
        if (previous_end) {
            d += length_um(c.front().pos - *previous_end);
        }
        if (next_start) {
            d += 0.5 * length_um(*next_start - c.back().pos);
        }
        return d;
    };
    std::vector<StitchCommand> best = chunk;
    double bestCost = cost(chunk);
    // Combinaison naturelle = `chunk` lui-même (déjà évalué) ; les trois autres suivent.
    for (const auto& [ru, rt] :
         {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}}) {
        if ((ru && under.empty()) || (rt && top.empty())) {
            continue;
        }
        std::vector<StitchCommand> candidate = assemble(layer(under, ru), layer(top, rt));
        const double c = cost(candidate);
        if (c < bestCost - 1.0) { // gain d'au moins 1 µm : sinon on garde le sens naturel
            bestCost = c;
            best = std::move(candidate);
        }
    }
    return best;
}

double total_jump_length_um(const stitch::StitchSequence& sequence) {
    return internal_jumps(sequence.commands);
}

} // namespace openstitch::stitch_generation
