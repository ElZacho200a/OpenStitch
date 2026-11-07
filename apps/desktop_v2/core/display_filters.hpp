// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- filtres d'affichage : « afficher uniquement cette couleur », « uniquement ce type
// de points », « tout afficher ». Ce sont des changements de VISIBILITÉ des objets (pas de calque
// ni de document parallèle) : une seule étape d'annulation chacun, rien n'est supprimé. Sans Qt.

#include <array>
#include <cstdint>
#include <string>

#include "document_services.hpp"

namespace openstitch::desktop_v2 {

// Cache tous les objets de broderie qui n'ont pas la couleur de fil `rgb` ; affiche ceux qui l'ont.
// Refus clair si aucun objet n'a cette couleur ou si rien ne change.
ServiceResult showOnlyColour(DocumentSession& session, const std::array<std::uint8_t, 3>& rgb);
// Même chose pour un type de points (`stitchTypeId`, ex. « tatami », « auto_satin »).
ServiceResult showOnlyType(DocumentSession& session, const std::string& typeId);
// Rend tous les objets visibles ; refus s'ils le sont déjà.
ServiceResult showAllObjects(DocumentSession& session);

// Nombre d'objets cachés (pour griser « Tout afficher » quand il n'y a rien à faire).
[[nodiscard]] std::size_t hiddenObjectCount(const document::Project& project);

} // namespace openstitch::desktop_v2
