// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/finish.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include "openstitch/stitch_generation/lock.hpp"

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

// Point d'arrêt ancré en `anchor`, orienté vers le premier point du tracé
// (dans le sens `step`) distinct de l'ancre -- le verrou reste donc sur un
// segment réellement cousu de l'objet. Le premier point renvoyé par
// `lock_stitches` (l'ancre elle-même, déjà cousue) est omis.
void append_lock(std::vector<StitchCommand>& out, const std::vector<StitchCommand>& cmds,
                 const Run& run, bool atStart, const document::SequenceFinishing& f) {
    if (f.lock_type == document::LockStitch::None || run.end - run.begin < 2) {
        return;
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
        return; // tracé dégénéré (un seul point distinct) : pas de direction fiable
    }
    const auto pts =
        lock_stitches(anchor, *toward, static_cast<LockType>(static_cast<int>(f.lock_type)),
                      f.lock_length, f.lock_passes);
    const ObjectId source = cmds[anchorIdx].source;
    for (std::size_t k = 1; k < pts.size(); ++k) {
        out.push_back({pts[k], CommandType::Stitch, source, StitchPass::Lock});
    }
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
    const auto& cmds = sequence.commands;
    const std::vector<Run> runs = find_runs(cmds);

    stitch::StitchSequence out;
    out.commands.reserve(cmds.size() + runs.size() * 8);
    std::size_t cursor = 0;
    const Run* prev = nullptr;
    for (const Run& run : runs) {
        bool entryLock = true; // premier tracé : le fil démarre ici
        if (prev != nullptr) {
            const StitchCommand& last = cmds[prev->end - 1];
            const StitchCommand& first = cmds[run.begin];
            bool colorChange = false;
            bool hasTrim = false;
            for (std::size_t k = cursor; k < run.begin; ++k) {
                colorChange = colorChange || cmds[k].type == CommandType::ColorChange ||
                              cmds[k].type == CommandType::Stop;
                hasTrim = hasTrim || cmds[k].type == CommandType::Trim;
            }
            const bool boundary = last.source != first.source;
            const bool needTrim =
                !hasTrim && (length_um(first.pos - last.pos) > f.trim_threshold.value ||
                             (colorChange && f.trim_before_color_change));
            const bool cut = needTrim || hasTrim || colorChange;
            if ((boundary || cut) && !ends_with_lock(cmds, *prev)) {
                append_lock(out.commands, cmds, *prev, false, f);
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
        out.commands.push_back(cmds[run.begin]);
        if (entryLock && !starts_with_lock(cmds, run)) {
            append_lock(out.commands, cmds, run, true, f);
        }
        out.commands.insert(out.commands.end(),
                            cmds.begin() + static_cast<std::ptrdiff_t>(run.begin + 1),
                            cmds.begin() + static_cast<std::ptrdiff_t>(run.end));
        cursor = run.end;
        prev = &run;
    }
    if (prev != nullptr && !ends_with_lock(cmds, *prev)) {
        append_lock(out.commands, cmds, *prev, false, f);
    }
    out.commands.insert(out.commands.end(), cmds.begin() + static_cast<std::ptrdiff_t>(cursor),
                        cmds.end());
    return out;
}

} // namespace openstitch::stitch_generation
