// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/moments.hpp"

#include <cmath>
#include <limits>
#include <numbers>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::geometry {

namespace {

// Tolérance d'aplatissement des Béziers : sous la résolution DST (100 µm).
constexpr Micrometers kFlattenTolerance{50};

// Moments bruts (∫1, ∫x, ∫y, ∫x², ∫y², ∫xy) d'un contour, en mm (origine
// `ref`) pour limiter les pertes de précision sur de grandes formes.
struct Moments {
    double a{0.0}, sx{0.0}, sy{0.0}, sxx{0.0}, syy{0.0}, sxy{0.0};
};

void accumulate(const Path& path, Vec2um ref, bool hole, Moments& m) {
    const Polyline poly = flatten(path, kFlattenTolerance);
    const auto& p = poly.points;
    const std::size_t n = p.size();
    if (n < 3) {
        return;
    }
    Moments r;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2um a = p[i];
        const Vec2um b = p[(i + 1) % n];
        const double x0 = (a.x.value - ref.x.value) / 1000.0;
        const double y0 = (a.y.value - ref.y.value) / 1000.0;
        const double x1 = (b.x.value - ref.x.value) / 1000.0;
        const double y1 = (b.y.value - ref.y.value) / 1000.0;
        const double c = x0 * y1 - x1 * y0;
        r.a += c / 2.0;
        r.sx += (x0 + x1) * c / 6.0;
        r.sy += (y0 + y1) * c / 6.0;
        r.sxx += (x0 * x0 + x0 * x1 + x1 * x1) * c / 12.0;
        r.syy += (y0 * y0 + y0 * y1 + y1 * y1) * c / 12.0;
        r.sxy += (x0 * y1 + 2.0 * x0 * y0 + 2.0 * x1 * y1 + x1 * y0) * c / 24.0;
    }
    // Orientation normalisée : un extérieur compte en plus, un trou en moins,
    // quel que soit le sens de parcours d'origine.
    const double sign = (r.a >= 0.0 ? 1.0 : -1.0) * (hole ? -1.0 : 1.0);
    m.a += sign * r.a;
    m.sx += sign * r.sx;
    m.sy += sign * r.sy;
    m.sxx += sign * r.sxx;
    m.syy += sign * r.syy;
    m.sxy += sign * r.sxy;
}

} // namespace

std::optional<PrincipalAxis> principal_axis(const std::vector<PathSet>& sets) {
    Vec2um ref{};
    bool hasRef = false;
    for (const auto& s : sets) {
        if (!s.outer.nodes.empty()) {
            ref = s.outer.nodes.front().pos;
            hasRef = true;
            break;
        }
    }
    if (!hasRef) {
        return std::nullopt;
    }
    Moments m;
    for (const auto& s : sets) {
        accumulate(s.outer, ref, false, m);
        for (const auto& h : s.holes) {
            accumulate(h, ref, true, m);
        }
    }
    if (m.a <= 1e-9) {
        return std::nullopt;
    }
    // Moments centrés (matrice de covariance de la surface).
    const double cx = m.sx / m.a;
    const double cy = m.sy / m.a;
    const double cxx = m.sxx - m.a * cx * cx;
    const double cyy = m.syy - m.a * cy * cy;
    const double cxy = m.sxy - m.a * cx * cy;
    const double mean = (cxx + cyy) / 2.0;
    const double dev = std::hypot((cxx - cyy) / 2.0, cxy);
    const double lmax = mean + dev;
    const double lmin = mean - dev;

    PrincipalAxis out;
    out.area_um2 = m.a * 1e6;
    out.anisotropy = lmin > 1e-12 ? lmax / lmin : std::numeric_limits<double>::infinity();
    double theta = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
    if (theta < 0.0) {
        theta += std::numbers::pi;
    }
    if (theta >= std::numbers::pi) {
        theta -= std::numbers::pi;
    }
    out.angle = Angle{theta};
    return out;
}

} // namespace openstitch::geometry
