// SPDX-License-Identifier: Apache-2.0
// Catalogue des types de points proposés dans l'IHM : une seule numérotation pour
// l'inspecteur, les menus contextuels, la barre contextuelle et MainWindow::setStitchType.
#pragma once

#include <QCoreApplication>
#include <QString>

#include "openstitch/document/embroidery_object.hpp"

namespace openstitch::desktop {

// 0 contour, 1 tatami, 2 satin, 3 directionnel, 4 blanket, 5 motif le long du contour,
// 6 remplissage à motif, 7 concentrique, 8 radial, 9 spirale, 10 croisé.
inline constexpr int kStitchTypeCount = 11;

// Nombre de types ayant un bouton direct (les suivants passent par le bouton « Plus »).
inline constexpr int kStitchTypeDirectButtons = 4;

[[nodiscard]] inline int stitchTypeIndexOf(const document::EmbroideryObject& object) {
    return object.is_tatami()                            ? 1
           : object.is_satin() || object.is_auto_satin() ? 2
           : object.is_directional()                     ? 3
           : object.is_blanket()                         ? 4
           : object.is_motif_run()                       ? 5
           : object.is_motif_fill()                      ? 6
           : object.is_contour_fill()                    ? 7
           : object.is_radial_fill()                     ? 8
           : object.is_spiral_fill()                     ? 9
           : object.is_crosshatch_fill()                 ? 10
                                                         : 0;
}

// Libellé traduit du type (défini dans stitch_type_catalog.cpp pour que lupdate l'extraie).
[[nodiscard]] QString stitchTypeLabel(int type);

} // namespace openstitch::desktop
