// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/density_map.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace openstitch::stitch_analysis {

namespace {

constexpr std::int64_t kMinCell = 100; // 0,1 mm : borne basse du côté de case

std::int64_t floor_div(double v, std::int64_t cell) {
    return static_cast<std::int64_t>(std::floor(v / static_cast<double>(cell)));
}

} // namespace

double DensityMap::cell_area_mm2() const {
    const double mm = static_cast<double>(cell.value) / 1000.0;
    return mm * mm;
}

double DensityMap::density_per_mm2(std::size_t column, std::size_t row) const {
    if (column >= columns || row >= rows) {
        return 0.0;
    }
    return static_cast<double>(penetrations[row * columns + column]) / cell_area_mm2();
}

double DensityMap::thickness_layers(std::size_t column, std::size_t row,
                                    Micrometers thread_width) const {
    if (column >= columns || row >= rows) {
        return 0.0;
    }
    const double c = static_cast<double>(cell.value);
    return thread_um[row * columns + column] * static_cast<double>(thread_width.value) / (c * c);
}

double DensityMap::max_density_per_mm2() const {
    if (penetrations.empty()) {
        return 0.0;
    }
    const std::uint32_t best = *std::max_element(penetrations.begin(), penetrations.end());
    return static_cast<double>(best) / cell_area_mm2();
}

DensityMap compute_density_map(const stitch::StitchSequence& sequence,
                               const DensityOptions& options) {
    DensityMap map;
    // Boîte englobante des piqûres.
    bool any = false;
    std::int64_t minX = 0;
    std::int64_t minY = 0;
    std::int64_t maxX = 0;
    std::int64_t maxY = 0;
    for (const auto& cmd : sequence.commands) {
        if (cmd.type != stitch::CommandType::Stitch) {
            continue;
        }
        const std::int64_t x = cmd.pos.x.value;
        const std::int64_t y = cmd.pos.y.value;
        if (!any) {
            minX = maxX = x;
            minY = maxY = y;
            any = true;
        } else {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }
    std::int64_t cell = std::max<std::int64_t>(kMinCell, options.cell.value);
    map.cell = Micrometers{static_cast<std::int32_t>(cell)};
    if (!any) {
        return map;
    }

    const std::size_t cap = std::max<std::size_t>(1, options.max_cells);
    std::int64_t col0 = 0;
    std::int64_t row0 = 0;
    std::int64_t cols = 0;
    std::int64_t rows = 0;
    for (;;) {
        col0 = floor_div(static_cast<double>(minX), cell);
        row0 = floor_div(static_cast<double>(minY), cell);
        cols = floor_div(static_cast<double>(maxX), cell) - col0 + 1;
        rows = floor_div(static_cast<double>(maxY), cell) - row0 + 1;
        if (static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows) <= cap) {
            break;
        }
        cell *= 2;
    }
    map.cell = Micrometers{static_cast<std::int32_t>(cell)};
    map.origin = Vec2um{Micrometers{static_cast<std::int32_t>(col0 * cell)},
                        Micrometers{static_cast<std::int32_t>(row0 * cell)}};
    map.columns = static_cast<std::size_t>(cols);
    map.rows = static_cast<std::size_t>(rows);
    map.penetrations.assign(map.columns * map.rows, 0);
    map.thread_um.assign(map.columns * map.rows, 0.0);

    const auto index_of = [&](double x, double y) {
        const auto c = std::clamp<std::int64_t>(floor_div(x, cell) - col0, 0, cols - 1);
        const auto r = std::clamp<std::int64_t>(floor_div(y, cell) - row0, 0, rows - 1);
        return static_cast<std::size_t>(r) * map.columns + static_cast<std::size_t>(c);
    };

    const double step = static_cast<double>(cell) / 2.0;
    for (std::size_t i = 0; i < sequence.commands.size(); ++i) {
        const auto& cmd = sequence.commands[i];
        if (cmd.type != stitch::CommandType::Stitch) {
            continue;
        }
        const double x = cmd.pos.x.value;
        const double y = cmd.pos.y.value;
        ++map.penetrations[index_of(x, y)];
        if (i > 0 && sequence.commands[i - 1].type == stitch::CommandType::Stitch) {
            const auto& prev = sequence.commands[i - 1];
            const double dx = x - prev.pos.x.value;
            const double dy = y - prev.pos.y.value;
            const double len = std::sqrt(dx * dx + dy * dy);
            const int n = std::max(1, static_cast<int>(std::ceil(len / step)));
            for (int k = 0; k < n; ++k) {
                const double t = (k + 0.5) / n;
                map.thread_um[index_of(prev.pos.x.value + dx * t, prev.pos.y.value + dy * t)] +=
                    len / n;
            }
        }
    }
    return map;
}

