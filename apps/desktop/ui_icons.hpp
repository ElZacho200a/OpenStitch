// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QIcon>

// Jeu d'icônes monochromes dessinées au QPainter (aucune dépendance externe,
// licence-safe). Style unique : trait fin, teinte neutre lisible en clair comme
// en sombre. Chaque icône reste accompagnée d'un libellé/infobulle côté widget.
namespace openstitch::desktop::icons {

[[nodiscard]] QIcon select();
[[nodiscard]] QIcon pan();
[[nodiscard]] QIcon rect();
[[nodiscard]] QIcon drawRect();
[[nodiscard]] QIcon ellipse();
[[nodiscard]] QIcon polygon();
[[nodiscard]] QIcon regularPolygon();
[[nodiscard]] QIcon freeform();
[[nodiscard]] QIcon knife();
[[nodiscard]] QIcon bezierCurve();
[[nodiscard]] QIcon satinColumn();
[[nodiscard]] QIcon checkmark();
[[nodiscard]] QIcon cancelDraw();

[[nodiscard]] QIcon newProject();
[[nodiscard]] QIcon openImage();
[[nodiscard]] QIcon openProject();
[[nodiscard]] QIcon save();
[[nodiscard]] QIcon undo();
[[nodiscard]] QIcon redo();
[[nodiscard]] QIcon zoomIn();
[[nodiscard]] QIcon zoomOut();
[[nodiscard]] QIcon fit();
[[nodiscard]] QIcon analyze();
[[nodiscard]] QIcon aiSegment();
[[nodiscard]] QIcon stitches();
[[nodiscard]] QIcon exportDst();
[[nodiscard]] QIcon editPoints();

// Icône de l'application (aiguille et fil), nette de 16 à 256 px.
[[nodiscard]] QIcon appIcon();

// Pastille de couleur pour listes/étiquettes : dessinée à la densité de pixels réelle de
// l'écran (nette en HiDPI) avec un liseré neutre, pour qu'une couleur proche du fond (blanc
// en thème clair, gris foncé en thème sombre) reste repérable.
[[nodiscard]] QIcon colorSwatch(const QColor& fill, int logicalSize = 12);

} // namespace openstitch::desktop::icons
