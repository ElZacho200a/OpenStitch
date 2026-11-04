// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/applique.hpp"

#include <algorithm>

#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/blanket_stitch.hpp"
#include "openstitch/stitch_generation/running_stitch.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr Micrometers kFlattenTolerance{50};

std::vector<Vec2um> closed_if_needed(std::vector<Vec2um> points, bool closed) {
    if (closed && points.size() >= 2 && points.front() != points.back()) {
        points.push_back(points.front());
    }
    return points;
}

geometry::Path ring_from_points(const std::vector<Vec2um>& points) {
    geometry::Path out;
    out.closed = true;
    out.nodes.reserve(points.size());
    for (const Vec2um& p : points) {
        out.nodes.push_back({p, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return out;
}

void append_rings(std::vector<geometry::Path>& out, const geometry::PathSet& set) {
    const auto add = [&](const geometry::Path& ring) {
        const geometry::Polyline flat = geometry::flatten(ring, kFlattenTolerance);
        if (flat.points.size() >= 3) {
            out.push_back(ring_from_points(flat.points));
        }
    };
    add(set.outer);
    for (const geometry::Path& hole : set.holes) {
        add(hole);
    }
}

} // namespace

document::AppliqueParams clamp_applique(const document::AppliqueParams& params) {
    document::AppliqueParams p = params;
    p.placement_length =
        std::clamp(p.placement_length, kAppliqueMinPlacement, kAppliqueMaxPlacement);
    p.tack_width = std::clamp(p.tack_width, kBlanketMinWidth, kBlanketMaxWidth);
    p.tack_spacing = std::clamp(p.tack_spacing, kBlanketMinSpacing, kBlanketMaxSpacing);
    p.cover_width = std::clamp(p.cover_width, kAppliqueMinCover, kAppliqueMaxCover);
    p.cover_density = std::clamp(p.cover_density, kAppliqueMinDensity, kAppliqueMaxDensity);
    p.cut_offset =
        std::clamp(p.cut_offset, Micrometers{-kAppliqueMaxCutOffset.value}, kAppliqueMaxCutOffset);
    return p;
}

std::vector<Vec2um> applique_placement_points(const geometry::Path& ring,
                                              const document::AppliqueParams& params) {
    const document::AppliqueParams p = clamp_applique(params);
    return closed_if_needed(
        sample_path(ring, p.placement_length, Micrometers{p.placement_length.value / 4}),
        ring.closed);
}

std::vector<Vec2um> applique_tack_points(const geometry::Path& ring,
                                         const document::AppliqueParams& params) {
    const document::AppliqueParams p = clamp_applique(params);
    if (p.tack == document::AppliqueTack::Straight) {
        return closed_if_needed(
            sample_path(ring, p.tack_spacing, Micrometers{p.tack_spacing.value / 4}), ring.closed);
    }
    document::BlanketStitchParams b;
    b.style = p.tack == document::AppliqueTack::Zigzag ? document::BlanketStyle::Zigzag
                                                       : document::BlanketStyle::Blanket;
    b.side = document::BlanketSide::Center;
    b.width = p.tack_width;
    b.spacing = p.tack_spacing;
    return blanket_stitch_points(ring, b);
}

std::vector<geometry::Path> applique_cut_paths(const std::vector<geometry::PathSet>& regions,
                                               const document::AppliqueParams& params) {
    const document::AppliqueParams p = clamp_applique(params);
    std::vector<geometry::Path> out;
    for (const geometry::PathSet& region : regions) {
        if (p.cut_offset.value == 0) {
            append_rings(out, region);
            continue;
        }
        const geometry::PathSet flat = geometry::flatten_to_polygon(region, kFlattenTolerance);
        const auto shifted = geometry::inset_path_set(flat, Micrometers{-p.cut_offset.value});
        if (!shifted) {
            continue; // décalage impossible : région ignorée plutôt qu'un contour faux
        }
        for (const geometry::PathSet& set : *shifted) {
            append_rings(out, set);
        }
    }
    return out;
}

} // namespace openstitch::stitch_generation
