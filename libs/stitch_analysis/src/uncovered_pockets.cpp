// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/uncovered_pockets.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "openstitch/document/footprint.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"
#include "raster_coverage.hpp"

namespace openstitch::stitch_analysis {

namespace {

using geometry::PathSet;

Result<std::vector<PathSet>> unite(const std::vector<PathSet>& sets) {
    return geometry::union_polygons(sets);
}

// Décale chaque morceau de `delta` (> 0 : vers l'intérieur) ; les morceaux qui disparaissent
// sont écartés.
Result<std::vector<PathSet>> offset_all(const std::vector<PathSet>& sets, std::int32_t delta) {
    std::vector<PathSet> out;
    for (const PathSet& set : sets) {
        auto moved = geometry::inset_path_set(set, Micrometers{delta});
        if (!moved) {
            return std::unexpected(moved.error());
        }
        out.insert(out.end(), std::make_move_iterator(moved->begin()),
                   std::make_move_iterator(moved->end()));
    }
    return out;
}

struct Box {
    double x0{std::numeric_limits<double>::max()};
    double y0{std::numeric_limits<double>::max()};
};

Box box_of(const PathSet& set) {
    Box box;
    for (const auto& node : set.outer.nodes) {
        box.x0 = std::min(box.x0, static_cast<double>(node.pos.x.value));
        box.y0 = std::min(box.y0, static_cast<double>(node.pos.y.value));
    }
    return box;
}

PathSet cell_rect(double x0, double y0, double x1, double y1) {
    PathSet set;
    const auto node = [](double x, double y) {
        return geometry::PathNode{Vec2um{Micrometers{static_cast<std::int32_t>(std::llround(x))},
                                         Micrometers{static_cast<std::int32_t>(std::llround(y))}},
                                  geometry::NodeType::Corner,
                                  {},
                                  {}};
    };
    set.outer.nodes = {node(x0, y0), node(x1, y0), node(x1, y1), node(x0, y1)};
    return set;
}

} // namespace

Result<std::vector<PathSet>> find_uncovered_pockets(const document::Project& project,
                                                    const stitch::StitchSequence& sequence,
                                                    const std::vector<ObjectId>& vector_ids,
                                                    const UncoveredPocketOptions& options) {
    // 1. Emprise nominale du bloc.
    std::vector<PathSet> footprints;
    for (const auto& obj : project.embroidery_objects) {
        if (std::find(vector_ids.begin(), vector_ids.end(), obj.source_vector) ==
            vector_ids.end()) {
            continue;
        }
        auto fp = document::embroidery_footprint(project, obj, options.flatten_tolerance);
        footprints.insert(footprints.end(), std::make_move_iterator(fp.begin()),
                          std::make_move_iterator(fp.end()));
    }
    auto raw = unite(footprints);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    if (raw->empty()) {
        return fail(ErrorCategory::OperationImpossible,
                    "Le bloc n'a aucune surface brodée (formes sans remplissage ni satin).");
    }

    // 2. Enveloppe : fermeture sur `closing_gap` (fentes entre formes), puis sans trous.
    std::vector<PathSet> hullSets = *raw;
    if (options.closing_gap.value > 0) {
        const std::int32_t half = std::max(1, options.closing_gap.value / 2);
        auto grown = offset_all(*raw, -half);
        if (!grown) {
            return std::unexpected(grown.error());
        }
        auto joined = unite(*grown);
        if (!joined) {
            return std::unexpected(joined.error());
        }
        auto shrunk = offset_all(*joined, half);
        if (!shrunk) {
            return std::unexpected(shrunk.error());
        }
        // Une fermeture ne retire jamais de matière : on garde aussi l'union brute.
        hullSets = std::move(*shrunk);
        hullSets.insert(hullSets.end(), raw->begin(), raw->end());
    }
    for (PathSet& set : hullSets) {
        set.holes.clear();
    }
    auto hull = unite(hullSets);
    if (!hull) {
        return std::unexpected(hull.error());
    }

    // 3. Masque des cellules de l'enveloppe, puis des cellules couvertes par un fil visible.
    double minX = std::numeric_limits<double>::max(), minY = minX;
    double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
    std::vector<std::vector<raster::Ring>> hullRings;
    for (const PathSet& set : *hull) {
        hullRings.push_back(raster::rings_of(set));
        for (const auto& ring : hullRings.back()) {
            for (const auto& [x, y] : ring) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    raster::Grid g;
    g.cell = static_cast<double>(std::max(1, options.cell.value));
    for (;;) {
        g.x0 = minX - g.cell;
        g.y0 = minY - g.cell;
        g.w = static_cast<int>(std::ceil((maxX - minX) / g.cell)) + 2;
        g.h = static_cast<int>(std::ceil((maxY - minY) / g.cell)) + 2;
        if (g.size() <= options.max_cells) {
            break;
        }
        g.cell *= 2.0;
    }
    std::vector<char> inside(g.size(), 0);
    for (const auto& rings : hullRings) {
        raster::fill_region(g, inside, rings);
    }
    std::vector<char> covered(g.size(), 0);
    const double r = static_cast<double>(options.coverage_width.value) / 2.0;
    const auto visible = [](stitch::StitchPass p) {
        return p == stitch::StitchPass::TopStitch || p == stitch::StitchPass::Manual;
    };
    const auto& cmds = sequence.commands;
    for (std::size_t i = 1; i < cmds.size(); ++i) {
        const auto& a = cmds[i - 1];
        const auto& b = cmds[i];
        // Mêmes règles que `object_coverage` : paires de points de dessus du même objet.
        if (a.type != stitch::CommandType::Stitch || b.type != stitch::CommandType::Stitch ||
            a.source != b.source || !b.source.valid() || !visible(a.pass) || !visible(b.pass)) {
            continue;
        }
        raster::stamp_segment(
            g, covered, static_cast<double>(a.pos.x.value), static_cast<double>(a.pos.y.value),
            static_cast<double>(b.pos.x.value), static_cast<double>(b.pos.y.value), r);
    }

    // 4. Cellules non couvertes de l'enveloppe -> rectangles (une course par ligne) -> union.
    std::vector<PathSet> rects;
    for (int iy = 0; iy < g.h; ++iy) {
        int ix = 0;
        while (ix < g.w) {
            if (!inside[g.index(ix, iy)] || covered[g.index(ix, iy)]) {
                ++ix;
                continue;
            }
            const int start = ix;
            while (ix < g.w && inside[g.index(ix, iy)] && !covered[g.index(ix, iy)]) {
                ++ix;
            }
            rects.push_back(cell_rect(g.x0 + start * g.cell, g.y0 + iy * g.cell, g.x0 + ix * g.cell,
                                      g.y0 + (iy + 1) * g.cell));
        }
    }
    if (rects.empty()) {
        return std::vector<PathSet>{};
    }
    auto pockets = unite(rects);
    if (!pockets) {
        return std::unexpected(pockets.error());
    }

    // 5. Ouverture morphologique : écarte les liserés et les interstices plus étroits que
    // `min_width`.
    std::vector<PathSet> opened = std::move(*pockets);
    if (options.min_width.value > 0) {
        const std::int32_t half = std::max(1, options.min_width.value / 2);
        auto eroded = offset_all(opened, half);
        if (!eroded) {
            return std::unexpected(eroded.error());
        }
        auto restored = offset_all(*eroded, -half);
        if (!restored) {
            return std::unexpected(restored.error());
        }
        opened = std::move(*restored);
    }

    // 6. Filtre d'aire, débordement sur les voisines, tri déterministe.
    std::erase_if(opened, [&](const PathSet& set) {
        const double area = geometry::path_set_area_um2(set);
        return area < options.min_area_um2 ||
               (options.max_area_um2 > 0.0 && area > options.max_area_um2);
    });
    if (opened.empty()) {
        return std::vector<PathSet>{};
    }
    std::vector<PathSet> result = std::move(opened);
    if (options.overlap.value > 0) {
        auto grown = offset_all(result, -options.overlap.value);
        if (!grown) {
            return std::unexpected(grown.error());
        }
        auto merged = unite(*grown);
        if (!merged) {
            return std::unexpected(merged.error());
        }
        result = std::move(*merged);
    }
    std::stable_sort(result.begin(), result.end(), [](const PathSet& a, const PathSet& b) {
        const double areaA = geometry::path_set_area_um2(a);
        const double areaB = geometry::path_set_area_um2(b);
        if (areaA != areaB) {
            return areaA > areaB;
        }
        const Box boxA = box_of(a);
        const Box boxB = box_of(b);
        if (boxA.x0 != boxB.x0) {
            return boxA.x0 < boxB.x0;
        }
        return boxA.y0 < boxB.y0;
    });
    return result;
}

} // namespace openstitch::stitch_analysis
