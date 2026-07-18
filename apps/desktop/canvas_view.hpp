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

signals:
    // Zoom ou défilement : les règles doivent se redessiner.
    void viewChanged();
    // Position du curseur en mm, repère physique (Y vers le haut).
    void cursorMovedMm(QPointF posMm);
    // Rectangle sélectionné en mode recadrage (coordonnées scène, mm).
    void cropSelectedMm(QRectF rectMm);

protected:
    void wheelEvent(QWheelEvent* event) override;
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
    QRectF lastRubberBandMm_;
};

}  // namespace openstitch::desktop
