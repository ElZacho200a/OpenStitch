// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- prétraitement de l'image source : luminosité / contraste, retournements, rotations,
// niveaux de gris, netteté, contraste local (CLAHE) et recadrage. Chaque étape est UNE
// `AppendImageOpCommand` annulable ajoutée à la pile `project.ops` ; la segmentation existante est
// invalidée par le moteur (il faut la recalculer). Sans Qt.

#include <string>
#include <vector>

#include "document_services.hpp"

namespace openstitch::desktop_v2 {

enum class PrepKind {
    BrightnessContrast, // a = luminosité, b = contraste (-100..100)
    FlipHorizontal,
    FlipVertical,
    RotateRight, // 90° horaire
    RotateLeft,  // 90° anti-horaire
    Rotate180,
    Grayscale,
    Sharpen, // a = force (0..3)
    Clahe,   // a = limite (1..8)
    Crop,    // x, y, w, h en pixels de l'image COURANTE (après les étapes précédentes)
};

struct PrepRequest {
    PrepKind kind{PrepKind::BrightnessContrast};
    double a{0.0};
    double b{0.0};
    int x{0}, y{0}, w{0}, h{0};
};

// Refus en clair (pas d'image source, valeur hors plage, recadrage hors de l'image). Une étape sans
// effet (luminosité et contraste nuls) est refusée plutôt qu'empilée.
ServiceResult applyImagePrep(DocumentSession& session, const PrepRequest& request);

// Libellés des étapes de la pile, dans l'ordre d'application.
[[nodiscard]] std::vector<std::string> imagePrepNames(const document::Project& project);

// Dimensions de l'image de travail (après la pile), (0, 0) sans image ou si la pile échoue.
struct PixelSize {
    int width{0};
    int height{0};
};
[[nodiscard]] PixelSize workingImageSize(const document::Project& project);

} // namespace openstitch::desktop_v2
