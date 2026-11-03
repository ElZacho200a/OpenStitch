// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/cross_stitch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "fill_geometry.hpp"

namespace openstitch::stitch_generation {

namespace {

using detail::P2;

constexpr long long kMaxCells = 4'000'000; // garde-fou contre une grille démesurée

struct Run {
    long long i0{0}; // première cellule (incluse)
    long long i1{0}; // dernière cellule (incluse)
};

struct Edge {
    P2 a;
    P2 b;
};

} // namespace

std::vector<FillStitch> fill_cross_stitch(const geometry::PathSet& region,
                                          const document::CrossStitchParams& params) {
    if (region.outer.nodes.size() < 3) {
        return {};
    }
    const double cell = double(std::clamp(params.cell.value, 1'000, 8'000));
    const int passes = std::clamp(params.passes, 1, 3);
    const double cs = std::cos(params.grid_angle.radians);
    const double sn = std::sin(params.grid_angle.radians);

    // Arêtes de la région dans le repère de la grille (rotation de -angle autour de l'origine).
    std::vector<Edge> edges;
    double minY = 0.0;
    double maxY = 0.0;
    bool first = true;
    detail::for_each_edge(region, [&](P2 a, P2 b) {
        const auto local = [&](P2 p) { return P2{p.x * cs + p.y * sn, -p.x * sn + p.y * cs}; };
        edges.push_back({local(a), local(b)});
        for (const P2 p : {edges.back().a, edges.back().b}) {
            minY = first ? p.y : std::min(minY, p.y);
            maxY = first ? p.y : std::max(maxY, p.y);
            first = false;
        }
    });
    const long long row0 = static_cast<long long>(std::floor(minY / cell));
    const long long row1 = static_cast<long long>(std::floor(maxY / cell));
    if (row1 - row0 > kMaxCells) {
        return {};
    }

    const auto world = [&](double lx, double ly) {
        return detail::to_um(P2{lx * cs - ly * sn, lx * sn + ly * cs});
    };

    std::vector<FillStitch> out;
    std::vector<double> xs;
    long long cellCount = 0;
    long long previousRow = row0 - 2;
    for (long long j = row0; j <= row1; ++j) {
        // Cellules de la rangée j dont le centre est dans la région : balayage à y = centre.
        const double yc = (double(j) + 0.5) * cell;
        xs.clear();
        for (const Edge& e : edges) {
            if ((e.a.y > yc) != (e.b.y > yc)) {
                xs.push_back(e.a.x + (yc - e.a.y) / (e.b.y - e.a.y) * (e.b.x - e.a.x));
            }
        }
        std::sort(xs.begin(), xs.end());
        std::vector<Run> runs;
        for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
            // centre (i + 0,5)·cell dans [xs[k] ; xs[k+1][
            const auto i0 = static_cast<long long>(std::ceil(xs[k] / cell - 0.5));
            const auto i1 = static_cast<long long>(std::ceil(xs[k + 1] / cell - 0.5)) - 1;
            if (i1 < i0) {
                continue;
            }
            if (!runs.empty() && runs.back().i1 + 1 >= i0) {
                runs.back().i1 = std::max(runs.back().i1, i1);
            } else {
                runs.push_back({i0, i1});
            }
        }
        const bool reversed = !params.full_cross && (((j % 2) + 2) % 2 == 1);
        if (reversed) {
            std::reverse(runs.begin(), runs.end());
        }
        for (const Run& run : runs) {
            cellCount += run.i1 - run.i0 + 1;
            if (cellCount > kMaxCells) {
                return out;
            }
            const double y0 = double(j) * cell;
            const double y1 = double(j + 1) * cell;
            std::vector<Vec2um> path;
            for (int pass = 0; pass < passes; ++pass) {
                std::vector<Vec2um> leg;
                // Aller : jambes « / » (bas-gauche -> haut-droite).
                for (long long i = run.i0; i <= run.i1; ++i) {
                    leg.push_back(world(double(i) * cell, y0));
                    leg.push_back(world(double(i + 1) * cell, y1));
                }
                if (params.full_cross) {
                    // Retour : jambes « \ » (bas-droite -> haut-gauche).
                    for (long long i = run.i1; i >= run.i0; --i) {
                        leg.push_back(world(double(i + 1) * cell, y0));
                        leg.push_back(world(double(i) * cell, y1));
                    }
                } else if (reversed) {
                    std::reverse(leg.begin(), leg.end());
                }
                path.insert(path.end(), leg.begin(), leg.end());
            }
            // Raccord avec le tronçon précédent : point commun (omis), pas court d'une rangée à
            // la suivante, ou saut.
            std::size_t from = 0;
            bool jump = !out.empty();
            if (!out.empty()) {
                const Vec2um tail = out.back().pos;
                const double d = std::hypot(double(path.front().x.value) - double(tail.x.value),
                                            double(path.front().y.value) - double(tail.y.value));
                if (d == 0.0) {
                    from = 1;
                    jump = false;
                } else if (d <= 2.0 * cell && j == previousRow + 1) {
                    jump = false;
                }
            }
            for (std::size_t k = from; k < path.size(); ++k) {
                out.push_back({path[k], k == from && jump, false});
            }
            previousRow = j;
        }
    }
    return out;
}

} // namespace openstitch::stitch_generation
