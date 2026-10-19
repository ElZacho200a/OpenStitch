// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/vector_path_commands.hpp"

#include <algorithm>
#include <functional>

#include "openstitch/geometry/reshape.hpp"
#include "openstitch/geometry/smooth.hpp"
#include "openstitch/geometry/to_curves.hpp"

namespace openstitch::commands {

void RewriteVectorPathsCommand::apply(document::Project& project) {
    before_.clear();
    for (const Entry& entry : entries_) {
        if (auto* object = project.findObject(entry.object)) {
            before_.push_back({entry.object, object->paths});
            object->paths = entry.paths;
        }
    }
}

void RewriteVectorPathsCommand::revert(document::Project& project) {
    for (const Entry& entry : before_) {
        if (auto* object = project.findObject(entry.object)) {
            object->paths = entry.paths;
        }
    }
    before_.clear();
}

namespace {

using Transform = std::function<geometry::PathSet(const geometry::PathSet&)>;

std::vector<RewriteVectorPathsCommand::Entry> compute(const document::Project& project,
                                                      const std::vector<ObjectId>& ids,
                                                      const Transform& transform) {
    std::vector<RewriteVectorPathsCommand::Entry> entries;
    std::vector<ObjectId> seen;
    for (const ObjectId id : ids) {
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
            continue;
        }
        seen.push_back(id);
        const auto* object = project.findObject(id);
        if (object == nullptr) {
            continue;
        }
        std::vector<geometry::PathSet> after;
        after.reserve(object->paths.size());
        for (const auto& set : object->paths) {
            after.push_back(transform(set));
        }
        if (after != object->paths) {
            entries.push_back({id, std::move(after)});
        }
    }
    return entries;
}

} // namespace

VectorOpResult make_smooth_command(const document::Project& project,
                                   const std::vector<ObjectId>& ids, Micrometers tolerance) {
    VectorOpResult result;
    auto entries = compute(project, ids, [tolerance](const geometry::PathSet& set) {
        return geometry::smooth_path_set(set, tolerance);
    });
    if (entries.empty()) {
        result.error = "Rien à lisser : les formes sont déjà lisses ou trop simples.";
        return result;
    }
    result.resulting_objects = entries.size();
    result.command = std::make_unique<SmoothVectorPathsCommand>(std::move(entries), tolerance);
    return result;
}

VectorOpResult make_convert_to_curves_command(const document::Project& project,
                                              const std::vector<ObjectId>& ids) {
    VectorOpResult result;
    auto entries = compute(project, ids, [](const geometry::PathSet& set) {
        return geometry::convert_to_curves(set);
    });
    if (entries.empty()) {
        result.error = "Rien à convertir : les formes n'ont pas de sommet à arrondir.";
        return result;
    }
    result.resulting_objects = entries.size();
    result.command = std::make_unique<ConvertToCurvesCommand>(std::move(entries));
    return result;
}

VectorOpResult make_reshape_segment_command(const document::Project& project, ObjectId id,
                                            std::size_t set_index, std::size_t path_index,
                                            std::size_t segment, double t, Vec2um delta) {
    VectorOpResult result;
    const auto* object = project.findObject(id);
    if (object == nullptr || set_index >= object->paths.size()) {
        result.error = "Objet ou morceau introuvable.";
        return result;
    }
    auto reshaped = geometry::reshape_path_set_segment(object->paths[set_index], path_index,
                                                       segment, t, delta, Micrometers{50});
    if (!reshaped) {
        result.error = "Remodelage refusé : segment introuvable ou contour qui se croiserait.";
        return result;
    }
    if (*reshaped == object->paths[set_index]) {
        result.error = "Rien à remodeler : la forme ne change pas.";
        return result;
    }
    std::vector<geometry::PathSet> after = object->paths;
    after[set_index] = std::move(*reshaped);
    result.resulting_objects = 1;
    result.command = std::make_unique<ReshapeSegmentCommand>(
        std::vector<RewriteVectorPathsCommand::Entry>{{id, std::move(after)}});
    return result;
}

} // namespace openstitch::commands
