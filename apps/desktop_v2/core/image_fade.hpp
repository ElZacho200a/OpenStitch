// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- « fondu depuis l'image » : les parts des fils sont lues dans l'image de travail du
// projet (`project_ops::image_blend_spec`). Le calcul (`computeImageFade`) ne lit que le projet
// qu'on lui passe : il s'exécute sur un fil de travail, sur un instantané. L'application
// (`commitImageFade`) tourne sur le fil du document et pousse UNE commande annulable. Sans Qt.

#include <cstdint>
#include <string>

#include "document_services.hpp"
#include "openstitch/core/error.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"

namespace openstitch::desktop_v2 {

struct ImageFadeSettings {
    int threads{6};            // 2 à 8
    double cellMm{3.0};        // pas de la grille de parts, 0,5 à 5 mm
    bool continuousRows{true}; // rangées continues par fil : beaucoup moins de coupes
};

// Spec calculée à une révision donnée du document : périmée si le document a changé depuis.
struct ImageFadePlan {
    std::uint64_t objectId{0};
    bool existingFade{false}; // vrai : le fondu existant est réédité, faux : créé
    project_ops::MulticolorGradientSpec spec;
};

// Pourquoi l'action est indisponible (pas d'image, type incompatible) ; vide = disponible.
[[nodiscard]] std::string imageFadeIncompatibility(const document::Project& project,
                                                   std::uint64_t objectId);

// Calcul lourd (lecture de l'image) : sûr sur un fil de travail, ne modifie rien.
[[nodiscard]] Result<ImageFadePlan> computeImageFade(const document::Project& project,
                                                     std::uint64_t objectId,
                                                     const ImageFadeSettings& settings);

// Pousse la commande de création ou de réédition : UN pas d'annulation.
ServiceResult commitImageFade(DocumentSession& session, const ImageFadePlan& plan);

} // namespace openstitch::desktop_v2
