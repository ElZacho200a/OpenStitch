// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/brush.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/geometry/reshape.hpp"

namespace openstitch::geometry {

namespace {

struct Pt {
    double x;
    double y;
};

Pt toPt(Vec2um v) {
    return {static_cast<double>(v.x.value), static_cast<double>(v.y.value)};
}

Vec2um toUm(Pt p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y))}};
}

double dist(Pt a, Pt b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

bool straight(const PathNode& n) {
    return !n.tan_in && !n.tan_out;
}

// Distance du point p à la droite (a, b).
double line_distance(Pt p, Pt a, Pt b) {
    const double len = dist(a, b);
    if (len < 1e-9) {
        return dist(p, a);
    }
    return std::abs((p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x)) / len;
}

// Subdivise les segments longs de la zone d'influence (milieu exact par De Casteljau).
Path subdivide_zone(Path path, Pt center, double radius, double maxSegment, std::size_t maxNew) {
    std::size_t added = 0;
    bool changed = true;
    while (changed && added < maxNew) {
        changed = false;
        const std::size_t n = path.nodes.size();
        const std::size_t edges = path.closed ? n : n - 1;
        for (std::size_t e = 0; e < edges && added < maxNew; ++e) {
            const Pt a = toPt(path.nodes[e].pos);
            const Pt b = toPt(path.nodes[(e + 1) % n].pos);
            const Pt mid{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
            const bool inZone =
                dist(a, center) < radius || dist(b, center) < radius || dist(mid, center) < radius;
            if (inZone && dist(a, b) > maxSegment) {
                path = insert_node_on_segment(path, e, 0.5);
                ++added;
                changed = true;
                break; // indices décalés : on recommence le balayage (déterministe)
            }
        }
    }
    return path;
}

} // namespace

double brush_weight(double distance_um, double radius_um, double strength) {
    if (radius_um <= 0.0 || distance_um >= radius_um) {
        return 0.0;
    }
    const double u = 1.0 - distance_um / radius_um;
    return std::clamp(strength, 0.0, 1.0) * u * u * (3.0 - 2.0 * u);
}

Path apply_brush_dab(const Path& path, const BrushDab& dab, std::size_t max_new_nodes,
                     double merge_tolerance_um) {
    if (path.nodes.size() < 2 || dab.radius_um <= 0.0 || dab.strength <= 0.0) {
        return path;
    }
    const Pt center = toPt(dab.center);
    const double maxSegment = std::max(dab.radius_um / 3.0, 300.0);
    Path out = subdivide_zone(path, center, dab.radius_um, maxSegment, max_new_nodes);

    const std::size_t n = out.nodes.size();
    const Path src = out; // positions de référence (Smooth lit les voisins d'origine)
    for (std::size_t i = 0; i < n; ++i) {
        const bool endpoint = !out.closed && (i == 0 || i + 1 == n);
        if (endpoint) {
            continue;
        }
        const Pt p = toPt(src.nodes[i].pos);
        const double w = brush_weight(dist(p, center), dab.radius_um, dab.strength);
        if (w <= 0.0) {
            continue;
        }
        if (dab.mode == BrushMode::Push) {
            const Pt d = toPt(dab.delta);
            out.nodes[i].pos = toUm({p.x + d.x * w, p.y + d.y * w});
        } else {
            const Pt a = toPt(src.nodes[(i + n - 1) % n].pos);
            const Pt b = toPt(src.nodes[(i + 1) % n].pos);
            const Pt target{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
            out.nodes[i].pos = toUm({p.x + (target.x - p.x) * w, p.y + (target.y - p.y) * w});
        }
    }

    // Retire les nœuds droits redondants de la zone (un nœud à la fois, déterministe).
    const std::size_t minNodes = out.closed ? 3 : 2;
    for (std::size_t i = 0; i < out.nodes.size() && out.nodes.size() > minNodes;) {
        const std::size_t m = out.nodes.size();
        const bool endpoint = !out.closed && (i == 0 || i + 1 == m);
        const PathNode& prev = out.nodes[(i + m - 1) % m];
        const PathNode& next = out.nodes[(i + 1) % m];
        if (!endpoint && straight(out.nodes[i]) && !prev.tan_out && !next.tan_in &&
            dist(toPt(out.nodes[i].pos), center) < dab.radius_um * 1.5 &&
            line_distance(toPt(out.nodes[i].pos), toPt(prev.pos), toPt(next.pos)) <=
                merge_tolerance_um) {
            out.nodes.erase(out.nodes.begin() + static_cast<std::ptrdiff_t>(i));
            continue; // même indice : le suivant a glissé
        }
        ++i;
    }
    return out;
}

PathSet apply_brush_dab(const PathSet& set, const BrushDab& dab, std::size_t max_new_nodes,
                        double merge_tolerance_um) {
    PathSet out;
    out.outer = apply_brush_dab(set.outer, dab, max_new_nodes, merge_tolerance_um);
    out.holes.reserve(set.holes.size());
    for (const Path& hole : set.holes) {
        out.holes.push_back(apply_brush_dab(hole, dab, max_new_nodes, merge_tolerance_um));
    }
    return out;
}

bool brush_result_is_valid(const PathSet& before, const PathSet& after, Micrometers tolerance) {
    return !path_set_has_crossings(after, tolerance) || path_set_has_crossings(before, tolerance);
}

} // namespace openstitch::geometry
