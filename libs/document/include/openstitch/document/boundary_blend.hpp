// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "openstitch/document/project.hpp"

namespace openstitch::document {

// Bornes d'une demi-bande de fondu à la frontière (docs/studio-fondu-frontiere-spec.md §4.1).
inline constexpr std::int32_t kBoundaryBlendMinWidth = 500;    // 0,5 mm
inline constexpr std::int32_t kBoundaryBlendMaxWidth = 15'000; // 15 mm

// Paire résolue d'un fondu à la frontière, vue depuis `self`.
struct BoundaryBlendPair {
    ObjectId self;
    ObjectId partner;
    bool operator==(const BoundaryBlendPair&) const = default;
};

// Le remplissage `object` peut-il porter un fondu à la frontière ? Tatami, ou directionnel sans
// dégradé de densité (les deux masques ne se composent pas).
[[nodiscard]] inline bool boundary_blend_fill_supported(const EmbroideryObject& object) {
    if (object.is_tatami()) {
        return true;
    }
    if (const auto* d = std::get_if<DirectionalFillParams>(&object.params)) {
        return !d->density_gradient.has_value();
    }
    return false;
}

// Résout le fondu à la frontière porté par `id`. Les liens doivent être réciproques, les deux
// objets des remplissages pris en charge, sur deux formes sources DISTINCTES et existantes, avec
// la même courbe et des largeurs dans les bornes. Sinon nullopt : le lien reste une métadonnée
// sans effet sur la génération (lien orphelin après suppression, conversion, édition à la main).
[[nodiscard]] inline std::optional<BoundaryBlendPair> find_boundary_blend(const Project& project,
                                                                          ObjectId id) {
    const auto* self = project.findEmbroidery(id);
    if (!id.valid() || self == nullptr || !self->boundary_blend) {
        return std::nullopt;
    }
    const auto& link = *self->boundary_blend;
    const auto* partner = project.findEmbroidery(link.partner);
    const auto widthOk = [](Micrometers w) {
        return w.value >= kBoundaryBlendMinWidth && w.value <= kBoundaryBlendMaxWidth;
    };
    if (!link.partner.valid() || link.partner == id || partner == nullptr ||
        !partner->boundary_blend || partner->boundary_blend->partner != id ||
        partner->boundary_blend->curve != link.curve || !widthOk(link.width_self) ||
        !widthOk(partner->boundary_blend->width_self) || !boundary_blend_fill_supported(*self) ||
        !boundary_blend_fill_supported(*partner) || !self->source_vector.valid() ||
        !partner->source_vector.valid() || self->source_vector == partner->source_vector ||
        project.findObject(self->source_vector) == nullptr ||
        project.findObject(partner->source_vector) == nullptr) {
        return std::nullopt;
    }
    return BoundaryBlendPair{id, link.partner};
}

} // namespace openstitch::document
