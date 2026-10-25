// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/vector_path_commands.hpp"

#include <algorithm>
#include <functional>

#include "openstitch/geometry/polyline.hpp"
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

namespace {

// Point dans un polygone (règle pair-impair), sur le chemin aplati.
bool inside_path(const geometry::Path& path, Vec2um p) {
    const geometry::Path flat = geometry::flatten_to_polygon(path, Micrometers{50});
    const auto& n = flat.nodes;
    bool inside = false;
    for (std::size_t i = 0, j = n.size() - 1; i < n.size(); j = i++) {
        const double yi = n[i].pos.y.value, yj = n[j].pos.y.value;
        if ((yi > p.y.value) != (yj > p.y.value)) {
            const double xCross = n[j].pos.x.value + (p.y.value - yj) *
                                                         (n[i].pos.x.value - n[j].pos.x.value) /
                                                         (yi - yj);
            if (p.x.value < xCross) {
                inside = !inside;
            }
        }
    }
    return inside;
}

} // namespace

VectorOpResult make_remove_hole_at_command(const document::Project& project, ObjectId id,
                                           Vec2um at) {
    VectorOpResult result;
    const auto* object = project.findObject(id);
    if (object == nullptr) {
        result.error = "Objet introuvable.";
        return result;
    }
    std::vector<geometry::PathSet> after = object->paths;
    for (auto& set : after) {
        for (std::size_t h = 0; h < set.holes.size(); ++h) {
            if (set.holes[h].nodes.size() >= 3 && inside_path(set.holes[h], at)) {
                set.holes.erase(set.holes.begin() + static_cast<std::ptrdiff_t>(h));
                result.resulting_objects = 1;
                result.command = std::make_unique<RemoveHoleCommand>(
                    std::vector<RewriteVectorPathsCommand::Entry>{{id, std::move(after)}});
                return result;
            }
        }
    }
    result.error = "Aucun trou à cet endroit.";
    return result;
}

VectorOpResult make_insert_node_command(const document::Project& project, ObjectId id,
                                        std::size_t set_index, std::size_t path_index,
                                        std::size_t segment, double t) {
    VectorOpResult result;
    const auto* object = project.findObject(id);
    if (object == nullptr || set_index >= object->paths.size()) {
        result.error = "Objet ou morceau introuvable.";
        return result;
    }
    const geometry::Path* path = document::path_in(*object, set_index, path_index);
    const std::size_t edges =
        path == nullptr ? 0 : (path->closed ? path->nodes.size() : path->nodes.size() - 1);
    if (path == nullptr || path->nodes.size() < 2 || segment >= edges) {
        result.error = "Segment introuvable.";
        return result;
    }
    if (!(t > 0.02 && t < 0.98)) {
        result.error = "Point trop proche d'un nœud existant.";
        return result;
    }
    std::vector<geometry::PathSet> after = object->paths;
    geometry::Path* target =
        path_index == 0 ? &after[set_index].outer : &after[set_index].holes[path_index - 1];
    *target = geometry::insert_node_on_segment(*target, segment, t);
    if (target->nodes.size() != path->nodes.size() + 1) {
        result.error = "Insertion impossible.";
        return result;
    }
    result.resulting_objects = 1;
    result.command = std::make_unique<InsertNodeCommand>(
        std::vector<RewriteVectorPathsCommand::Entry>{{id, std::move(after)}});
    return result;
}

VectorOpResult make_translate_edge_command(const document::Project& project, ObjectId id,
                                           std::size_t set_index, std::size_t path_index,
                                           std::size_t segment, Vec2um delta) {
    VectorOpResult result;
    const auto* object = project.findObject(id);
    if (object == nullptr || set_index >= object->paths.size()) {
        result.error = "Objet ou morceau introuvable.";
        return result;
    }
    auto moved = geometry::translate_path_set_segment(object->paths[set_index], path_index, segment,
                                                      delta, Micrometers{50});
    if (!moved) {
        result.error = "Déplacement refusé : segment introuvable ou contour qui se croiserait.";
        return result;
    }
    if (*moved == object->paths[set_index]) {
        result.error = "Rien à déplacer : la forme ne change pas.";
        return result;
    }
    std::vector<geometry::PathSet> after = object->paths;
    after[set_index] = std::move(*moved);
    result.resulting_objects = 1;
    result.command = std::make_unique<TranslateEdgeCommand>(
        std::vector<RewriteVectorPathsCommand::Entry>{{id, std::move(after)}});
    return result;
}

VectorOpResult make_transform_nodes_command(const document::Project& project, ObjectId id,
                                            std::size_t set_index, std::size_t path_index,
                                            const std::vector<std::size_t>& selected,
                                            const geometry::NodeTransform& transform,
                                            double soft_radius_um) {
    VectorOpResult result;
    const auto* object = project.findObject(id);
    if (object == nullptr || set_index >= object->paths.size()) {
        result.error = "Objet ou morceau introuvable.";
        return result;
    }
    auto moved = geometry::transform_path_set_nodes(object->paths[set_index], path_index, selected,
                                                    transform, soft_radius_um, Micrometers{50});
    if (!moved) {
        result.error = "Transformation refusée : cible introuvable ou contour qui se croiserait.";
        return result;
    }
    if (*moved == object->paths[set_index]) {
        result.error = "Rien à transformer : les nœuds ne changent pas.";
        return result;
    }
    std::vector<geometry::PathSet> after = object->paths;
    after[set_index] = std::move(*moved);
    result.resulting_objects = 1;
    result.command = std::make_unique<TransformNodesCommand>(
        std::vector<RewriteVectorPathsCommand::Entry>{{id, std::move(after)}});
    return result;
}

VectorOpResult make_brush_stroke_command(const document::Project& project, ObjectId id,
                                         std::vector<geometry::PathSet> after) {
    VectorOpResult result;
    const auto* object = project.findObject(id);
    if (object == nullptr || after.size() != object->paths.size()) {
        result.error = "Objet introuvable ou géométrie incohérente.";
        return result;
    }
    if (after == object->paths) {
        result.error = "Rien à retoucher : la forme ne change pas.";
        return result;
    }
    for (std::size_t i = 0; i < after.size(); ++i) {
        if (!geometry::brush_result_is_valid(object->paths[i], after[i], Micrometers{50})) {
            result.error = "Retouche refusée : le contour se croiserait.";
            return result;
        }
    }
    result.resulting_objects = 1;
    result.command = std::make_unique<BrushStrokeCommand>(
        std::vector<RewriteVectorPathsCommand::Entry>{{id, std::move(after)}});
    return result;
}

} // namespace openstitch::commands
