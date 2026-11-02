// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/cleanup_ops.hpp"

#include <algorithm>
#include <utility>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/simplify.hpp"
#include "openstitch/geometry/smooth.hpp"

namespace openstitch::commands {

namespace {

using geometry::PathSet;

VectorOpResult failure(std::string message) {
    VectorOpResult out;
    out.error = std::move(message);
    return out;
}

bool editable(const document::Project& project, const document::VectorObject& object,
              const std::vector<ObjectId>& ids) {
    return std::find(ids.begin(), ids.end(), object.id) != ids.end() &&
           !project.shapeEditLocked(object.id);
}

PathSet simplified(const PathSet& set, Micrometers tolerance) {
    PathSet out;
    out.outer = geometry::simplify(set.outer, tolerance);
    for (const auto& hole : set.holes) {
        out.holes.push_back(geometry::simplify(hole, tolerance));
    }
    return out;
}

} // namespace

VectorOpResult make_reshape_paths_command(const document::Project& project,
                                          const std::vector<ObjectId>& ids, ReshapeKind kind,
                                          Micrometers tolerance) {
    if (tolerance.value <= 0) {
        return failure("La tolérance doit être strictement positive.");
    }
    const char* label = kind == ReshapeKind::Smooth ? "Lisser les formes" : "Simplifier les formes";
    auto group = std::make_unique<CompositeCommand>(label);
    std::size_t changed = 0;
    for (const auto& object : project.vector_objects) {
        if (!editable(project, object, ids)) {
            continue;
        }
        std::vector<PathSet> paths;
        paths.reserve(object.paths.size());
        for (const PathSet& set : object.paths) {
            paths.push_back(kind == ReshapeKind::Smooth ? geometry::smooth_path_set(set, tolerance)
                                                        : simplified(set, tolerance));
        }
        if (paths != object.paths) {
            ++changed;
            group->add(std::make_unique<SetVectorPathsCommand>(object.id, std::move(paths), label));
        }
    }
    if (changed == 0) {
        return failure("Aucune forme n'a pu être lissée ou simplifiée à cette tolérance.");
    }
    VectorOpResult out;
    out.resulting_objects = changed;
    out.command = std::move(group);
    return out;
}

VectorOpResult make_remove_small_pieces_command(const document::Project& project,
                                                const std::vector<ObjectId>& ids,
                                                double min_area_um2) {
    auto group = std::make_unique<CompositeCommand>("Supprimer les petits morceaux");
    std::size_t removed = 0;
    for (const auto& object : project.vector_objects) {
        if (!editable(project, object, ids) || object.paths.size() < 2) {
            continue;
        }
        std::vector<PathSet> paths;
        for (const PathSet& set : object.paths) {
            if (geometry::path_set_area_um2(set) >= min_area_um2) {
                paths.push_back(set);
            }
        }
        if (paths.empty() || paths.size() == object.paths.size()) {
            continue;
        }
        removed += object.paths.size() - paths.size();
        group->add(std::make_unique<SetVectorPathsCommand>(object.id, std::move(paths),
                                                           "Supprimer les petits morceaux"));
    }
    if (removed == 0) {
        return failure("Aucun morceau sous ce seuil dans la sélection.");
    }
    VectorOpResult out;
    out.resulting_objects = removed;
    out.command = std::move(group);
    return out;
}

} // namespace openstitch::commands