std::vector<DensityHotspot> find_density_hotspots(const stitch::StitchSequence& sequence,
                                                  const DensityOptions& options) {
    std::vector<DensityHotspot> out;
    if (options.max_penetrations_per_mm2 <= 0.0) {
        return out;
    }
    const DensityMap map = compute_density_map(sequence, options);
    if (map.empty()) {
        return out;
    }
    const auto over = [&](std::size_t c, std::size_t r) {
        return map.density_per_mm2(c, r) > options.max_penetrations_per_mm2;
    };
    // Étiquette de zone par case (-1 : sous le seuil), composantes 4-connexes parcourues
    // ligne par ligne : ordre déterministe.
    std::vector<int> label(map.penetrations.size(), -1);
    const std::int64_t cell = map.cell.value;
    const std::int64_t ox = map.origin.x.value;
    const std::int64_t oy = map.origin.y.value;
    // Coordonnée (µm) du bord bas/gauche de la colonne/ligne `n`.
    const auto edge = [&](std::int64_t origin, std::size_t n) {
        return static_cast<std::int32_t>(origin + static_cast<std::int64_t>(n) * cell);
    };
    for (std::size_t r = 0; r < map.rows; ++r) {
        for (std::size_t c = 0; c < map.columns; ++c) {
            if (!over(c, r) || label[r * map.columns + c] >= 0) {
                continue;
            }
            const int id = static_cast<int>(out.size());
            DensityHotspot hot;
            hot.center = Vec2um{Micrometers{edge(ox, c) + static_cast<std::int32_t>(cell / 2)},
                                Micrometers{edge(oy, r) + static_cast<std::int32_t>(cell / 2)}};
            std::size_t minC = c;
            std::size_t maxC = c;
            std::size_t minR = r;
            std::size_t maxR = r;
            std::vector<std::pair<std::size_t, std::size_t>> stack{{c, r}};
            label[r * map.columns + c] = id;
            while (!stack.empty()) {
                const auto [cc, rr] = stack.back();
                stack.pop_back();
                ++hot.cells;
                hot.max_penetrations_per_mm2 =
                    std::max(hot.max_penetrations_per_mm2, map.density_per_mm2(cc, rr));
                minC = std::min(minC, cc);
                maxC = std::max(maxC, cc);
                minR = std::min(minR, rr);
                maxR = std::max(maxR, rr);
                const std::pair<std::size_t, std::size_t> neighbours[4] = {
                    {cc + 1, rr}, {cc, rr + 1}, {cc - 1, rr}, {cc, rr - 1}};
                for (const auto& [nc, nr] : neighbours) {
                    // cc - 1 sur 0 déborde en grand size_t : rejeté par le test de borne.
                    if (nc >= map.columns || nr >= map.rows || label[nr * map.columns + nc] >= 0 ||
                        !over(nc, nr)) {
                        continue;
                    }
                    label[nr * map.columns + nc] = id;
                    stack.emplace_back(nc, nr);
                }
            }
            hot.area_mm2 = static_cast<double>(hot.cells) * map.cell_area_mm2();
            hot.box.min = Vec2um{Micrometers{edge(ox, minC)}, Micrometers{edge(oy, minR)}};
            hot.box.max = Vec2um{Micrometers{edge(ox, maxC + 1)}, Micrometers{edge(oy, maxR + 1)}};
            out.push_back(hot);
        }
    }

    // Objet dominant par zone : le plus de piqûres dans ses cases (égalité : plus petit id).
    std::vector<std::map<std::uint64_t, std::size_t>> perZone(out.size());
    for (const auto& cmd : sequence.commands) {
        if (cmd.type != stitch::CommandType::Stitch) {
            continue;
        }
        const auto c = static_cast<std::size_t>(floor_div(cmd.pos.x.value, cell) - ox / cell);
        const auto r = static_cast<std::size_t>(floor_div(cmd.pos.y.value, cell) - oy / cell);
        if (c < map.columns && r < map.rows && label[r * map.columns + c] >= 0) {
            ++perZone[static_cast<std::size_t>(label[r * map.columns + c])][cmd.source.value];
        }
    }
    for (std::size_t z = 0; z < out.size(); ++z) {
        std::size_t best = 0;
        for (const auto& [id, count] : perZone[z]) {
            if (count > best) {
                best = count;
                out[z].object = ObjectId{id};
            }
        }
    }
    return out;
}

} // namespace openstitch::stitch_analysis
