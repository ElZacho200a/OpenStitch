// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/length_bounds.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <type_traits>
#include <variant>

#include "openstitch/stitch_generation/pull_auto.hpp"
#include "openstitch/stitch_generation/tatami.hpp"
#include "pull_grow.hpp"

namespace openstitch::stitch_generation {

namespace {

using stitch::CommandType;
using stitch::StitchCommand;
using stitch::StitchPass;

// Surfaces de référence des objets (objet vectoriel suivi), pour vérifier qu'une fusion de
// points ne fait jamais sortir le fil de sa région.
struct ObjectRegion {
    const std::vector<geometry::PathSet>* paths{nullptr};
    const document::EmbroideryObject* object{nullptr};
};

std::map<std::uint64_t, ObjectRegion> object_regions(const document::Project& project) {
    std::map<std::uint64_t, ObjectRegion> out;
    for (const auto& obj : project.embroidery_objects) {
        if (const auto* vec = project.findObject(obj.source_vector)) {
            out[obj.id.value] = {&vec->paths, &obj};
        }
    }
    return out;
}

// Compensation du tirage EFFECTIVE d'un remplissage sur un morceau (mode Auto résolu comme à la
// génération). Les bouts de rangée dépassent volontairement de la région de cette valeur : une
// fusion dont la corde reste dans ce dépassement ne fait pas sortir le fil plus loin que le
// moteur ne l'a déjà posé. Sans cela, le demi-tour court entre deux rangées compensées n'était
// jamais fusionné (corde partant d'un bout hors région) : 212 points courts sur un U de 780.
Micrometers effective_fill_pull(const document::EmbroideryObject& obj,
                                const geometry::PathSet& piece) {
    return std::visit(
        [&](const auto& p) -> Micrometers {
            using P = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<P, document::TatamiParams> ||
                          std::is_same_v<P, document::DirectionalFillParams> ||
                          std::is_same_v<P, document::RadialFillParams> ||
                          std::is_same_v<P, document::SpiralFillParams> ||
                          std::is_same_v<P, document::ContourFillParams> ||
                          std::is_same_v<P, document::MotifFillParams> ||
                          std::is_same_v<P, document::CrosshatchFillParams>) {
                return resolve_pull_compensation(p, piece).pull_compensation;
            } else {
                return Micrometers{0};
            }
        },
        obj.params);
}

bool is_protected(const StitchCommand& c) {
    return c.pass == StitchPass::Lock || c.pass == StitchPass::Manual;
}

// Fusion des points courts. Un testeur par morceau de l'objet vectoriel est construit à la
// première demande (même prédicat que `segment_stays_in_region`, sans reconstruire les
// polygones à chaque point court). Objet sans surface (satin manuel) : écart borné par la
// longueur minimale, accepté.
std::vector<StitchCommand> merge_short(const std::vector<StitchCommand>& cmds,
                                       const document::Project& project, double minLen,
                                       std::size_t& merged) {
    const auto regions = object_regions(project);
    std::map<std::uint64_t, std::vector<RegionSegmentTester>> testers;
    const auto chordInside = [&](ObjectId source, Vec2um a, Vec2um b) {
        const auto it = regions.find(source.value);
        if (it == regions.end()) {
            return true;
        }
        auto [tit, inserted] = testers.try_emplace(source.value);
        if (inserted) {
            for (const auto& set : *it->second.paths) {
                tit->second.emplace_back(
                    detail::grown_for_pull(set, effective_fill_pull(*it->second.object, set)));
            }
        }
        return std::any_of(tit->second.begin(), tit->second.end(),
                           [&](const RegionSegmentTester& t) { return t.stays_inside(a, b); });
    };

    std::vector<StitchCommand> out;
    out.reserve(cmds.size());
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const StitchCommand& c = cmds[i];
        const bool afterStitch = c.type == CommandType::Stitch && i > 0 &&
                                 cmds[i - 1].type == CommandType::Stitch && !out.empty();
        const bool hasNext = i + 1 < cmds.size() && cmds[i + 1].type == CommandType::Stitch;
        if (afterStitch && hasNext) {
            // Point intérieur : retiré s'il est trop près du dernier point conservé.
            if (!is_protected(c) && cmds[i + 1].pass != StitchPass::Lock) {
                const Vec2um prev = out.back().pos;
                if (length_um(c.pos - prev) < minLen &&
                    chordInside(c.source, prev, cmds[i + 1].pos)) {
                    ++merged;
                    continue;
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

std::vector<StitchCommand> split_long(const std::vector<StitchCommand>& cmds, double maxLen,
                                      LengthBoundsReport& report) {
    std::vector<StitchCommand> out;
    out.reserve(cmds.size());
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        const StitchCommand& c = cmds[i];
        if (c.type == CommandType::Stitch && i > 0 && cmds[i - 1].type == CommandType::Stitch) {
            const Vec2um from = cmds[i - 1].pos;
            const double len = length_um(c.pos - from);
            if (len > maxLen) {
                const int parts = static_cast<int>(std::ceil(len / maxLen));
                ++report.split;
                for (int k = 1; k < parts; ++k) {
                    const double t = static_cast<double>(k) / parts;
                    const Vec2um mid{
                        Micrometers{static_cast<std::int32_t>(std::lround(
                            from.x.value + static_cast<double>(c.pos.x.value - from.x.value) * t))},
                        Micrometers{static_cast<std::int32_t>(
                            std::lround(from.y.value +
                                        static_cast<double>(c.pos.y.value - from.y.value) * t))}};
                    out.push_back({mid, CommandType::Stitch, c.source, c.pass});
                    ++report.added;
                }
            }
        }
        out.push_back(c);
    }
    return out;
}

} // namespace

LengthBoundsReport measure_length_bounds(const std::vector<StitchCommand>& cmds,
                                         std::int32_t min_um, std::int32_t max_um) {
    LengthBoundsReport report;
    for (std::size_t i = 1; i < cmds.size(); ++i) {
        if (cmds[i].type != CommandType::Stitch || cmds[i - 1].type != CommandType::Stitch ||
            cmds[i].pass == StitchPass::Lock) {
            continue;
        }
        const double len = length_um(cmds[i].pos - cmds[i - 1].pos);
        if (min_um > 0 && len < static_cast<double>(min_um)) {
            ++report.short_left;
        }
        if (max_um > 0 && len > static_cast<double>(max_um)) {
            ++report.long_left;
        }
    }
    return report;
}

LengthBoundsResult enforce_length_bounds(const std::vector<StitchCommand>& cmds,
                                         const document::Project& project,
                                         const document::SequenceFinishing& finishing) {
    LengthBoundsResult result;
    result.commands = cmds;
    const bool doMerge = finishing.filter_short_stitches && finishing.min_stitch_length.value > 0;
    const bool doSplit = finishing.split_long_stitches && finishing.max_stitch_length.value > 0;
    if (doMerge) {
        result.commands = merge_short(result.commands, project,
                                      static_cast<double>(finishing.min_stitch_length.value),
                                      result.report.merged);
    }
    if (doSplit) {
        result.commands = split_long(
            result.commands, static_cast<double>(finishing.max_stitch_length.value), result.report);
    }
    const LengthBoundsReport left =
        measure_length_bounds(result.commands, doMerge ? finishing.min_stitch_length.value : 0,
                              doSplit ? finishing.max_stitch_length.value : 0);
    result.report.short_left = left.short_left;
    result.report.long_left = left.long_left;
    return result;
}

} // namespace openstitch::stitch_generation
