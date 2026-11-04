// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- canevas d'aperçu : AFFICHAGE seulement (zoom à la molette ancré sous le curseur,
// panoramique au glisser, « ajuster » avec F). Aucune logique métier ; les outils de dessin et la
// manipulation directe arriveront dans le lot « canevas et interactions » (voir le plan).

#include <QGraphicsView>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QVector>
#include <Qt>

class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QRubberBand;

namespace openstitch::desktop_v2 {

class PreviewCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit PreviewCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    [[nodiscard]] bool hasImage() const { return item_ != nullptr; }
    [[nodiscard]] double zoom() const { return transform().m11(); }

    // Cadres de sélection (pixels de l'image d'aperçu) dessinés au-dessus de l'image.
    void setSelectionBoxes(const QVector<QRectF>& boxesPx);
    [[nodiscard]] int selectionBoxCount() const { return static_cast<int>(boxItems_.size()); }
    // Coordonnées de la scène (pixels de l'image) sous un point du viewport.
    [[nodiscard]] QPointF scenePos(const QPoint& viewportPos) const {
        return mapToScene(viewportPos);
    }

    // Mode « sélection » : le glisser trace un cadre élastique (sinon, le glisser déplace la vue).
    void setRubberBandSelection(bool on);
    [[nodiscard]] bool rubberBandSelection() const { return bandMode_; }

signals:
    // Cadre élastique terminé (glisser avec l'outil Sélection). Rectangle en pixels de l'image.
    void rectPx(QRectF scenePx, Qt::KeyboardModifiers modifiers);
    // Clic gauche sans glisser (un glisser déplace la vue). Position en pixels de l'image.
    void clickedPx(QPointF scenePx, Qt::KeyboardModifiers modifiers);

public slots:
    void fitAll();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QGraphicsScene* scene_;
    QGraphicsPixmapItem* item_{nullptr};
    QVector<QGraphicsRectItem*> boxItems_;
    QPoint pressPos_;
    QRubberBand* band_{nullptr};
    QPoint bandOrigin_;
    bool bandMode_{false};
    bool bandDragging_{false};
    bool pressing_{false};
    bool userZoomed_{false}; // tant que faux, un redimensionnement re-ajuste l'image
};

} // namespace openstitch::desktop_v2
