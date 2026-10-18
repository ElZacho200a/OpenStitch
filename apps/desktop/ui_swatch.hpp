// SPDX-License-Identifier: Apache-2.0
#pragma once

// HP-UX-043 : fabrique unique des pastilles de couleur. Une pastille est dessinée dans un pixmap à
// la taille PHYSIQUE (taille logique x rapport de pixels de l'écran) et porte ce rapport : elle
// reste nette à 125 %, 150 %, 200 % (QT_SCALE_FACTOR, écrans mixtes) au lieu d'être agrandie floue.

#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <cmath>

namespace openstitch::desktop {

// `logicalSize` : côté en pixels logiques. `dpr` <= 0 : rapport de pixels de l'application.
[[nodiscard]] inline QPixmap makeSwatch(const QColor& color, int logicalSize = 16,
                                        qreal dpr = 0.0) {
    if (dpr <= 0.0) {
        dpr = qApp != nullptr ? qApp->devicePixelRatio() : 1.0;
    }
    dpr = std::max(1.0, dpr);
    logicalSize = std::max(2, logicalSize);
    const int physical = static_cast<int>(std::ceil(logicalSize * dpr));
    QPixmap pixmap(physical, physical);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(0.5, 0.5, logicalSize - 1.0, logicalSize - 1.0);
    p.setBrush(color);
    // Liseré à deux tons : lisible sur fond clair comme sombre, quelle que soit la teinte.
    p.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
    p.drawRoundedRect(r, 3.0, 3.0);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 110), 1.0));
    p.drawRoundedRect(r.adjusted(1.0, 1.0, -1.0, -1.0), 2.0, 2.0);
    return pixmap;
}

} // namespace openstitch::desktop
