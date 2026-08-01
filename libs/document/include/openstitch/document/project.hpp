// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/vector_object.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::document {

// État du document : une image source intacte, une résolution de travail,
// une pile de transformations rejouables et l'éventuelle segmentation de
// l'image de travail. L'image de travail est TOUJOURS recalculée
// (image::apply_pipeline), jamais stockée comme vérité.
// Ce type grandira (calques, objets, palette…) au fil des phases.
struct Project {
    image::Image original;                    // jamais modifiée après l'import
    Millimeters mm_per_px{25.4 / 96.0};       // résolution de travail
    Canvas canvas;                            // cadre de broderie (défaut 100x100 mm)
    std::vector<image::ImageOp> ops;          // pile de prétraitements

    // Segmentation de l'image de travail. Invalidée (remise à nullopt) par
    // toute nouvelle opération de prétraitement.
    std::optional<segmentation::Segmentation> segmentation;

    // Objets vectoriels (Phase 5). Contrairement à la segmentation, ils
    // survivent aux retouches d'image : leur géométrie est indépendante.
    std::vector<VectorObject> vector_objects;

    // Objets de broderie (Phase 6), dans l'ordre de couture.
    std::vector<EmbroideryObject> embroidery_objects;

    IdGenerator<ObjectId> object_ids;  // partagé par tous les types d'objets

    [[nodiscard]] bool hasImage() const { return !original.empty(); }
    [[nodiscard]] VectorObject* findObject(ObjectId id) {
        for (auto& object : vector_objects) {
            if (object.id == id) {
                return &object;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const VectorObject* findObject(ObjectId id) const {
        return const_cast<Project*>(this)->findObject(id);
    }
    [[nodiscard]] EmbroideryObject* findEmbroidery(ObjectId id) {
        for (auto& object : embroidery_objects) {
            if (object.id == id) {
                return &object;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const EmbroideryObject* findEmbroidery(ObjectId id) const {
        return const_cast<Project*>(this)->findEmbroidery(id);
    }
};

}  // namespace openstitch::document
