// SPDX-License-Identifier: Apache-2.0
// HP-AUTO-012 : « Numériser automatiquement un graphisme vectoriel ». Convertit les formes
// vectorielles d'un document (typiquement un SVG importé) qui n'ont encore AUCUN objet de
// broderie, sans repasser par l'image ni la segmentation. Les types (tatami / contour) sont
// choisis par la même classification que `auto_digitize` (`auto_digitize_vectors`).
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::autodigitize {

struct VectorConversion {
    // Objets de broderie à ajouter (identifiants alloués dans `ids`). Aucun nouveau vecteur.
    std::vector<document::EmbroideryObject> embroideries;
    std::size_t considered{0};          // formes sans broderie, soumises à la classification
    std::size_t already_embroidered{0}; // formes ignorées : déjà cousues
    std::size_t tatami{0};
    std::size_t contour{0};
    std::size_t other{0};
    std::vector<std::string> warnings;
};

// Les formes déjà référencées par un objet de broderie sont laissées telles quelles. Erreur si
// aucune forme n'est à convertir. Déterministe : l'ordre suit `project.vector_objects`.
[[nodiscard]] Result<VectorConversion>
convert_vectors_to_embroidery(const document::Project& project, IdGenerator<ObjectId>& ids,
                              const AutoOptions& options);

} // namespace openstitch::autodigitize
