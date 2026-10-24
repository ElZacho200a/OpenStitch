// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/color_blend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"

namespace openstitch::stitch_generation {

namespace {

template <typename Params> bool valid_common(const Params& params) {
    return params.row_spacing.value > 0 && params.stitch_length.value > 0 &&
           params.inset.value >= 0 && params.stagger > 0 && params.underlay_inset.value >= 0 &&
           params.underlay_spacing.value > 0 &&
           (params.underlay_mode == document::UnderlayMode::Manual ||
            params.underlay_mode == document::UnderlayMode::Auto);
}

bool valid_fill(const document::EmbroideryObject& object) {
    if (const auto* tatami = std::get_if<document::TatamiParams>(&object.params)) {
        return valid_common(*tatami) && std::isfinite(tatami->angle.radians);
    }
    if (const auto* directional = std::get_if<document::DirectionalFillParams>(&object.params)) {
        return valid_common(*directional) && std::isfinite(directional->edge_weight) &&
               directional->edge_weight >= 0.0 && directional->edge_weight <= 1.0 &&
               std::isfinite(directional->spacing_regularity) &&
               directional->spacing_regularity >= 0.0 && directional->spacing_regularity <= 1.0 &&
               directional->sector_overlap.value >= 0 && directional->handmade_intensity >= 0 &&
               directional->handmade_intensity <= 100;
    }
    return false;
}

// Contours aplatis pour aligner les guides, y compris une
// forme courbe dont les seuls nœuds ne reflètent pas l'étendue réelle.
std::optional<std::vector<geometry::PathSet>>
blend_outlines(const std::vector<geometry::PathSet>& shape) {
    if (shape.empty()) {
        return std::nullopt;
    }
    std::vector<geometry::PathSet> outlines;
    outlines.reserve(shape.size());
    for (const auto& set : shape) {
        if (!set.outer.closed || geometry::path_set_area_um2(set) <= 0.0 ||
            std::any_of(set.holes.begin(), set.holes.end(), [](const geometry::Path& hole) {
                return !hole.closed || std::abs(geometry::signed_area_um2(hole)) <= 0.0;
            })) {
            return std::nullopt;
        }
        outlines.push_back({geometry::flatten_to_polygon(set.outer, Micrometers{50}), {}});
    }
    return outlines;
}

// Comme align_guides_across_density_gradient, mais le fondu impose aussi son
// orientation lorsque les deux espacements sont égaux ou la forme est petite.
std::optional<geometry::Path> blend_guide(const std::vector<geometry::PathSet>& shape,
                                          const document::DensityGradient& gradient) {
    const double ax = static_cast<double>(gradient.to.x.value) - gradient.from.x.value;
    const double ay = static_cast<double>(gradient.to.y.value) - gradient.from.y.value;
    const double length = std::hypot(ax, ay);
    if (length < 1.0) {
        return std::nullopt; // axe dégénéré : garder les guides existants
    }
    const double dx = -ay / length;
    const double dy = ax / length;
    double x0 = std::numeric_limits<double>::max();
    double y0 = x0;
    double x1 = std::numeric_limits<double>::lowest();
    double y1 = x1;
    for (const auto& set : shape) {
        for (const auto& node : set.outer.nodes) {
            x0 = std::min(x0, static_cast<double>(node.pos.x.value));
            y0 = std::min(y0, static_cast<double>(node.pos.y.value));
            x1 = std::max(x1, static_cast<double>(node.pos.x.value));
            y1 = std::max(y1, static_cast<double>(node.pos.y.value));
        }
    }
    const double cx = (x0 + x1) * 0.5;
    const double cy = (y0 + y1) * 0.5;
    double lo = 0.0;
    double hi = 0.0;
    for (const auto& set : shape) {
        for (const auto& node : set.outer.nodes) {
            const double t = (node.pos.x.value - cx) * dx + (node.pos.y.value - cy) * dy;
            lo = std::min(lo, t);
            hi = std::max(hi, t);
        }
    }
    geometry::Path guide;
    guide.closed = false;
    for (const double t : {lo, hi}) {
        const double x = std::round(cx + 0.8 * t * dx);
        const double y = std::round(cy + 0.8 * t * dy);
        if (x < std::numeric_limits<std::int32_t>::min() ||
            x > std::numeric_limits<std::int32_t>::max() ||
            y < std::numeric_limits<std::int32_t>::min() ||
            y > std::numeric_limits<std::int32_t>::max()) {
            return std::nullopt;
        }
        guide.nodes.push_back({Vec2um{Micrometers{static_cast<std::int32_t>(x)},
                                      Micrometers{static_cast<std::int32_t>(y)}},
                               geometry::NodeType::Corner,
                               {},
                               {}});
    }
    return guide.nodes.front().pos == guide.nodes.back().pos ? std::nullopt
                                                             : std::optional{std::move(guide)};
}

document::DirectionalFillParams as_directional(const document::EmbroideryObject& object,
                                               const std::vector<geometry::PathSet>& shape) {
    if (const auto* directional = std::get_if<document::DirectionalFillParams>(&object.params)) {
        return *directional;
    }
    return directional_from_tatami(std::get<document::TatamiParams>(object.params), shape,
                                   static_cast<std::uint32_t>(object.id.value));
}

void set_blend_gradient(document::DirectionalFillParams& params,
                        const document::DensityGradient& gradient,
                        const std::optional<geometry::Path>& guide) {
    params.row_spacing = gradient.spacing_from;
    params.density_gradient = gradient;
    params.underlay_mode = document::UnderlayMode::Manual;
    params.underlay_edge = params.underlay_parallel = params.underlay_zigzag = false;
    if (guide) {
        params.guides = {*guide};
    }
}

} // namespace

std::optional<std::pair<document::EmbroideryObject, document::EmbroideryObject>>
make_color_blend_layers(const document::EmbroideryObject& background,
                        const document::EmbroideryObject& foreground,
                        const std::vector<geometry::PathSet>& shape,
                        const document::DensityGradient& gradient, bool complementary) {
    if (!valid_fill(background) || !valid_fill(foreground) || gradient.spacing_from.value < 100 ||
        gradient.spacing_from.value > 4'000 || gradient.spacing_to.value < 100 ||
        gradient.spacing_to.value > 4'000) {
        return std::nullopt;
    }
    const auto outlines = blend_outlines(shape);
    if (!outlines) {
        return std::nullopt;
    }
    const auto guide = blend_guide(*outlines, gradient);
    if (gradient.from != gradient.to && !guide) {
        return std::nullopt;
    }
    auto base = background;
    auto top = foreground;
    auto topParams = as_directional(foreground, *outlines);
    auto topGradient = gradient;
    if (complementary) {
        topGradient.interpolate_density = true;
        auto baseGradient = topGradient;
        std::swap(baseGradient.spacing_from, baseGradient.spacing_to);
        auto baseParams = as_directional(background, *outlines);
        set_blend_gradient(baseParams, baseGradient, guide);
        base.params = std::move(baseParams);
    } else if (auto* baseParams = std::get_if<document::DirectionalFillParams>(&base.params)) {
        baseParams->density_gradient.reset();
    }
    set_blend_gradient(topParams, topGradient, guide);
    top.params = std::move(topParams);
    return std::pair{std::move(base), std::move(top)};
}

} // namespace openstitch::stitch_generation
