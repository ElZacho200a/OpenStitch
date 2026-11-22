// SPDX-License-Identifier: Apache-2.0
#include "openstitch/commands/cleanup_ops.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"
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

namespace {

double area_of(const std::vector<PathSet>& sets) {
    double area = 0.0;
    for (const PathSet& set : sets) {
        area += geometry::path_set_area_um2(set);
    }
    return area;
}

// `sets` dilates de `reach` (union des morceaux) ; vide en cas d'echec.
std::vector<PathSet> grown(const std::vector<PathSet>& sets, Micrometers reach) {
    std::vector<PathSet> pieces;
    for (const PathSet& set : sets) {
        if (auto out = geometry::inset_path_set(set, Micrometers{-reach.value})) {
            pieces.insert(pieces.end(), out->begin(), out->end());
        }
    }
    auto united = geometry::union_polygons(pieces);
    return united ? std::move(*united) : std::vector<PathSet>{};
}

// Couleur de fil d'une forme : celle de sa premiere broderie, sinon celle de la forme.
std::array<std::uint8_t, 3> thread_rgb(const document::Project& project,
                                       const document::VectorObject& shape) {
    for (const auto& emb : project.embroidery_objects) {
        if (emb.source_vector == shape.id) {
            return emb.rgb;
        }
    }
    return shape.rgb;
}

bool shown(const document::Project& project, const document::VectorObject& shape) {
    const auto* calque = project.calqueOfShape(shape.id);
    return shape.visible && (calque == nullptr || calque->visible) && !shape.linework;
}

struct Neighbor {
    const document::VectorObject* shape{};
    double area{0.0};
};

// Formes pouvant recevoir de la matiere : visibles, editables, hors lettrage et trait, d'aire > 0,
// hors `excluded`.
std::vector<Neighbor> receivers(const document::Project& project,
                                const std::set<ObjectId>& excluded) {
    std::vector<Neighbor> out;
    for (const auto& shape : project.vector_objects) {
        if (!shown(project, shape) || shape.text_owner.has_value() ||
            project.shapeEditLocked(shape.id) || excluded.contains(shape.id)) {
            continue;
        }
        const double area = area_of(shape.paths);
        if (area > 0.0) {
            out.push_back({&shape, area});
        }
    }
    return out;
}

// Aire commune entre `probe` (zone dilatee) et `paths` : mesure de la frontiere partagee.
double contact(const std::vector<PathSet>& probe, const std::vector<PathSet>& paths) {
    auto common = geometry::intersect_polygons(probe, paths);
    return common ? area_of(*common) : 0.0;
}

} // namespace

VectorOpResult make_absorb_small_objects_command(const document::Project& project,
                                                 const std::vector<ObjectId>& crumbs,
                                                 AbsorbPriority priority, Micrometers reach) {
    const std::set<ObjectId> crumbSet(crumbs.begin(), crumbs.end());
    const auto neighbors = receivers(project, crumbSet);
    // Geometrie courante des voisins agrandis (cle = index dans `neighbors` : ordre stable).
    std::map<std::size_t, std::vector<PathSet>> grownShapes;
    const auto current = [&](std::size_t k) -> const std::vector<PathSet>& {
        const auto it = grownShapes.find(k);
        return it != grownShapes.end() ? it->second : neighbors[k].shape->paths;
    };
    std::vector<ObjectId> removed;

    for (const auto& shape : project.vector_objects) {
        if (!crumbSet.contains(shape.id) || project.shapeEditLocked(shape.id) ||
            shape.text_owner.has_value()) {
            continue;
        }
        if (area_of(shape.paths) <= 0.0) {
            removed.push_back(shape.id); // contour ouvert : aucune surface a rendre
            continue;
        }
        const auto probe = grown(shape.paths, reach);
        if (probe.empty()) {
            continue;
        }
        const auto rgb = thread_rgb(project, shape);
        std::optional<std::size_t> best;
        double bestScore = 0.0;
        for (const bool sameOnly : {true, false}) {
            if (sameOnly && priority != AbsorbPriority::SameThread) {
                continue;
            }
            for (std::size_t k = 0; k < neighbors.size(); ++k) {
                if (sameOnly && thread_rgb(project, *neighbors[k].shape) != rgb) {
                    continue;
                }
                const double touch = contact(probe, current(k));
                if (touch <= 0.0) {
                    continue;
                }
                const double score =
                    priority == AbsorbPriority::Largest ? neighbors[k].area : touch;
                if (!best || score > bestScore) { // egalite : premier dans l'ordre du document
                    best = k;
                    bestScore = score;
                }
            }
            if (best) {
                break;
            }
        }
        if (!best) {
            continue;
        }
        // Matiere ajoutee : la miette dilatee, privee des AUTRES formes (on comble la fente vers
        // le voisin sans recouvrir un tiers).
        std::vector<PathSet> others;
        for (std::size_t k = 0; k < neighbors.size(); ++k) {
            if (k != *best) {
                const auto& paths = current(k);
                others.insert(others.end(), paths.begin(), paths.end());
            }
        }
        std::vector<PathSet> added = probe;
        if (!others.empty()) {
            auto cut = geometry::difference_polygons(probe, others);
            if (!cut) {
                continue;
            }
            added = std::move(*cut);
        }
        std::vector<PathSet> all = current(*best);
        all.insert(all.end(), added.begin(), added.end());
        auto united = geometry::union_polygons(all);
        if (!united || united->empty()) {
            continue;
        }
        grownShapes[*best] = std::move(*united);
        removed.push_back(shape.id);
    }
    if (removed.empty()) {
        return failure("Aucune miette n'a de voisin à portée où se fondre.");
    }
    const char* label = "Absorber les miettes";
    auto group = std::make_unique<CompositeCommand>(label);
    for (auto& [k, paths] : grownShapes) {
        group->add(std::make_unique<SetVectorPathsCommand>(neighbors[k].shape->id, std::move(paths),
                                                           label));
    }
    for (const ObjectId id : removed) {
        group->add(std::make_unique<RemoveVectorObjectCommand>(id));
    }
    VectorOpResult out;
    out.resulting_objects = removed.size();
    out.command = std::move(group);
    return out;
}

