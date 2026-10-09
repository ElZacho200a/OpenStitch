// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/analyze.hpp"

#include <algorithm>
#include <map>

namespace openstitch::stitch_analysis {

namespace {

bool outside(Vec2um p, const stitch::BoundsUm& hoop) {
    return p.x < hoop.min.x || p.x > hoop.max.x || p.y < hoop.min.y || p.y > hoop.max.y;
}

} // namespace

std::string format_mm_fr(double micrometers) {
    // Dixièmes de millimètre entiers (troncature), puis assemblage manuel :
    // ni std::to_string ni locale (le séparateur décimal est la virgule).
    const auto tenths = static_cast<long long>(micrometers / 100.0);
    const long long abs_t = tenths < 0 ? -tenths : tenths;
    std::string out = tenths < 0 ? "-" : "";
    out += std::to_string(abs_t / 10);
    out += ',';
    out += std::to_string(abs_t % 10);
    return out;
}

std::vector<Finding> analyze(const stitch::StitchSequence& sequence,
                             const AnalysisOptions& options) {
    return analyze_detailed(sequence, options).findings;
}

AnalysisReport analyze_detailed(const stitch::StitchSequence& sequence,
                                const AnalysisOptions& options) {
    std::vector<Finding> findings;
    std::map<std::string, std::size_t> perCategory;

    // Ajoute un problème en respectant le plafond par catégorie.
    const auto add = [&](Severity sev, const std::string& cat, std::string msg, Vec2um loc,
                         ObjectId obj, std::string hint = {}) {
        std::size_t& count = perCategory[cat];
        if (count < options.max_findings_per_category) {
            findings.push_back({sev, cat, std::move(msg), loc, obj, std::move(hint)});
        }
        ++count;
    };

    const auto stats = stitch::compute_stats(sequence);
    if (stats.stitches == 0) {
        findings.push_back({Severity::Error,
                            "vide",
                            "Le motif ne contient aucun point.",
                            {},
                            {},
                            "Créez un objet puis générez les points."});
        return {std::move(findings), {}};
    }

    bool hasPrevStitch = false;
    Vec2um prevStitch{};
    Vec2um prevPos{};
    bool hasPrevPos = false;
    // Déplacement en cours depuis la dernière piqûre (Lot G : déplacement
    // long sans coupe).
    bool hasLastStitch = false;
    Vec2um lastStitch{};
    bool inMove = false;
    bool trimmed = false;

    for (const auto& cmd : sequence.commands) {
        switch (cmd.type) {
        case stitch::CommandType::Stitch: {
            if (inMove && hasLastStitch && !trimmed) {
                const double moved = length_um(cmd.pos - lastStitch);
                if (moved > static_cast<double>(options.trim_threshold.value)) {
                    add(Severity::Warning, "saut-sans-coupe",
                        "Déplacement de " + format_mm_fr(moved) +
                            " mm sans coupe : le fil traîne sur le tissu.",
                        cmd.pos, cmd.source,
                        "Activez les coupes automatiques ou rapprochez les objets.");
                }
            }
            inMove = false;
            trimmed = false;
            hasLastStitch = true;
            lastStitch = cmd.pos;
            // Les points d'arrêt sont courts par construction : jamais signalés.
            if (hasPrevStitch && cmd.pass != stitch::StitchPass::Lock) {
                const double len = length_um(cmd.pos - prevStitch);
                if (len < static_cast<double>(options.min_stitch.value)) {
                    add(Severity::Warning, "point-court",
                        "Point très court (" + format_mm_fr(len) +
                            " mm) : risque de casse du fil et de sur-densité.",
                        cmd.pos, cmd.source,
                        "Augmentez l'espacement ou simplifiez le contour de l'objet.");
                } else if (len > static_cast<double>(options.max_stitch.value)) {
                    add(Severity::Warning, "point-long",
                        "Point long (" + format_mm_fr(len) + " mm) : risque d'accrochage.", cmd.pos,
                        cmd.source, "Réduisez la longueur de point ou découpez l'objet.");
                }
            }
            if (options.hoop && outside(cmd.pos, *options.hoop)) {
                add(Severity::Error, "hors-cadre", "Point hors du cadre de broderie.", cmd.pos,
                    cmd.source, "Déplacez l'objet dans le cadre ou agrandissez le canevas.");
            }
            prevStitch = cmd.pos;
            hasPrevStitch = true;
            break;
        }
        case stitch::CommandType::Jump: {
            inMove = true;
            if (hasPrevPos) {
                const double len = length_um(cmd.pos - prevPos);
                if (len > static_cast<double>(options.max_jump.value)) {
                    add(Severity::Warning, "saut-long",
                        "Saut long (" + format_mm_fr(len) + " mm) : envisagez une coupe.", cmd.pos,
                        cmd.source,
                        "Réordonnez les objets pour réduire les sauts, ou ajoutez une coupe.");
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
        case stitch::CommandType::Trim:
            trimmed = true;
            break;
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
            {}, {}, "Augmentez l'espacement ou réduisez la taille du motif.");
    }

    // Tri par gravité décroissante (stable pour rester déterministe).
    std::stable_sort(findings.begin(), findings.end(),
                     [](const Finding& a, const Finding& b) { return a.severity > b.severity; });
    AnalysisReport report;
    report.findings = std::move(findings);
    for (const auto& [cat, count] : perCategory) {
        if (count > options.max_findings_per_category) {
            report.suppressed[cat] = count - options.max_findings_per_category;
        }
    }
    return report;
}

} // namespace openstitch::stitch_analysis
