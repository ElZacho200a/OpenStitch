// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/analyze.hpp"

#include <algorithm>
#include <map>

namespace openstitch::stitch_analysis {

namespace {

bool outside(Vec2um p, const stitch::BoundsUm& hoop) {
    return p.x < hoop.min.x || p.x > hoop.max.x || p.y < hoop.min.y || p.y > hoop.max.y;
}

}  // namespace

std::vector<Finding> analyze(const stitch::StitchSequence& sequence,
                             const AnalysisOptions& options) {
    std::vector<Finding> findings;
    std::map<std::string, std::size_t> perCategory;

    // Ajoute un problème en respectant le plafond par catégorie.
    const auto add = [&](Severity sev, const std::string& cat, std::string msg, Vec2um loc,
                         ObjectId obj) {
        std::size_t& count = perCategory[cat];
        if (count < options.max_findings_per_category) {
            findings.push_back({sev, cat, std::move(msg), loc, obj});
        }
        ++count;
    };

    const auto stats = stitch::compute_stats(sequence);
    if (stats.stitches == 0) {
        findings.push_back(
            {Severity::Error, "vide", "Le motif ne contient aucun point.", {}, {}});
        return findings;
    }

    bool hasPrevStitch = false;
    Vec2um prevStitch{};
    Vec2um prevPos{};
    bool hasPrevPos = false;

    for (const auto& cmd : sequence.commands) {
        switch (cmd.type) {
        case stitch::CommandType::Stitch: {
            if (hasPrevStitch) {
                const double len = length_um(cmd.pos - prevStitch);
                if (len < static_cast<double>(options.min_stitch.value)) {
                    add(Severity::Warning, "point-court",
                        "Point très court (" + std::to_string(static_cast<int>(len / 1000.0 * 10) /
                                                              10.0) +
                            " mm) : risque de casse du fil et de sur-densité.",
                        cmd.pos, cmd.source);
                } else if (len > static_cast<double>(options.max_stitch.value)) {
                    add(Severity::Warning, "point-long",
                        "Point long (" +
                            std::to_string(static_cast<int>(len / 100.0) / 10.0) +
                            " mm) : risque d'accrochage.",
                        cmd.pos, cmd.source);
                }
            }
            if (options.hoop && outside(cmd.pos, *options.hoop)) {
                add(Severity::Error, "hors-cadre",
                    "Point hors du cadre de broderie.", cmd.pos, cmd.source);
            }
            prevStitch = cmd.pos;
            hasPrevStitch = true;
            break;
        }
        case stitch::CommandType::Jump: {
            if (hasPrevPos) {
                const double len = length_um(cmd.pos - prevPos);
                if (len > static_cast<double>(options.max_jump.value)) {
                    add(Severity::Warning, "saut-long",
                        "Saut long (" + std::to_string(static_cast<int>(len / 100.0) / 10.0) +
                            " mm) : envisagez une coupe.",
                        cmd.pos, cmd.source);
                }
            }
            // Un saut lève l'aiguille : le fil n'est plus continu. Sans ce
            // reset, la prochaine couture (même à la position du saut, donc
            // distance réelle nulle) se comparait à `prevStitch` D'AVANT le
            // saut -- un « point-court »/« point-long » fantôme signalant la
            // distance du SAUT lui-même comme si c'était un unique point cousu
            // continu (défaut trouvé en usage réel : un saut long légitime
            // produisait systématiquement AUSSI un « point-long » redondant et
            // trompeur à la même distance, dès que la couture reprenait juste
            // après, ce qui est le cas normal — cf. `emit_polyline`).
            hasPrevStitch = false;
            break;
        }
        default:
            break;
        }
        prevPos = cmd.pos;
        hasPrevPos = true;
    }

    if (stats.stitches > options.max_stitches) {
        add(Severity::Warning, "trop-de-points",
            "Le motif compte " + std::to_string(stats.stitches) +
                " points : temps de broderie très long.",
            {}, {});
    }

    // Tri par gravité décroissante (stable pour rester déterministe).
    std::stable_sort(findings.begin(), findings.end(),
                     [](const Finding& a, const Finding& b) { return a.severity > b.severity; });
    return findings;
}

}  // namespace openstitch::stitch_analysis
