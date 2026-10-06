// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QCursor>
#include <QGraphicsView>
#include <QPoint>
#include <QTimer>

#include "interaction_map.hpp"

class QRubberBand;

namespace openstitch::desktop {

class SpaceKeyFilter;

// Vue du canevas. Unité de scène : le MILLIMÈTRE (double), origine au centre
// du canevas. La scène est en Y vers le bas (convention Qt) ; l'inversion
// vers le repère physique Y-vers-le-haut est faite à l'affichage (règles,
// position du curseur) — cf. ADR-003.
class CanvasView : public QGraphicsView {
    Q_OBJECT

public:
    explicit CanvasView(QGraphicsScene* scene, QWidget* parent = nullptr);
    ~CanvasView() override;

    void setCanvasSizeMm(QSizeF sizeMm);
    [[nodiscard]] QSizeF canvasSizeMm() const { return canvasMm_; }

    // Échelle courante en pixels d'écran par millimètre.
    [[nodiscard]] double pixelsPerMm() const;

    void zoomIn();
    void zoomOut();
    void fitCanvas();
    // Zoom ancré : le point de la scène situé sous `viewportPos` (coordonnées du
    // viewport) y reste après le zoom. Ancrage manuel (NoAnchor) : indépendant
    // de QCursor::pos(), donc valable avec des évènements synthétiques et un
    // pavé tactile. Borné à [kMin, kMax] pixels par mm.
    void zoomAt(double factor, QPointF viewportPos);

    // --- Modèle d'interaction (lot L5, specs/plans/ui-interaction-model.md §2) ---
    // Le PREMIER appel active le modèle : glisser gauche = NoDrag, contexte
    // piloté par l'appelant. Tant qu'il n'est pas appelé, la vue garde le
    // comportement historique (ScrollHandDrag) ; seules les branches purement
    // additives (clic milieu, Espace, molette Maj/Alt/Ctrl/pixelDelta, gestes
    // natifs) sont actives.
    void setBaseContext(Context context);
    [[nodiscard]] Context baseContext() const { return baseContext_; }
    [[nodiscard]] bool inputModelEnabled() const { return inputModelEnabled_; }
    // Sélection au clic différé / rectangle / appui long (contexte Select seulement).
    void setSelectionRectangleEnabled(bool enabled);
    [[nodiscard]] bool selectionRectangleEnabled() const { return selectionRectEnabled_; }
    [[nodiscard]] bool spaceHeld() const { return spaceHeld_; }
    // Point d'appui (scène, mm) du dernier cadre dessiné en mode boîte : valable pendant
    // l'émission de boxDrawnMm (Alt = dessiner depuis le centre).
    [[nodiscard]] QPointF lastBoxPressMm() const { return boxPressMm_; }
    // Un geste de la vue est en cours (panoramique, zoom continu, sélection/rectangle).
    [[nodiscard]] bool gestureActive() const {
        return panning_ || zoomDragging_ || selectionPress_ || rectActive_ || altBodyPress_;
    }

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

    // Mode dessin par paires alternées (colonne satin manuelle) : mêmes
    // mécaniques que le mode polygone (clics successifs, pas de glisser de
    // vue) ; l'appelant interprète chaque clic comme un point de rail A ou B
    // selon la parité déjà posée.
    void setSatinPairDrawMode(bool enabled);
    [[nodiscard]] bool satinPairDrawMode() const { return satinPairDrawMode_; }

    // Mode dessin en courbes de Bézier (outil plume) : clics successifs comme
    // le mode polygone, mais chaque point peut être glissé au moment de sa
    // pose pour poser un nœud lisse (poignées symétriques) au lieu d'un coin.
    void setBezierDrawMode(bool enabled);
    [[nodiscard]] bool bezierDrawMode() const { return bezierDrawMode_; }

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
    // Glisser en cours pendant la pose d'un nœud Bézier (outil plume) :
    // ancre (point pressé) et position courante du curseur, coordonnées
    // scène (mm) — sert à prévisualiser la poignée avant relâchement.
    void bezierPointDraggingMm(QPointF anchorMm, QPointF currentMm);
    // Nœud Bézier posé (relâchement du bouton) : ancre et position de
    // relâchement. Poignée nulle (== ancre) -> nœud Coin ; sinon -> nœud
    // Lisse dont les tangentes symétriques dérivent du vecteur de glisser.
    void bezierPointCommittedMm(QPointF anchorMm, QPointF handleMm);
    // Flèche du clavier, canevas focus (édition au pixel près impossible à
    // la souris passé un certain zoom — défaut remonté en usage réel) :
    // delta en coordonnées scène (mm), déjà dans le sens visuel attendu à
    // l'écran (haut = y scène décroissant). L'appelant interprète (objet
    // sélectionné, mode Sélection) et construit la commande d'undo.
    void nudgeRequestedMm(QPointF deltaMm);
    // Sélection (contexte Select, setSelectionRectangleEnabled(true)). Rectangle
    // en mm scène ; `crossing` = glissé vers la gauche (croise) sinon englobe.
    void selectionRectangleMm(QRectF rectMm, openstitch::desktop::SelectMode mode, bool crossing);
    // Clic avec Maj/Ctrl (mode != Replace) ; le clic simple reste canvasClickedMm.
    void selectionClickedMm(QPointF posMm, openstitch::desktop::SelectMode mode);
    // Appui long ou Alt + clic : objets sous le point.
    void selectBelowRequested(QPointF posMm, QPoint globalPos,
                              openstitch::desktop::SelectMode mode);
    // Modificateurs observés (évènements, jamais l'état global) : sur changement.
    void modifiersChanged(Qt::KeyboardModifiers modifiers);
    // Le curseur quitte le viewport (la surbrillance de survol doit disparaître).
    void cursorLeftViewport();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;

private:
    friend class SpaceKeyFilter;

