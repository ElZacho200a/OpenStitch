// SPDX-License-Identifier: Apache-2.0
#include "ui_icons.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <functional>

namespace openstitch::desktop::icons {

namespace {

constexpr int kSize = 32;

// Teinte neutre lisible sur fond clair ET sombre (icônes désactivées gérées par
// l'opacité du widget). Un seul jeu, pas de recoloration par thème.
const QColor kInk(0x5C, 0x62, 0x6A);

QIcon make(const std::function<void(QPainter&)>& draw) {
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(kInk);
    pen.setWidthF(2.2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p);
    p.end();
    return QIcon(pm);
}

}  // namespace

QIcon select() {
    return make([](QPainter& p) {
        QPolygonF arrow({{8, 6}, {8, 24}, {13, 19}, {17, 26}, {20, 24}, {16, 17}, {23, 17}});
        p.setBrush(kInk);
        p.drawPolygon(arrow);
    });
}

QIcon pan() {
    return make([](QPainter& p) {
        // Croix à quatre flèches (déplacement de la vue).
        p.drawLine(16, 5, 16, 27);
        p.drawLine(5, 16, 27, 16);
        p.drawLine(16, 5, 13, 9);
        p.drawLine(16, 5, 19, 9);
        p.drawLine(16, 27, 13, 23);
        p.drawLine(16, 27, 19, 23);
        p.drawLine(5, 16, 9, 13);
        p.drawLine(5, 16, 9, 19);
        p.drawLine(27, 16, 23, 13);
        p.drawLine(27, 16, 23, 19);
    });
}

QIcon rect() {
    return make([](QPainter& p) {
        QPen dashed = p.pen();
        dashed.setStyle(Qt::DashLine);
        p.setPen(dashed);
        p.drawRect(6, 8, 20, 16);
    });
}

QIcon drawRect() {
    return make([](QPainter& p) { p.drawRect(6, 8, 20, 16); });
}

QIcon ellipse() {
    return make([](QPainter& p) { p.drawEllipse(QRectF(5, 7, 22, 18)); });
}

QIcon polygon() {
    return make([](QPainter& p) {
        QPolygonF poly({{16, 5}, {27, 13}, {23, 26}, {9, 26}, {5, 13}});
        p.drawPolygon(poly);
    });
}

QIcon freeform() {
    return make([](QPainter& p) {
        // Contour organique (courbes), pour distinguer du polygone à angles
        // droits : symbolise un tracé « à main levée » (lasso).
        QPainterPath path;
        path.moveTo(16, 5);
        path.cubicTo(24, 4, 28, 12, 24, 16);
        path.cubicTo(29, 20, 24, 27, 17, 26);
        path.cubicTo(10, 29, 4, 22, 7, 16);
        path.cubicTo(3, 10, 9, 3, 16, 5);
        path.closeSubpath();
        p.drawPath(path);
    });
}

QIcon bezierCurve() {
    return make([](QPainter& p) {
        // Une courbe (deux ancres, poignées visibles) — distincte du polygone
        // à angles droits : symbolise l'outil plume.
        QPainterPath path;
        path.moveTo(6, 22);
        path.cubicTo(10, 6, 22, 6, 26, 22);
        p.drawPath(path);
        QPen thin = p.pen();
        thin.setWidthF(1.0);
        thin.setStyle(Qt::DashLine);
        p.setPen(thin);
        p.drawLine(QPointF(6, 22), QPointF(10, 6));
        p.drawLine(QPointF(26, 22), QPointF(22, 6));
        p.setPen(QPen(p.pen().color(), 2.2));
        p.setBrush(p.pen().color());
        p.drawEllipse(QPointF(6, 22), 1.6, 1.6);
        p.drawEllipse(QPointF(26, 22), 1.6, 1.6);
        p.drawEllipse(QPointF(10, 6), 1.6, 1.6);
        p.drawEllipse(QPointF(22, 6), 1.6, 1.6);
    });
}

QIcon checkmark() {
    return make([](QPainter& p) {
        QPainterPath path;
        path.moveTo(6, 17);
        path.lineTo(13, 24);
        path.lineTo(26, 8);
        p.drawPath(path);
    });
}

QIcon cancelDraw() {
    return make([](QPainter& p) {
        p.drawLine(QPointF(8, 8), QPointF(24, 24));
        p.drawLine(QPointF(24, 8), QPointF(8, 24));
    });
}

QIcon satinColumn() {
    return make([](QPainter& p) {
        // Deux rails parallèles légèrement courbes reliés par quelques
        // barreaux transversaux — discret, sans surcharge visuelle.
        QPainterPath rails;
        rails.moveTo(8, 8);
        rails.quadTo(4, 16, 8, 24);
        rails.moveTo(24, 7);
        rails.quadTo(28, 16, 24, 25);
        p.drawPath(rails);
        QPen thin = p.pen();
        thin.setWidthF(1.4);
        p.setPen(thin);
        p.drawLine(QPointF(8, 10), QPointF(24, 9));
        p.drawLine(QPointF(6.5, 16), QPointF(25.5, 16));
        p.drawLine(QPointF(8, 22), QPointF(24, 23));
    });
}

QIcon openImage() {
    return make([](QPainter& p) {
        p.drawRect(6, 9, 20, 15);
        // Petite « image » : soleil + montagne.
        p.drawEllipse(QPointF(11, 14), 1.6, 1.6);
        QPolygonF hill({{7, 23}, {14, 16}, {19, 21}, {23, 17}, {25, 23}});
        p.drawPolyline(hill);
    });
}

QIcon openProject() {
    return make([](QPainter& p) {
        QPainterPath folder;
        folder.moveTo(5, 11);
        folder.lineTo(12, 11);
        folder.lineTo(14, 14);
        folder.lineTo(27, 14);
        folder.lineTo(27, 25);
        folder.lineTo(5, 25);
        folder.closeSubpath();
        p.drawPath(folder);
    });
}

QIcon save() {
    return make([](QPainter& p) {
        p.drawRect(6, 6, 20, 20);
        p.drawRect(11, 6, 10, 7);   // volet
        p.drawRect(10, 17, 12, 9);  // étiquette
    });
}

QIcon undo() {
    return make([](QPainter& p) {
        QPainterPath arc;
        arc.moveTo(23, 22);
        arc.arcTo(QRectF(8, 8, 16, 16), -30, -170);
        p.drawPath(arc);
        p.drawLine(8, 15, 8, 9);
        p.drawLine(8, 15, 14, 15);
    });
}

QIcon redo() {
    return make([](QPainter& p) {
        QPainterPath arc;
        arc.moveTo(9, 22);
        arc.arcTo(QRectF(8, 8, 16, 16), 210, 170);
        p.drawPath(arc);
        p.drawLine(24, 15, 24, 9);
        p.drawLine(24, 15, 18, 15);
    });
}

QIcon zoomIn() {
    return make([](QPainter& p) {
        p.drawEllipse(QPointF(14, 14), 7, 7);
        p.drawLine(19, 19, 26, 26);
        p.drawLine(14, 11, 14, 17);
        p.drawLine(11, 14, 17, 14);
    });
}

QIcon zoomOut() {
    return make([](QPainter& p) {
        p.drawEllipse(QPointF(14, 14), 7, 7);
        p.drawLine(19, 19, 26, 26);
        p.drawLine(11, 14, 17, 14);
    });
}

QIcon fit() {
    return make([](QPainter& p) {
        const int m = 6, n = 26, k = 5;
        p.drawLine(m, m, m + k, m);
        p.drawLine(m, m, m, m + k);
        p.drawLine(n, m, n - k, m);
        p.drawLine(n, m, n, m + k);
        p.drawLine(m, n, m + k, n);
        p.drawLine(m, n, m, n - k);
        p.drawLine(n, n, n - k, n);
        p.drawLine(n, n, n, n - k);
    });
}

QIcon analyze() {
    return make([](QPainter& p) {
        p.drawEllipse(QPointF(13, 13), 7, 7);
        p.drawLine(18, 18, 26, 26);
        // petit « check » dans la loupe
        p.drawLine(10, 13, 12, 16);
        p.drawLine(12, 16, 16, 10);
    });
}

QIcon aiSegment() {
    return make([](QPainter& p) {
        // Image divisée en quelques régions...
        p.drawRoundedRect(QRectF(5, 8, 20, 16), 2, 2);
        QPainterPath boundaries;
        boundaries.moveTo(12, 8);
        boundaries.quadTo(10, 15, 15, 24);
        boundaries.moveTo(15, 12);
        boundaries.lineTo(25, 17);
        p.drawPath(boundaries);
        // ...avec un petit indicateur IA discret (étincelle, pas d'emoji).
        QPen thin = p.pen();
        thin.setWidthF(1.4);
        p.setPen(thin);
        p.drawLine(QPointF(24, 4.5), QPointF(24, 9.5));
        p.drawLine(QPointF(21.5, 7), QPointF(26.5, 7));
    });
}

QIcon stitches() {
    return make([](QPainter& p) {
        QPolygonF zig({{5, 20}, {11, 12}, {17, 20}, {23, 12}, {27, 17}});
        p.drawPolyline(zig);
        p.setBrush(kInk);
        for (const QPointF& pt : zig) {
            p.drawEllipse(pt, 1.3, 1.3);
        }
    });
}

QIcon editPoints() {
    return make([](QPainter& p) {
        // Crayon en diagonale (retouche) pointant vers un point de couture
        // (petit cercle plein) : distinct du crayon seul (pas d'icône de
        // dessin vectoriel dans ce jeu) et du zigzag de « stitches ».
        QPolygonF pencil({{9, 25}, {7, 26}, {8, 24}, {21, 11}, {23, 13}});
        p.setBrush(kInk);
        p.drawPolygon(pencil);
        p.drawLine(21, 11, 25, 7);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(25, 25), 3.0, 3.0);
        p.setBrush(kInk);
        p.drawEllipse(QPointF(25, 25), 1.1, 1.1);
    });
}

QIcon exportDst() {
    return make([](QPainter& p) {
        p.drawLine(16, 6, 16, 19);
        p.drawLine(16, 19, 12, 15);
        p.drawLine(16, 19, 20, 15);
        p.drawLine(6, 22, 6, 26);
        p.drawLine(6, 26, 26, 26);
        p.drawLine(26, 26, 26, 22);
    });
}

}  // namespace openstitch::desktop::icons
