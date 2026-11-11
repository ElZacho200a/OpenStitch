// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/directional_guides.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

#include "fill_geometry.hpp"
#include "openstitch/auto_satin/auto_satin.hpp"

namespace openstitch::stitch_generation {

namespace {

using detail::P2;

// Polyligne ré-échantillonnée tous les `step` µm (extrémités conservées).
std::vector<Vec2um> resample(const std::vector<Vec2um>& line, double step) {
    std::vector<Vec2um> out;
    if (line.size() < 2 || step <= 0.0) {
        return line;
    }
    out.push_back(line.front());
    double carry = 0.0;
    for (std::size_t i = 1; i < line.size(); ++i) {
        const P2 a{double(line[i - 1].x.value), double(line[i - 1].y.value)};
        const P2 b{double(line[i].x.value), double(line[i].y.value)};
        const double len = std::hypot(b.x - a.x, b.y - a.y);
        if (len <= 0.0) {
            continue;
        }
        double at = step - carry;
        while (at < len) {
            out.push_back(detail::to_um(detail::lerp(a, b, at / len)));
            at += step;
        }
        carry = len - (at - step);
    }
    if (out.back() != line.back()) {
        out.push_back(line.back());
    }
    return out;
}

} // namespace

Result<DirectionalGuideResult>
directional_guides_from_region(const geometry::PathSet& region,
                               const DirectionalGuideOptions& options) {
    auto analysis = auto_satin::analyze_region(region, auto_satin::AutoSatinParameters{});
    if (!analysis) {
        return fail_coded(ErrorCategory::OperationImpossible, "E_GUIDE_ANALYSIS",
                          "Analyse du squelette impossible", "Vérifiez que la forme est fermée.",
                          analysis.error().message);
    }
    const auto& edges = analysis->debug.graph.edges;

    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        if (edges[i].length_um >= double(options.min_edge_length.value) &&
            edges[i].centerline.size() >= 2) {
            order.push_back(i);
        }
    }
    if (order.empty()) {
        return fail_coded(ErrorCategory::OperationImpossible, "E_ROUND_SHAPE",
                          "Forme trop ronde ou trop petite pour un guide directionnel",
                          "Utilisez un remplissage radial ou dessinez le guide à la main.");
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (edges[a].length_um != edges[b].length_um) {
            return edges[a].length_um > edges[b].length_um;
        }
        return edges[a].id < edges[b].id;
    });

    DirectionalGuideResult result;
    for (const std::size_t idx : order) {
        if (result.guides.size() >= options.max_guides) {
            break;
        }
        geometry::Path guide;
        guide.closed = false;
        for (const Vec2um p :
             resample(edges[idx].centerline, double(options.resample_step.value))) {
            if (detail::point_in_region(region, P2{double(p.x.value), double(p.y.value)})) {
                guide.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
            }
        }
        if (guide.nodes.size() >= 2) {
            result.guides.push_back(std::move(guide));
        }
    }
    if (result.guides.empty()) {
        return fail_coded(ErrorCategory::OperationImpossible, "E_GUIDE_OUTSIDE",
                          "Aucun guide ne reste dans la forme", "Dessinez le guide à la main.");
    }
    return result;
}

} // namespace openstitch::stitch_generation
