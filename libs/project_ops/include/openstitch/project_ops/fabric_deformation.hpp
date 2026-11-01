// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_analysis/sheet_deformation.hpp"

// Prédiction de la déformation du tissu d'un projet : solveur mécanique 2D de la feuille
// (`stitch_analysis::solve_sheet_deformation`) appliqué à la séquence EFFECTIVE, cadre = canevas.
// Estimation NON calibrée : voir le chapitre de documentation « deformation-tissu ».
namespace openstitch::project_ops {

inline constexpr const char* kFabricDeformationSchema = "openstitch.fabric-deformation/1";

struct FabricDeformationOptions {
    // Vide : repris de `project.fabric.material` (woven, jersey, felt ; inconnu -> woven).
    std::string material;
    // Vide : repris de `project.fabric.stabilizer` (none, tearaway, cutaway).
    std::string stabilizer;
    double cell_mm{1.0};
    int max_cells_per_side{128};
    // true : cadre = canevas du projet ; false : emprise des points élargie de `margin_mm`.
    bool hoop_is_canvas{true};
    double margin_mm{10.0};
    // Surcharges facultatives des paramètres du solveur (<= 0 : valeur du préréglage).
    double thread_tension_n{0.0};
    double critical_strain{0.0};
    bool heatmap{false};
    double heatmap_px_per_cell{4.0};
};

struct FabricDeformationReport {
    std::string json; // openstitch.fabric-deformation/1
    std::string text;
    std::vector<std::uint8_t> heatmap_png;
    stitch_analysis::SheetDeformation deformation;
};

[[nodiscard]] Result<FabricDeformationReport>
predict_fabric_deformation(const document::Project& project,
                           const FabricDeformationOptions& options = {});

} // namespace openstitch::project_ops