VectorOpResult make_fill_gaps_between_command(const document::Project& project, double max_area_um2,
                                              Micrometers reach) {
    if (!(max_area_um2 > 0.0)) {
        return failure("L'aire maximale des interstices doit être strictement positive.");
    }
    // Toutes les formes de surface visibles bornent les vides (meme verrouillees) ; seules les
    // editables les recoivent.
    std::vector<PathSet> everything;
    std::vector<const document::VectorObject*> borders;
    for (const auto& shape : project.vector_objects) {
        if (shown(project, shape) && area_of(shape.paths) > 0.0) {
            everything.insert(everything.end(), shape.paths.begin(), shape.paths.end());
            borders.push_back(&shape);
        }
    }
    const auto neighbors = receivers(project, {});
    auto united = geometry::union_polygons(everything);
    if (!united || united->empty() || neighbors.empty()) {
        return failure("Aucune forme de surface ne borde d'interstice.");
    }
    std::map<std::size_t, std::vector<PathSet>> additions;
    std::size_t filled = 0;
    for (const PathSet& component : *united) {
        for (const auto& hole : component.holes) {
            PathSet voidSet;
            voidSet.outer = hole;
            voidSet.outer.closed = true;
            // Ilots enfermes dans le vide : ils n'en font pas partie.
            auto gap = geometry::difference_polygons({voidSet}, *united);
            if (!gap || gap->empty()) {
                continue;
            }
            const double area = area_of(*gap);
            if (area <= 0.0 || area >= max_area_um2) {
                continue;
            }
            const auto probe = grown(*gap, reach);
            std::size_t touching = 0;
            for (const auto* shape : borders) {
                if (contact(probe, shape->paths) > 0.0) {
                    ++touching;
                }
            }
            if (touching < 2) {
                continue; // trou borde d'une seule forme : voulu
            }
            std::optional<std::size_t> best;
            double bestTouch = 0.0;
            for (std::size_t k = 0; k < neighbors.size(); ++k) {
                const double touch = contact(probe, neighbors[k].shape->paths);
                if (touch > 0.0 && (!best || touch > bestTouch)) {
                    best = k;
                    bestTouch = touch;
                }
            }
            if (!best) {
                continue;
            }
            auto& bucket = additions[*best];
            bucket.insert(bucket.end(), gap->begin(), gap->end());
            ++filled;
        }
    }
    if (filled == 0) {
        return failure("Aucun interstice fermé entre formes sous ce seuil.");
    }
    const char* label = "Reboucher les interstices";
    auto group = std::make_unique<CompositeCommand>(label);
    for (auto& [k, gaps] : additions) {
        std::vector<PathSet> all = neighbors[k].shape->paths;
        all.insert(all.end(), gaps.begin(), gaps.end());
        auto merged = geometry::union_polygons(all);
        if (!merged || merged->empty()) {
            return failure("Échec de la fusion d'un interstice.");
        }
        group->add(std::make_unique<SetVectorPathsCommand>(neighbors[k].shape->id,
                                                           std::move(*merged), label));
    }
    VectorOpResult out;
    out.resulting_objects = filled;
    out.command = std::move(group);
    return out;
}

} // namespace openstitch::commands
