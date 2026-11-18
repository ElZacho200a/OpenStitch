// SPDX-License-Identifier: Apache-2.0
#include "shape_resolver.hpp"

#include <algorithm>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"

namespace openstitch::desktop_v2 {

std::optional<ObjectId> shapeOfTarget(const document::Project& project, Target target) {
    switch (target.kind) {
    case TargetKind::VectorObject:
        if (project.findObject(ObjectId{target.id}) != nullptr) {
            return ObjectId{target.id};
        }
        return std::nullopt;
    case TargetKind::EmbroideryObject:
        if (const auto* embroidery = project.findEmbroidery(ObjectId{target.id});
            embroidery != nullptr && project.findObject(embroidery->source_vector) != nullptr) {
            return embroidery->source_vector;
        }
        return std::nullopt;
    case TargetKind::Region:
        return std::nullopt;
    }
    return std::nullopt;
}

std::vector<ObjectId> resolveEditableShapes(const document::Project& project,
                                            const std::vector<Target>& targets) {
    std::vector<ObjectId> shapes;
    for (const Target& target : targets) {
        const auto shape = shapeOfTarget(project, target);
        if (!shape || project.shapeEditLocked(*shape) ||
            std::ranges::find(shapes, *shape) != shapes.end()) {
            continue;
        }
        shapes.push_back(*shape);
    }
    return shapes;
}

ShapeRemoval prepareShapeRemoval(const document::Project& project,
                                 const std::vector<Target>& targets) {
    ShapeRemoval removal;
    auto group = std::make_unique<commands::CompositeCommand>("Supprimer la forme");
    for (const ObjectId shape : resolveEditableShapes(project, targets)) {
        const auto* object = project.findObject(shape);
        if (object == nullptr || object->text_owner) {
            continue;
        }
        removal.embroideries += static_cast<std::size_t>(std::ranges::count_if(
            project.embroidery_objects,
            [&](const document::EmbroideryObject& e) { return e.source_vector == shape; }));
        group->add(std::make_unique<commands::RemoveVectorObjectCommand>(shape));
        ++removal.shapes;
    }
    if (removal.shapes > 0) {
        removal.command = std::move(group);
    }
    return removal;
}

} // namespace openstitch::desktop_v2