    void applyCenterZoom(double factor);
    // Applique le curseur à LA FOIS sur la vue et sur son viewport : c'est le
    // viewport qui reçoit réellement les évènements souris affichés à
    // l'écran, et QGraphicsView ne propage pas toujours automatiquement le
    // curseur de la vue vers lui (défaut trouvé par revue — le curseur de
    // survol ne changeait pas de façon fiable selon l'outil actif).
    void applyModeCursor(Qt::CursorShape shape);
    // Recalcule dragMode() depuis TOUS les booléens de mode courants —
    // jamais depuis un seul flag isolé (cf. commentaire détaillé dans
    // canvas_view.cpp, défaut « rectangle/ellipse ne dessinent rien »).
    void updateDragMode();

    [[nodiscard]] Context currentContext() const;
    // Filtre applicatif : Espace (tenu) et Alt seul. true = évènement consommé.
    bool filterKey(QKeyEvent* event);
    void setSpaceHeld(bool held);
    void updateModifiers(Qt::KeyboardModifiers mods);
    void resetTransientInput(bool cursorLeft = true);
    void startPan(const QPoint& viewportPos, Qt::MouseButton button);
    void startZoomDrag(const QPoint& viewportPos, Qt::MouseButton button);
    void endGesture();
    void scrollBy(int dx, int dy);
    void refreshCursor();
    void cancelSelectionPress();
    void ensureRubberBand();
    void emitSelectionClick(const QPoint& viewportPos, const QPoint& globalPos,
                            Qt::KeyboardModifiers mods);
    void fireLongPress();
    void queueSelectBelow(QPointF posMm, QPoint globalPos);

    QSizeF canvasMm_{100.0, 100.0};
    bool cropMode_{false};
    bool boxDrawMode_{false};
    bool polygonDrawMode_{false};
    bool freeformDrawMode_{false};
    bool satinPairDrawMode_{false};
    bool bezierDrawMode_{false};
    bool freeformActive_{false};
    bool bezierPressActive_{false};
    QPointF bezierAnchorMm_;
    QRectF lastRubberBandMm_;

    // --- modèle d'interaction ---
    bool inputModelEnabled_{false};
    Context baseContext_{Context::Select};
    bool selectionRectEnabled_{false};
    SpaceKeyFilter* spaceFilter_{nullptr};
    bool spaceHeld_{false};
    bool spaceConsumed_{false}; // l'appui d'Espace a été consommé par le filtre
    bool cursorOverViewport_{false};
    bool altUsedInGesture_{false};
    Qt::KeyboardModifiers lastModifiers_{};
    // Glisser actif (panoramique ou zoom continu) sur `gestureButton_`.
    bool panning_{false};
    bool zoomDragging_{false};
    Qt::MouseButton gestureButton_{Qt::NoButton};
    QPoint gestureLastPos_;
    QPoint zoomAnchorPos_;
    bool middleDoubleClickPending_{false};
    // Curseur transitoire (main, +, ±) posé par-dessus le curseur d'origine.
    bool transientCursor_{false};
    bool hadViewportCursor_{false};
    QCursor savedViewportCursor_;
    int cursorKind_{0};
    // Sélection (clic différé, rectangle, appui long).
    bool selectionPress_{false};
    bool rectActive_{false};
    bool altBodyPress_{false}; // Alt seul sur un corps sélectionné : glisser d'item
    QPointF boxPressMm_;
    bool longPressFired_{false};
    QPoint pressViewportPos_;
    QPoint pressGlobalPos_;
    Qt::KeyboardModifiers pressMods_{};
    QTimer longPressTimer_;
    QRubberBand* rubberBand_{nullptr};
};

} // namespace openstitch::desktop
