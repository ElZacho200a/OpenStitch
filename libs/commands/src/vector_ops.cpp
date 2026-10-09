// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/vector_ops.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <variant>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/cut.hpp"

namespace openstitch::commands {

namespace {

using geometry::PathSet;

struct Box {
    double x0{std::numeric_limits<double>::max()};
    double y0{std::numeric_limits<double>::max()};
    double x1{std::numeric_limits<double>::lowest()};
    double y1{std::numeric_limits<double>::lowest()};
};

Box box_of(const PathSet& set) {
    Box box;
    for (const auto& node : set.outer.nodes) {
        const double x = static_cast<double>(node.pos.x.value);
        const double y = static_cast<double>(node.pos.y.value);
        box.x0 = std::min(box.x0, x);
        box.y0 = std::min(box.y0, y);
        box.x1 = std::max(box.x1, x);
        box.y1 = std::max(box.y1, y);
    }
    return box;
}

VectorOpResult failure(std::string message) {
    VectorOpResult result;
    result.error = std::move(message);
    return result;
}

// Morceaux triés pour un résultat déterministe : le plus grand d'abord, puis par position.
void sort_pieces(std::vector<PathSet>& pieces) {
    std::stable_sort(pieces.begin(), pieces.end(), [](const PathSet& a, const PathSet& b) {
        const double areaA = geometry::path_set_area_um2(a);
        const double areaB = geometry::path_set_area_um2(b);
        if (areaA != areaB) {
            return areaA > areaB;
        }
        const Box boxA = box_of(a);
        const Box boxB = box_of(b);
        if (boxA.x0 != boxB.x0) {
            return boxA.x0 < boxB.x0;
        }
        return boxA.y0 < boxB.y0;
    });
}

// Les guides d'orientation d'un auto-satin sont ancrés dans le repère du modèle : un clone ne
// garde que ceux qui tombent dans (la boîte de) son morceau, avec une marge de 1 mm.
void keep_guides_inside(document::AutoSatinParams& params, const PathSet& piece) {
    constexpr double margin = 1'000.0;
    const Box box = box_of(piece);
    std::erase_if(params.guides, [&](const document::AutoSatinGuide& guide) {
        const double x = static_cast<double>(guide.anchor.x.value);
        const double y = static_cast<double>(guide.anchor.y.value);
        return x < box.x0 - margin || x > box.x1 + margin || y < box.y0 - margin ||
               y > box.y1 + margin;
    });
}

// Remplace la géométrie de `id` par le premier morceau et ajoute un objet (et les clones de ses
// broderies de remplissage) pour chacun des suivants.
void add_piece_commands(document::Project& project, CompositeCommand& group,
                        const document::VectorObject& original, std::vector<PathSet> pieces,
                        const std::string& label) {
    sort_pieces(pieces);
    group.add(std::make_unique<SetVectorPathsCommand>(original.id,
                                                      std::vector<PathSet>{pieces.front()}, label));
    const std::vector<document::EmbroideryObject> embroideries = [&] {
        std::vector<document::EmbroideryObject> found;
        for (const auto& emb : project.embroidery_objects) {
            if (emb.source_vector == original.id) {
                found.push_back(emb);
            }
        }
        return found;
    }();
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        document::VectorObject clone = original;
        clone.id = project.object_ids.next();
        clone.name = original.name + " (" + std::to_string(i + 1) + ")";
        clone.paths = {pieces[i]};
        const ObjectId newId = clone.id;
        group.add(std::make_unique<AddVectorObjectCommand>(std::move(clone)));
        for (const auto& emb : embroideries) {
            // Un satin à rails porte sa propre géométrie : la dupliquer n'aurait aucun sens.
            if (emb.is_satin()) {
                continue;
            }
            document::EmbroideryObject copy = emb;
            copy.id = project.object_ids.next();
            copy.source_vector = newId;
            copy.name = emb.name + " (" + std::to_string(i + 1) + ")";
            copy.overrides.clear();
            copy.edited_fingerprint = 0;
            copy.edited_point_count = 0;
            if (auto* autoSatin = std::get_if<document::AutoSatinParams>(&copy.params)) {
                keep_guides_inside(*autoSatin, pieces[i]);
            }
            group.add(std::make_unique<AddEmbroideryObjectCommand>(std::move(copy)));
        }
    }
}

} // namespace

