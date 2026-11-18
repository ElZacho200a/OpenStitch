// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
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
// E_BLEND_ALREADY_LINKED, E_BLEND_SAME_COLOR, E_BLEND_WIDTH_RANGE, E_BLEND_NO_FRONTIER.
// Un fondu déjà posé ENTRE a et b n'est pas un refus (réédition).
[[nodiscard]] std::optional<Error> boundary_blend_refusal(const document::Project& project,
                                                          ObjectId a, ObjectId b,
                                                          const BoundaryBlendOptions& options = {});

// Pose ou réédite le fondu entre a et b : une commande, un pas d'annulation, aucun changement de
// type, de paramètres ni d'ordre. Erreur codée (cf. `boundary_blend_refusal`) sinon.
[[nodiscard]] Result<std::unique_ptr<commands::ICommand>>
make_boundary_blend_command(const document::Project& project, ObjectId a, ObjectId b,
                            const BoundaryBlendOptions& options);

// Retire le fondu porté par `id` (et le lien de son partenaire s'il pointe vers `id`). Erreur
// E_BLEND_NONE si `id` ne porte aucun lien.
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

[[nodiscard]] std::optional<BoundaryBlendInfo> boundary_blend_info(const document::Project& project,
                                                                   ObjectId id);

} // namespace openstitch::project_ops
