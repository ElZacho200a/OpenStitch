// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- retouche point à point d'un objet de broderie. Les retouches ne stockent JAMAIS la
// séquence : ce sont des deltas épars (`StitchOverride`) rejoués sur la génération ; le moteur dit
// si l'objet est propre, retouché ou « périmé » (Dirty : la géométrie a changé depuis, les
// retouches ne s'appliquent plus). Chaque action est UNE `ICommand` (`EditStitchPointsCommand`)
// annulable. Sans Qt.

#include <cstdint>
#include <utility>
#include <vector>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

enum class PointEditState { Clean, ManuallyEdited, Dirty };

struct EditablePoint {
    std::size_t index{0}; // indice dans la vue brute de l'objet (domaine des retouches)
    PointMm pos;          // mm « modèle » (Y vers le haut), retouches appliquées
    bool movable{false}; // point cousu réel de la passe supérieure (un saut n'est pas déplaçable)
    bool jump{false};
    bool deleted{false}; // supprimé par une retouche (position = celle générée)
    bool edited{false};  // déplacé / retouché
};

struct PointView {
    PointEditState state{PointEditState::Clean};
    std::vector<EditablePoint> points; // passe supérieure uniquement, dans l'ordre de couture
    std::size_t editedCount{0};        // nombre de points déjà retouchés
};

// Points éditables de l'objet, ou erreur du moteur (génération impossible…).
[[nodiscard]] Result<PointView> stitchPoints(const document::Project& project,
                                             std::uint64_t objectId);

// Déplace les points `index -> nouvelle position` en UNE commande. Refuse : objet périmé (Dirty),
// indice hors de la vue ou non déplaçable, position hors |coord| <= 1000 mm, liste vide.
ServiceResult moveStitchPoints(DocumentSession& session, std::uint64_t objectId,
                               const std::vector<std::pair<std::size_t, PointMm>>& moves);
ServiceResult deleteStitchPoints(DocumentSession& session, std::uint64_t objectId,
                                 const std::vector<std::size_t>& indices);
// Rétablit le point généré d'origine (annule une suppression ou un déplacement).
ServiceResult restoreStitchPoints(DocumentSession& session, std::uint64_t objectId,
                                  const std::vector<std::size_t>& indices);
// Insère un point juste après le point `afterIndex`.
ServiceResult insertStitchPoint(DocumentSession& session, std::uint64_t objectId,
                                std::size_t afterIndex, PointMm pos);
// Efface TOUTES les retouches de l'objet (retour à la génération).
ServiceResult discardStitchEdits(DocumentSession& session, std::uint64_t objectId);

} // namespace openstitch::desktop_v2
