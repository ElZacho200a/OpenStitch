// SPDX-License-Identifier: Apache-2.0
#include "openstitch/geometry/simplify.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::geometry {

namespace {

// Distance perpendiculaire du point p au segment [a, b], en µm.
double perpendicular_distance(Vec2um p, Vec2um a, Vec2um b) {
    const double abx = static_cast<double>(b.x.value - a.x.value);
    const double aby = static_cast<double>(b.y.value - a.y.value);
    const double apx = static_cast<double>(p.x.value - a.x.value);
    const double apy = static_cast<double>(p.y.value - a.y.value);
    const double lenSq = abx * abx + aby * aby;
    if (lenSq == 0.0) {
        return std::sqrt(apx * apx + apy * apy);
    }
    const double t = std::clamp((apx * abx + apy * aby) / lenSq, 0.0, 1.0);
    const double dx = apx - t * abx;
    const double dy = apy - t * aby;
    return std::sqrt(dx * dx + dy * dy);
}

void douglas_peucker(const std::vector<PathNode>& nodes, std::size_t first, std::size_t last,
                     double tolerance, std::vector<bool>& keep) {
    if (last <= first + 1) {
        return;
    }
    double maxDist = -1.0;
    std::size_t maxIdx = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        const double d = perpendicular_distance(nodes[i].pos, nodes[first].pos, nodes[last].pos);
        if (d > maxDist) {
            maxDist = d;
            maxIdx = i;
        }
    }
    if (maxDist > tolerance) {
        keep[maxIdx] = true;
        douglas_peucker(nodes, first, maxIdx, tolerance, keep);
        douglas_peucker(nodes, maxIdx, last, tolerance, keep);
    }
}

}  // namespace

double signed_area_um2(const Path& path) {
    double area = 0.0;
    const auto& n = path.nodes;
    for (std::size_t i = 0; i < n.size(); ++i) {
        const auto& a = n[i].pos;
        const auto& b = n[(i + 1) % n.size()].pos;
        area += static_cast<double>(a.x.value) * static_cast<double>(b.y.value) -
                static_cast<double>(b.x.value) * static_cast<double>(a.y.value);
    }
    return area / 2.0;
}

Path simplify(const Path& path, Micrometers tolerance) {
    const std::size_t minNodes = path.closed ? 3 : 2;
    if (path.nodes.size() <= minNodes || tolerance.value <= 0) {
        return path;
    }

    const auto& nodes = path.nodes;
    std::vector<bool> keep(nodes.size(), false);
    keep.front() = true;
    keep.back() = true;

    if (path.closed) {
        // Coupe au point le plus éloigné du premier nœud : deux moitiés ouvertes.
        std::size_t split = nodes.size() / 2;
        double maxDist = -1.0;
        for (std::size_t i = 1; i < nodes.size(); ++i) {
            const double d = length_um(nodes[i].pos - nodes[0].pos);
            if (d > maxDist) {
                maxDist = d;
                split = i;
            }
        }
        keep[split] = true;
        douglas_peucker(nodes, 0, split, static_cast<double>(tolerance.value), keep);
        douglas_peucker(nodes, split, nodes.size() - 1, static_cast<double>(tolerance.value), keep);
    } else {
        douglas_peucker(nodes, 0, nodes.size() - 1, static_cast<double>(tolerance.value), keep);
    }

    Path result;
    result.closed = path.closed;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (keep[i]) {
            result.nodes.push_back(nodes[i]);
        }
    }
    // Fermé : si le dernier nœud duplique le premier, on le retire.
    if (result.closed && result.nodes.size() > 1 &&
        result.nodes.front().pos == result.nodes.back().pos) {
        result.nodes.pop_back();
    }
    // Fermé : les deux nœuds d'ancrage du découpage sont toujours conservés
    // par Douglas-Peucker même s'ils sont colinéaires — passe de nettoyage.
    if (result.closed) {
        bool removed = true;
        while (removed && result.nodes.size() > 3) {
            removed = false;
            for (std::size_t i = 0; i < result.nodes.size(); ++i) {
                const std::size_t n = result.nodes.size();
                const Vec2um prev = result.nodes[(i + n - 1) % n].pos;
                const Vec2um next = result.nodes[(i + 1) % n].pos;
                if (perpendicular_distance(result.nodes[i].pos, prev, next) <=
                    static_cast<double>(tolerance.value)) {
                    result.nodes.erase(result.nodes.begin() + static_cast<std::ptrdiff_t>(i));
                    removed = true;
                    break;
                }
            }
        }
    }
    return result;
}

}  // namespace openstitch::geometry
