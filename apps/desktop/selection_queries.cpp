// SPDX-License-Identifier: Apache-2.0
#include "selection_queries.hpp"

#include <algorithm>
#include <cstdint>

namespace openstitch::desktop {

namespace {

using document::EmbroideryObject;
using document::Project;
using document::VectorObject;

[[nodiscard]] bool ok(const ShapePredicate& selectable, const VectorObject& shape) {
    return !selectable || selectable(shape);
}

// Formes vérifiant `match`, dans l'ordre du document, `active` rejetée du corps puis remise en
// dernier : elle reste la forme principale.
template <typename Match>
[[nodiscard]] std::vector<ObjectId> collect_with_active(const Project& project, ObjectId active,
                                                        const ShapePredicate& selectable,
                                                        const Match& match) {
    std::vector<ObjectId> ids;
    for (const auto& shape : project.vector_objects) {
        if (shape.id != active && ok(selectable, shape) && match(shape)) {
            ids.push_back(shape.id);
        }
    }
    if (project.findObject(active) != nullptr) {
        ids.push_back(active);
    }
    return ids;
}

[[nodiscard]] bool boxes_touch(const ShapeBox& a, const ShapeBox& b, std::int64_t margin) {
    return a.valid && b.valid &&
           static_cast<std::int64_t>(a.min.x.value) - margin <= b.max.x.value &&
           static_cast<std::int64_t>(a.max.x.value) + margin >= b.min.x.value &&
           static_cast<std::int64_t>(a.min.y.value) - margin <= b.max.y.value &&
           static_cast<std::int64_t>(a.max.y.value) + margin >= b.min.y.value;
}

} // namespace

ShapeBox shape_box(const VectorObject& shape) {
    ShapeBox box;
    for (const auto& set : shape.paths) {
        for (const auto& node : set.outer.nodes) {
            if (!box.valid) {
                box = {true, node.pos, node.pos};
                continue;
            }
            box.min.x.value = std::min(box.min.x.value, node.pos.x.value);
            box.min.y.value = std::min(box.min.y.value, node.pos.y.value);
            box.max.x.value = std::max(box.max.x.value, node.pos.x.value);
            box.max.y.value = std::max(box.max.y.value, node.pos.y.value);
        }
    }
    return box;
}

const EmbroideryObject* embroidery_of_shape(const Project& project, ObjectId shape) {
    for (const auto& emb : project.embroidery_objects) {
        if (emb.source_vector == shape) {
            return &emb;
        }
    }
    return nullptr;
}

std::vector<ObjectId> invert_selection(const Project& project,
                                       const std::vector<ObjectId>& selected,
                                       const ShapePredicate& selectable) {
    std::vector<ObjectId> ids;
    for (const auto& shape : project.vector_objects) {
        if (ok(selectable, shape) &&
            std::find(selected.begin(), selected.end(), shape.id) == selected.end()) {
            ids.push_back(shape.id);
        }
    }
    return ids;
}

std::vector<ObjectId> select_same_stitch_type(const Project& project, ObjectId active,
                                              const ShapePredicate& selectable) {
    const EmbroideryObject* wanted = embroidery_of_shape(project, active);
    if (wanted == nullptr) {
        return {};
    }
    const std::size_t kind = wanted->params.index();
    return collect_with_active(project, active, selectable, [&](const VectorObject& shape) {
        const EmbroideryObject* emb = embroidery_of_shape(project, shape.id);
        return emb != nullptr && emb->params.index() == kind;
    });
}

std::vector<ObjectId> select_same_thread(const Project& project, ObjectId active,
                                         const ShapePredicate& selectable) {
    const EmbroideryObject* wanted = embroidery_of_shape(project, active);
    if (wanted == nullptr) {
        return {};
    }
    return collect_with_active(project, active, selectable, [&](const VectorObject& shape) {
        const EmbroideryObject* emb = embroidery_of_shape(project, shape.id);
        if (emb == nullptr) {
            return false;
        }
        // Une référence de nuancier prime : deux fils différents peuvent avoir la même teinte.
        return wanted->thread ? emb->thread == wanted->thread
                              : (!emb->thread && emb->rgb == wanted->rgb);
    });
}

std::vector<ObjectId> select_same_calque(const Project& project, ObjectId active,
                                         const ShapePredicate& selectable) {
    const VectorObject* shape = project.findObject(active);
    if (shape == nullptr) {
        return {};
    }
    const document::Calque* wanted = project.calqueOfShape(active);
    return collect_with_active(project, active, selectable, [&](const VectorObject& other) {
        return project.calqueOfShape(other.id) == wanted;
    });
}

std::vector<ObjectId> select_by_state(const Project& project, StateKind kind) {
    std::vector<ObjectId> ids;
    for (const auto& shape : project.vector_objects) {
        bool match = false;
        switch (kind) {
        case StateKind::EditLocked:
            match = shape.locked || project.shapeLockedByCalque(shape.id);
            break;
        case StateKind::NoEmbroidery:
            match = embroidery_of_shape(project, shape.id) == nullptr;
            break;
        case StateKind::OrderLocked:
        case StateKind::HiddenEmbroidery:
            for (const auto& emb : project.embroidery_objects) {
                if (emb.source_vector == shape.id &&
                    (kind == StateKind::OrderLocked ? emb.locked : !emb.visible)) {
                    match = true;
                    break;
                }
            }
            break;
        }
        if (match) {
            ids.push_back(shape.id);
        }
    }
    return ids;
}

std::optional<ObjectId> step_in_sewing_order(const Project& project,
                                             std::optional<ObjectId> current, int step,
                                             const ShapePredicate& selectable) {
    // Formes brodées dans l'ordre de couture réel, une seule fois chacune.
    std::vector<ObjectId> order;
    for (const std::size_t index : project.sewingOrder()) {
        const ObjectId shapeId = project.embroidery_objects[index].source_vector;
        const VectorObject* shape = project.findObject(shapeId);
        if (shape != nullptr && ok(selectable, *shape) &&
            std::find(order.begin(), order.end(), shapeId) == order.end()) {
            order.push_back(shapeId);
        }
    }
    if (order.empty() || step == 0) {
        return std::nullopt;
    }
    const auto at = current ? std::find(order.begin(), order.end(), *current) : order.end();
    if (at == order.end()) {
        return step > 0 ? order.front() : order.back();
    }
    const auto count = static_cast<std::ptrdiff_t>(order.size());
    const std::ptrdiff_t next = (((at - order.begin()) + step) % count + count) % count;
    return order[static_cast<std::size_t>(next)];
}

std::size_t sewing_rank_of_shape(const Project& project, ObjectId shape) {
    std::size_t rank = 0;
    for (const std::size_t index : project.sewingOrder()) {
        ++rank;
        if (project.embroidery_objects[index].source_vector == shape) {
            return rank;
        }
    }
    return 0;
}

std::vector<ObjectId> select_sewing_range(const Project& project, std::size_t first,
                                          std::size_t last, const ShapePredicate& selectable) {
    if (first > last) {
        std::swap(first, last);
    }
    std::vector<ObjectId> ids;
    std::size_t rank = 0;
    for (const std::size_t index : project.sewingOrder()) {
        ++rank;
        if (rank < first || rank > last) {
            continue;
        }
        const ObjectId shapeId = project.embroidery_objects[index].source_vector;
        const VectorObject* shape = project.findObject(shapeId);
        if (shape != nullptr && ok(selectable, *shape) &&
            std::find(ids.begin(), ids.end(), shapeId) == ids.end()) {
            ids.push_back(shapeId);
        }
    }
    return ids;
}

std::vector<ObjectId> select_touching(const Project& project, const std::vector<ObjectId>& selected,
                                      Micrometers margin, const ShapePredicate& selectable) {
    std::vector<ShapeBox> boxes;
    for (const ObjectId id : selected) {
        if (const VectorObject* shape = project.findObject(id)) {
            boxes.push_back(shape_box(*shape));
        }
    }
    std::vector<ObjectId> ids;
    for (const auto& shape : project.vector_objects) {
        if (!ok(selectable, shape) ||
            std::find(selected.begin(), selected.end(), shape.id) != selected.end()) {
            continue;
        }
        const ShapeBox box = shape_box(shape);
        for (const ShapeBox& other : boxes) {
            if (boxes_touch(box, other, std::max(0, margin.value))) {
                ids.push_back(shape.id);
                break;
            }
        }
    }
    return ids;
}

std::vector<ObjectId> select_in_lasso(const Project& project, const std::vector<Vec2um>& polygon,
                                      const ShapePredicate& selectable) {
    std::vector<ObjectId> ids;
    if (polygon.size() < 3) {
        return ids;
    }
    for (const auto& shape : project.vector_objects) {
        const ShapeBox box = shape_box(shape);
        if (!box.valid || !ok(selectable, shape)) {
            continue;
        }
        const double px = (static_cast<double>(box.min.x.value) + box.max.x.value) / 2.0;
        const double py = (static_cast<double>(box.min.y.value) + box.max.y.value) / 2.0;
        bool inside = false;
        for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
            const double xi = polygon[i].x.value;
            const double yi = polygon[i].y.value;
            const double xj = polygon[j].x.value;
            const double yj = polygon[j].y.value;
            if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) {
                inside = !inside;
            }
        }
        if (inside) {
            ids.push_back(shape.id);
        }
    }
    return ids;
}

} // namespace openstitch::desktop
