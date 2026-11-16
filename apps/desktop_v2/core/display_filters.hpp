// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- filtres d'affichage : « afficher uniquement cette couleur », « uniquement ce type
// de points », « tout afficher ». Ce sont des changements de VISIBILITÉ des objets (pas de calque
// ni de document parallèle) : une seule étape d'annulation chacun, rien n'est supprimé. Sans Qt.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "document_services.hpp"

namespace openstitch::desktop_v2 {

// Cache tous les objets de broderie qui n'ont pas la couleur de fil `rgb` ; affiche ceux qui l'ont.
// Refus clair si aucun objet n'a cette couleur ou si rien ne change.
ServiceResult showOnlyColour(DocumentSession& session, const std::array<std::uint8_t, 3>& rgb);
// Même chose pour un type de points (`stitchTypeId`, ex. « tatami », « auto_satin »).
ServiceResult showOnlyType(DocumentSession& session, const std::string& typeId);
// Rend tous les objets visibles ; refus s'ils le sont déjà.
ServiceResult showAllObjects(DocumentSession& session);

// Groupes de filtrage (V1 : types de points, couleurs de fil) avec leur nombre d'objets et de
// visibles. Ordre de première apparition. Clé : `stitchTypeId` ou « #rrggbb ».
struct FilterGroup {
    std::string key;
    int total{0};
    int visible{0};
};
[[nodiscard]] std::vector<FilterGroup> typeGroups(const document::Project& project);
[[nodiscard]] std::vector<FilterGroup> colourGroups(const document::Project& project);

// Affiche / cache TOUS les objets d'un type ou d'une couleur, sans toucher aux autres (les filtres
// se combinent comme les cases à cocher de la V1). Une étape d'annulation ; refus si rien ne
// change.
ServiceResult setTypeVisible(DocumentSession& session, const std::string& typeId, bool visible);
ServiceResult setColourVisible(DocumentSession& session, const std::array<std::uint8_t, 3>& rgb,
                               bool visible);
// V1 « Toutes / Aucune / Inverser » les couleurs : mode all | none | invert.
ServiceResult setAllColours(DocumentSession& session, const std::string& mode);
// Cache les objets dont la forme fait moins de `minAreaMm2` (taille minimale des zones de la V1).
ServiceResult hideSmallerThan(DocumentSession& session, double minAreaMm2);

// Nombre d'objets cachés (pour griser « Tout afficher » quand il n'y a rien à faire).
[[nodiscard]] std::size_t hiddenObjectCount(const document::Project& project);

} // namespace openstitch::desktop_v2
