// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- canevas d'aperçu : AFFICHAGE et GESTES seulement. Zoom à la molette ancré sous le
// curseur, « ajuster » avec F, et quatre modes de glisser/clic (`Mode`). Le canevas ne connaît ni
// le document ni le moteur : il émet des gestes en pixels de l'image d'aperçu, que `ShellWindow`
// convertit en intentions (sélection, création de forme…).

#include <QGraphicsView>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>
#include <functional>

class QGraphicsItemGroup;
class QGraphicsLineItem;
class QGraphicsPathItem;
class QGraphicsSimpleTextItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QGraphicsScene;
class QRubberBand;
class QContextMenuEvent;

namespace openstitch::desktop_v2 {

class PreviewCanvas : public QGraphicsView {
    Q_OBJECT
public:
    // Select : clic = sélection, glisser = cadre élastique (sélection fenêtre).
    // Pan : glisser = déplacer la vue.
    // DrawBox : glisser = cadre de création (rectangle, ellipse).
    // DrawPolygon : clics successifs, double-clic ou Entrée = terminer, Échap = annuler,
    //               Retour arrière = retirer le dernier sommet.
    // Measure : glisser = règle (le résultat est affiché par `showMeasure`).
    // Pick : un clic échantillonne (pipette) ; ni cadre élastique ni déplacement.
    // Cut : glisser = ligne de coupe (couteau), émise par `lineDrawnPx`.
    // Stroke : glisser = trait de pinceau (zones de détail), émis par `strokeDrawnPx`.
    enum class Mode { Select, Pan, DrawBox, DrawPolygon, Measure, Pick, Cut, Stroke };