VectorOpResult make_boolean_command(document::Project& project, BooleanOp op,
                                    const std::vector<ObjectId>& ids) {
    std::vector<const document::VectorObject*> objects;
    for (const ObjectId id : ids) {
        const auto* object = project.findObject(id);
        if (object != nullptr &&
            std::none_of(objects.begin(), objects.end(),
                         [&](const document::VectorObject* o) { return o->id == id; })) {
            objects.push_back(object);
        }
    }
    if (objects.size() < 2) {
        return failure("Sélectionnez au moins deux formes.");
    }

    // Objet retenu et opérandes.
    const document::VectorObject* kept = objects.back();
    if (op == BooleanOp::Subtract) {
        // La forme la plus basse du document (index le plus petit) est celle qu'on entaille.
        auto lowest = [&](const document::VectorObject* a, const document::VectorObject* b) {
            const auto indexOf = [&](const document::VectorObject* o) {
                return static_cast<std::size_t>(o - project.vector_objects.data());
            };
            return indexOf(a) < indexOf(b);
        };
        kept = *std::min_element(objects.begin(), objects.end(), lowest);
    }

    std::vector<PathSet> result;
    if (op == BooleanOp::Union) {
        std::vector<PathSet> all;
        for (const auto* object : objects) {
            all.insert(all.end(), object->paths.begin(), object->paths.end());
        }
        auto united = geometry::union_polygons(all);
        if (!united) {
            return failure(united.error().message);
        }
        result = std::move(*united);
    } else if (op == BooleanOp::Subtract) {
        std::vector<PathSet> cutters;
        for (const auto* object : objects) {
            if (object != kept) {
                cutters.insert(cutters.end(), object->paths.begin(), object->paths.end());
            }
        }
        auto merged = geometry::union_polygons(cutters);
        if (!merged) {
            return failure(merged.error().message);
        }
        auto diff = geometry::difference_polygons(kept->paths, *merged);
        if (!diff) {
            return failure(diff.error().message);
        }
        result = std::move(*diff);
    } else {
        result = objects.front()->paths;
        for (std::size_t i = 1; i < objects.size() && !result.empty(); ++i) {
            auto inter = geometry::intersect_polygons(result, objects[i]->paths);
            if (!inter) {
                return failure(inter.error().message);
            }
            result = std::move(*inter);
        }
    }
    if (result.empty()) {
        return failure(op == BooleanOp::Intersect
                           ? "Les formes ne se recouvrent pas : l'intersection est vide."
                           : "Le résultat est vide.");
    }
    sort_pieces(result);

    const char* label = op == BooleanOp::Union      ? "Unir les formes"
                        : op == BooleanOp::Subtract ? "Soustraire des formes"
                                                    : "Intersecter les formes";
    auto group = std::make_unique<CompositeCommand>(label);
    group->add(std::make_unique<SetVectorPathsCommand>(kept->id, std::move(result), label));
    for (const auto* object : objects) {
        if (object != kept) {
            group->add(std::make_unique<RemoveVectorObjectCommand>(object->id));
        }
    }
    VectorOpResult out;
    out.resulting_objects = 1;
    out.command = std::move(group);
    return out;
}

VectorOpResult make_split_command(document::Project& project, const std::vector<ObjectId>& ids,
                                  Vec2um a, Vec2um b) {
    if (a == b) {
        return failure("La ligne de coupe est réduite à un point.");
    }
    auto group = std::make_unique<CompositeCommand>("Découper les formes");
    std::size_t total = 0;
    // Copie : `add_piece_commands` tire des ids du projet, sans toucher aux objets.
    std::vector<document::VectorObject> targets;
    for (const ObjectId id : ids) {
        if (const auto* object = project.findObject(id)) {
            targets.push_back(*object);
        }
    }
    for (const auto& object : targets) {
        std::vector<PathSet> pieces;
        bool crossed = false;
        for (const PathSet& set : object.paths) {
            auto cut = geometry::cut_path_set(set, a, b);
            if (!cut) {
                return failure(cut.error().message);
            }
            if (cut->size() >= 2) {
                crossed = true;
            }
            pieces.insert(pieces.end(), cut->begin(), cut->end());
        }
        if (!crossed || pieces.size() < 2) {
            continue;
        }
        total += pieces.size();
        add_piece_commands(project, *group, object, std::move(pieces), "Découper les formes");
    }
    if (total == 0) {
        return failure("La ligne de coupe ne traverse aucune forme sélectionnée.");
    }
    VectorOpResult out;
    out.resulting_objects = total;
    out.command = std::move(group);
    return out;
}

VectorOpResult make_break_apart_command(document::Project& project, ObjectId id) {
    const auto* found = project.findObject(id);
    if (found == nullptr) {
        return failure("Objet introuvable.");
    }
    if (found->paths.size() < 2) {
        return failure("Cette forme n'a qu'un seul morceau.");
    }
    const document::VectorObject object = *found;
    auto group = std::make_unique<CompositeCommand>("Séparer les morceaux");
    add_piece_commands(project, *group, object, object.paths, "Séparer les morceaux");
    VectorOpResult out;
    out.resulting_objects = object.paths.size();
    out.command = std::move(group);
    return out;
}

} // namespace openstitch::commands
