// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/object_coverage.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <variant>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "raster_coverage.hpp"

namespace openstitch::stitch_analysis {

namespace {

using Ring = std::vector<std::pair<double, double>>;

struct Segment {
    double ax, ay, bx, by;
};

// Anneaux d'un PathSet aplati (extérieur puis trous).
std::vector<Ring> rings_of(const geometry::PathSet& flat) {
    std::vector<Ring> rings;
    const auto add = [&](const geometry::Path& path) {
        Ring r;
        r.reserve(path.nodes.size());
        for (const auto& n : path.nodes) {
            r.emplace_back(static_cast<double>(n.pos.x.value), static_cast<double>(n.pos.y.value));
        }
        if (r.size() >= 3) {
            rings.push_back(std::move(r));
        }
    };
    add(flat.outer);
    for (const auto& h : flat.holes) {
        add(h);
    }
    return rings;
}

// Remplit `mask` (OU) avec l'intérieur d'un PathSet : balayage par ligne au centre des cellules,
// règle pair-impair sur l'ensemble des anneaux (extérieur + trous).
void fill_region(const raster::Grid& g, std::vector<char>& mask, const std::vector<Ring>& rings) {
    std::vector<double> xs;
    for (int iy = 0; iy < g.h; ++iy) {
        const double y = g.center_y(iy);
        xs.clear();
        for (const Ring& r : rings) {
            for (std::size_t i = 0; i < r.size(); ++i) {
                const auto [px, py] = r[i];
                const auto [qx, qy] = r[(i + 1) % r.size()];
                if ((py <= y) == (qy <= y)) {
                    continue;
                }
                xs.push_back(px + (y - py) * (qx - px) / (qy - py));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
            // Cellules dont le centre x est dans [xs[k], xs[k+1]).
            const int a = std::max(0, static_cast<int>(std::ceil((xs[k] - g.x0) / g.cell - 0.5)));
            const int b = std::min(
                g.w - 1, static_cast<int>(std::ceil((xs[k + 1] - g.x0) / g.cell - 0.5)) - 1);
            for (int ix = a; ix <= b; ++ix) {
                mask[g.index(ix, iy)] = 1;
            }
        }
    }
}

// Polygone d'un satin à rails : rail A puis rail B parcouru à l'envers.
geometry::PathSet satin_polygon(const document::SatinParams& p, Micrometers tolerance) {
    geometry::PathSet set;
    const auto a = geometry::flatten(p.rail_a, tolerance).points;
    auto b = geometry::flatten(p.rail_b, tolerance).points;
    std::reverse(b.begin(), b.end());
    for (const auto& pt : a) {
        set.outer.nodes.push_back({pt, geometry::NodeType::Corner, {}, {}});
    }
    for (const auto& pt : b) {
        set.outer.nodes.push_back({pt, geometry::NodeType::Corner, {}, {}});
    }
    set.outer.closed = true;
    return set;
}

} // namespace

std::vector<ObjectCoverage> object_coverage(const document::Project& project,
                                            const stitch::StitchSequence& sequence,
                                            const ObjectCoverageOptions& options) {
    // Segments visibles par objet (map ordonnée : aucune dépendance à l'ordre de hachage).
    std::map<std::uint64_t, std::vector<Segment>> segments;
    const auto& cmds = sequence.commands;
    const auto visible = [](stitch::StitchPass p) {
        return p == stitch::StitchPass::TopStitch || p == stitch::StitchPass::Manual;
    };
    for (std::size_t i = 1; i < cmds.size(); ++i) {
        const auto& a = cmds[i - 1];
        const auto& b = cmds[i];
        if (a.type != stitch::CommandType::Stitch || b.type != stitch::CommandType::Stitch ||
            a.source != b.source || !b.source.valid() || !visible(a.pass) || !visible(b.pass)) {
            continue;
        }
        segments[b.source.value].push_back(
            {static_cast<double>(a.pos.x.value), static_cast<double>(a.pos.y.value),
             static_cast<double>(b.pos.x.value), static_cast<double>(b.pos.y.value)});
    }

    const double r = static_cast<double>(options.coverage_width.value) / 2.0;
    std::vector<ObjectCoverage> out;
    for (const auto& obj : project.embroidery_objects) {
        if (!obj.visible) {
            continue;
        }
        ObjectCoverage oc;
        oc.object = obj.id;
        oc.source_vector = obj.source_vector;
        const auto unmeasured = [&](std::string why) {
            oc.measured = false;
            oc.reason = std::move(why);
            out.push_back(oc);
        };

        // Région : sets aplatis (un par morceau du vecteur, ou le polygone des rails du satin).
        std::vector<geometry::PathSet> sets;
        if (std::holds_alternative<document::RunningStitchParams>(obj.params)) {
            unmeasured("contour : pas de surface à couvrir");
            continue;
        }
        if (const auto* satin = std::get_if<document::SatinParams>(&obj.params)) {
            sets.push_back(satin_polygon(*satin, options.flatten_tolerance));
        } else {
            const auto* vec = project.findObject(obj.source_vector);
            if (vec == nullptr) {
                unmeasured("objet vectoriel source introuvable");
                continue;
            }
            for (const auto& s : vec->paths) {
                sets.push_back(geometry::flatten_to_polygon(s, options.flatten_tolerance));
            }
        }
        double areaUm2 = 0.0;
        for (const auto& s : sets) {
            areaUm2 += geometry::path_set_area_um2(s);
        }
        oc.region_area_mm2 = areaUm2 / 1e6;
        if (areaUm2 <= 0.0) {
            unmeasured("région vide");
            continue;
        }
        const auto segIt = segments.find(obj.id.value);
        if (segIt == segments.end() || segIt->second.empty()) {
            unmeasured("aucun point");
            continue;
        }

        // Emprise : région + segments + demi-largeur de trace.
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        const auto grow = [&](double x, double y) {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        };
        std::vector<std::vector<Ring>> ringsPerSet;
        for (const auto& s : sets) {
            ringsPerSet.push_back(rings_of(s));
            for (const auto& ring : ringsPerSet.back()) {
                for (const auto& [x, y] : ring) {
                    grow(x, y);
                }
            }
        }
        for (const auto& s : segIt->second) {
            grow(s.ax - r, s.ay - r);
            grow(s.bx + r, s.by + r);
        }
        if (minX > maxX) {
            unmeasured("région vide");
            continue;
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

        std::vector<char> region(g.size(), 0);
        for (const auto& rings : ringsPerSet) {
            fill_region(g, region, rings);
        }
        std::vector<char> swept(g.size(), 0);
        for (const auto& s : segIt->second) {
            raster::stamp_segment(g, swept, s.ax, s.ay, s.bx, s.by, r);
        }
        std::size_t regionCells = 0, sweptCells = 0, both = 0;
        for (std::size_t i = 0; i < region.size(); ++i) {
            regionCells += region[i] ? 1 : 0;
            sweptCells += swept[i] ? 1 : 0;
            both += (region[i] && swept[i]) ? 1 : 0;
        }
        if (regionCells == 0) {
            unmeasured("région plus petite que la cellule de mesure");
            continue;
        }
        oc.measured = true;
        oc.covered_ratio = static_cast<double>(both) / static_cast<double>(regionCells);
        oc.overflow_ratio = sweptCells == 0 ? 0.0
                                            : static_cast<double>(sweptCells - both) /
                                                  static_cast<double>(sweptCells);
        out.push_back(oc);
    }
    return out;
}

} // namespace openstitch::stitch_analysis
