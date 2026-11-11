// SPDX-License-Identifier: Apache-2.0
#include "openstitch/auto_satin/guided_satin.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "chord.hpp"
#include "coverage.hpp"
#include "geometry_detail.hpp"
#include "openstitch/geometry/moments.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::auto_satin {
namespace {

using detail::P2;
using Path = std::vector<P2>;

P2 point(Vec2um p) {
    return {static_cast<double>(p.x.value), static_cast<double>(p.y.value)};
}

Vec2um to_um(P2 p) {
    return {{static_cast<std::int32_t>(std::lround(p.x))},
            {static_cast<std::int32_t>(std::lround(p.y))}};
}

double cross(P2 a, P2 b) {
    return a.x * b.y - a.y * b.x;
}

bool crosses(P2 a, P2 b, P2 c, P2 d) {
    // Proper intersection only: shared boundary endpoints are not crossings.
    const double abC = cross(b - a, c - a), abD = cross(b - a, d - a);
    const double cdA = cross(d - c, a - c), cdB = cross(d - c, b - c);
    return ((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) &&
           ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0));
}

Path clean(const std::vector<Vec2um>& source) {
    Path p;
    for (const Vec2um v : source) {
        const P2 next = point(v);
        if (p.empty() || detail::norm(next - p.back()) > 1.0) {
            p.push_back(next);
        }
    }
    return p;
}

Path automatic_axis(const geometry::PathSet& region, const std::vector<detail::Poly>& polys) {
    if (polys.empty() || polys.front().size() < 3) {
        return {};
    }
    double angle = 0.0;
    if (const auto principal = geometry::principal_axis({region})) {
        angle = principal->angle.radians;
    }
    const P2 u{std::cos(angle), std::sin(angle)};
    P2 centre{};
    for (const auto p : polys.front()) {
        centre = centre + p;
    }
    centre = centre * (1.0 / static_cast<double>(polys.front().size()));
    double lo = std::numeric_limits<double>::max();
    double hi = -lo;
    for (const auto p : polys.front()) {
        const double s = detail::dot(p - centre, u);
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    return hi - lo >= 1.0 ? Path{centre + u * lo, centre + u * hi} : Path{};
}

struct Station {
    P2 pos{};
    P2 tangent{};
};

std::vector<Station> stations(const Path& path, double spacing) {
    std::vector<double> lengths{0.0};
    for (std::size_t i = 1; i < path.size(); ++i) {
        lengths.push_back(lengths.back() + detail::norm(path[i] - path[i - 1]));
    }
    if (lengths.size() < 2 || lengths.back() < spacing * 0.5) {
        return {};
    }
    const double total = lengths.back();
    // Hard cap protects interactive editing from huge/repeated guide paths.
    const std::size_t count = std::min<std::size_t>(
        100000, std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(total / spacing))));
    std::vector<Station> out;
    out.reserve(count);
    std::size_t segment = 1;
    for (std::size_t i = 0; i < count; ++i) {
        const double s = (static_cast<double>(i) + 0.5) * total / count;
        while (segment + 1 < lengths.size() && lengths[segment] < s) {
            ++segment;
        }
        const double span = lengths[segment] - lengths[segment - 1];
        if (span <= 1e-9) {
            continue;
        }
        const double t = std::clamp((s - lengths[segment - 1]) / span, 0.0, 1.0);
        const P2 tangent = (path[segment] - path[segment - 1]) * (1.0 / span);
        // A local chord is normal to the user-controlled centreline.
        out.push_back({path[segment - 1] + (path[segment] - path[segment - 1]) * t, tangent});
    }
    return out;
}

} // namespace

Result<SkeletonSatinResult> generate_guided_satin(const geometry::PathSet& curved,
                                                  const SkeletonSatinParameters& params) {
    const geometry::PathSet region = geometry::flatten_to_polygon(curved, Micrometers{50});
    const auto polygons = detail::region_polys(region);
    SkeletonSatinResult result;
    auto& diagnostics = result.diagnostics;
    if (polygons.empty()) {
        diagnostics.messages.push_back("satin guidé : contour vide");
        return result;
    }

    std::vector<Path> guidePaths;
    for (const auto& g : params.guides) {
        const Path p = clean(g.path);
        if (p.size() >= 2) {
            guidePaths.push_back(p);
        } else {
            ++diagnostics.orphan_guides;
        }
    }
    if (guidePaths.empty()) {
        guidePaths.push_back(automatic_axis(region, polygons));
        diagnostics.messages.push_back("satin guidé : axe initial automatique ; tracez un guide "
                                       "courbe pour piloter les points");
    }

    const double spacing = std::max(50.0, static_cast<double>(params.spacing.value));
    const double minWidth = std::max(0.0, static_cast<double>(params.min_thread_length.value));
    int singles = 0;
    int intersections = 0;

    for (const auto& path : guidePaths) {
        SkeletonSatinColumn col;
        std::vector<Vec2um> axis;
        std::vector<std::pair<P2, P2>> recent;
        const auto finish = [&]() {
            if (col.crossings.size() >= 2) {
                result.columns.push_back(std::move(col));
                result.axes.push_back(std::move(axis));
            } else if (!col.crossings.empty()) {
                ++singles;
            }
            col = {};
            axis.clear();
            recent.clear();
        };

        for (const Station& s : stations(path, spacing)) {
            if (!detail::in_region(polygons, s.pos)) {
                ++diagnostics.outside_samples;
                finish();
                continue;
            }
            const P2 normal{-s.tangent.y, s.tangent.x};
            // Always select the interval containing the guide, never bridge a hole
            // or an adjacent lobe with a different interval on the same line.
            const auto chord = detail::chord_through(polygons, s.pos, normal, 0.0);
            if (!chord || chord->length() < minWidth) {
                ++diagnostics.too_short;
                finish();
                continue;
            }
            const P2 a = s.pos + normal * chord->t_lo;
            const P2 b = s.pos + normal * chord->t_hi;
            bool collision = false;
            // Check several prior stations, not just the immediately previous
            // one: a sharply turning path can fold back over an older chord.
            for (const auto& old : recent) {
                if (crosses(old.first, old.second, a, b)) {
                    collision = true;
                    break;
                }
            }
            if (collision) {
                ++intersections;
                finish();
                // The colliding station is discarded to preserve a non-crossing column.
                continue;
            }
            col.crossings.push_back({to_um(a), to_um(b)});
            axis.push_back(to_um(s.pos));
            recent.push_back({a, b});
            if (recent.size() > 12) {
                recent.erase(recent.begin());
            }
        }
        finish();
    }

    diagnostics.pieces = static_cast<int>(result.columns.size());
    if (singles) {
        diagnostics.messages.push_back("satin guidé : " + std::to_string(singles) +
                                       " bande(s) isolée(s) ignorée(s)");
    }
    if (intersections) {
        diagnostics.messages.push_back("satin guidé : " + std::to_string(intersections) +
                                       " traversée(s) croisées rejetée(s) ; adoucissez le guide");
    }
    if (result.columns.empty()) {
        diagnostics.messages.push_back("satin guidé : aucune colonne continue ; vérifiez le tracé");
    }
    if (params.measure_coverage) {
        detail::measure_coverage(region, result);
    }
    return result;
}

} // namespace openstitch::auto_satin
