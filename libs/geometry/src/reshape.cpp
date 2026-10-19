// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/reshape.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/polyline.hpp"

namespace openstitch::geometry {

namespace {

struct Pt {
    double x{0.0};
    double y{0.0};
};

Pt toPt(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}

std::int32_t roundUm(double v) {
    return static_cast<std::int32_t>(std::lround(v));
}

Vec2um toUm(Pt p) {
    return Vec2um{Micrometers{roundUm(p.x)}, Micrometers{roundUm(p.y)}};
}

Pt operator+(Pt a, Pt b) {
    return {a.x + b.x, a.y + b.y};
}
Pt operator-(Pt a, Pt b) {
    return {a.x - b.x, a.y - b.y};
}
Pt operator*(Pt a, double s) {
    return {a.x * s, a.y * s};
}

// Points de contrôle absolus de la cubique du segment (une droite devient la cubique à
// poignées au tiers ; une tangente absente est confondue avec son extrémité).
struct Cubic {
    Pt p0, p1, p2, p3;
};

Cubic cubic_of(const PathNode& a, const PathNode& b) {
    Cubic c{toPt(a.pos), toPt(a.pos), toPt(b.pos), toPt(b.pos)};
    if (!a.tan_out && !b.tan_in) {
        c.p1 = c.p0 + (c.p3 - c.p0) * (1.0 / 3.0);
        c.p2 = c.p0 + (c.p3 - c.p0) * (2.0 / 3.0);
        return c;
    }
    if (a.tan_out) {
        c.p1 = c.p0 + toPt(*a.tan_out);
    }
    if (b.tan_in) {
        c.p2 = c.p3 + toPt(*b.tan_in);
    }
    return c;
}

Pt eval(const Cubic& c, double t) {
    const double u = 1.0 - t;
    return c.p0 * (u * u * u) + c.p1 * (3.0 * u * u * t) + c.p2 * (3.0 * u * t * t) +
           c.p3 * (t * t * t);
}

double dist2(Pt a, Pt b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
}

// Garde la tangente `opposite` colinéaire (sens inverse) à `fresh`, longueur conservée.
std::optional<Vec2um> mirror_tangent(const std::optional<Vec2um>& opposite, Pt fresh) {
    if (!opposite) {
        return opposite;
    }
    const double fl = std::hypot(fresh.x, fresh.y);
    if (fl < 1.0) {
        return opposite;
    }
    const double ol =
        std::hypot(static_cast<double>(opposite->x.value), static_cast<double>(opposite->y.value));
    return toUm(fresh * (-ol / fl));
}

int orient(Pt a, Pt b, Pt c) {
    const double v = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
}

// Croisement transversal strict (les contacts en bout / colinéaires ne comptent pas).
bool proper_cross(Pt a, Pt b, Pt c, Pt d) {
    const int o1 = orient(a, b, c);
    const int o2 = orient(a, b, d);
    const int o3 = orient(c, d, a);
    const int o4 = orient(c, d, b);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

bool bbox_disjoint(Pt a, Pt b, Pt c, Pt d) {
    return std::max(a.x, b.x) < std::min(c.x, d.x) || std::max(c.x, d.x) < std::min(a.x, b.x) ||
           std::max(a.y, b.y) < std::min(c.y, d.y) || std::max(c.y, d.y) < std::min(a.y, b.y);
}

} // namespace

std::optional<SegmentHit> nearest_segment(const Path& path, Vec2um p, double max_distance_um) {
    const std::size_t n = path.nodes.size();
    if (n < 2) {
        return std::nullopt;
    }
    constexpr int kSamples = 24;
    const std::size_t edges = path.closed ? n : n - 1;
    const Pt q = toPt(p);
    std::optional<SegmentHit> best;
    double bestD2 = max_distance_um * max_distance_um;
    for (std::size_t e = 0; e < edges; ++e) {
        const PathNode& a = path.nodes[e];
        const PathNode& b = path.nodes[(e + 1) % n];
        const Cubic c = cubic_of(a, b);
        // Échantillonnage grossier puis raffinement local (recherche ternaire).
        double bt = 0.0;
        double bd = dist2(eval(c, 0.0), q);
        for (int i = 1; i <= kSamples; ++i) {
            const double t = static_cast<double>(i) / kSamples;
            const double d = dist2(eval(c, t), q);
            if (d < bd) {
                bd = d;
                bt = t;
            }
        }
        double lo = std::max(0.0, bt - 1.0 / kSamples);
        double hi = std::min(1.0, bt + 1.0 / kSamples);
        for (int it = 0; it < 24; ++it) {
            const double m1 = lo + (hi - lo) / 3.0;
            const double m2 = hi - (hi - lo) / 3.0;
            if (dist2(eval(c, m1), q) < dist2(eval(c, m2), q)) {
                hi = m2;
            } else {
                lo = m1;
            }
        }
        const double t = 0.5 * (lo + hi);
        const double d2 = dist2(eval(c, t), q);
        if (d2 <= bestD2) {
            bestD2 = d2;
            best = SegmentHit{e, t, std::sqrt(d2), toUm(eval(c, t))};
        }
    }
    return best;
}

Path reshape_segment(const Path& path, std::size_t segment, double t, Vec2um delta) {
    const std::size_t n = path.nodes.size();
    if (n < 2 || segment >= (path.closed ? n : n - 1) || delta == Vec2um{}) {
        return path; // rien à remodeler (un segment droit ne devient pas une cubique pour rien)
    }
    const std::size_t ia = segment;
    const std::size_t ib = (segment + 1) % n;
    Path out = path;
    PathNode& a = out.nodes[ia];
    PathNode& b = out.nodes[ib];

    const Cubic c = cubic_of(path.nodes[ia], path.nodes[ib]);
    // Poids de Bernstein des deux poignées au paramètre de pondération (borné) : le point
    // saisi se déplace de `delta` quand t n'est pas clampé.
    const double tw = std::clamp(t, 0.15, 0.85);
    const double u = 1.0 - tw;
    const double b1 = 3.0 * u * u * tw;
    const double b2 = 3.0 * u * tw * tw;
    const double w1 = u;
    const double w2 = tw;
    const double norm = b1 * w1 + b2 * w2;
    const Pt d = toPt(delta);
    const Pt p1 = c.p1 + d * (w1 / norm);
    const Pt p2 = c.p2 + d * (w2 / norm);

    const Pt tanOut = p1 - c.p0;
    const Pt tanIn = p2 - c.p3;
    a.tan_out = toUm(tanOut);
    b.tan_in = toUm(tanIn);
    // Continuité aux nœuds lisses : la poignée de l'autre côté pivote (longueur conservée).
    if (a.type == NodeType::Smooth) {
        a.tan_in = mirror_tangent(a.tan_in, tanOut);
    }
    if (b.type == NodeType::Smooth) {
        b.tan_out = mirror_tangent(b.tan_out, tanIn);
    }
    return out;
}

bool path_self_intersects(const Path& path, Micrometers tolerance) {
    const Polyline flat = flatten(path, tolerance);
    const auto& pts = flat.points;
    const std::size_t m = pts.size();
    if (m < 4) {
        return false;
    }
    const std::size_t edges = path.closed ? m : m - 1;
    for (std::size_t i = 0; i < edges; ++i) {
        const Pt a = toPt(pts[i]);
        const Pt b = toPt(pts[(i + 1) % m]);
        for (std::size_t j = i + 2; j < edges; ++j) {
            if (path.closed && i == 0 && j == edges - 1) {
                continue; // arêtes adjacentes par la fermeture
            }
            const Pt c = toPt(pts[j]);
            const Pt e = toPt(pts[(j + 1) % m]);
            if (!bbox_disjoint(a, b, c, e) && proper_cross(a, b, c, e)) {
                return true;
            }
        }
    }
    return false;
}

bool path_set_has_crossings(const PathSet& set, Micrometers tolerance) {
    std::vector<const Path*> all;
    all.push_back(&set.outer);
    for (const Path& h : set.holes) {
        all.push_back(&h);
    }
    std::vector<Polyline> flats;
    flats.reserve(all.size());
    for (const Path* p : all) {
        if (path_self_intersects(*p, tolerance)) {
            return true;
        }
        flats.push_back(flatten(*p, tolerance));
    }
    for (std::size_t i = 0; i < flats.size(); ++i) {
        for (std::size_t j = i + 1; j < flats.size(); ++j) {
            if (polylines_cross(flats[i].points, flats[j].points)) {
                return true;
            }
        }
    }
    return false;
}

std::optional<PathSet> reshape_path_set_segment(const PathSet& set, std::size_t path_index,
                                                std::size_t segment, double t, Vec2um delta,
                                                Micrometers tolerance) {
    if (path_index > set.holes.size()) {
        return std::nullopt;
    }
    const Path& source = path_index == 0 ? set.outer : set.holes[path_index - 1];
    const std::size_t n = source.nodes.size();
    if (n < 2 || segment >= (source.closed ? n : n - 1)) {
        return std::nullopt;
    }
    PathSet out = set;
    (path_index == 0 ? out.outer : out.holes[path_index - 1]) =
        reshape_segment(source, segment, t, delta);
    if (path_set_has_crossings(out, tolerance) && !path_set_has_crossings(set, tolerance)) {
        return std::nullopt;
    }
    return out;
}

} // namespace openstitch::geometry
