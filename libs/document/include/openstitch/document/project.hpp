// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/document/finishing.hpp"
#include "openstitch/document/imported_design.hpp"
#include "openstitch/document/text_object.hpp"
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
    image::Image original;              // jamais modifiée après l'import
    Millimeters mm_per_px{25.4 / 96.0}; // résolution de travail
    Canvas canvas;                      // cadre de broderie (défaut 100x100 mm)
    std::vector<image::ImageOp> ops;    // pile de prétraitements

    // Segmentation de l'image de travail. Invalidée (remise à nullopt) par
    // toute nouvelle opération de prétraitement.
    std::optional<segmentation::Segmentation> segmentation;

    // Objets vectoriels (Phase 5). Contrairement à la segmentation, ils
    // survivent aux retouches d'image : leur géométrie est indépendante.
    std::vector<VectorObject> vector_objects;

    // Objets de broderie (Phase 6), dans l'ordre de couture.
    std::vector<EmbroideryObject> embroidery_objects;

    // Objets texte (lettrage, HP-TXT-001) : l'intention ; les lettres sont des
    // objets vectoriels (`VectorObject::text_owner`) et de broderie dérivés.
    std::vector<TextObject> text_objects;

    IdGenerator<ObjectId> object_ids; // partagé par tous les types d'objets

    // Finitions de la séquence (coupes, points d'arrêt, points courts),
    // appliquées par `stitch_generation::effective_sequence`.
    SequenceFinishing finishing;

    // AD-04 : design importé depuis un fichier machine (DST aujourd'hui) --
    // donnée source immuable, restituée telle quelle par `generate_sequence`
    // (inscription S2a) à la place de la régénération par objet. Remplace
    // l'ancienne exception desktop `MainWindow::sequenceImported_` (§17).
    // `nullopt` = comportement historique inchangé (régénération normale
    // depuis `embroidery_objects`).
    std::optional<ImportedDesign> imported_design;

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
    [[nodiscard]] TextObject* findText(ObjectId id) {
        for (auto& object : text_objects) {
            if (object.id == id) {
                return &object;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const TextObject* findText(ObjectId id) const {
        return const_cast<Project*>(this)->findText(id);
    }
    // Objet texte propriétaire d'un objet de broderie (via son vecteur source), ou nullptr.
    [[nodiscard]] const TextObject* textOwnerOf(const EmbroideryObject& object) const {
        const VectorObject* vector = findObject(object.source_vector);
        if (vector == nullptr || !vector->text_owner) {
            return nullptr;
        }
        return findText(*vector->text_owner);
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

} // namespace openstitch::document
