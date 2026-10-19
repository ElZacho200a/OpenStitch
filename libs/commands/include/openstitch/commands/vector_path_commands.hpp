// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/commands/vector_ops.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/brush.hpp"
#include "openstitch/geometry/node_transform.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::commands {

// Réécriture de la géométrie d'objets vectoriels : « Lisser » (HP-VEC-007) et « Convertir en
// courbes » (HP-VEC-008). La géométrie est calculée à la construction (fonctions pures de
// libs/geometry) ; `apply` pose le résultat, `revert` rend EXACTEMENT les chemins d'origine
// (instantané pris à l'`apply`). Une commande couvre tous les objets de la sélection : un seul
// pas d'annulation.
class RewriteVectorPathsCommand : public ICommand {
public:
    struct Entry {
        ObjectId object;
        std::vector<geometry::PathSet> paths; // géométrie APRÈS l'opération
    };

    void apply(document::Project& project) override;
    void revert(document::Project& project) override;

protected:
    explicit RewriteVectorPathsCommand(std::vector<Entry> entries) : entries_(std::move(entries)) {}

private:
    std::vector<Entry> entries_;
    std::vector<Entry> before_;
};

// Lisse les chemins (`geometry::smooth_path_set`, écart maximal `tolerance`).
class SmoothVectorPathsCommand final : public RewriteVectorPathsCommand {
public:
    SmoothVectorPathsCommand(std::vector<Entry> entries, Micrometers tolerance)
        : RewriteVectorPathsCommand(std::move(entries)), tolerance_(tolerance) {}
    [[nodiscard]] std::string name() const override { return "Lisser"; }
    [[nodiscard]] Micrometers tolerance() const { return tolerance_; }

private:
    Micrometers tolerance_;
};

// Convertit les polygones en courbes de Bézier (`geometry::convert_to_curves`).
class ConvertToCurvesCommand final : public RewriteVectorPathsCommand {
public:
    explicit ConvertToCurvesCommand(std::vector<Entry> entries)
        : RewriteVectorPathsCommand(std::move(entries)) {}
    [[nodiscard]] std::string name() const override { return "Convertir en courbes"; }
};

// Remodelage interactif d'un segment (HP-VEC-016) : le résultat est calculé au relâchement par
// `geometry::reshape_path_set_segment` ; UN pas d'annulation par geste.
class ReshapeSegmentCommand final : public RewriteVectorPathsCommand {
public:
    explicit ReshapeSegmentCommand(std::vector<Entry> entries)
        : RewriteVectorPathsCommand(std::move(entries)) {}
    [[nodiscard]] std::string name() const override { return "Remodeler un segment"; }
};

// Déplacement / rotation / échelle d'un groupe de nœuds (HP-VEC-017) : un geste, un pas.
class TransformNodesCommand final : public RewriteVectorPathsCommand {
public:
    explicit TransformNodesCommand(std::vector<Entry> entries)
        : RewriteVectorPathsCommand(std::move(entries)) {}
    [[nodiscard]] std::string name() const override { return "Transformer des nœuds"; }
};

// Coup de pinceau de déformation (HP-VEC-018) : toute la géométrie finale du geste, un seul pas.
class BrushStrokeCommand final : public RewriteVectorPathsCommand {
public:
    explicit BrushStrokeCommand(std::vector<Entry> entries)
        : RewriteVectorPathsCommand(std::move(entries)) {}
    [[nodiscard]] std::string name() const override { return "Pinceau de déformation"; }
};

// Tolérance de lissage par défaut : 0,2 mm.
inline constexpr Micrometers kDefaultSmoothTolerance{200};

// Fabriques : calculent la géométrie maintenant, ne modifient pas le document. Commande nulle
// (avec `error`) si aucun objet de `ids` n'existe ou si l'opération ne change rien.
// `resulting_objects` = nombre d'objets réellement modifiés.
[[nodiscard]] VectorOpResult make_smooth_command(const document::Project& project,
                                                 const std::vector<ObjectId>& ids,
                                                 Micrometers tolerance = kDefaultSmoothTolerance);
[[nodiscard]] VectorOpResult make_convert_to_curves_command(const document::Project& project,
                                                            const std::vector<ObjectId>& ids);

// Remodèle le segment `segment` (index du nœud de départ) du chemin `path_index` (0 = extérieur,
// i+1 = trou i) du morceau `set_index` de l'objet `id`, en tirant son point `t` de `delta`
// (µm, repère du document). Commande nulle (avec `error`) si l'objet/segment n'existe pas, si
// rien ne change ou si le résultat introduirait un croisement.
[[nodiscard]] VectorOpResult
make_reshape_segment_command(const document::Project& project, ObjectId id, std::size_t set_index,
                             std::size_t path_index, std::size_t segment, double t, Vec2um delta);

// Transforme les nœuds `selected` du chemin `path_index` du morceau `set_index` de l'objet `id`
// (`geometry::transform_path_set_nodes`, transition douce si `soft_radius_um` > 0). Commande
// nulle (avec `error`) si la cible n'existe pas, si rien ne change ou si un croisement
// apparaîtrait.
[[nodiscard]] VectorOpResult
make_transform_nodes_command(const document::Project& project, ObjectId id, std::size_t set_index,
                             std::size_t path_index, const std::vector<std::size_t>& selected,
                             const geometry::NodeTransform& transform, double soft_radius_um);

// Pose `after` (géométrie complète de l'objet `id` à la fin du coup de pinceau, calculée par
// `geometry::apply_brush_dab`). Commande nulle (avec `error`) si l'objet n'existe pas, si rien
// ne change ou si un contour se croiserait.
[[nodiscard]] VectorOpResult make_brush_stroke_command(const document::Project& project,
                                                       ObjectId id,
                                                       std::vector<geometry::PathSet> after);

} // namespace openstitch::commands
