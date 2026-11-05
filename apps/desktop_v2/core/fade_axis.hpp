// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- axe d'un fondu manipulable au canevas : où commence et où finit la transition entre
// les deux fils, déplacée ou tournée par des poignées. Un aperçu de glisser est de la géométrie
// pure (aucune régénération de points) ; la validation est UNE commande annulable
// (`make_edit_gradient_fill_command`). Sans Qt.

#include <array>
#include <cstdint>
#include <optional>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

enum class FadeHandle : std::uint8_t { Start, End, Move, Rotate };

struct FadeAxisView {
    PointMm start;         // début de la transition (fil du fond pur avant ce point)
    PointMm end;           // fin de la transition (fil du dessus pur après ce point)
    PointMm centre;        // poignée de déplacement de tout l'axe
    PointMm rotate;        // poignée de rotation, à 90° de l'axe
    double zoneStart{0.0}; // fraction de l'étendue de la forme où la rampe commence (0..1)
    double zoneEnd{1.0};   // ... et où elle finit
    std::array<std::uint8_t, 3> background{};
    std::array<std::uint8_t, 3> foreground{};
};

// Axe du fondu dont `objectId` est une couche ; rien si l'objet ne porte pas de fondu valide.
[[nodiscard]] std::optional<FadeAxisView> fadeAxisView(const document::Project& project,
                                                       std::uint64_t objectId);

// Axe après glisser de `handle` vers `pointer` (mm modèle), sans toucher au document. `symmetric`
// (Maj) : l'autre extrémité bouge symétriquement. Rien si le geste est refusé (axe dégénéré).
[[nodiscard]] std::optional<FadeAxisView> previewFadeDrag(const document::Project& project,
                                                          std::uint64_t objectId, FadeHandle handle,
                                                          PointMm pointer, bool symmetric);

// Valide le glisser en UNE commande. Refus clair si le moteur refuse le nouveau fondu.
ServiceResult dragFadeHandle(DocumentSession& session, std::uint64_t objectId, FadeHandle handle,
                             PointMm pointer, bool symmetric);

// Phrase décrivant la transition (« Transition de 20 % à 80 % de la forme, 4 mm de long »).
[[nodiscard]] std::string fadeSummary(const FadeAxisView& view);

} // namespace openstitch::desktop_v2
