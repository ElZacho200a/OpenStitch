// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- axe d'un fondu manipulable au canevas : où commence et où finit la transition entre
// les deux fils, déplacée ou tournée par des poignées. Un aperçu de glisser est de la géométrie
// pure (aucune régénération de points) ; la validation est UNE commande annulable
// (`make_edit_gradient_fill_command`). Sans Qt.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

enum class FadeHandle : std::uint8_t { Start, End, Move, Rotate };

// Courbe de transition de la densité entre les deux fils (mêmes formules que le moteur) : le profil
// 0 → 1 le long de l'axe, de « fond pur » à « dessus pur ».
enum class FadeCurve : std::uint8_t {
    Linear = 0,
    Smooth = 1,
    Steep = 2,
    EaseIn = 3,
    EaseOut = 4,
    Convex = 5,
    Concave = 6,
    ConvexConcave = 7,
    ConcaveConvex = 8,
};

[[nodiscard]] std::string fadeCurveLabel(FadeCurve curve);
// Valeur de la courbe en t ∈ [0 ; 1] (0 → 0, 1 → 1, monotone).
[[nodiscard]] double fadeCurveValue(FadeCurve curve, double t);
// `samples` valeurs régulièrement espacées de t = 0 à t = 1 : de quoi tracer la courbe.
[[nodiscard]] std::vector<double> fadeCurveProfile(FadeCurve curve, int samples);

struct FadeAxisView {
    PointMm start;         // début de la transition (fil du fond pur avant ce point)
    PointMm end;           // fin de la transition (fil du dessus pur après ce point)
    PointMm centre;        // poignée de déplacement de tout l'axe
    PointMm rotate;        // poignée de rotation, à 90° de l'axe
    double zoneStart{0.0}; // fraction de l'étendue de la forme où la rampe commence (0..1)
    double zoneEnd{1.0};   // ... et où elle finit
    std::array<std::uint8_t, 3> background{};
    std::array<std::uint8_t, 3> foreground{};
    FadeCurve curve{FadeCurve::Linear};
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

// Inverse le sens du fondu (début <-> fin, écarts échangés) en UNE commande annulable ; refus si
// l'objet ne porte pas de fondu.
ServiceResult reverseFade(DocumentSession& session, std::uint64_t objectId);

// Change la courbe de transition en UNE commande annulable ; refus si l'objet ne porte pas de
// fondu.
ServiceResult setFadeCurve(DocumentSession& session, std::uint64_t objectId, FadeCurve curve);

// Phrase décrivant la transition (« Transition de 20 % à 80 % de la forme, 4 mm de long »).
[[nodiscard]] std::string fadeSummary(const FadeAxisView& view);

} // namespace openstitch::desktop_v2
