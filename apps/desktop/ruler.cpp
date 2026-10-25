// SPDX-License-Identifier: Apache-2.0
#include "ruler.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "canvas_view.hpp"
#include "length_units.hpp"

namespace openstitch::desktop {

namespace {

// Pas d'étiquette, en unité d'affichage (mm ou pouces) ; `pxPerUnit` = pixels par unité.
double niceLabelStep(double pxPerUnit, length_units::Unit unit) {
    static constexpr double stepsMm[] = {0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500};
    static constexpr double stepsIn[] = {0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 20, 50};
    const auto& steps = unit == length_units::Unit::Inches ? stepsIn : stepsMm;
    for (const double s : steps) {
        if (s * pxPerUnit >= 45.0) { // au moins 45 px entre deux étiquettes
            return s;
        }
    }
    return 1000.0;
}

QString formatTick(double value, length_units::Unit unit) {
    if (unit == length_units::Unit::Inches) {
        return QString::number(value, 'f', 2);
    }
    return (std::abs(value - std::round(value)) < 1e-9) ? QString::number(std::lround(value))
                                                        : QString::number(value, 'f', 1);
}

} // namespace

Ruler::Ruler(Qt::Orientation orientation, CanvasView* view, QWidget* parent)
    : QWidget(parent), orientation_(orientation), view_(view) {
    connect(view_, &CanvasView::viewChanged, this, qOverload<>(&QWidget::update));
}

// Environ 85 % de la police de l'interface (7,5 pt pour 9 pt auparavant) : la règle suit une
// police agrandie au lieu de rester à une taille absolue.
QFont Ruler::rulerFont() const {
    QFont f = font();
    if (f.pointSizeF() > 0.0) {
        f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.85));
    } else if (f.pixelSize() > 0) {
        f.setPixelSize(std::max(8, static_cast<int>(std::lround(f.pixelSize() * 0.85))));
    }
    return f;
}

int Ruler::thickness() const {
    return QFontMetrics(rulerFont()).height() + 12;
}

QSize Ruler::sizeHint() const {
    const int t = thickness();
    return orientation_ == Qt::Horizontal ? QSize(0, t) : QSize(t, 0);
}

void Ruler::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange) {
        updateGeometry();
        update();
    }
}

void Ruler::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());

    const length_units::Unit unit = length_units::current();
    const double unitMm = length_units::mmPerUnit(unit);
    // Tout le tracé se fait en unité d'affichage ; la scène, elle, reste en mm.
    const double pxPerMm = view_->pixelsPerMm() * unitMm; // pixels par unité d'affichage
    if (pxPerMm <= 0.0) {
        return;
    }
    painter.setFont(rulerFont());
    const QFontMetrics fm(painter.font());
    const int thick = thickness();
    const int labelHeight = fm.height();
    const int labelWidth = fm.horizontalAdvance(QStringLiteral("-0000.0")) + 4;
    const int majorTick = std::max(10, thick * 10 / 26);
    const int minorTick = std::max(5, majorTick / 2);

    const double labelStep = niceLabelStep(pxPerMm, unit);
    const double tickStep = labelStep / 5.0;

    painter.setPen(palette().color(QPalette::WindowText));

    const bool horizontal = (orientation_ == Qt::Horizontal);
    const int lengthPx = horizontal ? width() : height();

    // Coordonnée scène (mm) aux deux extrémités du viewport.
    const QPointF sceneStart = view_->mapToScene(QPoint(0, 0));
    const double startMm = (horizontal ? sceneStart.x() : sceneStart.y()) / unitMm;
    const double mmSpan = lengthPx / pxPerMm;
    const double endMm = startMm + mmSpan;

    const double first = std::floor(startMm / tickStep) * tickStep;
    for (double mm = first; mm <= endMm + tickStep; mm += tickStep) {
        const double px = (mm - startMm) * pxPerMm;
        if (px < 0 || px > lengthPx) {
            continue;
        }
        const bool isLabel = std::abs(std::remainder(mm, labelStep)) < tickStep * 0.25;
        const int tickLen = isLabel ? majorTick : minorTick;
        if (horizontal) {
            painter.drawLine(QPointF(px, thick), QPointF(px, thick - tickLen));
            if (isLabel) {
                painter.drawText(QRectF(px + 2, 0, labelWidth, thick - 8), Qt::AlignLeft,
                                 formatTick(mm, unit));
            }
        } else {
            painter.drawLine(QPointF(thick, px), QPointF(thick - tickLen, px));
            if (isLabel) {
                painter.save();
                painter.translate(thick - labelHeight, px - 2);
                painter.rotate(-90);
                // Repère physique : l'axe Y de la scène pointe vers le bas,
                // la règle affiche donc -mm pour que « vers le haut » soit positif.
                painter.drawText(QRectF(0, 0, labelWidth, labelHeight), Qt::AlignLeft,
                                 formatTick(-mm, unit));
                painter.restore();
            }
        }
    }
}

} // namespace openstitch::desktop
