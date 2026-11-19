// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <vector>

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

// Lien de fondu porté par `object` vers `partner` (au plus un par partenaire), nullptr sinon.
[[nodiscard]] inline const BoundaryBlendLink* boundary_blend_link(const EmbroideryObject& object,
                                                                  ObjectId partner) {
    const auto it = std::ranges::find_if(
        object.boundary_blends, [partner](const auto& link) { return link.partner == partner; });
    return it == object.boundary_blends.end() ? nullptr : &*it;
}

// Pose (`link` non vide, dont `partner` est imposé) ou retire (`nullopt`) le lien de `links` vers
// `partner`, sans toucher aux autres liens, et garde la liste triée par `partner.value`.
inline void set_boundary_blend_link(std::vector<BoundaryBlendLink>& links, ObjectId partner,
                                    std::optional<BoundaryBlendLink> link) {
    std::erase_if(links, [partner](const auto& l) { return l.partner == partner; });
    if (link) {
        link->partner = partner;
        const auto at = std::ranges::find_if(
            links, [partner](const auto& l) { return l.partner.value > partner.value; });
        links.insert(at, std::move(*link));
    }
}

// Trie `links` par `partner.value` croissant (ordre canonique du document).
inline void sort_boundary_blend_links(std::vector<BoundaryBlendLink>& links) {
    std::ranges::stable_sort(links, {}, [](const auto& l) { return l.partner.value; });
}

// Remappe les partenaires de `links` (copie, insertion de motif) : un lien dont le partenaire
// n'est pas dans `remap` est retiré, jamais conservé vers l'original. La liste reste triée.
inline void remap_boundary_blend_links(std::vector<BoundaryBlendLink>& links,
                                       const std::map<ObjectId, ObjectId>& remap) {
    std::vector<BoundaryBlendLink> kept;
    kept.reserve(links.size());
    for (auto& link : links) {
        if (const auto it = remap.find(link.partner); it != remap.end()) {
            link.partner = it->second;
            kept.push_back(std::move(link));
        }
    }
    sort_boundary_blend_links(kept);
    links = std::move(kept);
}

// Résout le fondu à la frontière du couple (`id`, `partner`). Les liens doivent être réciproques
// (lien de `id` vers `partner` ET lien de `partner` vers `id`), les deux objets des remplissages
// pris en charge, sur deux formes sources DISTINCTES et existantes, avec la même courbe, le même
// sens des rangées, la même frontière tracée (vide ou au moins deux points) et des largeurs dans
// les bornes. Sinon nullopt : le lien reste une métadonnée sans effet sur la génération (lien
// orphelin après suppression, conversion, édition à la main).
[[nodiscard]] inline std::optional<BoundaryBlendPair>
find_boundary_blend(const Project& project, ObjectId id, ObjectId partner) {
    const auto* self = project.findEmbroidery(id);
    if (!id.valid() || !partner.valid() || partner == id || self == nullptr) {
        return std::nullopt;
    }
    const auto* other = project.findEmbroidery(partner);
    if (other == nullptr) {
        return std::nullopt;
    }
    const auto* link = boundary_blend_link(*self, partner);
    const auto* back = boundary_blend_link(*other, id);
    const auto widthOk = [](Micrometers w) {
        return w.value >= kBoundaryBlendMinWidth && w.value <= kBoundaryBlendMaxWidth;
    };
    if (link == nullptr || back == nullptr || back->curve != link->curve ||
        back->rows != link->rows || back->manual_frontier != link->manual_frontier ||
        link->manual_frontier.size() == 1 || !widthOk(link->width_self) ||
        !widthOk(back->width_self) || !boundary_blend_fill_supported(*self) ||
        !boundary_blend_fill_supported(*other) || !self->source_vector.valid() ||
        !other->source_vector.valid() || self->source_vector == other->source_vector ||
        project.findObject(self->source_vector) == nullptr ||
        project.findObject(other->source_vector) == nullptr) {
        return std::nullopt;
    }
    return BoundaryBlendPair{id, partner};
}

// Tous les fondus valides de `id` (un par voisine), triés par `partner.value` croissant.
[[nodiscard]] inline std::vector<BoundaryBlendPair> find_boundary_blends(const Project& project,
                                                                         ObjectId id) {
    std::vector<BoundaryBlendPair> pairs;
    const auto* self = project.findEmbroidery(id);
    if (self == nullptr) {
        return pairs;
    }
    for (const auto& link : self->boundary_blends) {
        if (auto pair = find_boundary_blend(project, id, link.partner)) {
            pairs.push_back(*pair);
        }
    }
    std::ranges::sort(pairs, {}, [](const BoundaryBlendPair& p) { return p.partner.value; });
    return pairs;
}

} // namespace openstitch::document
