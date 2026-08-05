// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/polyline.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::geometry {

namespace {

struct Pt {
    double x{0.0};
    double y{0.0};
};

Pt toPt(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}

Vec2um toUm(Pt p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

Pt add(Pt a, Pt b) { return {a.x + b.x, a.y + b.y}; }
Pt mid(Pt a, Pt b) { return {(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}; }

// Distance du point p à la droite (a,b).
double dist_to_line(Pt p, Pt a, Pt b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 == 0.0) {
        return std::hypot(p.x - a.x, p.y - a.y);
    }
    const double cross = std::abs((p.x - a.x) * dy - (p.y - a.y) * dx);
    return cross / std::sqrt(len2);
}

// Aplatissement adaptatif d'une Bézier cubique (De Casteljau), points ajoutés
// dans `out` SANS le premier (déjà présent). Profondeur bornée par sécurité.
void flatten_cubic(Pt p0, Pt p1, Pt p2, Pt p3, double tol, int depth, std::vector<Pt>& out) {
    if (depth <= 0 || (dist_to_line(p1, p0, p3) <= tol && dist_to_line(p2, p0, p3) <= tol)) {
        out.push_back(p3);
        return;
    }
    const Pt p01 = mid(p0, p1);
    const Pt p12 = mid(p1, p2);
    const Pt p23 = mid(p2, p3);
    const Pt p012 = mid(p01, p12);
    const Pt p123 = mid(p12, p23);
    const Pt p0123 = mid(p012, p123);
    flatten_cubic(p0, p01, p012, p0123, tol, depth - 1, out);
    flatten_cubic(p0123, p123, p23, p3, tol, depth - 1, out);
}

}  // namespace

Polyline flatten(const Path& path, Micrometers tolerance) {
    Polyline out;
    out.closed = path.closed;
    if (path.nodes.empty()) {
        return out;
    }
    const double tol = std::max(1.0, static_cast<double>(tolerance.value));

    std::vector<Pt> pts;
    pts.push_back(toPt(path.nodes.front().pos));

    const std::size_t n = path.nodes.size();
    const std::size_t edges = path.closed ? n : n - 1;
    for (std::size_t e = 0; e < edges; ++e) {
        const PathNode& a = path.nodes[e];
        const PathNode& b = path.nodes[(e + 1) % n];
        const Pt pa = toPt(a.pos);
        const Pt pb = toPt(b.pos);
        if (a.tan_out || b.tan_in) {
            const Pt c1 = a.tan_out ? add(pa, toPt(*a.tan_out)) : pa;
            const Pt c2 = b.tan_in ? add(pb, toPt(*b.tan_in)) : pb;
            flatten_cubic(pa, c1, c2, pb, tol, 16, pts);
        } else {
            pts.push_back(pb);
        }
    }

    // Fusion des points consécutifs identiques.
    out.points.reserve(pts.size());
    for (const Pt& p : pts) {
        const Vec2um v = toUm(p);
        if (out.points.empty() || out.points.back() != v) {
            out.points.push_back(v);
        }
    }
    // Chemin fermé : le dernier point peut égaler le premier après aplatissement
    // (on ne garde pas ce doublon ; la fermeture est implicite).
    if (out.closed && out.points.size() > 1 && out.points.front() == out.points.back()) {
        out.points.pop_back();
    }
    return out;
}

std::vector<double> cumulative_lengths(const std::vector<Vec2um>& points) {
    std::vector<double> cum(points.size(), 0.0);
    for (std::size_t i = 1; i < points.size(); ++i) {
        cum[i] = cum[i - 1] + length_um(points[i] - points[i - 1]);
    }
    return cum;
}

double polyline_length(const std::vector<Vec2um>& points) {
    if (points.size() < 2) {
        return 0.0;
    }
    return cumulative_lengths(points).back();
}

Vec2um point_at_length(const std::vector<Vec2um>& points, const std::vector<double>& cumulative,
                       double s) {
    if (points.empty()) {
        return {};
    }
    if (points.size() == 1 || s <= 0.0) {
        return points.front();
    }
    if (s >= cumulative.back()) {
        return points.back();
    }
    const auto it = std::upper_bound(cumulative.begin(), cumulative.end(), s);
    const std::size_t i = static_cast<std::size_t>(it - cumulative.begin());
    const double segLen = cumulative[i] - cumulative[i - 1];
    const double t = segLen > 0.0 ? (s - cumulative[i - 1]) / segLen : 0.0;
    const Pt a = toPt(points[i - 1]);
    const Pt b = toPt(points[i]);
    return toUm({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)});
}

