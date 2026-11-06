// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- guides de direction et lignes de rupture du remplissage directionnel : tracer un
// guide (courbe qui oriente le fil) ou une ligne de rupture (découpe la forme en secteurs), puis
// les effacer. Un tatami (ou tout autre remplissage) est converti en directionnel au premier tracé,
// dans la MÊME étape d'annulation. Sans Qt.

#include <cstdint>
#include <vector>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

// Nombre de guides / de lignes de rupture, ou -1 si l'objet n'est pas un remplissage directionnel.
[[nodiscard]] int directionGuideCount(const document::Project& project, std::uint64_t objectId);
[[nodiscard]] int breakLineCount(const document::Project& project, std::uint64_t objectId);

// Trace un guide (`isBreak == false`, courbe lissée) ou une ligne de rupture (`true`, segments) à
// partir de points en mm modèle. Au moins deux points distincts, dans les limites du motif. L'objet
// doit avoir un contour source si une conversion est nécessaire.
ServiceResult addDirectionGuide(DocumentSession& session, std::uint64_t objectId,
                                const std::vector<PointMm>& points, bool isBreak);

// Efface tous les guides ou toutes les lignes de rupture (une étape d'annulation).
ServiceResult clearDirectionGuides(DocumentSession& session, std::uint64_t objectId,
                                   bool breakLines);

} // namespace openstitch::desktop_v2
