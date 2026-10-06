// SPDX-License-Identifier: Apache-2.0
#pragma once

// Détection géométrique de sélection (lot L5-T4b) : fonctions PURES sur le
// document (aucun état, aucune dépendance à MainWindow ni à QtWidgets), donc
// testables sans fenêtre. Repère : scène du canevas (mm, Y vers le bas), le
// même que celui du dessin des objets et des signaux de CanvasView.

#include <QPainterPath>
#include <QPointF>
#include <QRectF>

#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop {

// Contour Qt (scène, mm, Y bas) d'un objet vectoriel : cubiques de Bézier pour
// les segments courbes, remplissage pair-impair. Même contour pour l'affichage
// et pour toute détection.
[[nodiscard]] QPainterPath objectScenePath(const document::VectorObject& object);

// Objets vectoriels VISIBLES sous le point, du plus haut (dessiné en dernier) au
// plus bas. Ordre déterministe (celui du document, inversé).
[[nodiscard]] std::vector<ObjectId> objectsAtPointMm(const document::Project& project,
                                                     QPointF posMm);

// Objets vectoriels VISIBLES retenus par un rectangle (scène, mm). Fenêtre
// (crossing == false) : la boîte englobante de l'objet est entièrement dans le
// rectangle. Croisement : le contour/remplissage coupe le rectangle. Résultat
// trié par ObjectId croissant (indépendant de l'ordre de dessin).
[[nodiscard]] std::vector<ObjectId>
objectsInRectangleMm(const document::Project& project, const QRectF& rectMm, bool crossing);

} // namespace openstitch::desktop
