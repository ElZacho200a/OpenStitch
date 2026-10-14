// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Prépare deux couches sans allouer d'id, modifier leurs métadonnées ni retoucher
// leurs overrides. Le lien relationnel est posé séparément par une commande.
// `complementary` : rampes de densité opposées, sans sous-couche sur aucun fil.
// Sinon : fond uniforme inchangé, dessus sans sous-couche, interpolation reçue.
// nullopt pour une forme vide/dégénérée ou des paramètres de remplissage invalides.
[[nodiscard]] std::optional<std::pair<document::EmbroideryObject, document::EmbroideryObject>>
make_color_blend_layers(const document::EmbroideryObject& background,
                        const document::EmbroideryObject& foreground,
                        const std::vector<geometry::PathSet>& shape,
                        const document::DensityGradient& gradient, bool complementary);

} // namespace openstitch::stitch_generation
