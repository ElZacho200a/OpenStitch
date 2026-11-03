// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/stitch_pick.hpp"

#include <algorithm>
#include <cstdint>

namespace openstitch::stitch_analysis {

namespace {

// Carré de la distance (µm²) de `p` au segment [a, b], en arithmétique entière sauf la projection.
std::int64_t segment_distance_sq(Vec2um p, Vec2um a, Vec2um b) {
    const std::int64_t abx = static_cast<std::int64_t>(b.x.value) - a.x.value;
    const std::int64_t aby = static_cast<std::int64_t>(b.y.value) - a.y.value;
    const std::int64_t apx = static_cast<std::int64_t>(p.x.value) - a.x.value;
    const std::int64_t apy = static_cast<std::int64_t>(p.y.value) - a.y.value;
    const std::int64_t len_sq = abx * abx + aby * aby;
    if (len_sq == 0) {
        return apx * apx + apy * apy;
    }
    const std::int64_t dot = std::clamp<std::int64_t>(apx * abx + apy * aby, 0, len_sq);
    // Point projeté = a + ab * dot / len_sq (arrondi à l'entier le plus proche).
    const std::int64_t qx = (abx * dot + (dot >= 0 ? len_sq / 2 : -len_sq / 2)) / len_sq;
    const std::int64_t qy = (aby * dot + (dot >= 0 ? len_sq / 2 : -len_sq / 2)) / len_sq;
    const std::int64_t dx = apx - qx;
    const std::int64_t dy = apy - qy;
    return dx * dx + dy * dy;
}

} // namespace

std::optional<ObjectId> stitch_object_at(const stitch::StitchSequence& sequence, Vec2um pos,
                                         Micrometers tolerance) {
    const std::int64_t tol = std::max<std::int64_t>(0, tolerance.value);
    const std::int64_t tol_sq = tol * tol;
    std::optional<ObjectId> found;
    const stitch::StitchCommand* previous = nullptr;
    for (const auto& command : sequence.commands) {
        const bool sewn = command.type == stitch::CommandType::Stitch &&
                          command.pass != stitch::StitchPass::Travel &&
                          command.pass != stitch::StitchPass::Lock;
        if (!sewn) {
            // Un saut, une coupe ou un changement de fil rompt la chaîne de piqûres.
            if (command.type != stitch::CommandType::Stitch) {
                previous = nullptr;
            }
            continue;
        }
        const Vec2um from = previous != nullptr ? previous->pos : command.pos;
        if (segment_distance_sq(pos, from, command.pos) <= tol_sq && command.source.value != 0) {
            found = command.source; // le dernier cousu gagne
        }
        previous = &command;
    }
    return found;
}

} // namespace openstitch::stitch_analysis
