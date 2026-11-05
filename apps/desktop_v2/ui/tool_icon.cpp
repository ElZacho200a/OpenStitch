// SPDX-License-Identifier: Apache-2.0
#include "tool_icon.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>

namespace openstitch::desktop_v2 {

QIcon toolIcon(std::string_view id, const QColor& foreground) {
    QPixmap image(24, 24);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(foreground, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (id == "select") {
        QPainterPath arrow;
        arrow.moveTo(5, 3);
        arrow.lineTo(5, 20);
        arrow.lineTo(9, 15);
        arrow.lineTo(13, 21);
        arrow.lineTo(16, 19);
        arrow.lineTo(12, 13);
        arrow.lineTo(19, 13);
        arrow.closeSubpath();
        p.drawPath(arrow);
    } else if (id == "pan") {
        p.drawLine(QPointF(12, 2), QPointF(12, 22));
        p.drawLine(QPointF(2, 12), QPointF(22, 12));
        p.drawLine(QPointF(9, 5), QPointF(12, 2));
        p.drawLine(QPointF(15, 5), QPointF(12, 2));
        p.drawLine(QPointF(9, 19), QPointF(12, 22));
        p.drawLine(QPointF(15, 19), QPointF(12, 22));
        p.drawLine(QPointF(5, 9), QPointF(2, 12));
        p.drawLine(QPointF(5, 15), QPointF(2, 12));
        p.drawLine(QPointF(19, 9), QPointF(22, 12));
        p.drawLine(QPointF(19, 15), QPointF(22, 12));
    } else if (id == "draw.rectangle" || id == "crop") {
        p.drawRect(QRectF(4, 5, 16, 14));
    } else if (id == "draw.ellipse") {
        p.drawEllipse(QRectF(4, 4, 16, 16));
    } else if (id == "draw.polygon" || id == "draw.polygon-regular") {
        QPolygonF poly;
        poly << QPointF(12, 3) << QPointF(21, 11) << QPointF(17, 21) << QPointF(5, 19)
             << QPointF(3, 10);
        p.drawPolygon(poly);
    } else if (id == "measure") {
        p.drawRect(QRectF(3, 8, 18, 8));
        for (int x = 6; x < 21; x += 4) {
            p.drawLine(QPointF(x, 8), QPointF(x, x % 8 == 2 ? 13 : 11));
        }
    } else if (id == "knife") {
        QPainterPath blade;
        blade.moveTo(5, 19);
        blade.lineTo(14, 4);
        blade.lineTo(20, 4);
        blade.lineTo(13, 13);
        blade.lineTo(5, 19);
        p.drawPath(blade);
        p.drawLine(QPointF(8, 16), QPointF(13, 21));
    } else if (id == "eyedropper") {
        p.drawLine(QPointF(5, 19), QPointF(17, 7));
        p.drawEllipse(QRectF(14, 3, 6, 7));
        p.drawEllipse(QRectF(3, 17, 4, 4));
    } else if (id == "satin.column" || id == "fill.direction-guide" || id == "fill.break-line") {
        p.drawLine(QPointF(5, 3), QPointF(8, 21));
        p.drawLine(QPointF(18, 3), QPointF(15, 21));
        for (int y = 6; y <= 18; y += 4) {
            p.drawLine(QPointF(6, y), QPointF(17, y));
        }
    } else if (id == "node.edit") {
        QPainterPath curve;
        curve.moveTo(4, 18);
        curve.cubicTo(6, 4, 16, 4, 20, 16);
        p.drawPath(curve);
        for (const QPointF pt : {QPointF(4, 18), QPointF(20, 16)}) {
            p.drawRect(QRectF(pt.x() - 2, pt.y() - 2, 4, 4));
        }
        p.drawEllipse(QPointF(12, 7.5), 2.0, 2.0);
    } else if (id == "stitch.points") {
        p.drawLine(QPointF(5, 18), QPointF(12, 5));
        p.drawLine(QPointF(12, 5), QPointF(19, 17));
        for (const QPointF pt : {QPointF(5, 18), QPointF(12, 5), QPointF(19, 17)}) {
            p.drawEllipse(pt, 2.1, 2.1);
        }
    } else if (id == "click-to-stitch" || id == "bucket") {
        p.drawEllipse(QPointF(12, 12), 7, 7);
        p.drawLine(QPointF(12, 2), QPointF(12, 8));
        p.drawLine(QPointF(12, 16), QPointF(12, 22));
        p.drawLine(QPointF(2, 12), QPointF(8, 12));
        p.drawLine(QPointF(16, 12), QPointF(22, 12));
    } else if (id == "detail-brush" || id == "warp-brush" || id == "draw.freeform") {
        p.drawLine(QPointF(18, 4), QPointF(9, 14));
        p.drawEllipse(QRectF(5, 12, 7, 8));
        p.drawLine(QPointF(5, 20), QPointF(12, 20));
    } else if (id == "draw.bezier") {
        p.drawLine(QPointF(4, 19), QPointF(12, 5));
        p.drawLine(QPointF(12, 5), QPointF(20, 19));
        p.drawEllipse(QPointF(12, 5), 2, 2);
    } else if (id == "text") {
        p.drawLine(QPointF(4, 5), QPointF(20, 5));
        p.drawLine(QPointF(12, 5), QPointF(12, 20));
        p.drawLine(QPointF(8, 20), QPointF(16, 20));
    } else {
        p.drawEllipse(QPointF(12, 12), 7, 7);
        p.drawLine(QPointF(8, 12), QPointF(16, 12));
    }
    p.end();
    return QIcon(image);
}

} // namespace openstitch::desktop_v2
