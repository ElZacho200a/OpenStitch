// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/blanket_stitch.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::stitch_generation {

namespace {

constexpr Micrometers kFlattenTolerance{50};

struct Station {
    Vec2um pos;
    double nx; // normale gauche unitaire
    double ny;
};

Vec2um offset_point(Vec2um p, double nx, double ny, double distance) {
    return {Micrometers{static_cast<std::int32_t>(std::llround(p.x.value + nx * distance))},
            Micrometers{static_cast<std::int32_t>(std::llround(p.y.value + ny * distance))}};
}

void push_unique(std::vector<Vec2um>& out, Vec2um p) {
    if (out.empty() || out.back() != p) {
        out.push_back(p);
    }
}

} // namespace

std::vector<Vec2um> blanket_stitch_points(const geometry::Path& path,
                                          const document::BlanketStitchParams& params) {
    const geometry::Polyline flat = geometry::flatten(path, kFlattenTolerance);
    std::vector<Vec2um> pts = flat.points;
    const bool closed = path.closed && pts.size() >= 3;
    if (closed && pts.front() != pts.back()) {
        pts.push_back(pts.front());
    }
    if (pts.size() < 2) {
        return {};
    }
    const std::vector<double> cum = geometry::cumulative_lengths(pts);
    const double total = cum.back();
    if (total <= 0.0) {
        return {};
    }

    const double spacing = static_cast<double>(
        std::clamp(params.spacing, kBlanketMinSpacing, kBlanketMaxSpacing).value);
    const double width =
        static_cast<double>(std::clamp(params.width, kBlanketMinWidth, kBlanketMaxWidth).value);
    const long long steps = std::max<long long>(1, std::llround(total / spacing));
    const long long stations = closed ? steps : steps + 1;

    std::vector<Station> st;
    st.reserve(static_cast<std::size_t>(stations));
    for (long long i = 0; i < stations; ++i) {
        const double s = total * static_cast<double>(i) / static_cast<double>(steps);
        // Segment portant s (le suivant sur un sommet ; le dernier à la fin).
        std::size_t seg =
            static_cast<std::size_t>(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin());
        seg = seg == 0 ? 0 : seg - 1;
        seg = std::min(seg, pts.size() - 2);
        while (seg + 2 < pts.size() && cum[seg + 1] <= cum[seg]) {
            ++seg; // segment de longueur nulle (ne devrait pas arriver après fusion)
        }
        const double len = cum[seg + 1] - cum[seg];
        const double dx = static_cast<double>(pts[seg + 1].x.value - pts[seg].x.value);
        const double dy = static_cast<double>(pts[seg + 1].y.value - pts[seg].y.value);
        const double t = len > 0.0 ? std::clamp((s - cum[seg]) / len, 0.0, 1.0) : 0.0;
        const Vec2um pos{
            Micrometers{static_cast<std::int32_t>(std::llround(pts[seg].x.value + dx * t))},
            Micrometers{static_cast<std::int32_t>(std::llround(pts[seg].y.value + dy * t))}};
        const double norm = std::hypot(dx, dy);
        st.push_back({pos, norm > 0.0 ? -dy / norm : 0.0, norm > 0.0 ? dx / norm : 1.0});
    }

    using document::BlanketSide;
    const double half = width / 2.0;
    std::vector<Vec2um> out;
    out.reserve(st.size() * 3 + 1);
    for (std::size_t i = 0; i < st.size(); ++i) {
        const Station& a = st[i];
        const bool odd = (i % 2) != 0;
        if (params.style == document::BlanketStyle::Blanket) {
            double d = width;
            if (params.side == BlanketSide::Right) {
                d = -width;
            } else if (params.side == BlanketSide::Center) {
                d = odd ? -half : half;
            }
            push_unique(out, a.pos);
            push_unique(out, offset_point(a.pos, a.nx, a.ny, d));
            push_unique(out, a.pos);
        } else {
            double d = 0.0;
            switch (params.side) {
            case BlanketSide::Left:
                d = odd ? width : 0.0;
                break;
            case BlanketSide::Right:
                d = odd ? -width : 0.0;
                break;
            case BlanketSide::Center:
                d = odd ? -half : half;
                break;
            }
            push_unique(out, offset_point(a.pos, a.nx, a.ny, d));
        }
    }
    if (closed) {
        // Referme sur la première station (base pour le blanket, premier bord pour le zigzag).
        push_unique(out, out.front());
    }
    return out;
}

} // namespace openstitch::stitch_generation
