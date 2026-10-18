// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/object_coverage.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <variant>

#include "openstitch/document/footprint.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "raster_coverage.hpp"

namespace openstitch::stitch_analysis {

namespace {

struct Segment {
    double ax, ay, bx, by;
};

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
            sets.push_back(document::satin_polygon(*satin, options.flatten_tolerance));
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
        std::vector<std::vector<raster::Ring>> ringsPerSet;
        for (const auto& s : sets) {
            ringsPerSet.push_back(raster::rings_of(s));
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
            raster::fill_region(g, region, rings);
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
