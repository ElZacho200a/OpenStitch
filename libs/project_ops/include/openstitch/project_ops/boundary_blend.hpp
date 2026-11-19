// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "openstitch/commands/command.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/polyline.hpp"

namespace openstitch::project_ops {

// FONDU À LA FRONTIÈRE entre deux zones voisines (docs/studio-fondu-frontiere-spec.md §7) :
// source unique des créations, rééditions et retraits, pour le CLI, le MCP et Studio.
struct BoundaryBlendOptions {
    Micrometers width_a{2'500}; // demi-bande dans la zone A, [0,5 ; 15] mm
    Micrometers width_b{2'500}; // demi-bande dans la zone B, [0,5 ; 15] mm
    document::DensityCurve curve{document::DensityCurve::Linear};
    // Frontière tracée à la main (lot L4), coordonnées modèle ; vide = calculée. Refus codés
    // E_BLEND_FRONTIER_SHORT (moins de deux points ou de 1 mm) et E_BLEND_FRONTIER_FAR (une zone
    // hors de sa demi-bande). Avec une frontière tracée, les zones n'ont pas à se toucher.
    std::vector<Vec2um> manual_frontier;
    // Sens des rangées dans la bande : par zone (défaut), angle commun, ou rangées qui tournent.
    document::BoundaryBlendRows rows{document::BoundaryBlendRows::PerZone};

    bool operator==(const BoundaryBlendOptions&) const = default;
};

// Pourquoi (a, b) ne peut pas recevoir de fondu à la frontière, ou nullopt si c'est possible.
// Codes stables : E_BLEND_SAME_OBJECT, E_BLEND_UNSUPPORTED_FILL, E_BLEND_SAME_SHAPE,
// E_BLEND_SAME_COLOR, E_BLEND_WIDTH_RANGE, E_BLEND_NO_FRONTIER.
// Un fondu déjà posé ENTRE a et b n'est pas un refus (réédition), ni un fondu de a ou de b avec
// une AUTRE zone (une zone peut fondre avec plusieurs voisines, un fondu par couple).
[[nodiscard]] std::optional<Error> boundary_blend_refusal(const document::Project& project,
                                                          ObjectId a, ObjectId b,
                                                          const BoundaryBlendOptions& options = {});

// Pose ou réédite le fondu du couple a-b : une commande, un pas d'annulation, aucun changement de
// type, de paramètres ni d'ordre, les autres fondus de a et de b intacts. Erreur codée (cf.
// `boundary_blend_refusal`) sinon.
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_boundary_blend_command(const document::Project& project, ObjectId a, ObjectId b,
                            const BoundaryBlendOptions& options);

// FONDU MULTI-ZONES : pose un fondu sur CHAQUE couple de zones de `ids` qui partagent une
// frontière (couples éligibles selon `boundary_blend_refusal`, avec `options`). Les couples déjà
// fondus gardent leurs réglages ; les couples refusés (zones qui ne se touchent pas, même fil,
// remplissage non pris en charge…) sont ignorés. Une seule commande, un pas d'annulation. La
// frontière tracée de `options` est ignorée (chaque couple utilise sa frontière calculée).
// Erreurs : E_BLEND_FEW_ZONES (moins de deux zones distinctes), E_BLEND_NO_PAIR (aucun couple
// éligible). Ordre déterministe : couples (a, b) par identifiants croissants.
struct MultiBoundaryBlend {
    std::unique_ptr<commands::ICommand> command;
    std::vector<std::pair<ObjectId, ObjectId>> created; // couples fondus par la commande
    std::size_t kept{0};                                // couples déjà fondus, laissés intacts
};
[[nodiscard]] Result<MultiBoundaryBlend>
make_multi_boundary_blend_command(const document::Project& project, std::vector<ObjectId> ids,
                                  const BoundaryBlendOptions& options);
// Retire le fondu du couple (`id`, `partner`) : le lien de `id` vers `partner` et, s'il existe,
// celui de `partner` vers `id`. Les autres fondus des deux zones restent intacts. Erreur
// E_BLEND_NONE si `id` ne porte aucun lien vers `partner`.
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_remove_boundary_blend_command(const document::Project& project, ObjectId id, ObjectId partner);

// Retire l'UNIQUE fondu porté par `id`. Erreurs E_BLEND_NONE (aucun lien) et E_BLEND_AMBIGUOUS
// (plusieurs fondus : préciser le partenaire avec la surcharge ci-dessus).
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_remove_boundary_blend_command(const document::Project& project, ObjectId id);

// Description d'un fondu posé, vue depuis `id` : `a` = id, `b` = partenaire, largeurs du point de
// vue de `a`. `active` : le lien est valide ET la frontière existe (sinon la génération coud les
// deux zones en aplat). `frontier` : frontière courante (vide si inactive).
struct BoundaryBlendInfo {
    ObjectId a;
    ObjectId b;
    BoundaryBlendOptions options;
    bool active{false};
    std::vector<geometry::Polyline> frontier;
};

// Fondu du couple (`id`, `partner`), nullopt si `id` ne porte aucun lien vers `partner` (un lien
// orphelin ou non réciproque est décrit, avec `active` faux).
[[nodiscard]] std::optional<BoundaryBlendInfo> boundary_blend_info(const document::Project& project,
                                                                   ObjectId id, ObjectId partner);

// Tous les fondus portés par `id`, un par lien, triés par partenaire (`b.value`) croissant.
[[nodiscard]] std::vector<BoundaryBlendInfo> boundary_blend_infos(const document::Project& project,
                                                                  ObjectId id);

// COMPATIBILITÉ (une zone, un fondu) : le PREMIER de `boundary_blend_infos` (plus petit
// partenaire), nullopt si `id` ne porte aucun lien. Préférer la surcharge à partenaire.
[[nodiscard]] std::optional<BoundaryBlendInfo> boundary_blend_info(const document::Project& project,
                                                                   ObjectId id);

} // namespace openstitch::project_ops
