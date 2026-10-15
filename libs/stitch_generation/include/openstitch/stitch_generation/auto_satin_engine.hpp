// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/auto_satin/skeleton_satin.hpp"
#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::stitch_generation {

// Paramètres du moteur d'auto-satin déduits des réglages d'un objet du document : UNE seule
// correspondance pour la génération, l'aperçu, l'inspecteur, l'API programmatique et l'export,
// de sorte qu'ils utilisent tous la même méthode (`AutoSatinParams::strategy`) et les mêmes
// réglages. À appeler avec `auto_satin::generate_satin`, jamais `generate_skeleton_satin`.
[[nodiscard]] auto_satin::SkeletonSatinParameters
auto_satin_engine(const document::AutoSatinParams& params);

} // namespace openstitch::stitch_generation
