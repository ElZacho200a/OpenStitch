// SPDX-License-Identifier: Apache-2.0
// Prédiction de la déformation du tissu (solveur 2D de la feuille). Estimation NON calibrée.
#include "openstitch/project_ops/fabric_deformation.hpp"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "openstitch/image/image.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::project_ops {

namespace {

constexpr const char* kWarning =
    "Estimation NON calibree : rigidites, tension du fil et deformation critique sont des "
    "ordres de grandeur empiriques. Compare deux versions d'un motif ; ne mesure ni les plis "
    "reels (flambage hors plan) ni une deformation en mm sur machine.";

} // namespace

Result<FabricDeformationReport>
predict_fabric_deformation(const document::Project& project,
                           const FabricDeformationOptions& options) {
    if (!std::isfinite(options.cell_mm) || options.cell_mm < 0.2 || options.cell_mm > 10.0 ||
        options.max_cells_per_side < 8 || options.max_cells_per_side > 256 ||
        !std::isfinite(options.margin_mm) || options.margin_mm < 0.0 || options.margin_mm > 200.0 ||
        !std::isfinite(options.thread_tension_n) || !std::isfinite(options.critical_strain) ||
        !std::isfinite(options.heatmap_px_per_cell) || options.heatmap_px_per_cell < 1.0 ||
        options.heatmap_px_per_cell > 16.0) {
        return std::unexpected(
            Error{ErrorCategory::UserInput, "deformation tissu : parametres invalides", {}});
    }
    const bool any_visible =
        std::any_of(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                    [](const auto& e) { return e.visible; });
    if (!any_visible) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "deformation tissu : aucun objet visible a analyser",
                                     {}});
    }
    auto sequence = stitch_generation::effective_sequence(project);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }

    const std::string material =
        options.material.empty() ? project.fabric.material : options.material;
    const std::string stabilizer =
        options.stabilizer.empty() ? project.fabric.stabilizer : options.stabilizer;
    stitch_analysis::SheetParams params = stitch_analysis::sheet_preset(material, stabilizer);
    params.grain_angle_rad = project.fabric.grain_angle.radians;
    const double softness = std::clamp(project.fabric.weft_softness, 0.25, 4.0);
    params.stiffness_weft_n_per_mm /= softness;
    if (options.thread_tension_n > 0.0) {
        params.thread_tension_n = options.thread_tension_n;
    }
    if (options.critical_strain > 0.0) {
        params.critical_compressive_strain = options.critical_strain;
    }

    stitch_analysis::SheetDomain domain;
    if (options.hoop_is_canvas) {
        const auto hw = static_cast<std::int32_t>(project.canvas.width.value / 2);
        const auto hh = static_cast<std::int32_t>(project.canvas.height.value / 2);
        domain = {{{-hw}, {-hh}}, {{hw}, {hh}}};
    } else {
        domain = stitch_analysis::domain_around_stitches(
            *sequence, to_micrometers(Millimeters{options.margin_mm}));
    }
    if (domain.max.x.value <= domain.min.x.value || domain.max.y.value <= domain.min.y.value) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "deformation tissu : aucun segment cousu a analyser",
                                     {}});
    }
    FabricDeformationReport report;
    report.deformation = stitch_analysis::solve_sheet_deformation(
        *sequence, params, domain, to_micrometers(Millimeters{options.cell_mm}),
        options.max_cells_per_side);
    const stitch_analysis::SheetDeformation& d = report.deformation;
    if (d.empty()) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "deformation tissu : aucun segment cousu a analyser",
                                     {}});
    }

    const auto mm = [](Vec2um p) {
        return nlohmann::json{{"x", to_millimeters(p.x).value}, {"y", to_millimeters(p.y).value}};
    };
    nlohmann::json doc = {
        {"schema", kFabricDeformationSchema},
        {"method", "plane_stress_fem_q4_v1"},
        {"calibrated", d.calibrated},
        {"material", material},
        {"stabilizer", stabilizer},
        {"grid",
         {{"columns", d.columns},
          {"rows", d.rows},
          {"cell_mm", to_millimeters(d.cell).value},
          {"origin_mm", mm(d.origin)}}},
        {"solver",
         {{"iterations", d.iterations}, {"converged", d.converged}, {"residual", d.residual}}},
        {"segments", d.segment_count},
        {"max_displacement_mm", d.max_displacement_mm},
        {"max_displacement_at_mm", mm(d.max_displacement_at)},
        {"max_wrinkle_index", d.max_wrinkle_index},
        {"max_wrinkle_at_mm", mm(d.max_wrinkle_at)},
        {"p99_wrinkle_index", d.p99_wrinkle_index},
        {"wrinkle_area_mm2", d.wrinkle_area_mm2},
        {"critical_compressive_strain", params.critical_compressive_strain},
        {"warning", kWarning}};
    report.json = doc.dump(2);
    report.text = std::string("Deformation prevue du tissu (estimation NON calibree)\n") +
                  "  tissu : " + material + (stabilizer.empty() ? "" : " + " + stabilizer) + "\n" +
                  "  deplacement max : " + std::to_string(d.max_displacement_mm) + " mm\n" +
                  "  indice de plissement max : " + std::to_string(d.max_wrinkle_index) +
                  " (1 = seuil critique)\n" +
                  "  surface au-dessus du seuil : " + std::to_string(d.wrinkle_area_mm2) +
                  " mm2\n" + (d.converged ? "" : "  Attention : solveur non converge.\n") +
                  "  Attention : ordres de grandeur empiriques, plis reels non simules.\n";

    if (options.heatmap) {
        // Échelle : le seuil critique, ou le 99e centile s'il le dépasse (sinon un motif dense
        // sature en rouge partout et la carte n'indique plus où ça comprime le plus).
        const stitch_analysis::SheetRaster raster =
            stitch_analysis::rasterize_wrinkle_index(d, std::max(1.0, d.p99_wrinkle_index));
        const int scale = static_cast<int>(std::lround(options.heatmap_px_per_cell));
        image::Image map;
        map.width = raster.width * scale;
        map.height = raster.height * scale;
        map.rgba.assign(static_cast<std::size_t>(map.width) * map.height * 4, 255);
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const std::uint8_t* src =
                    raster.rgba.data() +
                    (static_cast<std::size_t>(y / scale) * raster.width + x / scale) * 4;
                std::uint8_t* dst =
                    map.rgba.data() + (static_cast<std::size_t>(y) * map.width + x) * 4;
                // Fond gris clair sous la surimpression, comme la carte de risque.
                const double a = src[3] / 255.0;
                for (int ch = 0; ch < 3; ++ch) {
                    dst[ch] = static_cast<std::uint8_t>(235.0 * (1.0 - a) + src[ch] * a);
                }
            }
        }
        auto png = image::encode_png(map);
        if (!png) {
            return std::unexpected(png.error());
        }
        report.heatmap_png = std::move(*png);
    }
    return report;
}

} // namespace openstitch::project_ops
