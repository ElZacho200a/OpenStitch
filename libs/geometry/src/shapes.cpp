// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/shapes.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "openstitch/geometry/primitives.hpp"

namespace openstitch::geometry {

namespace {

Micrometers um(double v) {
    return Micrometers{static_cast<std::int32_t>(std::lround(v))};
}

Vec2um vec(double x, double y) {
    return Vec2um{um(x), um(y)};
}

PathNode corner(Vec2um p) {
    return PathNode{p, NodeType::Corner, std::nullopt, std::nullopt};
}

PathNode smooth(Vec2um p, std::optional<Vec2um> tan_in, std::optional<Vec2um> tan_out) {
    return PathNode{p, NodeType::Smooth, tan_in, tan_out};
}

double d(Micrometers v) {
    return static_cast<double>(v.value);
}

} // namespace

Path star_path(Vec2um center, Micrometers outer_radius, Micrometers inner_radius, int points,
               double start_angle) {
    if (points < 3 || outer_radius.value <= 0 || inner_radius.value <= 0 ||
        inner_radius.value >= outer_radius.value) {
        return Path{};
    }
    Path path;
    path.closed = true;
    path.nodes.reserve(static_cast<std::size_t>(points) * 2);
    const double cx = d(center.x);
    const double cy = d(center.y);
    for (int i = 0; i < points * 2; ++i) {
        const double r = (i % 2 == 0) ? d(outer_radius) : d(inner_radius);
        const double a = start_angle + i * std::numbers::pi / points;
        path.nodes.push_back(corner(vec(cx + r * std::cos(a), cy + r * std::sin(a))));
    }
    return path;
}

Path heart_path(Vec2um corner1, Vec2um corner2) {
    const double left = std::min(d(corner1.x), d(corner2.x));
    const double bottom = std::min(d(corner1.y), d(corner2.y));
    const double w = std::abs(d(corner1.x) - d(corner2.x));
    const double h = std::abs(d(corner1.y) - d(corner2.y));
    if (w <= 0.0 || h <= 0.0) {
        return Path{};
    }
    // Coordonnées normalisées (0..1, Y vers le haut) mises à l'échelle du cadre.
    const auto at = [&](double ux, double uy) { return vec(left + ux * w, bottom + uy * h); };
    const auto rel = [&](double ux, double uy) { return vec(ux * w, uy * h); };

    Path path;
    path.closed = true;
    // Sens antihoraire : pointe -> flanc droit -> lobe droit -> creux -> lobe
    // gauche -> flanc gauche. Symétrique par construction.
    path.nodes = {
        PathNode{at(0.5, 0.0), NodeType::Corner, rel(-0.30, 0.22), rel(0.30, 0.22)},
        smooth(at(1.0, 0.68), rel(0.0, -0.30), rel(0.0, 0.20)),
        smooth(at(0.75, 1.0), rel(0.15, 0.0), rel(-0.12, 0.0)),
        PathNode{at(0.5, 0.72), NodeType::Corner, rel(0.08, 0.22), rel(-0.08, 0.22)},
        smooth(at(0.25, 1.0), rel(0.12, 0.0), rel(-0.15, 0.0)),
        smooth(at(0.0, 0.68), rel(0.0, 0.20), rel(0.0, -0.30)),
    };
    return path;
}

Path arrow_path(Vec2um tail, Vec2um tip, Micrometers shaft_width, Micrometers head_width,
                Micrometers head_length) {
    const double dx = d(tip.x) - d(tail.x);
    const double dy = d(tip.y) - d(tail.y);
    const double len = std::hypot(dx, dy);
    if (len <= 0.0 || shaft_width.value <= 0 || head_width.value <= 0 || head_length.value <= 0) {
        return Path{};
    }
    const double ux = dx / len;
    const double uy = dy / len;
    const double nx = -uy; // normale gauche
    const double ny = ux;
    const double hw = std::max(d(head_width), d(shaft_width)) / 2.0;
    const double sw = d(shaft_width) / 2.0;
    const double base = len - std::min(d(head_length), len); // abscisse de la base de tête

    const auto at = [&](double along, double side) {
        return corner(vec(d(tail.x) + ux * along + nx * side, d(tail.y) + uy * along + ny * side));
    };
    Path path;
    path.closed = true;
    path.nodes = {at(0.0, -sw), at(base, -sw), at(base, -hw), at(len, 0.0),
                  at(base, hw), at(base, sw),  at(0.0, sw)};
    return path;
}

Path rounded_rectangle_path(Vec2um corner1, Vec2um corner2, Micrometers radius) {
    const double left = std::min(d(corner1.x), d(corner2.x));
    const double right = std::max(d(corner1.x), d(corner2.x));
    const double bottom = std::min(d(corner1.y), d(corner2.y));
    const double top = std::max(d(corner1.y), d(corner2.y));
    if (right - left <= 0.0 || top - bottom <= 0.0) {
        return Path{};
    }
    const double r = std::min(d(radius), std::min(right - left, top - bottom) / 2.0);
    if (r <= 0.0) {
        return rectangle_path(corner1, corner2);
    }
    constexpr double kKappa = 0.5522847498307936;
    const double k = kKappa * r;
    Path path;
    path.closed = true;
    // Chaque coin : nœud de départ d'arc (poignée sortante) puis nœud d'arrivée
    // (poignée entrante) ; les côtés droits relient l'arrivée au départ suivant.
    path.nodes = {
        smooth(vec(right - r, bottom), std::nullopt, vec(k, 0)),
        smooth(vec(right, bottom + r), vec(0, -k), std::nullopt),
        smooth(vec(right, top - r), std::nullopt, vec(0, k)),
        smooth(vec(right - r, top), vec(k, 0), std::nullopt),
        smooth(vec(left + r, top), std::nullopt, vec(-k, 0)),
        smooth(vec(left, top - r), vec(0, k), std::nullopt),
        smooth(vec(left, bottom + r), std::nullopt, vec(0, -k)),
        smooth(vec(left + r, bottom), vec(-k, 0), std::nullopt),
    };
    return path;
}

Path arc_path(Vec2um center, Micrometers radius, double start_angle, double sweep_angle) {
    const double sweep = std::clamp(sweep_angle, -2.0 * std::numbers::pi, 2.0 * std::numbers::pi);
    if (radius.value <= 0 || std::abs(sweep) < 1e-9) {
        return Path{};
    }
    const double r = d(radius);
    const int segments =
        std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 2.0) - 1e-9)));
    const double step = sweep / segments;
    const double handle = 4.0 / 3.0 * std::tan(step / 4.0) * r; // signé avec step
    const double cx = d(center.x);
    const double cy = d(center.y);

    Path path;
    path.closed = false;
    path.nodes.reserve(static_cast<std::size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
        const double a = start_angle + i * step;
        const Vec2um pos = vec(cx + r * std::cos(a), cy + r * std::sin(a));
        // Dérivée unitaire (sens du balayage) = (-sin a, cos a) * signe ; `handle`
        // porte déjà le signe via tan(step/4).
        const Vec2um fwd = vec(-std::sin(a) * handle, std::cos(a) * handle);
        const Vec2um back = vec(std::sin(a) * handle, -std::cos(a) * handle);
        PathNode node = corner(pos);
        if (i > 0 && i < segments) {
            node.type = NodeType::Smooth;
        }
        if (i < segments) {
            node.tan_out = fwd;
        }
        if (i > 0) {
            node.tan_in = back;
        }
        path.nodes.push_back(node);
    }
    return path;
}

Path spiral_path(Vec2um center, Micrometers start_radius, Micrometers end_radius, double turns,
                 double start_angle) {
    if (std::abs(turns) < 1e-9 || start_radius.value < 0 || end_radius.value < 0 ||
        (start_radius.value == 0 && end_radius.value == 0)) {
        return Path{};
    }
    constexpr int kSamplesPerTurn = 12;
    const int n = std::max(2, static_cast<int>(std::ceil(std::abs(turns) * kSamplesPerTurn)));
    std::vector<Vec2um> pts;
    pts.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        const double r = d(start_radius) + (d(end_radius) - d(start_radius)) * t;
        const double a = start_angle + turns * 2.0 * std::numbers::pi * t;
        pts.push_back(vec(d(center.x) + r * std::cos(a), d(center.y) + r * std::sin(a)));
    }
    return smooth_open_path(pts);
}

} // namespace openstitch::geometry
