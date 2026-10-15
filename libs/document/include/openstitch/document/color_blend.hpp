// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include "openstitch/document/project.hpp"

namespace openstitch::document {

struct ColorBlendLayers {
    ObjectId background;
    ObjectId foreground;

    bool operator==(const ColorBlendLayers&) const = default;
};

// Les rôles viennent des liens, pas de l'ordre de couture, des couleurs ni des
// gradients. Une suppression/conversion peut laisser une métadonnée historique :
// elle ne se résout plus tant que les deux couches et leur source ne sont pas valides.
[[nodiscard]] inline std::optional<ColorBlendLayers> find_color_blend(const Project& project,
                                                                      ObjectId id) {
    const auto* layer = project.findEmbroidery(id);
    if (!id.valid() || layer == nullptr || !layer->color_blend ||
        !(layer->is_tatami() || layer->is_directional())) {
        return std::nullopt;
    }
    const auto& link = *layer->color_blend;
    const auto* partner = project.findEmbroidery(link.partner);
    if (!link.partner.valid() || link.partner == id || partner == nullptr ||
        !partner->color_blend || partner->color_blend->partner != id ||
        partner->color_blend->foreground == link.foreground ||
        !(partner->is_tatami() || partner->is_directional()) || !layer->source_vector.valid() ||
        partner->source_vector != layer->source_vector ||
        project.findObject(layer->source_vector) == nullptr) {
        return std::nullopt;
    }
    return link.foreground ? ColorBlendLayers{partner->id, layer->id}
                           : ColorBlendLayers{layer->id, partner->id};
}

} // namespace openstitch::document
