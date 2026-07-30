// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/debug_export.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace openstitch::auto_satin {

namespace {

double mmx(Vec2um p) { return static_cast<double>(p.x.value) / 1000.0; }
double mmy(Vec2um p) { return -static_cast<double>(p.y.value) / 1000.0; }  // Y vers le bas en SVG

void poly(std::ostringstream& o, const std::vector<Vec2um>& pts, const char* stroke, double w,
          bool closed, const char* dash = "") {
    if (pts.size() < 2) {
        return;
    }
    o << "<path d=\"M";
    for (std::size_t i = 0; i < pts.size(); ++i) {
        o << (i ? "L" : "") << mmx(pts[i]) << " " << mmy(pts[i]) << " ";
    }
    if (closed) {
        o << "Z";
    }
    o << "\" fill=\"none\" stroke=\"" << stroke << "\" stroke-width=\"" << w << "\"";
    if (dash[0]) {
        o << " stroke-dasharray=\"" << dash << "\"";
    }
    o << "/>\n";
}

std::vector<Vec2um> nodes_of(const geometry::Path& p) {
    std::vector<Vec2um> v;
    for (const auto& n : p.nodes) {
        v.push_back(n.pos);
    }
    return v;
}

}  // namespace

std::string to_debug_svg(
    const geometry::PathSet& region, const AutoSatinAnalysis& a,
    const std::optional<std::pair<geometry::Path, geometry::Path>>& current_rails) {
    // Bornes.
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (const auto& n : region.outer.nodes) {
        minx = std::min(minx, mmx(n.pos));
        maxx = std::max(maxx, mmx(n.pos));
        miny = std::min(miny, mmy(n.pos));
        maxy = std::max(maxy, mmy(n.pos));
    }
    const double m = 3.0;
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << (minx - m) << " " << (miny - m)
      << " " << (maxx - minx + 2 * m) << " " << (maxy - miny + 2 * m) << "\">\n";
    o << "<!-- satinabilite: " << to_string(a.report.status)
      << " branches=" << a.report.branch_count << " jonctions=" << a.report.junction_count
      << " extremites=" << a.report.endpoint_count << " -->\n";

    // Contour + trous.
    poly(o, nodes_of(region.outer), "#111", 0.15, true);
    for (const auto& h : region.holes) {
        poly(o, nodes_of(h), "#c22", 0.15, true);
    }
    // Rails actuels (comparaison).
    if (current_rails) {
        poly(o, nodes_of(current_rails->first), "#999", 0.12, false, "0.6 0.4");
        poly(o, nodes_of(current_rails->second), "#999", 0.12, false, "0.6 0.4");
    }
    // Arêtes du squelette.
    for (const auto& e : a.debug.graph.edges) {
        poly(o, e.centerline, "#1a6", 0.18, false);
    }
    // Nœuds.
    for (const auto& n : a.debug.graph.nodes) {
        const char* col = n.type == SkeletonNodeType::Junction ? "#e80"
                          : n.type == SkeletonNodeType::Endpoint ? "#0a0"
                                                                 : "#66c";
        o << "<circle cx=\"" << mmx(n.position) << "\" cy=\"" << mmy(n.position)
          << "\" r=\"0.5\" fill=\"" << col << "\"/>\n";
    }
    o << "</svg>\n";
    return o.str();
}

std::string columns_to_svg(const geometry::PathSet& region, const SatinColumnsResult& result) {
    double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
    for (const auto& n : region.outer.nodes) {
        minx = std::min(minx, mmx(n.pos));
        maxx = std::max(maxx, mmx(n.pos));
        miny = std::min(miny, mmy(n.pos));
        maxy = std::max(maxy, mmy(n.pos));
    }
    const double m = 3.0;
    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" << (minx - m) << " " << (miny - m)
      << " " << (maxx - minx + 2 * m) << " " << (maxy - miny + 2 * m) << "\">\n";
    o << "<!-- statut: " << to_string(result.status) << " colonnes=" << result.columns.size();
    if (!result.refusal.empty()) {
        o << " REFUS: " << result.refusal;
    }
    o << " -->\n";

    poly(o, nodes_of(region.outer), "#111", 0.15, true);
    for (const auto& h : region.holes) {
        poly(o, nodes_of(h), "#c22", 0.15, true);
    }
    for (const auto& e : result.debug.graph.edges) {
        poly(o, e.centerline, "#8c8", 0.12, false, "0.5 0.4");
    }
    for (const auto& col : result.columns) {
        // Barreaux d'abord (dessous).
        for (const auto& r : col.rungs) {
            o << "<line x1=\"" << mmx(r.a) << "\" y1=\"" << mmy(r.a) << "\" x2=\"" << mmx(r.b)
              << "\" y2=\"" << mmy(r.b) << "\" stroke=\"#999\" stroke-width=\"0.12\"/>\n";
        }
        poly(o, nodes_of(col.rail_a), "#1560c8", 0.2, false);
        poly(o, nodes_of(col.rail_b), "#e07000", 0.2, false);
    }
    o << "</svg>\n";
    return o.str();
}

}  // namespace openstitch::auto_satin
