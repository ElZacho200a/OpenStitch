// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_ops/boundary_blend.hpp"

#include <string>
#include <utility>

#include "openstitch/commands/boundary_blend_commands.hpp"
#include "openstitch/document/boundary_blend.hpp"
#include "openstitch/stitch_generation/boundary_blend.hpp"

namespace openstitch::project_ops {

namespace {

Error coded(ErrorCategory category, std::string code, std::string message, std::string hint) {
    return Error{category, std::move(message), {}, std::move(code), std::move(hint)};
}

bool width_ok(Micrometers w) {
    return w.value >= document::kBoundaryBlendMinWidth &&
           w.value <= document::kBoundaryBlendMaxWidth;
}

} // namespace

std::optional<Error> boundary_blend_refusal(const document::Project& project, ObjectId a,
                                            ObjectId b, const BoundaryBlendOptions& options) {
    const auto* za = project.findEmbroidery(a);
    const auto* zb = project.findEmbroidery(b);
    if (za == nullptr || zb == nullptr) {
        return coded(ErrorCategory::UserInput, "E_BLEND_SAME_OBJECT",
                     "Deux zones existantes sont nécessaires",
                     "Sélectionner exactement deux zones remplies voisines.");
    }
    if (a == b) {
        return coded(ErrorCategory::UserInput, "E_BLEND_SAME_OBJECT",
                     "Un fondu à la frontière relie deux zones différentes",
                     "Sélectionner exactement deux zones remplies voisines.");
    }
    if (!document::boundary_blend_fill_supported(*za) ||
        !document::boundary_blend_fill_supported(*zb)) {
        return coded(ErrorCategory::OperationImpossible, "E_BLEND_UNSUPPORTED_FILL",
                     "Le fondu à la frontière demande deux remplissages tatami ou directionnels",
                     "Convertir les zones en tatami ou en directionnel (sans dégradé de densité).");
    }
    if (!za->source_vector.valid() || za->source_vector == zb->source_vector ||
        project.findObject(za->source_vector) == nullptr ||
        project.findObject(zb->source_vector) == nullptr) {
        return coded(ErrorCategory::OperationImpossible, "E_BLEND_SAME_SHAPE",
                     "Les deux zones doivent suivre deux formes distinctes",
                     "Pour fondre deux fils dans une même forme, utiliser un fondu de fils.");
    }
    if (za->rgb == zb->rgb) {
        return coded(ErrorCategory::OperationImpossible, "E_BLEND_SAME_COLOR",
                     "Les deux zones ont la même couleur", "Choisir deux fils différents.");
    }
    if (!width_ok(options.width_a) || !width_ok(options.width_b)) {
        return coded(ErrorCategory::UserInput, "E_BLEND_WIDTH_RANGE",
                     "Largeur de fondu hors limites",
                     "Choisir une demi-bande entre 0,5 et 15 mm de chaque côté.");
    }
    stitch_generation::BoundaryBlendSpec spec;
    spec.width_a = options.width_a;
    spec.width_b = options.width_b;
    spec.curve = options.curve;
    spec.manual_frontier = options.manual_frontier;
    spec.rows = options.rows;
    const auto plan = stitch_generation::plan_boundary_blend(
        stitch_generation::boundary_blend_zone(project, *za),
        stitch_generation::boundary_blend_zone(project, *zb), spec);
    if (!plan) {
        return plan.error();
    }
    return std::nullopt;
}

Result<std::unique_ptr<commands::ICommand>>
make_boundary_blend_command(const document::Project& project, ObjectId a, ObjectId b,
                            const BoundaryBlendOptions& options) {
    if (auto refusal = boundary_blend_refusal(project, a, b, options)) {
        return std::unexpected(std::move(*refusal));
    }
    const bool edit = document::boundary_blend_link(*project.findEmbroidery(a), b) != nullptr;
    return std::make_unique<commands::SetBoundaryBlendLinkCommand>(
        a,
        document::BoundaryBlendLink{b, options.width_a, options.curve, options.manual_frontier,
                                    options.rows},
        b,
        document::BoundaryBlendLink{a, options.width_b, options.curve, options.manual_frontier,
                                    options.rows},
        edit ? "Modifier le fondu à la frontière" : "Fondu à la frontière");
}

Result<std::unique_ptr<commands::ICommand>>
make_remove_boundary_blend_command(const document::Project& project, ObjectId id,
                                   ObjectId partner) {
    const auto* zone = project.findEmbroidery(id);
    if (zone == nullptr || document::boundary_blend_link(*zone, partner) == nullptr) {
        return std::unexpected(
            coded(ErrorCategory::UserInput, "E_BLEND_NONE",
                  "Ces deux zones n'ont pas de fondu à la frontière entre elles",
                  "Sélectionner une zone et la voisine avec laquelle elle fond."));
    }
    // Retire le lien de `id` vers `partner` et, s'il existe, celui de `partner` vers `id` ; les
    // autres fondus des deux zones restent intacts.
    return std::make_unique<commands::SetBoundaryBlendLinkCommand>(
        id, std::nullopt, partner, std::optional<document::BoundaryBlendLink>{},
        "Retirer le fondu à la frontière");
}

Result<std::unique_ptr<commands::ICommand>>
make_remove_boundary_blend_command(const document::Project& project, ObjectId id) {
    const auto* zone = project.findEmbroidery(id);
    if (zone == nullptr || zone->boundary_blends.empty()) {
        return std::unexpected(coded(ErrorCategory::UserInput, "E_BLEND_NONE",
                                     "Cette zone n'a pas de fondu à la frontière",
                                     "Sélectionner une zone qui porte un fondu."));
    }
    if (zone->boundary_blends.size() > 1) {
        return std::unexpected(
            coded(ErrorCategory::UserInput, "E_BLEND_AMBIGUOUS",
                  "Cette zone a plusieurs fondus à la frontière : préciser la voisine",
                  "Indiquer la zone partenaire du fondu à retirer (argument partner)."));
    }
    return make_remove_boundary_blend_command(project, id, zone->boundary_blends.front().partner);
}

std::optional<BoundaryBlendInfo> boundary_blend_info(const document::Project& project, ObjectId id,
                                                     ObjectId partner) {
    const auto* zone = project.findEmbroidery(id);
    const auto* link = zone != nullptr ? document::boundary_blend_link(*zone, partner) : nullptr;
    if (link == nullptr) {
        return std::nullopt;
    }
    BoundaryBlendInfo info;
    info.a = id;
    info.b = partner;
    info.options.width_a = link->width_self;
    info.options.curve = link->curve;
    info.options.manual_frontier = link->manual_frontier;
    info.options.rows = link->rows;
    const auto* other = project.findEmbroidery(partner);
    if (other != nullptr) {
        if (const auto* back = document::boundary_blend_link(*other, id)) {
            info.options.width_b = back->width_self;
        }
    }
    if (!document::find_boundary_blend(project, id, partner) || other == nullptr) {
        return info;
    }
    stitch_generation::BoundaryBlendSpec spec;
    spec.width_a = info.options.width_a;
    spec.width_b = info.options.width_b;
    spec.curve = info.options.curve;
    spec.manual_frontier = info.options.manual_frontier;
    spec.rows = info.options.rows;
    const auto plan = stitch_generation::plan_boundary_blend(
        stitch_generation::boundary_blend_zone(project, *zone),
        stitch_generation::boundary_blend_zone(project, *other), spec);
    if (plan) {
        info.active = true;
        info.frontier = plan->frontier;
    }
    return info;
}

std::vector<BoundaryBlendInfo> boundary_blend_infos(const document::Project& project, ObjectId id) {
    std::vector<BoundaryBlendInfo> infos;
    const auto* zone = project.findEmbroidery(id);
    if (zone == nullptr) {
        return infos;
    }
    for (const auto& link : zone->boundary_blends) { // déjà triés par partenaire
        if (auto info = boundary_blend_info(project, id, link.partner)) {
            infos.push_back(std::move(*info));
        }
    }
    return infos;
}

std::optional<BoundaryBlendInfo> boundary_blend_info(const document::Project& project,
                                                     ObjectId id) {
    auto infos = boundary_blend_infos(project, id);
    if (infos.empty()) {
        return std::nullopt;
    }
    return std::move(infos.front());
}

} // namespace openstitch::project_ops
