// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpenStitch V2 visual identity. Presentation only: no machine or thread colors are modified.
#include <QString>

class QApplication;

namespace openstitch::desktop_v2 {

enum class StudioScheme { Light, Dark };

// Tokens are shared by the shell, toolbars and dock controls. Keep actual thread swatches
// data-driven; these colors are for interaction feedback and neutral surfaces only.
struct StudioColors {
    QString background;
    QString surface;
    QString surfaceRaised;
    QString border;
    QString text;
    QString muted;
    QString accent;
    QString accentSoft;
};

[[nodiscard]] StudioColors studioColors(StudioScheme scheme);
// `paddingScale` multiplie les marges intérieures (propriétés `padding`) de la feuille de style :
// c'est la « densité d'interface » (1,0 = historique). Bornée à [0,5 ; 2].
[[nodiscard]] QString studioStyleSheet(StudioScheme scheme, double paddingScale = 1.0);
// mode: "system", "light", "dark". Follows Qt's current color scheme in system mode.
void applyStudioTheme(QApplication& app, const QString& mode, double paddingScale = 1.0);

} // namespace openstitch::desktop_v2
