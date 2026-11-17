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
    const auto linkedElsewhere = [&](const document::EmbroideryObject& z, ObjectId other) {
        return z.boundary_blend && z.boundary_blend->partner != other &&
               document::find_boundary_blend(project, z.id).has_value();
    };
    if (linkedElsewhere(*za, b) || linkedElsewhere(*zb, a)) {
        return coded(ErrorCategory::OperationImpossible, "E_BLEND_ALREADY_LINKED",
                     "Une des zones a déjà un fondu à la frontière avec une autre zone",
                     "Retirer d'abord ce fondu (une zone ne fond qu'avec une seule voisine).");
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
    const bool edit = [&] {
        const auto* za = project.findEmbroidery(a);
        return za->boundary_blend && za->boundary_blend->partner == b;
    }();
    return std::make_unique<commands::SetBoundaryBlendLinkCommand>(
        a, document::BoundaryBlendLink{b, options.width_a, options.curve}, b,
        document::BoundaryBlendLink{a, options.width_b, options.curve},
        edit ? "Modifier le fondu à la frontière" : "Fondu à la frontière");
}

Result<std::unique_ptr<commands::ICommand>>
make_remove_boundary_blend_command(const document::Project& project, ObjectId id) {
    const auto* zone = project.findEmbroidery(id);
    if (zone == nullptr || !zone->boundary_blend) {
        return std::unexpected(coded(ErrorCategory::UserInput, "E_BLEND_NONE",
                                     "Cette zone n'a pas de fondu à la frontière",
                                     "Sélectionner une zone qui porte un fondu."));
    }
    const ObjectId partner = zone->boundary_blend->partner;
    const auto* other = project.findEmbroidery(partner);
    // Le lien du partenaire n'est retiré que s'il pointe bien vers `id`.
    const bool reciprocal =
        other != nullptr && other->boundary_blend && other->boundary_blend->partner == id;
    return std::make_unique<commands::SetBoundaryBlendLinkCommand>(
        id, std::nullopt, reciprocal ? partner : ObjectId{},
        std::optional<document::BoundaryBlendLink>{}, "Retirer le fondu à la frontière");
}

std::optional<BoundaryBlendInfo> boundary_blend_info(const document::Project& project,
                                                     ObjectId id) {
    const auto* zone = project.findEmbroidery(id);
    if (zone == nullptr || !zone->boundary_blend) {
        return std::nullopt;
    }
    BoundaryBlendInfo info;
    info.a = id;
    info.b = zone->boundary_blend->partner;
    info.options.width_a = zone->boundary_blend->width_self;
    info.options.curve = zone->boundary_blend->curve;
    const auto* other = project.findEmbroidery(info.b);
    if (other != nullptr && other->boundary_blend) {
        info.options.width_b = other->boundary_blend->width_self;
    }
    if (!document::find_boundary_blend(project, id) || other == nullptr) {
        return info;
    }
    stitch_generation::BoundaryBlendSpec spec;
    spec.width_a = info.options.width_a;
    spec.width_b = info.options.width_b;
    spec.curve = info.options.curve;
    const auto plan = stitch_generation::plan_boundary_blend(
        stitch_generation::boundary_blend_zone(project, *zone),
        stitch_generation::boundary_blend_zone(project, *other), spec);
    if (plan) {
        info.active = true;
        info.frontier = plan->frontier;
    }
    return info;
}

} // namespace openstitch::project_ops
