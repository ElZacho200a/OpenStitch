// SPDX-License-Identifier: Apache-2.0
#include "fabric_risk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "openstitch/project_ops/fabric_deformation.hpp"
#include "openstitch/stitch_analysis/sheet_deformation.hpp"

namespace openstitch::desktop_v2 {

namespace {
constexpr std::size_t kArrowGrid = 12; // au plus 12 x 12 flèches : lisible, jamais une forêt
}

std::string FabricRisk::level() const {
    if (maxWrinkleIndex >= 1.0) {
        return "élevé";
    }
    if (maxWrinkleIndex >= 0.5) {
        return "à surveiller";
    }
    return "faible";
}

std::string FabricRisk::verdict() const {
    char buffer[260];
    std::snprintf(buffer, sizeof buffer,
                  "Risque de plissement %s (indice max %.2f, seuil = 1) ; déplacement prévu "
                  "jusqu'à %.2f mm ; %.1f mm² au-delà du seuil.",
                  level().c_str(), maxWrinkleIndex, maxDisplacementMm, wrinkleAreaMm2);
    std::string out = buffer;
    if (!converged) {
        out += " Le calcul n'a pas convergé : résultat à prendre avec prudence.";
    }
    if (!calibrated) {
        out += " Estimation NON calibrée sur pièce-test : utile pour comparer deux versions, pas "
               "comme mesure.";
    }
    return out;
}

Result<FabricRisk> runFabricRisk(std::shared_ptr<const document::Project> project,
                                 std::uint64_t revision, const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Analyse du tissu demandée sans projet");
    }
    if (project->embroidery_objects.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Rien à analyser",
                    "Le document ne contient aucun objet brodé.");
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    project_ops::FabricDeformationOptions options;
    options.hoop_is_canvas = false; // emprise des points, élargie : indépendant du cadre choisi
    auto report = project_ops::predict_fabric_deformation(*project, options);
    if (!report) {
        return std::unexpected(report.error());
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    const auto& d = report->deformation;
    if (d.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Aucun point à analyser");
    }
    const auto raster = stitch_analysis::rasterize_wrinkle_index(d);

    FabricRisk out;
    out.columns = raster.width;
    out.rows = raster.height;
    out.rgba = raster.rgba;
    out.minXmm = d.origin.x.value / 1000.0;
    out.minYmm = d.origin.y.value / 1000.0;
    out.widthMm = static_cast<double>(d.columns) * d.cell.value / 1000.0;
    out.heightMm = static_cast<double>(d.rows) * d.cell.value / 1000.0;
    out.maxWrinkleIndex = d.max_wrinkle_index;
    out.p99WrinkleIndex = d.p99_wrinkle_index;
    out.wrinkleAreaMm2 = d.wrinkle_area_mm2;
    out.maxDisplacementMm = d.max_displacement_mm;
    out.maxWrinkleXmm = d.max_wrinkle_at.x.value / 1000.0;
    out.maxWrinkleYmm = d.max_wrinkle_at.y.value / 1000.0;
    out.converged = d.converged;
    out.calibrated = d.calibrated;
    out.material = project->fabric.material;
    out.stabilizer = project->fabric.stabilizer;

    // Flèches : échantillon régulier de la grille de nœuds.
    const std::size_t nodeCols = d.node_columns();
    const std::size_t nodeRows = d.rows + 1;
    const std::size_t stepX = std::max<std::size_t>(1, nodeCols / kArrowGrid);
    const std::size_t stepY = std::max<std::size_t>(1, nodeRows / kArrowGrid);
    const double cellMm = d.cell.value / 1000.0;
    for (std::size_t row = 0; row < nodeRows; row += stepY) {
        for (std::size_t col = 0; col < nodeCols; col += stepX) {
            const std::size_t index = row * nodeCols + col;
            if (index >= d.ux_mm.size() || index >= d.uy_mm.size()) {
                continue;
            }
            const double dx = d.ux_mm[index];
            const double dy = d.uy_mm[index];
            if (std::hypot(dx, dy) < 1e-4) {
                continue;
            }
            out.arrows.push_back({out.minXmm + static_cast<double>(col) * cellMm,
                                  out.minYmm + static_cast<double>(row) * cellMm, dx, dy});
        }
    }
    out.revision = revision;
    return out;
}

} // namespace openstitch::desktop_v2
