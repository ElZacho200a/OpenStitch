// SPDX-License-Identifier: Apache-2.0
#include "density_service.hpp"

#include <algorithm>

#include "openstitch/stitch_analysis/density_map.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

namespace openstitch::desktop_v2 {

std::array<std::uint8_t, 4> densityColour(double perMm2, double threshold) {
    if (threshold <= 0.0 || perMm2 <= 0.0) {
        return {0, 0, 0, 0};
    }
    const double ratio = perMm2 / threshold;
    if (ratio < 0.25) {
        return {0, 0, 0, 0};
    }
    if (ratio >= 1.0) {
        return {220, 30, 30, 170}; // sur-dense
    }
    // 0,25 -> jaune clair translucide ; 1,0 -> orange soutenu.
    const double t = (ratio - 0.25) / 0.75;
    const auto lerp = [t](double a, double b) {
        return static_cast<std::uint8_t>(a + (b - a) * t);
    };
    return {255, lerp(220.0, 120.0), lerp(40.0, 20.0), lerp(60.0, 140.0)};
}

Result<DensityOverlay> run_density(std::shared_ptr<const document::Project> project,
                                   std::uint64_t revision, const TaskContext& task) {
    if (!project) {
        return fail(ErrorCategory::Internal, "Carte de densité demandée sans projet");
    }
    if (project->embroidery_objects.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Rien à analyser",
                    "Le document ne contient aucun objet brodé.");
    }
    auto sequence = stitch_generation::effective_sequence(*project, task);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    if (task.cancelled()) {
        return cancelled_error();
    }
    const stitch_analysis::DensityOptions options;
    const auto map = stitch_analysis::compute_density_map(*sequence, options);
    if (map.empty()) {
        return fail(ErrorCategory::OperationImpossible, "Aucun point à analyser");
    }
    DensityOverlay out;
    out.columns = static_cast<int>(map.columns);
    out.rows = static_cast<int>(map.rows);
    out.thresholdPerMm2 = options.max_penetrations_per_mm2;
    out.maxPenetrationsPerMm2 = map.max_density_per_mm2();
    out.minXmm = map.origin.x.value / 1000.0;
    out.minYmm = map.origin.y.value / 1000.0;
    out.widthMm = static_cast<double>(map.columns) * map.cell.value / 1000.0;
    out.heightMm = static_cast<double>(map.rows) * map.cell.value / 1000.0;
    out.rgba.assign(map.columns * map.rows * 4, 0);
    for (std::size_t row = 0; row < map.rows; ++row) {
        // Ligne 0 de la carte = Y le plus bas ; l'image écran commence par le haut.
        const std::size_t imageRow = map.rows - 1 - row;
        for (std::size_t col = 0; col < map.columns; ++col) {
            const auto c = densityColour(map.density_per_mm2(col, row), out.thresholdPerMm2);
            std::copy(c.begin(), c.end(), out.rgba.begin() + (imageRow * map.columns + col) * 4);
        }
    }
    for (const auto& h : stitch_analysis::find_density_hotspots(*sequence, options)) {
        out.hotspots.push_back({h.center.x.value / 1000.0, h.center.y.value / 1000.0,
                                h.max_penetrations_per_mm2, h.area_mm2, h.object.value});
    }
    out.revision = revision;
    return out;
}

} // namespace openstitch::desktop_v2
