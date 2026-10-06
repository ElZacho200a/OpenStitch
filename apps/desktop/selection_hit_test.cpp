// SPDX-License-Identifier: Apache-2.0
#include "selection_hit_test.hpp"

#include <algorithm>

#include "interaction_map.hpp"
#include "openstitch/core/units.hpp"

namespace openstitch::desktop {

QPainterPath objectScenePath(const document::VectorObject& object) {
    QPainterPath painterPath;
    painterPath.setFillRule(Qt::OddEvenFill);
    // Scène en mm, Y vers le bas : inversion du repère physique.
    const auto toScene = [](Vec2um p) {
        return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
    };
    const auto addPath = [&](const geometry::Path& path) {
        const std::size_t n = path.nodes.size();
        if (n == 0) {
            return;
        }
        painterPath.moveTo(toScene(path.nodes[0].pos));
        const std::size_t edges = path.closed ? n : n - 1;
        for (std::size_t e = 0; e < edges; ++e) {
            const auto& a = path.nodes[e];
            const auto& b = path.nodes[(e + 1) % n];
            // Segment courbe (au moins une tangente) -> cubique de Bézier
            // réelle, jamais une approximation par segments droits : c'est ce
            // même contour qui sert à l'affichage ET au hit-test des clics.
            if (a.tan_out || b.tan_in) {
                const QPointF c1 = a.tan_out ? toScene(a.pos + *a.tan_out) : toScene(a.pos);
                const QPointF c2 = b.tan_in ? toScene(b.pos + *b.tan_in) : toScene(b.pos);
                painterPath.cubicTo(c1, c2, toScene(b.pos));
            } else {
                painterPath.lineTo(toScene(b.pos));
            }
        }
        if (path.closed) {
            painterPath.closeSubpath();
        }
    };
    for (const auto& set : object.paths) {
        addPath(set.outer);
        for (const auto& hole : set.holes) {
            addPath(hole);
        }
    }
    return painterPath;
}

std::vector<ObjectId> objectsAtPointMm(const document::Project& project, QPointF posMm) {
    std::vector<ObjectId> hits;
    for (auto it = project.vector_objects.rbegin(); it != project.vector_objects.rend(); ++it) {
        if (it->visible && objectScenePath(*it).contains(posMm)) {
            hits.push_back(it->id);
        }
    }
    return hits;
}

std::vector<ObjectId> objectsInRectangleMm(const document::Project& project, const QRectF& rectMm,
                                           bool crossing) {
    std::vector<ObjectId> hits;
    for (const auto& object : project.vector_objects) {
        if (object.visible &&
            InteractionMap::rectSelects(rectMm, objectScenePath(object), crossing)) {
            hits.push_back(object.id);
        }
    }
    std::sort(hits.begin(), hits.end());
    return hits;
}

} // namespace openstitch::desktop
