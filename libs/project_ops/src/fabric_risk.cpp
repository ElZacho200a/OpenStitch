// SPDX-License-Identifier: Apache-2.0
// Indicateur heuristique de risque de tirage du tissu, PAS une simulation mecanique.
#include "openstitch/project_ops/analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "openstitch/image/image.hpp"
#include "thread_model.hpp"

namespace openstitch::project_ops {

Result<FabricRiskReport> measure_fabric_risk(const document::Project& project,
                                             const FabricRiskOptions& options) {
    if (!std::isfinite(options.cell_mm) || options.cell_mm < 0.1 || options.cell_mm > 10.0 ||
        !std::isfinite(options.reference_spacing_mm) || options.reference_spacing_mm <= 0.0 ||
        !std::isfinite(options.heatmap_px_per_cell) || options.heatmap_px_per_cell < 1.0 ||
        options.heatmap_px_per_cell > 16.0) {
        return std::unexpected(
            Error{ErrorCategory::UserInput, "risque tissu : parametres de grille invalides", {}});
    }

    // Coefficients exploratoires, sans signification metrologique : a calibrer sur machine.
    double susceptibility = 1.0;
    if (options.fabric == "jersey") {
        susceptibility = 1.45;
    } else if (options.fabric == "felt") {
        susceptibility = 0.75;
    } else if (options.fabric != "woven") {
        return std::unexpected(
            Error{ErrorCategory::UserInput, "risque tissu : choisir woven, jersey ou felt", {}});
    }

    auto model = detail::build_thread_model(project);
    if (!model) {
        return std::unexpected(model.error());
    }
    if (model->segs.empty()) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "risque tissu : aucun segment cousu a analyser",
                                     {}});
    }

    const double cell = options.cell_mm;
    const double spanX = model->max_x - model->min_x;
    const double spanY = model->max_y - model->min_y;
    if (!std::isfinite(spanX) || !std::isfinite(spanY) || spanX < 0.0 || spanY < 0.0 ||
        spanX / cell > 2000.0 || spanY / cell > 2000.0) {
        return std::unexpected(Error{ErrorCategory::OperationImpossible,
                                     "risque tissu : dimensions du motif excessives",
                                     {}});
    }
    const int cols = static_cast<int>(std::ceil(spanX / cell)) + 1;
    const int rows = static_cast<int>(std::ceil(spanY / cell)) + 1;
    const std::size_t count = static_cast<std::size_t>(cols) * rows;
    if (count > 2'000'000) {
        return std::unexpected(
            Error{ErrorCategory::OperationImpossible, "risque tissu : grille trop grande", {}});
    }

    struct Cell {
        double length{0.0};
        double xx{0.0};
        double xy{0.0};
        double yy{0.0};
    };
    std::vector<Cell> cells(count);
    for (const auto& s : model->segs) {
        const double dx = s.bx - s.ax;
        const double dy = s.by - s.ay;
        const double length = std::hypot(dx, dy);
        if (!std::isfinite(length) || length <= 0.0) {
            continue;
        }
        const double ux = dx / length;
        const double uy = dy / length;
        // Echantillonnage deterministe par centres de sous-segments.
        // Limite les traversals pour les brins aberrants sans depasser la grille.
        const int steps = std::max(1, static_cast<int>(std::ceil(length / 0.2)));
        const double dl = length / steps;
        for (int i = 0; i < steps; ++i) {
            const double t = (static_cast<double>(i) + 0.5) / steps;
            const int x =
                std::clamp(static_cast<int>((s.ax + t * dx - model->min_x) / cell), 0, cols - 1);
            const int y =
                std::clamp(static_cast<int>((model->max_y - s.ay - t * dy) / cell), 0, rows - 1);
            Cell& c = cells[static_cast<std::size_t>(y) * cols + x];
            c.length += dl;
            c.xx += dl * ux * ux;
            c.xy += dl * ux * uy;
            c.yy += dl * uy * uy;
        }
    }

    const double oneLayer = cell * cell / options.reference_spacing_mm;
    std::vector<double> scores(count, 0.0);
    std::vector<double> occupied;
    occupied.reserve(count / 4);
    double maximum = 0.0;
    double hotspots = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const Cell& c = cells[i];
        if (c.length <= 0.0) {
            continue;
        }
        const double layers = c.length / oneLayer;
        // 0 = orientations dispersees, 1 = fil localement aligne.
        const double coherence =
            std::clamp(std::hypot(c.xx - c.yy, 2.0 * c.xy) / c.length, 0.0, 1.0);
        // Indice relatif et sature, aucun modele de force/retrait physique.
        const double score = std::clamp(100.0 * susceptibility * (1.0 - std::exp(-0.42 * layers)) *
                                            (0.7 + 0.3 * coherence),
                                        0.0, 100.0);
        scores[i] = score;
        occupied.push_back(score);
        maximum = std::max(maximum, score);
        if (score >= 70.0) {
            hotspots += cell * cell;
        }
    }
    std::sort(occupied.begin(), occupied.end());
    FabricRiskReport report;
    report.max_score = maximum;
    report.p99_score =
        occupied.empty() ? 0.0 : occupied[static_cast<std::size_t>(0.99 * (occupied.size() - 1))];
    report.hotspot_area_mm2 = hotspots;

    nlohmann::json doc = {
        {"schema", kFabricRiskSchema},
        {"method", "relative_heuristic_v1"},
        {"fabric", options.fabric},
        {"cell_mm", cell},
        {"reference_spacing_mm", options.reference_spacing_mm},
        {"grid",
         {{"cols", cols}, {"rows", rows}, {"min_x_mm", model->min_x}, {"max_y_mm", model->max_y}}},
        {"max_score", report.max_score},
        {"p99_score", report.p99_score},
        {"area_score_ge_70_mm2", report.hotspot_area_mm2},
        {"warning", "Indice relatif NON valide comme tension en newtons, "
                    "deformation mesuree ou prediction de plis. "
                    "Profils textiles non calibres sur machine."}};
    report.json = doc.dump(2);
    report.text = "Risque relatif de tirage du tissu (heuristique NON calibree)\n"
                  "  tissu : " +
                  options.fabric + "\n" + "  indice max : " + std::to_string(report.max_score) +
                  " / 100\n" + "  indice p99 : " + std::to_string(report.p99_score) + " / 100\n" +
                  "  surface indice >= 70 : " + std::to_string(hotspots) + " mm2\n" +
                  "  Attention : aucune mesure de tension ni de deformation physique.\n";

    if (options.heatmap) {
        const int scale = static_cast<int>(std::lround(options.heatmap_px_per_cell));
        if (count * static_cast<std::size_t>(scale) * scale > 32'000'000) {
            return std::unexpected(Error{
                ErrorCategory::OperationImpossible, "risque tissu : carte PNG trop grande", {}});
        }
        image::Image map;
        map.width = cols * scale;
        map.height = rows * scale;
        map.rgba.assign(static_cast<std::size_t>(map.width) * map.height * 4, 255);
        // Fond gris, puis interpolation sur une palette fixe : bleu, vert, jaune, rouge.
        constexpr std::array<std::array<double, 3>, 4> palette = {{{{60.0, 135.0, 230.0}},
                                                                   {{65.0, 195.0, 120.0}},
                                                                   {{250.0, 215.0, 65.0}},
                                                                   {{215.0, 45.0, 45.0}}}};
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const double v = scores[static_cast<std::size_t>(y / scale) * cols + x / scale];
                std::array<double, 3> rgb{235.0, 235.0, 235.0};
                if (v > 0.0) {
                    const double pos = std::clamp(v / 100.0 * 3.0, 0.0, 3.0);
                    const int lo = std::min(2, static_cast<int>(pos));
                    const double t = pos - lo;
                    for (int ch = 0; ch < 3; ++ch) {
                        rgb[static_cast<std::size_t>(ch)] =
                            palette[static_cast<std::size_t>(lo)][static_cast<std::size_t>(ch)] *
                                (1.0 - t) +
                            palette[static_cast<std::size_t>(lo + 1)]
                                   [static_cast<std::size_t>(ch)] *
                                t;
                    }
                }
                std::uint8_t* dst =
                    map.rgba.data() + (static_cast<std::size_t>(y) * map.width + x) * 4;
                for (int ch = 0; ch < 3; ++ch) {
                    dst[ch] = static_cast<std::uint8_t>(rgb[static_cast<std::size_t>(ch)]);
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