    explicit PreviewCanvas(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    void clearImage();
    [[nodiscard]] bool hasImage() const { return item_ != nullptr; }
    [[nodiscard]] double zoom() const { return transform().m11(); }

    void setMode(Mode mode);
    void centreOnPx(const QPointF& scenePx) { centerOn(scenePx); }
    [[nodiscard]] Mode mode() const { return mode_; }

    // Cadres de sélection (pixels de l'image d'aperçu) dessinés au-dessus de l'image.
    void setSelectionBoxes(const QVector<QRectF>& boxesPx);
    [[nodiscard]] int selectionBoxCount() const { return static_cast<int>(boxItems_.size()); }
    // Test « y a-t-il un objet sous ce point ? » (pixels de l'image) fourni par la fenêtre : il
    // décide si un appui en mode Select démarre un déplacement plutôt qu'un cadre élastique.
    void setMoveHitTest(std::function<bool(const QPointF&)> test) { moveHit_ = std::move(test); }
    // Grille : traits verticaux (x) et horizontaux (y) en pixels de l'image, sur le rectangle
    // donné.
    void setGrid(const QVector<double>& xsPx, const QVector<double>& ysPx, const QRectF& imageRect);
    [[nodiscard]] bool hasGrid() const { return gridItem_ != nullptr; }
    // Règle : segment et texte affichés jusqu'à `clearMeasure` / prochain appui en mode Measure.
    void showMeasure(const QPointF& a, const QPointF& b, const QString& text);
    void clearMeasure();
    [[nodiscard]] bool hasMeasure() const { return measureLine_ != nullptr; }
    void setHoopVisible(bool visible);
    [[nodiscard]] bool hoopVisible() const { return hoopVisible_; }
    // Image source affichée sous forme de calque semi-transparent (pixels de l'image d'aperçu).
    void setSourceImage(const QImage& image, const QRectF& rectPx);
    void setSourceVisible(bool visible);
    [[nodiscard]] bool hasSource() const { return sourceItem_ != nullptr; }
    [[nodiscard]] bool sourceVisible() const { return sourceVisible_; }
    // Masque de détail (ARGB) superposé à l'image source ; image nulle = aucun.
    void setDetailOverlay(const QImage& image, const QRectF& rectPx);
    [[nodiscard]] bool hasDetailOverlay() const { return detailItem_ != nullptr; }
    // Poignées des points d'un objet (pixels de l'image) ; `selected` = indice mis en évidence, -1
    // = aucun. Liste vide = aucune poignée.
    void setPointMarkers(const QVector<QPointF>& scenePx, int selected);
    [[nodiscard]] int pointMarkerCount() const { return static_cast<int>(pointItems_.size()); }
    // Cadre de broderie (pixels de l'image) ; rectangle nul = aucun.
    void setHoopRect(const QRectF& rectPx);
    [[nodiscard]] bool hasHoop() const { return hoopItem_ != nullptr; }
    // Sommets du polygone en cours de tracé (pixels de l'image).
    [[nodiscard]] QVector<QPointF> polygonInProgress() const { return polyPts_; }

signals:
    // Règle : segment tracé (pixels de l'image).
    void measuredPx(QPointF a, QPointF b);
    // Trait de pinceau terminé (pixels de l'image), Ctrl = gomme.
    void strokeDrawnPx(QVector<QPointF> scenePx, Qt::KeyboardModifiers modifiers);
    // Couteau : ligne tracée (pixels de l'image).
    void lineDrawnPx(QPointF a, QPointF b);
    // Clic droit : position globale (pour le menu) et position dans l'image.
    void contextMenuRequested(QPoint globalPos, QPointF scenePx);
    // Clic gauche sans glisser, en mode Select. Position en pixels de l'image.
    void clickedPx(QPointF scenePx, Qt::KeyboardModifiers modifiers);
    // Cadre élastique terminé en mode Select.
    void rectPx(QRectF scenePx, Qt::KeyboardModifiers modifiers);
    // Cadre de création terminé en mode DrawBox.
    void boxDrawnPx(QRectF scenePx, Qt::KeyboardModifiers modifiers);
    // Flèche pressée en mode Select : pas (-1, 0 ou 1) selon X (droite) et Y (haut) ; `big` = Maj.
    void nudgeRequested(int stepX, int stepUp, bool big);
    // Un déplacement va démarrer sur l'objet sous `scenePx` (la fenêtre l'ajoute à la sélection).
    void moveStartRequested(QPointF scenePx);
    // Glisser terminé sur la sélection : décalage en pixels de l'image.
    void moveDraggedPx(QPointF deltaPx);
    // Poignée de coin tirée : pivot (coin opposé, pixels de l'image) et facteur d'échelle uniforme.
    void scaleDraggedPx(QPointF pivotPx, double factor);
    // Polygone terminé en mode DrawPolygon (au moins 3 sommets).
    void polygonDrawnPx(QVector<QPointF> scenePx);

public slots:
    void fitAll();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void updatePolygonItem(const QPointF* cursor);
    void finishPolygon();
    void cancelPolygon();

    QGraphicsScene* scene_;
    QGraphicsPixmapItem* item_{nullptr};
    QGraphicsRectItem* hoopItem_{nullptr};
    QVector<QGraphicsItem*> pointItems_;
    QGraphicsPixmapItem* sourceItem_{nullptr};
    QGraphicsPixmapItem* detailItem_{nullptr};
    QGraphicsPathItem* strokeItem_{nullptr};
    QVector<QPointF> strokePts_;
    bool stroking_{false};
    bool sourceVisible_{true};
    QGraphicsPathItem* gridItem_{nullptr};
    QGraphicsLineItem* measureLine_{nullptr};
    QGraphicsSimpleTextItem* measureText_{nullptr};
    QPointF measureFrom_;
    bool measuring_{false};
    bool hoopVisible_{true};
    QGraphicsItemGroup* selGroup_{nullptr}; // cadres + poignées de la sélection
    QGraphicsRectItem* scaleOutline_{nullptr};
    QRectF unionPx_;
    QGraphicsPathItem* polyItem_{nullptr};
    QVector<QGraphicsRectItem*> boxItems_;
    QVector<QPointF> polyPts_;
    QPoint pressPos_;
    QRubberBand* band_{nullptr};
    QPoint bandOrigin_;
    Mode mode_{Mode::Select};
    std::function<bool(const QPointF&)> moveHit_;
    QPointF moveOrigin_;
    QPointF scalePivot_;
    QPointF scaleCorner_;
    int scaleHandle_{-1};
    bool moving_{false};
    bool bandDragging_{false};
    bool pressing_{false};
    bool userZoomed_{false}; // tant que faux, un redimensionnement re-ajuste l'image
};

} // namespace openstitch::desktop_v2
