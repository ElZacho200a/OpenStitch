// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- guides de direction et lignes de rupture du remplissage directionnel : tracer un
// guide (courbe qui oriente le fil) ou une ligne de rupture (découpe la forme en secteurs), puis
// les effacer. Un tatami (ou tout autre remplissage) est converti en directionnel au premier tracé,
// dans la MÊME étape d'annulation. Sans Qt.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "document_services.hpp"
#include "shape_batch.hpp"

namespace openstitch::desktop_v2 {

// Nombre de guides / de lignes de rupture, ou -1 si l'objet n'est pas un remplissage directionnel.
[[nodiscard]] int directionGuideCount(const document::Project& project, std::uint64_t objectId);
[[nodiscard]] int breakLineCount(const document::Project& project, std::uint64_t objectId);

// Courbes centrales utilisées par la stratégie Guided. Les points restent en
// coordonnées modèle pour la surcouche Studio et sont édités par commande annulable.
[[nodiscard]] std::vector<std::vector<PointMm>>
autoSatinGuidePaths(const document::Project& project, std::uint64_t objectId);
ServiceResult moveAutoSatinGuideNode(DocumentSession& session, std::uint64_t objectId,
                                     std::size_t guideIndex, std::size_t nodeIndex, PointMm to);

// Trace un guide (`isBreak == false`, courbe lissée) ou une ligne de rupture (`true`, segments) à
// partir de points en mm modèle. Au moins deux points distincts, dans les limites du motif. L'objet
// doit avoir un contour source si une conversion est nécessaire.
ServiceResult addDirectionGuide(DocumentSession& session, std::uint64_t objectId,
                                const std::vector<PointMm>& points, bool isBreak);

// Efface tous les guides ou toutes les lignes de rupture (une étape d'annulation).
ServiceResult clearDirectionGuides(DocumentSession& session, std::uint64_t objectId,
                                   bool breakLines);

// --- Gestion des guides d'un remplissage directionnel (affichage et retouche au canevas) ------

// Vrai si l'objet peut recevoir des guides de direction : remplissage directionnel, ou tatami doté
// d'un contour source (converti au premier guide).
[[nodiscard]] bool canDirectionGuide(const document::Project& project, std::uint64_t objectId);

// Un guide (ou une ligne de rupture) tel que dessiné : `nodes` = nœuds éditables (positions des
// ancres), `curve` = tracé aplati (Béziers comprises) pour l'affichage et la désignation.
struct FillGuideCurve {
    std::vector<PointMm> nodes;
    std::vector<PointMm> curve;
};
// Guides (`breakLines == false`) ou lignes de rupture d'un remplissage directionnel, dans l'ordre
// du document. Vide si l'objet n'est pas directionnel.
[[nodiscard]] std::vector<FillGuideCurve> fillGuideCurves(const document::Project& project,
                                                          std::uint64_t objectId, bool breakLines);

// Indice du guide (ou de la rupture) dont le tracé passe à moins de `toleranceMm` de `p` ; le plus
// proche en cas d'égalité de tolérance, -1 si aucun.
[[nodiscard]] int fillGuideAt(const document::Project& project, std::uint64_t objectId,
                              bool breakLines, PointMm p, double toleranceMm);

// Déplace le nœud `nodeIndex` du guide `guideIndex` (ses tangentes, relatives, le suivent) : une
// étape d'annulation.
ServiceResult moveFillGuideNode(DocumentSession& session, std::uint64_t objectId, bool breakLines,
                                std::size_t guideIndex, std::size_t nodeIndex, PointMm to);

// Retire UN guide (ou une rupture) : une étape d'annulation.
ServiceResult removeFillGuide(DocumentSession& session, std::uint64_t objectId, bool breakLines,
                              std::size_t guideIndex);

// Remplace les guides par ceux que propose la forme (opération `directional_guides`, axe médian de
// chaque région source) ; un tatami est converti dans la même étape. Ruptures conservées.
ServiceResult autoDirectionGuides(DocumentSession& session, std::uint64_t objectId);

} // namespace openstitch::desktop_v2
