// SPDX-License-Identifier: Apache-2.0
// Service auto-satin Qt-free : équivalent de `MainWindow::stitchParamsForType` (type « satin »),
// `previewAutoSatin` et `pristineSatinContour` du bureau. Aucun algorithme n'est réimplémenté :
// contour brut = `vectorization::vectorize_region`, colonnes =
// `auto_satin::generate_satin`, statut = `auto_satin::analyze_region`.
#include "openstitch/project_ops/auto_satin_service.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/satin_strategy.hpp"
#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/stitch_generation/auto_satin_engine.hpp"
#include "openstitch/vectorization/vectorize.hpp"

namespace openstitch::project_ops {

namespace {

void add_message(std::vector<std::string>& messages, const std::string& message) {
    if (std::find(messages.begin(), messages.end(), message) == messages.end()) {
        messages.push_back(message);
    }
}

// Contour brut de la région source (sans le recouvrement des tatamis voisins) : même règle que
// `MainWindow::pristineSatinContour`. Absent si la région n'est plus disponible, si le contour
// courant n'est pas sensiblement plus grand (moins de 3 %), ou si la vectorisation échoue
// (dans ce cas un message le dit : jamais silencieux).
std::optional<std::vector<geometry::PathSet>> pristine_contour(const document::Project& project,
                                                               const document::VectorObject& vector,
                                                               std::vector<std::string>& messages) {
    if (!vector.source_region || !project.segmentation) {
        return std::nullopt;
    }
    vectorization::VectorizeOptions options;
    options.mm_per_px = project.mm_per_px;
    const auto raw =
        vectorization::vectorize_region(*project.segmentation, *vector.source_region, options);
    if (!raw) {
        add_message(messages, "contour brut de la région non rétabli : " + raw.error().message);
        return std::nullopt;
    }
    if (raw->empty()) {
        return std::nullopt;
    }
    // Seul un contour élargi par le recouvrement des tatamis (bande fine) est ramené à celui de
    // la région ; une forme fusionnée ou retouchée est gardée telle quelle.
    if (!vectorization::is_overlap_grown_contour(vector.paths, *raw)) {
        return std::nullopt;
    }
    return *raw;
}

bool any_message_contains(const std::vector<std::string>& messages, const std::string& needle) {
    return std::any_of(messages.begin(), messages.end(),
                       [&](const std::string& m) { return m.find(needle) != std::string::npos; });
}

// Raison structurée + piste de correction d'un refus. Les opérations citées existent dans le
// registre (`break_apart`, `split`, `set_geometry`, `remove_holes`, `set_stitch_type`).
void explain_refusal(AutoSatinPlan& plan,
                     const std::optional<auto_satin::SatinabilityReport>& report) {
    if (report) {
        plan.min_width_mm = report->minimum_width_mm;
        plan.max_width_mm = report->maximum_width_mm;
        plan.branch_count = report->branch_count;
        plan.junction_count = report->junction_count;
    }
    const std::string fallback =
        "ou passer l'objet en remplissage directionnel via `set_stitch_type` "
        "(type=directional) ou tatami";
    if (any_message_contains(plan.messages, "se coupe lui-même")) {
        plan.reason_code = "self_intersecting_contour";
        plan.suggestion = "Le contour se recoupe : séparer les branches avec `split` ou "
                          "`break_apart`, ou redessiner un contour simple via `set_geometry`, " +
                          fallback + ".";
    } else if (any_message_contains(plan.messages, "contour trop complexe")) {
        plan.reason_code = "contour_too_complex";
        plan.suggestion = "Contour trop riche en segments : le simplifier via `set_geometry`, "
                          "ou découper l'objet avec `split`, " +
                          fallback + ".";
    } else if (!report) {
        plan.reason_code = "engine_no_column";
        plan.suggestion = "Aucune colonne construite : essayer une autre `strategy`, "
                          "`compact_axis`=true pour une forme compacte, " +
                          fallback + ".";
    } else if (report->hole_count > 0) {
        plan.reason_code = "has_holes";
        plan.suggestion = "La région contient des trous : les retirer avec `remove_holes`, ou "
                          "séparer l'objet avec `split`, " +
                          fallback + ".";
    } else if (report->status == auto_satin::SatinabilityStatus::RequiresDecomposition) {
        plan.reason_code = "branched_shape";
        plan.suggestion = "Forme branchée (" + std::to_string(report->junction_count) +
                          " jonction(s)) : séparer les branches avec `split`, ou simplifier le "
                          "contour via `set_geometry`, " +
                          fallback + ".";
    } else if (report->status == auto_satin::SatinabilityStatus::Ambiguous) {
        plan.reason_code = "compact_shape";
        plan.suggestion = "Forme compacte, direction ambiguë : relancer `auto_satin` avec "
                          "compact_axis=true, " +
                          fallback + ".";
    } else if (report->status == auto_satin::SatinabilityStatus::Unsuitable &&
               report->maximum_width_mm > 0.0 && report->maximum_width_mm < 0.8) {
        plan.reason_code = "too_narrow";
        plan.suggestion = "Région trop étroite pour un satin (< 0,8 mm) : utiliser un contour "
                          "(`set_stitch_type` type=running) ou élargir la forme via "
                          "`set_geometry`.";
    } else if (report->maximum_width_mm > 12.0) {
        plan.reason_code = "width_out_of_range";
        plan.suggestion = "Largeur hors plage satin (> 12 mm) : découper l'objet en bandes avec "
                          "`split`, " +
                          fallback + ".";
    } else if (report->status == auto_satin::SatinabilityStatus::Unsuitable) {
        plan.reason_code = "invalid_geometry";
        plan.suggestion = "Géométrie invalide ou dégénérée : la redéfinir via `set_geometry`.";
    } else {
        plan.reason_code = "engine_no_column";
        plan.suggestion =
            "Aucune colonne construite : essayer une autre `strategy`, " + fallback + ".";
    }
}

} // namespace

Result<AutoSatinPlan> plan_auto_satin(const document::Project& project, ObjectId vector_object,
                                      const AutoSatinPlanRequest& request) {
    const document::VectorObject* current = project.findObject(vector_object);
    if (current == nullptr) {
        return fail(ErrorCategory::UserInput,
                    "objet vectoriel " + std::to_string(vector_object.value) + " introuvable");
    }

    AutoSatinPlan plan;
    if (request.spacing) {
        plan.params.spacing = *request.spacing;
    }
    plan.params.compact_axis = request.compact_axis;
    plan.params.strategy = request.strategy;
    plan.params.sweep_angle = request.sweep_angle;
    plan.params.guides = request.guides;

    if (current->paths.empty()) {
        plan.refusal = "Aucun contour source pour construire le satin.";
        plan.reason_code = "no_source_contour";
        plan.suggestion = "L'objet n'a pas de contour : le définir via `set_geometry`.";
        plan.refusal += " [reason_code=" + plan.reason_code + "] Suggestion : " + plan.suggestion;
        return plan;
    }

    // Le satin suit la région telle que segmentée, pas le contour agrandi par le recouvrement
    // des tatamis voisins (sinon il déborde de sa zone).
    const std::vector<geometry::PathSet>* source_paths = &current->paths;
    if (request.use_pristine_contour) {
        plan.restored_contour = pristine_contour(project, *current, plan.messages);
        if (plan.restored_contour) {
            source_paths = &*plan.restored_contour;
        }
    }

    auto engine = stitch_generation::auto_satin_engine(plan.params);
    engine.measure_coverage = true;
    double area_sum = 0.0;
    double covered_sum = 0.0;
    double uncovered_mm2 = 0.0;
    int orphan_guides = 0;
    double largest_area = -1.0;
    std::string status = "Indéterminé";
    std::optional<auto_satin::SatinabilityReport> largest_report;
    const auto analysis_params = auto_satin::stable_skeleton_analysis();

    for (const auto& set : *source_paths) {
        const auto res = auto_satin::generate_satin(set, engine);
        if (!res) {
            add_message(plan.messages, res.error().message);
            continue;
        }
        plan.columns += res->columns.size();
        orphan_guides += res->diagnostics.orphan_guides;
        for (const auto& m : res->diagnostics.messages) {
            add_message(plan.messages, m);
        }
        double area = std::abs(geometry::signed_area_um2(set.outer));
        for (const auto& hole : set.holes) {
            area -= std::abs(geometry::signed_area_um2(hole));
        }
        area = std::max(area, 0.0) / 1e6; // mm²
        if (res->diagnostics.coverage_measured) {
            area_sum += area;
            covered_sum += area * res->diagnostics.coverage_ratio;
            uncovered_mm2 += res->diagnostics.uncovered_area_mm2;
        }
        // Statut de satinabilité du plus grand morceau (celui qui décide de l'allure du satin).
        if (area > largest_area) {
            const auto analysis = auto_satin::analyze_region(set, analysis_params);
            if (analysis) {
                largest_area = area;
                status = auto_satin::to_string(analysis->report.status);
                largest_report = analysis->report;
            } else {
                add_message(plan.messages,
                            "satinabilité non évaluée : " + analysis.error().message);
            }
        }
    }
    plan.satinability_status = status;

    if (area_sum > 0.0) {
        plan.coverage_measured = true;
        plan.coverage_ratio = covered_sum / area_sum;
    }
    if (orphan_guides > 0) {
        add_message(plan.messages,
                    std::to_string(orphan_guides) + " guide(s) trop loin de l'axe, ignoré(s)");
    }
    if (plan.coverage_measured && uncovered_mm2 >= 1.0) {
        add_message(plan.messages, std::to_string(static_cast<int>(std::lround(uncovered_mm2))) +
                                       " mm² resteraient sans point");
    }

    if (plan.columns == 0) {
        // Refus nommé, jamais contourné par une géométrie de moindre qualité.
        std::string reasons;
        for (const auto& m : plan.messages) {
            if (!reasons.empty()) {
                reasons += " ; ";
            }
            reasons += m;
        }
        plan.refusal = "Aucune colonne satin n'a pu être construite pour cette région : " +
                       (reasons.empty() ? std::string("forme non exploitable") : reasons);
        explain_refusal(plan, largest_report);
        plan.refusal += " [reason_code=" + plan.reason_code + "] Suggestion : " + plan.suggestion;
        plan.accepted = false;
        return plan;
    }
    plan.accepted = true;
    return plan;
}

} // namespace openstitch::project_ops
