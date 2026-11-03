// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/vector_ops.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <variant>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/cut.hpp"
#include "openstitch/geometry/fill_gap.hpp"
#include "openstitch/geometry/offset.hpp"

namespace openstitch::commands {

namespace {

using geometry::PathSet;

// Deux passes : tous les ids sont attribués avant de remapper les relations.
// Un partenaire absent/non réciproque est retiré, jamais conservé vers l'original.
void remap_color_blend_links(std::vector<document::EmbroideryObject>& copies,
                             const std::map<ObjectId, ObjectId>& remap) {
    std::map<ObjectId, const document::EmbroideryObject*> byId;
    for (auto& copy : copies) {
        byId.emplace(copy.id, &copy);
        if (copy.color_blend) {
            const auto partner = remap.find(copy.color_blend->partner);
            if (partner == remap.end()) {
                copy.color_blend.reset();
            } else {
                copy.color_blend->partner = partner->second;
            }
        }
    }
    for (auto& copy : copies) {
        if (!copy.color_blend) {
            continue;
        }
        const auto partner = byId.find(copy.color_blend->partner);
        if (partner == byId.end() || partner->first == copy.id ||
            !(copy.is_tatami() || copy.is_directional()) ||
            !(partner->second->is_tatami() || partner->second->is_directional()) ||
            partner->second->source_vector != copy.source_vector || !partner->second->color_blend ||
            partner->second->color_blend->partner != copy.id ||
            partner->second->color_blend->foreground == copy.color_blend->foreground) {
            copy.color_blend.reset();
        }
    }
}

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
        std::vector<document::EmbroideryObject> copies;
        copies.reserve(embroideries.size());
        std::map<ObjectId, ObjectId> embroideryRemap;
        for (const auto& emb : embroideries) {
            // Un satin à rails porte sa propre géométrie : la dupliquer n'aurait aucun sens.
            if (emb.is_satin()) {
                continue;
            }
            document::EmbroideryObject copy = emb;
            copy.id = project.object_ids.next();
            embroideryRemap.emplace(emb.id, copy.id);
            copy.source_vector = newId;
            copy.name = emb.name + " (" + std::to_string(i + 1) + ")";
            copy.overrides.clear();
            copy.edited_fingerprint = 0;
            copy.edited_point_count = 0;
            if (auto* autoSatin = std::get_if<document::AutoSatinParams>(&copy.params)) {
                keep_guides_inside(*autoSatin, pieces[i]);
            }
            copies.push_back(std::move(copy));
        }
        remap_color_blend_links(copies, embroideryRemap);
        for (auto& copy : copies) {
            group.add(std::make_unique<AddEmbroideryObjectCommand>(std::move(copy)));
        }
    }
}

} // namespace

