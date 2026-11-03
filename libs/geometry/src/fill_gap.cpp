// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/fill_gap.hpp"

#include <algorithm>
#include <cstdint>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::geometry {

namespace {

constexpr std::int64_t kMinMarginUm = 10'000; // 10 mm de vide autour du dessin

struct Bounds {
    std::int64_t min_x{INT64_MAX};
    std::int64_t min_y{INT64_MAX};
    std::int64_t max_x{INT64_MIN};
    std::int64_t max_y{INT64_MIN};

    void add(const Path& path) {
        for (const PathNode& node : path.nodes) {
            min_x = std::min<std::int64_t>(min_x, node.pos.x.value);
            min_y = std::min<std::int64_t>(min_y, node.pos.y.value);
            max_x = std::max<std::int64_t>(max_x, node.pos.x.value);
            max_y = std::max<std::int64_t>(max_y, node.pos.y.value);
        }
    }
    [[nodiscard]] bool empty() const { return min_x > max_x; }
};

// Pair-impair sur les sommets (les contours issus des booléennes sont des polygones droits).
bool inside(const Path& path, Vec2um p) {
    bool in = false;
    const std::size_t n = path.nodes.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& a = path.nodes[i].pos;
        const auto& b = path.nodes[j].pos;
        if ((a.y.value > p.y.value) != (b.y.value > p.y.value)) {
            const double xCross = static_cast<double>(b.x.value - a.x.value) *
                                      static_cast<double>(p.y.value - a.y.value) /
                                      static_cast<double>(b.y.value - a.y.value) +
                                  static_cast<double>(a.x.value);
            if (static_cast<double>(p.x.value) < xCross) {
                in = !in;
            }
        }
    }
    return in;
}

bool inside(const PathSet& set, Vec2um p) {
    if (set.outer.nodes.size() < 3 || !inside(set.outer, p)) {
        return false;
    }
    return std::none_of(set.holes.begin(), set.holes.end(), [&](const Path& hole) {
        return hole.nodes.size() >= 3 && inside(hole, p);
    });
}

Path rectangle(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
    const auto node = [](std::int64_t x, std::int64_t y) {
        return PathNode{Vec2um{Micrometers{static_cast<std::int32_t>(x)},
                               Micrometers{static_cast<std::int32_t>(y)}},
                        NodeType::Corner, std::nullopt, std::nullopt};
    };
    Path path;
    path.closed = true;
    path.nodes = {node(x0, y0), node(x1, y0), node(x1, y1), node(x0, y1)};
    return path;
}

} // namespace

Result<PathSet> fill_gap_at(const std::vector<PathSet>& obstacles, Vec2um seed, Micrometers grow) {
    // Obstacles aplatis (les courbes deviennent des polygones) et éventuellement dilatés.
    std::vector<PathSet> solids;
    Bounds bounds;
    for (const PathSet& set : obstacles) {
        const PathSet flat = flatten_to_polygon(set, Micrometers{20});
        if (flat.outer.nodes.size() < 3) {
            continue;
        }
        bounds.add(flat.outer);
        if (grow.value > 0) {
            auto grown = inset_path_set(flat, Micrometers{-grow.value});
            if (!grown) {
                return std::unexpected(grown.error());
            }
            solids.insert(solids.end(), grown->begin(), grown->end());
        } else {
            solids.push_back(flat);
        }
    }
    if (bounds.empty() || solids.empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Aucune forme ne délimite de zone à remplir.", "fill_gap_at : obstacles vides");
    }

    const std::int64_t span = std::max(bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y);
    const std::int64_t margin = kMinMarginUm + span / 2 + 2 * grow.value;
    const std::int64_t fx0 = bounds.min_x - margin;
    const std::int64_t fy0 = bounds.min_y - margin;
    const std::int64_t fx1 = bounds.max_x + margin;
    const std::int64_t fy1 = bounds.max_y + margin;
    const auto frame = std::vector<PathSet>{PathSet{rectangle(fx0, fy0, fx1, fy1), {}}};

    auto faces = difference_polygons(frame, solids);
    if (!faces) {
        return std::unexpected(faces.error());
    }
    const PathSet* hit = nullptr;
    for (const PathSet& face : *faces) {
        if (inside(face, seed)) {
            hit = &face;
            break;
        }
    }
    if (hit == nullptr) {
        return fail(ErrorCategory::OperationImpossible,
                    "Cliquez dans un vide : ce point est sur une forme existante.",
                    "fill_gap_at : graine hors de toute face libre");
    }
    Bounds faceBounds;
    faceBounds.add(hit->outer);
    if (faceBounds.min_x <= fx0 || faceBounds.min_y <= fy0 || faceBounds.max_x >= fx1 ||
        faceBounds.max_y >= fy1) {
        return fail(ErrorCategory::OperationImpossible,
                    "La zone n'est pas fermée : elle rejoint l'extérieur du dessin.",
                    "fill_gap_at : face non bornée");
    }

    if (grow.value <= 0) {
        return *hit;
    }
    // Rend à la zone la bande mangée par la dilatation des obstacles.
    auto restored = inset_path_set(*hit, Micrometers{-grow.value});
    if (!restored) {
        return std::unexpected(restored.error());
    }
    const PathSet* best = nullptr;
    for (const PathSet& piece : *restored) {
        if (best == nullptr || path_set_area_um2(piece) > path_set_area_um2(*best)) {
            best = &piece;
        }
    }
    if (best == nullptr) {
        return *hit;
    }
    return *best;
}

} // namespace openstitch::geometry
