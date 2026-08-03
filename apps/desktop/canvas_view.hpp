// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QGraphicsView>

namespace openstitch::desktop {

// Vue du canevas. Unité de scène : le MILLIMÈTRE (double), origine au centre
// du canevas. La scène est en Y vers le bas (convention Qt) ; l'inversion
// vers le repère physique Y-vers-le-haut est faite à l'affichage (règles,
// position du curseur) — cf. ADR-003.
class CanvasView : public QGraphicsView {
    Q_OBJECT

public:
    explicit CanvasView(QGraphicsScene* scene, QWidget* parent = nullptr);

    void setCanvasSizeMm(QSizeF sizeMm);
    [[nodiscard]] QSizeF canvasSizeMm() const { return canvasMm_; }

    // Échelle courante en pixels d'écran par millimètre.
    [[nodiscard]] double pixelsPerMm() const;

    void zoomIn();
    void zoomOut();
    void fitCanvas();

    // Mode recadrage : sélection au rectangle élastique au lieu du déplacement.
    void setCropMode(bool enabled);
    [[nodiscard]] bool cropMode() const { return cropMode_; }

    // Mode dessin par rectangle élastique (rectangle/ellipse) : même mécanique
    // que le recadrage (glisser un cadre), mais émet `boxDrawnMm` au lieu de
    // `cropSelectedMm` — l'appelant interprète le cadre selon l'outil actif.
    void setBoxDrawMode(bool enabled);
    [[nodiscard]] bool boxDrawMode() const { return boxDrawMode_; }

    // Mode dessin par clics successifs (polygone) : désactive le glisser de
    // vue (un clic ne doit jamais faire défiler le canevas pendant le tracé).
    void setPolygonDrawMode(bool enabled);
    [[nodiscard]] bool polygonDrawMode() const { return polygonDrawMode_; }

    // Mode dessin à main levée (lasso) : capture un tracé continu pendant le
    // glisser (contrairement au cadre élastique ou aux clics successifs) ;
    // désactive le glisser de vue, comme le mode polygone.
    void setFreeformDrawMode(bool enabled);
    [[nodiscard]] bool freeformDrawMode() const { return freeformDrawMode_; }

signals:
    // Zoom ou défilement : les règles doivent se redessiner.
    void viewChanged();
    // Position du curseur en mm, repère physique (Y vers le haut).
    void cursorMovedMm(QPointF posMm);
    // Rectangle sélectionné en mode recadrage (coordonnées scène, mm).
    void cropSelectedMm(QRectF rectMm);
    // Rectangle dessiné en mode dessin par cadre (coordonnées scène, mm) et les
    // modificateurs clavier tels qu'observés sur l'évènement de relâchement qui
    // termine le geste (pas une relecture différée de l'état clavier global,
    // qui n'est pas fiable à rejouer dans un test — cf. Maj = cercle).
    void boxDrawnMm(QRectF rectMm, Qt::KeyboardModifiers modifiers);
    // Clic gauche sur le canevas (coordonnées scène, mm) — hors mode recadrage.
    void canvasClickedMm(QPointF posMm);
    // Double-clic gauche (coordonnées scène, mm) — hors mode recadrage ; sert
    // à clore un polygone en cours de tracé.
    void canvasDoubleClickedMm(QPointF posMm);
    // Point ajouté au tracé à main levée (coordonnées scène, mm) : émis à
    // l'appui initial puis à chaque déplacement tant que le bouton reste
    // enfoncé en mode dessin freeform.
    void freeformPointMm(QPointF posMm);
    // Fin du tracé à main levée (relâchement du bouton gauche).
    void freeformStrokeFinished();
    // Clic droit : position scène (mm) et position écran (pour placer le menu).
    void canvasContextMenu(QPointF posMm, QPoint globalPos);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    void applyZoom(double factor, bool anchorUnderMouse);

    QSizeF canvasMm_{100.0, 100.0};
    bool cropMode_{false};
    bool boxDrawMode_{false};
    bool polygonDrawMode_{false};
    bool freeformDrawMode_{false};
    bool freeformActive_{false};
    QRectF lastRubberBandMm_;
};

}  // namespace openstitch::desktop