VectorOpResult make_boolean_command(document::Project& project, BooleanOp op,
                                    const std::vector<ObjectId>& ids, Micrometers union_gap) {
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
        auto united = geometry::union_polygons_closing(all, union_gap);
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
    } else if (op == BooleanOp::Exclude) {
        // Différence symétrique, de proche en proche : (A - B) et (B - A) sont disjoints, leur
        // réunion est donc une simple concaténation.
        result = objects.front()->paths;
        for (std::size_t i = 1; i < objects.size(); ++i) {
            auto onlyA = geometry::difference_polygons(result, objects[i]->paths);
            auto onlyB = geometry::difference_polygons(objects[i]->paths, result);
            if (!onlyA) {
                return failure(onlyA.error().message);
            }
            if (!onlyB) {
                return failure(onlyB.error().message);
            }
            result = std::move(*onlyA);
            result.insert(result.end(), onlyB->begin(), onlyB->end());
        }
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

    const char* label = op == BooleanOp::Union       ? "Unir les formes"
                        : op == BooleanOp::Subtract  ? "Soustraire des formes"
                        : op == BooleanOp::Intersect ? "Intersecter les formes"
                                                     : "Exclure les formes";
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

document::ObjectFragment collect_fragment(const document::Project& project,
                                          const std::vector<ObjectId>& ids) {
    document::ObjectFragment fragment;
    const auto wanted = [&](ObjectId id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    for (const auto& vec : project.vector_objects) {
        if (wanted(vec.id)) {
            fragment.vectors.push_back(vec);
        }
    }
    for (const auto& emb : project.embroidery_objects) {
        if (wanted(emb.source_vector) && project.findObject(emb.source_vector) != nullptr) {
            fragment.embroideries.push_back(emb);
        }
    }
    return fragment;
}

PasteResult make_paste_command(document::Project& project, const document::ObjectFragment& fragment,
                               Vec2um delta, const std::string& label) {
    PasteResult out;
    if (fragment.empty()) {
        return out;
    }
    const bool moved = delta.x.value != 0 || delta.y.value != 0;
    std::vector<std::pair<ObjectId, ObjectId>> remap; // ancien -> nouveau
    std::vector<document::VectorObject> vectors;
    vectors.reserve(fragment.vectors.size());
    for (const auto& source : fragment.vectors) {
        document::VectorObject copy = source;
        copy.id = project.object_ids.next();
        copy.text_owner.reset();
        if (copy.source_region &&
            (!project.segmentation || project.segmentation->find(*copy.source_region) == nullptr)) {
            copy.source_region.reset();
        }
        remap.emplace_back(source.id, copy.id);
        out.pasted.push_back(copy.id);
        vectors.push_back(std::move(copy));
    }
    std::vector<document::EmbroideryObject> embroideries;
    std::map<ObjectId, ObjectId> embroideryRemap;
    embroideries.reserve(fragment.embroideries.size());
    for (const auto& source : fragment.embroideries) {
        const auto it = std::find_if(remap.begin(), remap.end(), [&](const auto& pair) {
            return pair.first == source.source_vector;
        });
        if (it == remap.end()) {
            continue; // broderie orpheline : jamais collée
        }
        document::EmbroideryObject copy = source;
        copy.id = project.object_ids.next();
        embroideryRemap.emplace(source.id, copy.id);
        copy.source_vector = it->second;
        if (moved) {
            copy.overrides.clear();
            copy.edited_fingerprint = 0;
            copy.edited_point_count = 0;
        }
        embroideries.push_back(std::move(copy));
    }
    remap_color_blend_links(embroideries, embroideryRemap);

    auto group = std::make_unique<CompositeCommand>(label);
    group->add(std::make_unique<AddObjectBatchCommand>(std::move(vectors), std::move(embroideries),
                                                       label));
    if (moved) {
        for (const ObjectId id : out.pasted) {
            group->add(std::make_unique<TranslateVectorObjectCommand>(id, delta));
        }
    }
    out.command = std::move(group);
    return out;
}

VectorOpResult make_remove_holes_command(const document::Project& project,
                                         const std::vector<ObjectId>& ids, double max_area_um2) {
    auto group = std::make_unique<CompositeCommand>("Supprimer les trous");
    std::size_t removed = 0;
    for (const auto& object : project.vector_objects) {
        if (std::find(ids.begin(), ids.end(), object.id) == ids.end()) {
            continue;
        }
        std::vector<PathSet> paths = object.paths;
        std::size_t here = 0;
        for (PathSet& set : paths) {
            const std::size_t before = set.holes.size();
            std::erase_if(set.holes, [&](const geometry::Path& hole) {
                if (max_area_um2 <= 0.0) {
                    return true;
                }
                PathSet alone;
                alone.outer = hole;
                return geometry::path_set_area_um2(alone) <= max_area_um2;
            });
            here += before - set.holes.size();
        }
        if (here > 0) {
            removed += here;
            group->add(std::make_unique<SetVectorPathsCommand>(object.id, std::move(paths),
                                                               "Supprimer les trous"));
        }
    }
    if (removed == 0) {
        return failure(max_area_um2 > 0.0 ? "Aucun trou sous ce seuil dans la sélection."
                                          : "Les formes sélectionnées n'ont aucun trou.");
    }
    VectorOpResult out;
    out.resulting_objects = removed;
    out.command = std::move(group);
    return out;
}

VectorOpResult make_fill_gap_command(document::Project& project, Vec2um seed, Micrometers seal) {
    std::vector<PathSet> obstacles;
    for (const auto& object : project.vector_objects) {
        if (object.visible) {
            obstacles.insert(obstacles.end(), object.paths.begin(), object.paths.end());
        }
    }
    auto gap = geometry::fill_gap_at(obstacles, seed, seal);
    if (!gap) {
        return failure(gap.error().message);
    }
    document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "Vide rempli";
    object.rgb = {80, 120, 200};
    object.paths.push_back(std::move(*gap));

    auto group = std::make_unique<CompositeCommand>("Remplir un vide");
    group->add(std::make_unique<AddVectorObjectCommand>(std::move(object)));
    VectorOpResult out;
    out.resulting_objects = 1;
    out.command = std::move(group);
    return out;
}

VectorOpResult make_split_command(document::Project& project, const std::vector<ObjectId>& ids,
                                  Vec2um a, Vec2um b, Micrometers cut_width) {
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
    // La coupe se limite au segment [a,b] tracé (bande centrée sur son milieu,
    // demi-portée |ab|/2) : les parties hors de cette bande ne sont pas touchées.
    const double reach = length_um(b - a) / 2.0;
    for (const auto& object : targets) {
        std::vector<PathSet> pieces;
        bool changed = false;
        bool separated = false;
        for (const PathSet& set : object.paths) {
            auto cut = geometry::cut_path_set_bounded(set, a, b, reach, cut_width);
            if (!cut) {
                return failure(cut.error().message);
            }
            if (cut->empty()) {
                // Le couteau ne supprime pas une forme entièrement engloutie par sa bande.
                pieces.push_back(set);
                continue;
            }
            // Une coupe doit changer la topologie, pas seulement creuser une
            // entaille qui s'arrête au milieu d'une surface pleine. Un pont
            // coupé peut laisser une seule composante, mais ouvre alors un trou
            // préexistant vers l'extérieur (le nombre de trous diminue).
            const bool splits = cut->size() >= 2;
            const bool opens_bridge =
                cut->size() == 1 && cut->front().holes.size() < set.holes.size();
            if (!splits && !opens_bridge) {
                pieces.push_back(set);
                continue;
            }
            changed = true;
            separated = separated || splits;
            pieces.insert(pieces.end(), cut->begin(), cut->end());
        }
        if (!changed) {
            continue;
        }
        if (separated) {
            total += pieces.size();
            add_piece_commands(project, *group, object, std::move(pieces), "Découper les formes");
        } else {
            // Une seule composante par région : conserver l'identité, les autres
            // chemins et les remplissages associés, sans créer de clone inutile.
            ++total;
            group->add(std::make_unique<SetVectorPathsCommand>(object.id, std::move(pieces),
                                                               "Découper les formes"));
        }
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
