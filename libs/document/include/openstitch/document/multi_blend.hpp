// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <optional>
#include <vector>

#include "openstitch/document/project.hpp"

namespace openstitch::document {

// Les liens du groupe sont validés ensemble : pas de trous dans les indices,
// pas de mélange de formes, et chaque couche doit produire du remplissage.
// Les liens orphelins restent des métadonnées sans effet sur la génération.
struct MultiBlendLayers {
    ObjectId group;
    std::vector<ObjectId> layers; // ordre des fils, de l'arrière vers l'avant
    bool complementary{true};
};

[[nodiscard]] inline std::optional<MultiBlendLayers> find_multi_blend(const Project& project,
                                                                      ObjectId id) {
    const auto* selected = project.findEmbroidery(id);
    if (selected == nullptr || !selected->multi_blend) {
        return std::nullopt;
    }
    const auto tag = *selected->multi_blend;
    if (!tag.group.valid() || tag.count < 2 || tag.count > 8 || !selected->source_vector.valid() ||
        project.findObject(selected->source_vector) == nullptr) {
        return std::nullopt;
    }
    MultiBlendLayers result;
    result.group = tag.group;
    result.complementary = tag.complementary;
    result.layers.resize(tag.count);
    int found = 0;
    for (const auto& member : project.embroidery_objects) {
        if (!member.multi_blend || member.multi_blend->group != tag.group) {
            continue;
        }
        const auto& link = *member.multi_blend;
        if (link.count != tag.count || link.index >= tag.count ||
            link.complementary != tag.complementary ||
            member.source_vector != selected->source_vector ||
            !(member.is_tatami() || member.is_directional() || member.is_satin()) ||
            result.layers[link.index].valid() || member.color_blend) {
            return std::nullopt;
        }
        result.layers[link.index] = member.id;
        ++found;
    }
    if (found != tag.count || result.layers.front() != tag.group ||
        std::any_of(result.layers.begin(), result.layers.end(),
                    [](ObjectId layer) { return !layer.valid(); })) {
        return std::nullopt;
    }
    return result;
}

// Groupe d'un fondu multicolore AUTONOME (mission 2026-11-14) : `anchor` est un véritable
// `EmbroideryObject` (`MulticolorGradientParams`), jamais `layers.front()` comme dans
// `find_multi_blend` -- la palette et la méthode vivent sur l'ancre, indépendamment des couches
// physiques. `id` peut être l'ancre OU n'importe quelle couche ; résolution identique.
struct GradientGroupLayers {
    ObjectId anchor;
    std::vector<ObjectId> layers; // ordre des fils, de l'arrière vers l'avant

    bool operator==(const GradientGroupLayers&) const = default;
};

[[nodiscard]] inline std::optional<GradientGroupLayers> find_gradient_group(const Project& project,
                                                                            ObjectId id) {
    const auto* object = project.findEmbroidery(id);
    if (!id.valid() || object == nullptr) {
        return std::nullopt;
    }
    ObjectId anchorId;
    if (object->is_multicolor_gradient()) {
        anchorId = object->id;
    } else if (object->multi_blend) {
        anchorId = object->multi_blend->group;
    } else {
        return std::nullopt;
    }
    const auto* anchor = project.findEmbroidery(anchorId);
    if (anchor == nullptr || !anchor->is_multicolor_gradient() || !anchor->source_vector.valid() ||
        project.findObject(anchor->source_vector) == nullptr) {
        return std::nullopt;
    }
    const auto& params = std::get<MulticolorGradientParams>(anchor->params);
    const std::size_t n = params.palette.size();
    if (n < 2 || n > 8) {
        return std::nullopt;
    }
    GradientGroupLayers result;
    result.anchor = anchorId;
    result.layers.assign(n, ObjectId{});
    std::size_t found = 0;
    for (const auto& member : project.embroidery_objects) {
        if (!member.multi_blend || member.multi_blend->group != anchorId) {
            continue;
        }
        const auto& link = *member.multi_blend;
        if (link.count != n || link.index >= n || result.layers[link.index].valid() ||
            member.source_vector != anchor->source_vector ||
            !(member.is_tatami() || member.is_directional() || member.is_satin())) {
            return std::nullopt;
        }
        result.layers[link.index] = member.id;
        ++found;
    }
    if (found != n || std::any_of(result.layers.begin(), result.layers.end(),
                                  [](ObjectId layer) { return !layer.valid(); })) {
        return std::nullopt;
    }
    return result;
}

} // namespace openstitch::document
