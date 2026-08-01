// SPDX-License-Identifier: Apache-2.0
#include <QGraphicsScene>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

#include "canvas_view.hpp"

using openstitch::desktop::CanvasView;

namespace {

// Prépare une vue prête à recevoir des évènements souris synthétiques :
// taille non nulle, fenêtre effectivement exposée (requis même en offscreen
// pour que mapToScene/mapFromScene reflètent la géométrie du viewport).
void exposeView(CanvasView& view) {
    view.resize(400, 400);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
}

}  // namespace

class CanvasViewTest : public QObject {
    Q_OBJECT

private slots:
    void zoomInThenOutChangesScaleAndEmitsViewChanged();
    void fitCanvasEmitsViewChanged();
    void leftClickOutsideCropModeEmitsCanvasClickedMmAtSceneCoordinates();
    void cropModeSuppressesCanvasClicked();
    void cropModeRubberBandEmitsCropSelectedMm();
};

void CanvasViewTest::zoomInThenOutChangesScaleAndEmitsViewChanged() {
    QGraphicsScene scene;
    CanvasView view(&scene);
    view.setCanvasSizeMm(QSizeF(100.0, 100.0));
    exposeView(view);

    const double initial = view.pixelsPerMm();
    QSignalSpy spy(&view, &CanvasView::viewChanged);

    view.zoomIn();
    QVERIFY(view.pixelsPerMm() > initial);
    QVERIFY(spy.count() >= 1);

    const double afterZoomIn = view.pixelsPerMm();
    spy.clear();
    view.zoomOut();
    QVERIFY(view.pixelsPerMm() < afterZoomIn);
    QVERIFY(spy.count() >= 1);
}

void CanvasViewTest::fitCanvasEmitsViewChanged() {
    QGraphicsScene scene;
    CanvasView view(&scene);
    view.setCanvasSizeMm(QSizeF(300.0, 50.0));  // rectangle très allongé
    exposeView(view);

    QSignalSpy spy(&view, &CanvasView::viewChanged);
    view.fitCanvas();
    QVERIFY(spy.count() >= 1);
    // La vue doit s'être adaptée : le canevas (300 mm de large) doit tenir
    // dans les 400 px de la fenêtre de test, donc l'échelle est < 1,4 px/mm.
    QVERIFY(view.pixelsPerMm() > 0.0);
    QVERIFY(view.pixelsPerMm() < 1.4);
}

void CanvasViewTest::leftClickOutsideCropModeEmitsCanvasClickedMmAtSceneCoordinates() {
    QGraphicsScene scene;
    CanvasView view(&scene);
    view.setCanvasSizeMm(QSizeF(100.0, 100.0));
    exposeView(view);
    QVERIFY(!view.cropMode());

    QSignalSpy spy(&view, &CanvasView::canvasClickedMm);
    const QPoint clickPos(150, 120);
    const QPointF expectedScene = view.mapToScene(clickPos);

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);

    QCOMPARE(spy.count(), 1);
    const QPointF emitted = spy.at(0).at(0).toPointF();
    QVERIFY(std::abs(emitted.x() - expectedScene.x()) < 0.5);
    QVERIFY(std::abs(emitted.y() - expectedScene.y()) < 0.5);
}

void CanvasViewTest::cropModeSuppressesCanvasClicked() {
    QGraphicsScene scene;
    CanvasView view(&scene);
    view.setCanvasSizeMm(QSizeF(100.0, 100.0));
    exposeView(view);
    view.setCropMode(true);

    QSignalSpy spy(&view, &CanvasView::canvasClickedMm);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
    QCOMPARE(spy.count(), 0);
}

void CanvasViewTest::cropModeRubberBandEmitsCropSelectedMm() {
    QGraphicsScene scene;
    CanvasView view(&scene);
    view.setCanvasSizeMm(QSizeF(100.0, 100.0));
    exposeView(view);
    view.setCropMode(true);
    QVERIFY(view.cropMode());

    QSignalSpy cropSpy(&view, &CanvasView::cropSelectedMm);

    const QPoint from(50, 50);
    const QPoint mid(150, 150);
    const QPoint to(280, 260);
    // Rect attendu = celui réellement dessiné à l'écran (viewport -> scène),
    // pas une reconstruction indépendante à partir de 'from'/'to' seuls :
    // régression pour un bug découvert par ce test (cf. canvas_view.cpp) où
    // le rectangle final utilisait par erreur la position du glisser
    // *précédent* (fromScenePoint/toScenePoint du signal Qt, en retard d'un
    // cran sur viewportRect).
    const QRectF expected = view.mapToScene(QRect(from, to).normalized()).boundingRect();

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
    // Plusieurs pas de déplacement : le rubber band ne démarre qu'après avoir
    // dépassé le seuil de démarrage du glisser de Qt.
    QTest::mouseMove(view.viewport(), mid);
    QTest::mouseMove(view.viewport(), to);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, to);

    QCOMPARE(cropSpy.count(), 1);
    const QRectF rectMm = cropSpy.at(0).at(0).toRectF();
    QVERIFY(std::abs(rectMm.left() - expected.left()) < 1.0);
    QVERIFY(std::abs(rectMm.top() - expected.top()) < 1.0);
    QVERIFY(std::abs(rectMm.right() - expected.right()) < 1.0);
    QVERIFY(std::abs(rectMm.bottom() - expected.bottom()) < 1.0);
}

QTEST_MAIN(CanvasViewTest)
#include "test_canvas_view.moc"
