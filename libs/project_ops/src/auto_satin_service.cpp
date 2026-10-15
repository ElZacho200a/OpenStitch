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
    double current = 0.0;
    for (const auto& set : vector.paths) {
        current += geometry::path_set_area_um2(set);
    }
    double pristine = 0.0;
    for (const auto& set : *raw) {
        pristine += geometry::path_set_area_um2(set);
    }
    // Moins de 3 % de plus : le contour est déjà celui de la région (ou retouché à la main).
    if (pristine <= 0.0 || current <= pristine * 1.03) {
        return std::nullopt;
    }
    return *raw;
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
        plan.accepted = false;
        return plan;
    }
    plan.accepted = true;
    return plan;
}

} // namespace openstitch::project_ops
