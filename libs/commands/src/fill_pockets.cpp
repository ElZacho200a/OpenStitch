// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <utility>
#include <variant>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/document/footprint.hpp"
#include "openstitch/geometry/boolean.hpp"
#include "openstitch/geometry/offset.hpp"

namespace openstitch::commands {

namespace {

using geometry::PathSet;

constexpr Micrometers kFlattenTolerance{20};
// Part minimale d'une poche située hors du vecteur voisin pour qu'agrandir ce vecteur serve à
// quelque chose (sinon la poche est déjà « dans » la zone et le tatami ne l'a pas couverte).
constexpr double kMinOutsideRatio = 0.5;
// Portée de rattachement d'une poche à un objet (µm) : en mode de tirage automatique, les rangées
// dépassent de l'emprise nominale (jusqu'à 0,4 mm) et la détection compte une demi-largeur de fil
// (0,25 mm) de plus ; une poche encore visible peut donc se trouver à ~0,65 mm de l'emprise.
constexpr std::int32_t kNeighborReach = 700;

struct Candidate {
    const document::EmbroideryObject* object{};
    std::vector<PathSet> footprint;
    std::vector<PathSet> reach; // emprise élargie de `kNeighborReach` : test de rattachement
    double area{0.0};
    bool fill{false}; // tatami ou directionnel
};

double total_area(const std::vector<PathSet>& sets) {
    double area = 0.0;
    for (const PathSet& set : sets) {
        area += geometry::path_set_area_um2(set);
    }
    return area;
}

VectorOpResult refuse(std::string message) {
    VectorOpResult result;
    result.error = std::move(message);
    return result;
}

// Aire de l'intersection entre `pocket` et l'emprise d'un candidat ; 0 si rien ou en cas d'échec.
double overlap_area(const PathSet& pocket, const Candidate& candidate) {
    auto common = geometry::intersect_polygons({pocket}, candidate.reach);
    return common ? total_area(*common) : 0.0;
}

// Meilleur voisin d'une poche : le plus grand remplissage qui la touche, à défaut le plus grand
// objet de surface. Égalité d'aire : l'identifiant le plus bas (déterminisme).
const Candidate* pick_neighbor(const PathSet& pocket, const std::vector<Candidate>& candidates) {
    const Candidate* best = nullptr;
    for (const bool wantFill : {true, false}) {
        for (const Candidate& candidate : candidates) {
            if (candidate.fill != wantFill && wantFill) {
                continue;
            }
            if (overlap_area(pocket, candidate) <= 0.0) {
                continue;
            }
            if (best == nullptr || candidate.area > best->area ||
                (candidate.area == best->area && candidate.object->id < best->object->id)) {
                best = &candidate;
            }
        }
        if (best != nullptr) {
            return best;
        }
    }
    return nullptr;
}

} // namespace

VectorOpResult make_fill_pockets_command(document::Project& project,
                                         const std::vector<ObjectId>& ids,
                                         const std::vector<PathSet>& pockets) {
    if (pockets.empty()) {
        return refuse("Aucun vide à combler dans ce bloc.");
    }
    std::vector<Candidate> candidates;
    for (const auto& emb : project.embroidery_objects) {
        if (std::find(ids.begin(), ids.end(), emb.source_vector) == ids.end()) {
            continue;
        }
        Candidate candidate;
        candidate.object = &emb;
        candidate.footprint = document::embroidery_footprint(project, emb, kFlattenTolerance);
        candidate.area = total_area(candidate.footprint);
        for (const PathSet& set : candidate.footprint) {
            if (auto grown = geometry::inset_path_set(set, Micrometers{-kNeighborReach}); grown) {
                candidate.reach.insert(candidate.reach.end(),
                                       std::make_move_iterator(grown->begin()),
                                       std::make_move_iterator(grown->end()));
            } else {
                candidate.reach.push_back(set);
            }
        }
        candidate.fill = emb.is_tatami() || emb.is_directional();
        if (candidate.area > 0.0) {
            candidates.push_back(std::move(candidate));
        }
    }
    if (candidates.empty()) {
        return refuse("Le bloc n'a aucun objet de surface où rattacher les vides.");
    }

    // Poches par voisin retenu (ordre des candidats = ordre du document : déterministe).
    std::map<std::size_t, std::vector<const PathSet*>> byNeighbor;
    for (const PathSet& pocket : pockets) {
        if (const Candidate* neighbor = pick_neighbor(pocket, candidates)) {
            byNeighbor[static_cast<std::size_t>(neighbor - candidates.data())].push_back(&pocket);
        }
    }

    auto group = std::make_unique<CompositeCommand>("Combler les vides du bloc");
    std::vector<document::VectorObject> newVectors;
    std::vector<document::EmbroideryObject> newEmbroideries;
    std::size_t filled = 0;

    for (const auto& [index, assigned] : byNeighbor) {
        const Candidate& neighbor = candidates[index];
        const document::EmbroideryObject& source = *neighbor.object;
        const document::VectorObject* vector = project.findObject(source.source_vector);
        const bool soleOwner =
            std::count_if(project.embroidery_objects.begin(), project.embroidery_objects.end(),
                          [&](const document::EmbroideryObject& other) {
                              return other.source_vector == source.source_vector;
                          }) == 1;
        const bool mergeable = neighbor.fill && vector != nullptr && soleOwner &&
                               source.overrides.empty() && !vector->text_owner.has_value();

        std::vector<PathSet> merged;
        std::vector<PathSet> separate;
        for (const PathSet* pocket : assigned) {
            bool absorb = mergeable;
            if (absorb) {
                auto outside = geometry::difference_polygons({*pocket}, neighbor.footprint);
                const double outsideArea = outside ? total_area(*outside) : 0.0;
                absorb = outsideArea >= kMinOutsideRatio * geometry::path_set_area_um2(*pocket);
            }
            (absorb ? merged : separate).push_back(*pocket);
        }

        if (!merged.empty()) {
            std::vector<PathSet> all = vector->paths;
            all.insert(all.end(), merged.begin(), merged.end());
            auto united = geometry::union_polygons(all);
            if (united && !united->empty()) {
                group->add(std::make_unique<SetVectorPathsCommand>(vector->id, std::move(*united),
                                                                   "Combler les vides du bloc"));
                filled += merged.size();
            } else {
                separate.insert(separate.end(), merged.begin(), merged.end());
            }
        }
        if (!separate.empty()) {
            document::VectorObject fresh;
            fresh.id = project.object_ids.next();
            fresh.name = "Comblement (" + source.name + ")";
            fresh.rgb = source.rgb;
            fresh.paths = separate;
            document::EmbroideryObject emb;
            if (neighbor.fill) {
                emb = source;
                emb.overrides.clear();
                emb.edited_fingerprint = 0;
                emb.edited_point_count = 0;
                emb.color_blend.reset();
            } else {
                emb.rgb = source.rgb;
                emb.thread = source.thread;
                emb.params = document::TatamiParams{};
            }
            emb.id = project.object_ids.next();
            emb.name = fresh.name;
            emb.source_vector = fresh.id;
            emb.visible = true;
            newVectors.push_back(std::move(fresh));
            newEmbroideries.push_back(std::move(emb));
            filled += separate.size();
        }
    }
    if (filled == 0) {
        return refuse("Aucun vide ne touche un objet du bloc.");
    }
    if (!newVectors.empty()) {
        group->add(std::make_unique<AddObjectBatchCommand>(
            std::move(newVectors), std::move(newEmbroideries), "Combler les vides du bloc"));
    }
    VectorOpResult out;
    out.resulting_objects = filled;
    out.command = std::move(group);
    return out;
}

} // namespace openstitch::commands
