// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/thickness_map.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace openstitch::stitch_analysis {

double layers_to_mm(double layers, const ThicknessOptions& options) {
    if (!std::isfinite(layers) || layers <= 0.0) {
        return 0.0;
    }
    return layers * static_cast<double>(options.layer_thickness.value) / 1000.0;
}

DensityMap compute_thickness_grid(const stitch::StitchSequence& sequence,
                                  const ThicknessOptions& options) {
    DensityOptions density;
    density.cell = options.cell;
    density.thread_width = options.thread_width;
    density.max_penetrations_per_mm2 = 0.0; // aucune zone sur-dense : on ne mesure que le fil
    density.max_cells = options.max_cells;
    return compute_density_map(sequence, density);
}

ThicknessPeak find_thickness_peak(const stitch::StitchSequence& sequence, const DensityMap& grid,
                                  const ThicknessOptions& options) {
    ThicknessPeak peak;
    if (grid.empty()) {
        return peak;
    }
    // Balayage ligne par ligne : à épaisseur égale, la première case gagne (déterministe).
    std::size_t bestColumn = 0;
    std::size_t bestRow = 0;
    double best = 0.0;
    for (std::size_t row = 0; row < grid.rows; ++row) {
        for (std::size_t column = 0; column < grid.columns; ++column) {
            const double layers = grid.thickness_layers(column, row, options.thread_width);
            if (layers > best) {
                best = layers;
                bestColumn = column;
                bestRow = row;
            }
        }
    }
    if (best <= 0.0) {
        return peak;
    }
    peak.found = true;
    peak.layers = best;
    peak.mm = layers_to_mm(best, options);
    peak.over_limit = options.max_layers > 0.0 && best > options.max_layers;
    const std::int64_t cell = grid.cell.value;
    peak.position =
        Vec2um{Micrometers{static_cast<std::int32_t>(
                   grid.origin.x.value + static_cast<std::int64_t>(bestColumn) * cell + cell / 2)},
               Micrometers{static_cast<std::int32_t>(
                   grid.origin.y.value + static_cast<std::int64_t>(bestRow) * cell + cell / 2)}};

    // Objet dominant : celui dont les piqûres tombent le plus souvent dans la case du pic
    // (égalité : plus petit identifiant).
    std::map<std::uint64_t, std::size_t> counts;
    for (const auto& cmd : sequence.commands) {
        if (cmd.type != stitch::CommandType::Stitch) {
            continue;
        }
        const auto column = static_cast<std::int64_t>(std::floor(
                                static_cast<double>(cmd.pos.x.value) / static_cast<double>(cell))) -
                            grid.origin.x.value / cell;
        const auto row = static_cast<std::int64_t>(std::floor(static_cast<double>(cmd.pos.y.value) /
                                                              static_cast<double>(cell))) -
                         grid.origin.y.value / cell;
        if (column == static_cast<std::int64_t>(bestColumn) &&
            row == static_cast<std::int64_t>(bestRow)) {
            ++counts[cmd.source.value];
        }
    }
    std::size_t most = 0;
    for (const auto& [id, count] : counts) {
        if (count > most) {
            most = count;
            peak.object = ObjectId{id};
        }
    }
    return peak;
}

Micrometers adaptive_cell(double pixels_per_mm, int target_cell_px) {
    constexpr std::array<std::int32_t, 5> kCells{500, 1'000, 2'000, 4'000, 8'000};
    if (!std::isfinite(pixels_per_mm) || pixels_per_mm <= 0.0 || target_cell_px <= 0) {
        return Micrometers{1'000};
    }
    const double wantedUm = 1000.0 * static_cast<double>(target_cell_px) / pixels_per_mm;
    for (const std::int32_t cell : kCells) {
        if (static_cast<double>(cell) >= wantedUm) {
            return Micrometers{cell};
        }
    }
    return Micrometers{kCells.back()};
}

} // namespace openstitch::stitch_analysis