Vec2um tangent_at_length(const std::vector<Vec2um>& points, const std::vector<double>& cumulative,
                         double s) {
    if (points.size() < 2) {
        return Vec2um{Micrometers{1}, Micrometers{0}};
    }
    const double clamped = std::clamp(s, 0.0, cumulative.back());
    const auto it = std::upper_bound(cumulative.begin(), cumulative.end(), clamped);
    std::size_t i = static_cast<std::size_t>(it - cumulative.begin());
    i = std::clamp<std::size_t>(i, 1, points.size() - 1);
    return points[i] - points[i - 1];
}

std::vector<Vec2um> resample_run(const std::vector<Vec2um>& points, Micrometers target) {
    std::vector<Vec2um> out;
    if (points.size() < 2 || target.value <= 0) {
        return points;
    }
    const auto cum = cumulative_lengths(points);
    const double L = cum.back();
    if (L == 0.0) {
        out.push_back(points.front());
        return out;
    }
    // Division en n parts ÉGALES avec n = ceil(L / cible) : chaque point reste
    // <= cible (la cible est traitée comme une longueur maximale) et il n'y a
    // jamais de segment résiduel minuscule. (Écart assumé au §6.2 qui suggère
    // round ; ceil garantit en plus le respect strict de la longueur maximale.)
    const double t = static_cast<double>(target.value);
    const int n = std::max(1, static_cast<int>(std::ceil(L / t - 1e-6)));
    out.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const double s = L * static_cast<double>(i) / static_cast<double>(n);
        out.push_back(point_at_length(points, cum, s));
    }
    // Garantit des extrémités exactes malgré l'arrondi µm.
    out.front() = points.front();
    out.back() = points.back();
    return out;
}

namespace {

double cross2(Vec2um o, Vec2um a, Vec2um b) {
    const double ax = static_cast<double>(a.x.value - o.x.value);
    const double ay = static_cast<double>(a.y.value - o.y.value);
    const double bx = static_cast<double>(b.x.value - o.x.value);
    const double by = static_cast<double>(b.y.value - o.y.value);
    return ax * by - ay * bx;
}

bool on_segment(Vec2um p, Vec2um a, Vec2um b) {
    return std::min(a.x.value, b.x.value) <= p.x.value && p.x.value <= std::max(a.x.value, b.x.value) &&
           std::min(a.y.value, b.y.value) <= p.y.value && p.y.value <= std::max(a.y.value, b.y.value);
}

bool segments_intersect(Vec2um p1, Vec2um p2, Vec2um p3, Vec2um p4) {
    const double d1 = cross2(p3, p4, p1);
    const double d2 = cross2(p3, p4, p2);
    const double d3 = cross2(p1, p2, p3);
    const double d4 = cross2(p1, p2, p4);
    if (((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0)) &&
        ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))) {
        return true;
    }
    if (d1 == 0.0 && on_segment(p1, p3, p4)) return true;
    if (d2 == 0.0 && on_segment(p2, p3, p4)) return true;
    if (d3 == 0.0 && on_segment(p3, p1, p2)) return true;
    if (d4 == 0.0 && on_segment(p4, p1, p2)) return true;
    return false;
}

}  // namespace

bool polylines_cross(const std::vector<Vec2um>& a, const std::vector<Vec2um>& b) {
    if (a.size() < 2 || b.size() < 2) {
        return false;
    }
    for (std::size_t i = 0; i + 1 < a.size(); ++i) {
        for (std::size_t j = 0; j + 1 < b.size(); ++j) {
            // Un sommet exactement partagé (bouts de rails convergents) n'est
            // jamais compté comme un croisement transversal.
            if (a[i] == b[j] || a[i] == b[j + 1] || a[i + 1] == b[j] || a[i + 1] == b[j + 1]) {
                continue;
            }
            if (segments_intersect(a[i], a[i + 1], b[j], b[j + 1])) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace openstitch::geometry
