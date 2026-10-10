// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/underlay_auto.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::stitch_generation {

namespace {

// Aire signée (µm²) et périmètre (µm) d'un chemin fermé aplati.
struct RingMeasure {
    double signed_area{0.0};
    double perimeter{0.0};
};

RingMeasure measure_ring(const std::vector<Vec2um>& pts) {
    RingMeasure m;
    const std::size_t n = pts.size();
    if (n < 2) {
        return m;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2um a = pts[i];
        const Vec2um b = pts[(i + 1) % n];
        m.signed_area += 0.5 * (static_cast<double>(a.x.value) * static_cast<double>(b.y.value) -
                                static_cast<double>(b.x.value) * static_cast<double>(a.y.value));
        m.perimeter += length_um(b - a);
    }
    return m;
}

} // namespace

ShapeMetrics measure_shape(const geometry::PathSet& region) {
    constexpr Micrometers kFlatten{100};
    ShapeMetrics out;
    const auto outer = measure_ring(geometry::flatten(region.outer, kFlatten).points);
    double area = std::abs(outer.signed_area);
    double perimeter = outer.perimeter;
    for (const geometry::Path& hole : region.holes) {
        const auto h = measure_ring(geometry::flatten(hole, kFlatten).points);
        area -= std::abs(h.signed_area);
        perimeter += h.perimeter;
    }
    area = std::max(area, 0.0);
    out.area_mm2 = area / 1.0e6;
    out.perimeter_mm = perimeter / 1.0e3;
    out.mean_width_mm = out.perimeter_mm > 1e-9 ? 2.0 * out.area_mm2 / out.perimeter_mm : 0.0;
    return out;
}

FillUnderlayChoice choose_fill_underlay(const ShapeMetrics& shape) {
    FillUnderlayChoice c;
    if (shape.area_mm2 < 6.0 || shape.mean_width_mm < 0.8) {
        return c; // rien : forme trop petite ou trop fine
    }
    c.edge = true;
    c.parallel = shape.area_mm2 >= 60.0;
    c.spacing = Micrometers{shape.area_mm2 >= 400.0 ? 2'500 : 2'000};
    const double inset = std::clamp(shape.mean_width_mm * 0.25, 0.3, 0.6) * 1000.0;
    c.inset = Micrometers{static_cast<std::int32_t>(std::lround(inset))};
    return c;
}

SatinUnderlayChoice choose_satin_underlay(double width_mm) {
    SatinUnderlayChoice c;
    if (width_mm < 1.0) {
        return c;
    }
    if (width_mm < 3.5) {
        c.center = true;
    } else if (width_mm < 7.0) {
        c.edge = true;
    } else {
        c.edge = true;
        c.zigzag = true;
    }
    return c;
}

double satin_mean_width_mm(const geometry::Path& rail_a, const geometry::Path& rail_b) {
    const auto a = geometry::flatten(rail_a, Micrometers{100}).points;
    auto b = geometry::flatten(rail_b, Micrometers{100}).points;
    if (a.size() < 2 || b.size() < 2) {
        return 0.0;
    }
    // Rails fournis tête-bêche : on retourne B si ses extrémités sont croisées.
    if (length_um(a.front() - b.back()) + length_um(a.back() - b.front()) <
        length_um(a.front() - b.front()) + length_um(a.back() - b.back())) {
        std::reverse(b.begin(), b.end());
    }
    std::vector<Vec2um> ring = a;
    ring.insert(ring.end(), b.rbegin(), b.rend());
    const double area = std::abs(measure_ring(ring).signed_area);
    const double len = 0.5 * (geometry::polyline_length(a) + geometry::polyline_length(b));
    return len > 1e-9 ? area / len / 1000.0 : 0.0;
}

document::TatamiParams resolve_underlay(const document::TatamiParams& params,
                                        const geometry::PathSet& region) {
    if (params.underlay_mode != document::UnderlayMode::Auto) {
        return params;
    }
    const FillUnderlayChoice c = choose_fill_underlay(measure_shape(region));
    document::TatamiParams out = params;
    out.underlay_edge = c.edge;
    out.underlay_parallel = c.parallel;
    out.underlay_inset = c.inset;
    out.underlay_spacing = c.spacing;
    return out;
}

document::DirectionalFillParams resolve_underlay(const document::DirectionalFillParams& params,
                                                 const geometry::PathSet& region) {
    if (params.underlay_mode != document::UnderlayMode::Auto) {
        return params;
    }
    const FillUnderlayChoice c = choose_fill_underlay(measure_shape(region));
    document::DirectionalFillParams out = params;
    out.underlay_edge = c.edge;
    out.underlay_parallel = c.parallel;
    out.underlay_inset = c.inset;
    out.underlay_spacing = c.spacing;
    return out;
}

} // namespace openstitch::stitch_generation
