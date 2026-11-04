// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- canevas d'aperçu : AFFICHAGE seulement (zoom à la molette ancré sous le curseur,
// panoramique au glisser, « ajuster » avec F). Aucune logique métier ; les outils de dessin et la
// manipulation directe arriveront dans le lot « canevas et interactions » (voir le plan).

#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;
class QGraphicsScene;

namespace openstitch::desktop_v2 {

class PreviewCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit PreviewCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    [[nodiscard]] bool hasImage() const { return item_ != nullptr; }
    [[nodiscard]] double zoom() const { return transform().m11(); }

public slots:
    void fitAll();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QGraphicsScene* scene_;
    QGraphicsPixmapItem* item_{nullptr};
    bool userZoomed_{false}; // tant que faux, un redimensionnement re-ajuste l'image
};

} // namespace openstitch::desktop_v2
