// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <algorithm>
#include <cstddef>
#include <numeric>
#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/document/calque.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/colorway.hpp"
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
    std::string machine_id;             // HP-HOOP-002 : profil machine (vide = aucun)
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

    // Calques (HP-OBJ-024), du fond (cousu en premier) vers le dessus. Vide = un seul calque
    // implicite : aucun document existant n'est modifié.
    std::vector<Calque> calques;
    // Calque qui reçoit les formes créées (état d'interface, ni annulable ni enregistré) ;
    // invalide = le calque du fond.
    ObjectId active_calque;

    // Coloris multiples (HP-THR-006). `active_colorway` = indice dans `colorways`, -1 = aucun
    // (document sans coloris : comportement historique). Voir `Colorway`.
    std::vector<Colorway> colorways;
    int active_colorway{-1};

    // Tissu de fond (HP-THR-008), persisté dans le projet.
    FabricSettings fabric;

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

    // ---- Calques (HP-OBJ-024) ----------------------------------------------------------------
    [[nodiscard]] const Calque* findCalque(ObjectId id) const {
        for (const auto& calque : calques) {
            if (calque.id == id) {
                return &calque;
            }
        }
        return nullptr;
    }
    // Rang (0 = fond) du calque `id` ; 0 pour un calque invalide ou inconnu.
    [[nodiscard]] std::size_t calqueRank(ObjectId id) const {
        for (std::size_t i = 0; i < calques.size(); ++i) {
            if (calques[i].id == id) {
                return i;
            }
        }
        return 0;
    }
    // Calque effectif d'une forme : le sien, ou celui du fond si invalide/inconnu ; nullptr si le
    // document n'a aucun calque (calque implicite visible et déverrouillé).
    [[nodiscard]] const Calque* calqueOfShape(ObjectId vectorId) const {
        if (calques.empty()) {
            return nullptr;
        }
        const VectorObject* shape = findObject(vectorId);
        const Calque* own = shape != nullptr ? findCalque(shape->calque) : nullptr;
        return own != nullptr ? own : &calques.front();
    }
    // Rang de couture d'un objet de broderie : celui du calque de sa forme source.
    [[nodiscard]] std::size_t calqueRankOf(const EmbroideryObject& object) const {
        const Calque* calque = calqueOfShape(object.source_vector);
        return calque != nullptr ? calqueRank(calque->id) : 0;
    }
    [[nodiscard]] bool calqueVisible(const EmbroideryObject& object) const {
        const Calque* calque = calqueOfShape(object.source_vector);
        return calque == nullptr || calque->visible;
    }
    [[nodiscard]] bool shapeLockedByCalque(ObjectId vectorId) const {
        const Calque* calque = calqueOfShape(vectorId);
        return calque != nullptr && calque->locked;
    }
    // `ids` (objets de broderie) regroupés par calque : tri STABLE sur le rang de calque, l'ordre
    // relatif d'un même calque est conservé. Garde un ordre calculé (optimisation) compatible
    // avec la clé principale de l'ordre de couture.
    [[nodiscard]] std::vector<ObjectId> groupedByCalque(std::vector<ObjectId> ids) const {
        if (calques.size() > 1) {
            std::stable_sort(ids.begin(), ids.end(), [this](ObjectId a, ObjectId b) {
                const EmbroideryObject* ea = findEmbroidery(a);
                const EmbroideryObject* eb = findEmbroidery(b);
                return (ea != nullptr ? calqueRankOf(*ea) : 0) <
                       (eb != nullptr ? calqueRankOf(*eb) : 0);
            });
        }
        return ids;
    }
    // Ordre de couture réel : les objets triés par rang de calque, l'ordre du document
    // départageant ceux d'un même calque (tri stable). Indices dans `embroidery_objects`.
    [[nodiscard]] std::vector<std::size_t> sewingOrder() const {
        std::vector<std::size_t> order(embroidery_objects.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        if (calques.size() > 1) {
            std::vector<std::size_t> rank(order.size());
            for (std::size_t i = 0; i < order.size(); ++i) {
                rank[i] = calqueRankOf(embroidery_objects[i]);
            }
            std::stable_sort(order.begin(), order.end(),
                             [&](std::size_t a, std::size_t b) { return rank[a] < rank[b]; });
        }
        return order;
    }

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
