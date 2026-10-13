// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/vector_object.hpp"

namespace openstitch::document {

// Sous-ensemble autonome d'un document : des objets vectoriels et les objets de
// broderie qui en dépendent (`source_vector` pointe toujours vers un vecteur du
// fragment). C'est l'unité du presse-papiers (HP-OBJ-002) : copié depuis un
// projet, collé dans le même ou dans un autre avec de nouveaux identifiants.
struct ObjectFragment {
    std::vector<VectorObject> vectors;
    std::vector<EmbroideryObject> embroideries;

    [[nodiscard]] bool empty() const { return vectors.empty(); }
};

} // namespace openstitch::document
