// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/finish.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <vector>

#include "openstitch/stitch_generation/lock.hpp"
#include "openstitch/stitch_generation/tatami.hpp"

namespace openstitch::stitch_generation {

namespace {

using stitch::CommandType;
using stitch::StitchCommand;
using stitch::StitchPass;

// Tracé : commandes [begin, end) toutes de type Stitch.
struct Run {
    std::size_t begin;
    std::size_t end;
};

std::vector<Run> find_runs(const std::vector<StitchCommand>& cmds) {
    std::vector<Run> runs;
    for (std::size_t i = 0; i < cmds.size();) {
        if (cmds[i].type != CommandType::Stitch) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < cmds.size() && cmds[j].type == CommandType::Stitch) {
            ++j;
        }
        runs.push_back({i, j});
        i = j;
    }
    return runs;
}

// Point d'arrêt ancré en `anchor` (premier ou dernier point du tracé),
// orienté vers le point du tracé le plus proche distinct de l'ancre -- le
// verrou reste donc sur un segment réellement cousu de l'objet. Le premier
// point renvoyé par `lock_stitches` (l'ancre elle-même) est omis. Vide si
// aucun verrou n'est demandé ou si le tracé n'a pas de direction.
std::vector<StitchCommand> lock_points(const std::vector<StitchCommand>& cmds, const Run& run,
                                       bool atStart, const document::SequenceFinishing& f) {
    std::vector<StitchCommand> out;
    if (f.lock_type == document::LockStitch::None || run.end - run.begin < 2) {
        return out;
    }
    const std::size_t anchorIdx = atStart ? run.begin : run.end - 1;
    const Vec2um anchor = cmds[anchorIdx].pos;
    std::optional<Vec2um> toward;
    if (atStart) {
        for (std::size_t k = run.begin + 1; k < run.end && !toward; ++k) {
            if (cmds[k].pos != anchor) {
                toward = cmds[k].pos;
            }
        }
    } else {
        for (std::size_t k = run.end - 1; k-- > run.begin && !toward;) {
            if (cmds[k].pos != anchor) {
                toward = cmds[k].pos;
            }
        }
    }
    if (!toward) {
        return out; // tracé dégénéré (un seul point distinct) : pas de direction fiable
    }
    const auto pts =
        lock_stitches(anchor, *toward, static_cast<LockType>(static_cast<int>(f.lock_type)),
                      f.lock_length, f.lock_passes);
    const ObjectId source = cmds[anchorIdx].source;
    for (std::size_t k = 1; k < pts.size(); ++k) {
        out.push_back({pts[k], CommandType::Stitch, source, StitchPass::Lock});
    }
    return out;
}

void append(std::vector<StitchCommand>& out, const std::vector<StitchCommand>& more) {
    out.insert(out.end(), more.begin(), more.end());
}

// Surfaces de référence des objets (objet vectoriel suivi), pour vérifier
// qu'une fusion de points ne fait jamais sortir le fil de sa région.
std::map<std::uint64_t, const std::vector<geometry::PathSet>*>
object_regions(const document::Project& project) {
    std::map<std::uint64_t, const std::vector<geometry::PathSet>*> out;
    for (const auto& obj : project.embroidery_objects) {
        if (const auto* vec = project.findObject(obj.source_vector)) {
            out[obj.id.value] = &vec->paths;
        }
    }
    return out;
}

// Lot F : fusionne chaque point cousu plus court que `min_stitch_length` avec
// le suivant (le point court est retiré, aucun point n'est déplacé). Jamais
// le premier ni le dernier point d'un tracé, jamais une passe de verrou ni un
// point retouché à la main (passe Manual), et seulement si le nouveau segment
// reste dans la région de l'objet (`segment_stays_in_region` sur son objet
// vectoriel ; objet sans surface -- satin manuel -- : écart borné par la
// longueur minimale, accepté).
std::vector<StitchCommand> filter_short_stitches(const std::vector<StitchCommand>& cmds,
                                                 const document::Project& project,
                                                 const document::SequenceFinishing& f) {
    if (!f.filter_short_stitches || f.min_stitch_length.value <= 0) {
        return cmds;
    }
    const auto regions = object_regions(project);
    // Un testeur par morceau de l'objet vectoriel, construit à la première
    // demande : même prédicat que `segment_stays_in_region`, sans reconstruire
    // les polygones de l'objet à chaque point court (audit perf 2026-09).
    std::map<std::uint64_t, std::vector<RegionSegmentTester>> testers;
    const auto chordInside = [&](ObjectId source, Vec2um a, Vec2um b) {
        const auto it = regions.find(source.value);
        if (it == regions.end()) {
            return true;
        }
        auto [tit, inserted] = testers.try_emplace(source.value);
        if (inserted) {
            for (const auto& set : *it->second) {
                tit->second.emplace_back(set);
            }
        }
        return std::any_of(tit->second.begin(), tit->second.end(),
                           [&](const RegionSegmentTester& t) { return t.stays_inside(a, b); });
    };
    const double minLen = static_cast<double>(f.min_stitch_length.value);
    std::vector<StitchCommand> out;
    out.reserve(cmds.size());
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const StitchCommand& c = cmds[i];
        const bool interior = c.type == CommandType::Stitch && i > 0 &&
                              cmds[i - 1].type == CommandType::Stitch && i + 1 < cmds.size() &&
                              cmds[i + 1].type == CommandType::Stitch;
        if (interior && c.pass != StitchPass::Lock && c.pass != StitchPass::Manual &&
            cmds[i + 1].pass != StitchPass::Lock) {
            const Vec2um prev = out.back().pos; // dernier point conservé du tracé
            if (length_um(c.pos - prev) < minLen && chordInside(c.source, prev, cmds[i + 1].pos)) {
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

bool starts_with_lock(const std::vector<StitchCommand>& cmds, const Run& run) {
    return cmds[run.begin].pass == StitchPass::Lock ||
           (run.end - run.begin >= 2 && cmds[run.begin + 1].pass == StitchPass::Lock);
}

bool ends_with_lock(const std::vector<StitchCommand>& cmds, const Run& run) {
    return cmds[run.end - 1].pass == StitchPass::Lock;
}

} // namespace

stitch::StitchSequence finish_sequence(const stitch::StitchSequence& sequence,
                                       const document::Project& project) {
    const document::SequenceFinishing& f = project.finishing;
    if (!f.enabled) {
        return sequence;
    }
    // Points courts d'abord (Lot F) : les verrous ajoutés ensuite ne sont
    // jamais filtrés.
    const std::vector<StitchCommand> cmds = filter_short_stitches(sequence.commands, project, f);
    const std::vector<Run> runs = find_runs(cmds);

    stitch::StitchSequence out;
    out.commands.reserve(cmds.size() + runs.size() * 8);
    std::size_t cursor = 0;
    const Run* prev = nullptr;
    for (const Run& run : runs) {
        bool entryLock = true; // premier tracé : le fil démarre ici
        // Verrou d'entrée potentiel et piqûre de longueur nulle à l'arrivée
        // d'un saut (Jump p0, Stitch p0) : superflue quand un verrou d'entrée
        // suit, puisqu'il revient piquer en p0 -- sinon c'est un « point » de
        // 0 mm dans le fichier machine. Le fil atterrit alors sur le premier
        // point du verrou, jusqu'à `lock_length` plus loin : c'est CE point qui
        // décide de la coupe, pas p0 (sinon un déplacement jugé à 2,9 mm
        // devient 3,5 mm sans coupe -- défaut mesuré sur la marine).
        const auto entryCandidate = starts_with_lock(cmds, run) ? std::vector<StitchCommand>{}
                                                                : lock_points(cmds, run, true, f);
        const bool arrivesByJump = run.begin > cursor &&
                                   cmds[run.begin - 1].type == CommandType::Jump &&
                                   cmds[run.begin - 1].pos == cmds[run.begin].pos;
        if (prev != nullptr) {
            const StitchCommand& last = cmds[prev->end - 1];
            StitchCommand first = cmds[run.begin];
            if (arrivesByJump && !entryCandidate.empty()) {
                const double viaLock = length_um(entryCandidate.front().pos - last.pos);
                if (viaLock > length_um(first.pos - last.pos)) {
                    first.pos = entryCandidate.front().pos;
                }
            }
            bool colorChange = false;
            bool hasTrim = false;
            for (std::size_t k = cursor; k < run.begin; ++k) {
                colorChange = colorChange || cmds[k].type == CommandType::ColorChange ||
                              cmds[k].type == CommandType::Stop;
                hasTrim = hasTrim || cmds[k].type == CommandType::Trim;
            }
            const bool boundary = last.source != first.source;
            // Entre deux objets distincts, tout déplacement réel est coupé : le
            // seuil ne protège que les sauts internes à un objet. Sans cela, un
            // écart de moins de `trim_threshold` entre deux formes laissait un
            // fil visible tendu d'une forme à l'autre (correctif 2026-10-08).
            const double travel = length_um(first.pos - last.pos);
            const bool objectGap = boundary && !colorChange && travel > 0.0;
            const bool needTrim = !hasTrim && (travel > f.trim_threshold.value || objectGap ||
                                               (colorChange && f.trim_before_color_change));
            const bool cut = needTrim || hasTrim || colorChange;
            if ((boundary || cut) && !ends_with_lock(cmds, *prev)) {
                append(out.commands, lock_points(cmds, *prev, false, f));
            }
            if (needTrim) {
                out.commands.push_back(
                    {last.pos, CommandType::Trim, last.source, StitchPass::Travel});
            }
            entryLock = boundary || cut;
        }
        // Déplacement(s)/coupe/changement de fil d'origine, puis le tracé.
        out.commands.insert(out.commands.end(), cmds.begin() + static_cast<std::ptrdiff_t>(cursor),
                            cmds.begin() + static_cast<std::ptrdiff_t>(run.begin));
        const auto entry = entryLock ? entryCandidate : std::vector<StitchCommand>{};
        const bool tieInOnly = !entry.empty() && arrivesByJump;
        if (!tieInOnly) {
            out.commands.push_back(cmds[run.begin]);
        }
        append(out.commands, entry);
        out.commands.insert(out.commands.end(),
                            cmds.begin() + static_cast<std::ptrdiff_t>(run.begin + 1),
                            cmds.begin() + static_cast<std::ptrdiff_t>(run.end));
        cursor = run.end;
        prev = &run;
    }
    if (prev != nullptr && !ends_with_lock(cmds, *prev)) {
        append(out.commands, lock_points(cmds, *prev, false, f));
    }
    // Coupe FINALE : sans elle la machine s'arrête fil attaché au dernier point (fil volant à
    // couper à la main). Pas de doublon si la suite de la séquence en porte déjà une.
    if (prev != nullptr && !out.commands.empty()) {
        const bool tailHasTrim =
            std::any_of(cmds.begin() + static_cast<std::ptrdiff_t>(cursor), cmds.end(),
                        [](const StitchCommand& c) { return c.type == CommandType::Trim; });
        if (!tailHasTrim) {
            const StitchCommand& tip = out.commands.back();
            out.commands.push_back({tip.pos, CommandType::Trim, tip.source, StitchPass::Travel});
        }
    }
    out.commands.insert(out.commands.end(), cmds.begin() + static_cast<std::ptrdiff_t>(cursor),
                        cmds.end());
    return out;
}

} // namespace openstitch::stitch_generation
