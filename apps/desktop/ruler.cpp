// SPDX-License-Identifier: Apache-2.0
#include "ruler.hpp"

#include <QPainter>
#include <QPaintEvent>

#include <cmath>

#include "canvas_view.hpp"

namespace openstitch::desktop {

namespace {
constexpr int kThickness = 26;

double niceLabelStepMm(double pxPerMm) {
    static constexpr double steps[] = {0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500};
    for (const double s : steps) {
        if (s * pxPerMm >= 45.0) {  // au moins 45 px entre deux étiquettes
            return s;
        }
    }
    return 1000.0;
}

QString formatMm(double mm) {
    return (std::abs(mm - std::round(mm)) < 1e-9) ? QString::number(std::lround(mm))
                                                  : QString::number(mm, 'f', 1);
}
}  // namespace

Ruler::Ruler(Qt::Orientation orientation, CanvasView* view, QWidget* parent)
    : QWidget(parent), orientation_(orientation), view_(view) {
    connect(view_, &CanvasView::viewChanged, this, qOverload<>(&QWidget::update));
    setFont([] {
        QFont f;
        f.setPointSizeF(7.5);
        return f;
    }());
}

QSize Ruler::sizeHint() const {
    return orientation_ == Qt::Horizontal ? QSize(0, kThickness) : QSize(kThickness, 0);
}

void Ruler::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().window());

    const double pxPerMm = view_->pixelsPerMm();
    if (pxPerMm <= 0.0) {
        return;
    }
    const double labelStep = niceLabelStepMm(pxPerMm);
    const double tickStep = labelStep / 5.0;

    painter.setPen(palette().color(QPalette::WindowText));

    const bool horizontal = (orientation_ == Qt::Horizontal);
    const int lengthPx = horizontal ? width() : height();

    // Coordonnée scène (mm) aux deux extrémités du viewport.
    const QPointF sceneStart = view_->mapToScene(QPoint(0, 0));
    const double startMm = horizontal ? sceneStart.x() : sceneStart.y();
    const double mmSpan = lengthPx / pxPerMm;
    const double endMm = startMm + mmSpan;

    const double first = std::floor(startMm / tickStep) * tickStep;
    for (double mm = first; mm <= endMm + tickStep; mm += tickStep) {
        const double px = (mm - startMm) * pxPerMm;
        if (px < 0 || px > lengthPx) {
            continue;
        }
        const bool isLabel = std::abs(std::remainder(mm, labelStep)) < tickStep * 0.25;
        const int tickLen = isLabel ? 10 : 5;
        if (horizontal) {
            painter.drawLine(QPointF(px, kThickness), QPointF(px, kThickness - tickLen));
            if (isLabel) {
                painter.drawText(QRectF(px + 2, 0, 60, kThickness - 8), Qt::AlignLeft,
                                 formatMm(mm));
            }
        } else {
            painter.drawLine(QPointF(kThickness, px), QPointF(kThickness - tickLen, px));
            if (isLabel) {
                painter.save();
                painter.translate(kThickness - 12, px - 2);
                painter.rotate(-90);
                // Repère physique : l'axe Y de la scène pointe vers le bas,
                // la règle affiche donc -mm pour que « vers le haut » soit positif.
                painter.drawText(QRectF(0, 0, 60, 12), Qt::AlignLeft, formatMm(-mm));
                painter.restore();
            }
        }
    }
}

}  // namespace openstitch::desktop
