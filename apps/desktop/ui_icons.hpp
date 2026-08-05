// SPDX-License-Identifier: Apache-2.0
#pragma once

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
[[nodiscard]] QIcon freeform();
[[nodiscard]] QIcon bezierCurve();
[[nodiscard]] QIcon satinColumn();
[[nodiscard]] QIcon checkmark();
[[nodiscard]] QIcon cancelDraw();

[[nodiscard]] QIcon openImage();
[[nodiscard]] QIcon openProject();
[[nodiscard]] QIcon save();
[[nodiscard]] QIcon undo();
[[nodiscard]] QIcon redo();
[[nodiscard]] QIcon zoomIn();
[[nodiscard]] QIcon zoomOut();
[[nodiscard]] QIcon fit();
[[nodiscard]] QIcon analyze();
[[nodiscard]] QIcon stitches();
[[nodiscard]] QIcon exportDst();
[[nodiscard]] QIcon editPoints();

}  // namespace openstitch::desktop::icons
