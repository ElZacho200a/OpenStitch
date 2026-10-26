// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <functional>

class QPrinter;
class QWidget;

namespace openstitch::desktop {

// HP-PROD-004 : aperçu avant impression GÉNÉRAL. Tout document imprimable (fiche de
// production, gabarit 1:1, …) fournit une fonction de dessin `paint(QPrinter*)` ; cette
// fonction ouvre l'aperçu paginé de Qt (zoom, navigation, orientation, impression depuis
// l'aperçu) sur l'imprimante donnée. Renvoie vrai si l'utilisateur a validé l'aperçu.
bool showPrintPreview(QWidget* parent, const QString& title, QPrinter& printer,
                      const std::function<void(QPrinter*)>& paint);

// Impression directe après le dialogue système. Renvoie vrai si l'impression a eu lieu.
bool printWithDialog(QWidget* parent, const QString& title, QPrinter& printer,
                     const std::function<void(QPrinter*)>& paint);

} // namespace openstitch::desktop
