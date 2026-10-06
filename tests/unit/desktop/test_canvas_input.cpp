// SPDX-License-Identifier: Apache-2.0
// Caractérisation des entrées souris/clavier de CanvasView (lot L0 de la modernisation
// UI, docs/ui-audit-2026-10.md §3 bis et §4) : ce que le canevas fait AUJOURD'HUI de
// chaque geste. Les lignes QEXPECT_FAIL(Continue) décrivent le comportement CIBLE du
// lot L5 (clic molette = panoramique, Espace + glisser, pavé tactile...) : la suite
// reste verte, et dès que L5 implémente le geste le test échoue en « XPASS » jusqu'à ce
// que le marqueur soit retiré. Évènements injectés (QTest::mouse*, QWheelEvent), aucun
// sleep, aucune comparaison de pixels.
#include <QApplication>
#include <QGraphicsScene>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

#include <cmath>

#include "canvas_view.hpp"

using openstitch::desktop::CanvasView;

namespace {

// Vue zoomée (~3 px/mm) : la scène (canevas 100 mm + marge) dépasse le viewport de
// 400 px, donc les barres de défilement ont de la course dans les deux sens ; elles
// sont recentrées pour qu'un panoramique soit possible dans toutes les directions.
void prepareView(CanvasView& view) {
    view.setCanvasSizeMm(QSizeF(100.0, 100.0));
    view.resize(400, 400);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    for (int i = 0; i < 8; ++i) {
        view.zoomIn();
    }
    auto* h = view.horizontalScrollBar();
    auto* v = view.verticalScrollBar();
    QVERIFY(h->maximum() > h->minimum());
    QVERIFY(v->maximum() > v->minimum());
    h->setValue((h->minimum() + h->maximum()) / 2);
    v->setValue((v->minimum() + v->maximum()) / 2);
}

QPoint scrollPos(const CanvasView& view) {
    return {view.horizontalScrollBar()->value(), view.verticalScrollBar()->value()};
}

// Molette injectée sur le viewport. angle : delta en huitièmes de degré (120 = un cran).
void sendWheel(CanvasView& view, const QPoint& at, const QPoint& angleDelta,
               const QPoint& pixelDelta = QPoint(),
               Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QWheelEvent event(QPointF(at), QPointF(view.viewport()->mapToGlobal(at)), pixelDelta,
                      angleDelta, Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &event);
}

// Glisser (appui, deux déplacements, relâchement) avec un bouton donné.
void drag(CanvasView& view, Qt::MouseButton button, const QPoint& from, const QPoint& to,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QPoint mid((from.x() + to.x()) / 2, (from.y() + to.y()) / 2);
    QTest::mousePress(view.viewport(), button, modifiers, from);
    QTest::mouseMove(view.viewport(), mid);
    QTest::mouseMove(view.viewport(), to);
    QTest::mouseRelease(view.viewport(), button, modifiers, to);
}

} // namespace

class CanvasInputTest : public QObject {
    Q_OBJECT

private slots:
    // ---- clic gauche et modificateurs ----------------------------------------------

    void leftClickEmitsCanvasClickedAtSceneCoordinates() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint at(150, 120);
        const QPointF expected = view.mapToScene(at);

        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, at);

        QCOMPARE(clicked.count(), 1);
        QVERIFY(std::abs(clicked.at(0).at(0).toPointF().x() - expected.x()) < 0.5);
        QVERIFY(std::abs(clicked.at(0).at(0).toPointF().y() - expected.y()) < 0.5);
        // Le signal ne porte QUE la position : aucune information de modificateur.
        QCOMPARE(clicked.at(0).size(), 1);
    }

    // Ctrl+clic et Maj+clic sont aujourd'hui indistinguables d'un clic simple : même
    // signal, même position, aucun modificateur transmis (cible L5 : Ctrl = basculer,
    // Maj = ajouter).
    void ctrlClickAndShiftClickBehaveLikePlainClick() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint at(180, 210);

        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier, at);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, at);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::AltModifier, at);

        QCOMPARE(clicked.count(), 4);
        const QPointF plain = clicked.at(0).at(0).toPointF();
        for (int i = 1; i < 4; ++i) {
            QCOMPARE(clicked.at(i).at(0).toPointF(), plain);
            QCOMPARE(clicked.at(i).size(), 1);
        }
    }

    void rightClickEmitsContextMenuNotCanvasClicked() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        QSignalSpy menu(&view, &CanvasView::canvasContextMenu);
        const QPoint at(150, 150);

        QContextMenuEvent event(QContextMenuEvent::Mouse, at, view.viewport()->mapToGlobal(at));
        QApplication::sendEvent(view.viewport(), &event);

        QCOMPARE(clicked.count(), 0);
        QCOMPARE(menu.count(), 1);
    }

    // ---- glisser gauche dans le vide : panoramique ---------------------------------

    void leftDragInEmptyAreaPansTheViewAndStillEmitsOneClick() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint before = scrollPos(view);

        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(150, 170));

        // ScrollHandDrag : le contenu suit la main -> les barres bougent en sens inverse.
        const QPoint after = scrollPos(view);
        QVERIFY(after != before);
        QVERIFY2(after.x() > before.x(), "glisser vers la gauche -> défilement horizontal +");
        QVERIFY2(after.y() > before.y(), "glisser vers le haut -> défilement vertical +");
        // Le même appui émet aussi un « clic » (désélection) : glisser dans le vide ne
        // sélectionne donc pas au rectangle (cible L5 : englobe/croise).
        QCOMPARE(clicked.count(), 1);
    }

    // ---- molette --------------------------------------------------------------------

    void wheelUpZoomsInAndWheelDownZoomsOutByTheSameStep() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const QPoint at(200, 200);
        QSignalSpy changed(&view, &CanvasView::viewChanged);
        const double initial = view.pixelsPerMm();

        sendWheel(view, at, QPoint(0, 120));
        const double zoomedIn = view.pixelsPerMm();
        QVERIFY(zoomedIn > initial);
        QVERIFY(std::abs(zoomedIn / initial - 1.15) < 1e-6); // kZoomStep par cran
        QVERIFY(changed.count() >= 1);

        sendWheel(view, at, QPoint(0, -120));
        QVERIFY(std::abs(view.pixelsPerMm() - initial) < 1e-6);
    }

    // Ancrage : le point de la scène sous le curseur reste sous le curseur (à quelques
    // pixels près : arrondi des barres de défilement).
    void wheelZoomKeepsTheSceneAnchorUnderTheCursor() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const QPoint at(120, 300);
        // QGraphicsView mémorise la dernière position souris pour AnchorUnderMouse.
        QTest::mouseMove(view.viewport(), at);
        const QPointF anchorBefore = view.mapToScene(at);

        sendWheel(view, at, QPoint(0, 120));
        sendWheel(view, at, QPoint(0, 120));

        const QPointF anchorAfter = view.mapToScene(at);
        const double tolerance = 2.0 / view.pixelsPerMm(); // 2 px écran, en mm
        QVERIFY2(std::abs(anchorAfter.x() - anchorBefore.x()) < tolerance,
                 qPrintable(QString::number(anchorAfter.x() - anchorBefore.x())));
        QVERIFY2(std::abs(anchorAfter.y() - anchorBefore.y()) < tolerance,
                 qPrintable(QString::number(anchorAfter.y() - anchorBefore.y())));
    }

    void wheelZoomIsClampedToTheSupportedRange() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        for (int i = 0; i < 80; ++i) {
            sendWheel(view, QPoint(200, 200), QPoint(0, 120));
        }
        QVERIFY(view.pixelsPerMm() <= 400.0 + 1e-6);
        QVERIFY(view.pixelsPerMm() > 399.0);
        for (int i = 0; i < 200; ++i) {
            sendWheel(view, QPoint(200, 200), QPoint(0, -120));
        }
        QVERIFY(view.pixelsPerMm() >= 0.2 - 1e-9);
        QVERIFY(view.pixelsPerMm() < 0.21);
    }

    // Les modificateurs sont ignorés par la molette : Maj/Ctrl/Alt + molette zooment
    // comme la molette seule (cible L5 : Maj/Alt = défilement horizontal/vertical).
    void wheelWithModifiersZoomsLikePlainWheel() {
        for (const Qt::KeyboardModifiers mod :
             {Qt::KeyboardModifiers(Qt::ShiftModifier), Qt::KeyboardModifiers(Qt::ControlModifier),
              Qt::KeyboardModifiers(Qt::AltModifier)}) {
            QGraphicsScene scene;
            CanvasView view(&scene);
            prepareView(view);
            const double initial = view.pixelsPerMm();
            sendWheel(view, QPoint(200, 200), QPoint(0, 120), QPoint(), mod);
            QVERIFY(view.pixelsPerMm() > initial);
            QVERIFY(std::abs(view.pixelsPerMm() / initial - 1.15) < 1e-6);
        }
    }

    void shiftWheelZoomsInsteadOfScrollingHorizontallyYet() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double zoom = view.pixelsPerMm();
        sendWheel(view, QPoint(200, 200), QPoint(0, 120), QPoint(), Qt::ShiftModifier);
        // Aujourd'hui Maj + molette zoome (l'ancrage déplace les barres comme effet de
        // bord) ; cible L5 : défilement horizontal SANS changement de zoom.
        QEXPECT_FAIL("", "cible L5 : Maj + molette = défilement horizontal (audit §3 bis)",
                     Continue);
        QCOMPARE(view.pixelsPerMm(), zoom);
    }

    // Pavé tactile : un évènement qui ne porte que pixelDelta (angleDelta nul) est
    // consommé sans effet -- ni zoom, ni panoramique.
    void pixelDeltaOnlyWheelIsIgnored() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double zoom = view.pixelsPerMm();
        const QPoint before = scrollPos(view);

        QWheelEvent event(QPointF(200, 200),
                          QPointF(view.viewport()->mapToGlobal(QPoint(200, 200))), QPoint(0, 40),
                          QPoint(0, 0), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(view.viewport(), &event);

        QVERIFY(event.isAccepted());
        QCOMPARE(view.pixelsPerMm(), zoom);
        QEXPECT_FAIL("", "cible L5 : pixelDelta (pavé tactile) = panoramique (audit §3 bis)",
                     Continue);
        QVERIFY(scrollPos(view) != before);
    }

    // ---- clic molette et Espace ------------------------------------------------------

    void middleButtonDragDoesNotPanNorClick() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint before = scrollPos(view);

        drag(view, Qt::MiddleButton, QPoint(200, 200), QPoint(150, 170));

        // Comportement actuel : le bouton du milieu est entièrement ignoré.
        QCOMPARE(clicked.count(), 0);
        QEXPECT_FAIL("", "cible L5 : clic molette + glisser = panoramique (audit §3 bis)",
                     Continue);
        QVERIFY(scrollPos(view) != before);
    }

    // Espace n'a aucun rôle : Espace + glisser se comporte exactement comme le glisser
    // seul (ici : panoramique, parce que le glisser gauche pan déjà).
    void spaceKeyHasNoEffectOnLeftDrag() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setFocus();
        const QPoint start = scrollPos(view);

        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(160, 180));
        const QPoint plainDelta = scrollPos(view) - start;
        view.horizontalScrollBar()->setValue(start.x());
        view.verticalScrollBar()->setValue(start.y());

        QTest::keyPress(&view, Qt::Key_Space);
        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(160, 180));
        QTest::keyRelease(&view, Qt::Key_Space);
        QCOMPARE(scrollPos(view) - start, plainDelta);
    }

    // Cible L5 : avec un outil de dessin actif (ici le cadre élastique), Espace + glisser
    // doit panoramiquer au lieu de dessiner. Aujourd'hui : le cadre est dessiné, la vue
    // ne bouge pas.
    void spaceDragDuringBoxDrawToolDrawsInsteadOfPanning() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setFocus();
        view.setBoxDrawMode(true);
        QSignalSpy box(&view, &CanvasView::boxDrawnMm);
        const QPoint before = scrollPos(view);

        QTest::keyPress(&view, Qt::Key_Space);
        drag(view, Qt::LeftButton, QPoint(100, 100), QPoint(220, 200));
        QTest::keyRelease(&view, Qt::Key_Space);

        QCOMPARE(box.count(), 1);
        QEXPECT_FAIL("", "cible L5 : Espace + glisser = panoramique, même en dessin (§3 bis)",
                     Continue);
        QVERIFY(scrollPos(view) != before);
    }

    // ---- flèches ------------------------------------------------------------------------

    void arrowKeysRequestNudgesOfTenthOfMillimeterAndShiftOneMillimeter() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setFocus();
        QSignalSpy nudge(&view, &CanvasView::nudgeRequestedMm);

        QTest::keyClick(&view, Qt::Key_Left);
        QTest::keyClick(&view, Qt::Key_Right);
        QTest::keyClick(&view, Qt::Key_Up);
        QTest::keyClick(&view, Qt::Key_Down);
        QTest::keyClick(&view, Qt::Key_Left, Qt::ShiftModifier);
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(&view, Qt::Key_Up, Qt::ShiftModifier);
        QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier);

        const QList<QPointF> expected = {
            {-0.1, 0.0}, {0.1, 0.0}, {0.0, -0.1}, {0.0, 0.1},
            {-1.0, 0.0}, {1.0, 0.0}, {0.0, -1.0}, {0.0, 1.0},
        };
        QCOMPARE(nudge.count(), expected.size());
        for (int i = 0; i < expected.size(); ++i) {
            const QPointF got = nudge.at(i).at(0).toPointF();
            QVERIFY2(std::abs(got.x() - expected[i].x()) < 1e-9 &&
                         std::abs(got.y() - expected[i].y()) < 1e-9,
                     qPrintable(QStringLiteral("index %1").arg(i)));
        }
    }

    // Ctrl/Alt + flèche : même pas qu'une flèche seule (seul Maj compte aujourd'hui).
    void ctrlAndAltDoNotChangeTheNudgeStep() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setFocus();
        QSignalSpy nudge(&view, &CanvasView::nudgeRequestedMm);

        QTest::keyClick(&view, Qt::Key_Right, Qt::ControlModifier);
        QTest::keyClick(&view, Qt::Key_Right, Qt::AltModifier);

        QCOMPARE(nudge.count(), 2);
        QCOMPARE(nudge.at(0).at(0).toPointF(), QPointF(0.1, 0.0));
        QCOMPARE(nudge.at(1).at(0).toPointF(), QPointF(0.1, 0.0));
    }

    // ---- Maj contraint l'ellipse (côté canevas : le modificateur voyage avec le cadre) ---

    void boxDrawReportsShiftFromTheReleaseEvent() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setBoxDrawMode(true);
        QSignalSpy box(&view, &CanvasView::boxDrawnMm);

        drag(view, Qt::LeftButton, QPoint(100, 100), QPoint(260, 200));
        drag(view, Qt::LeftButton, QPoint(100, 100), QPoint(260, 200), Qt::ShiftModifier);

        QCOMPARE(box.count(), 2);
        const auto first = box.at(0).at(1).value<Qt::KeyboardModifiers>();
        const auto second = box.at(1).at(1).value<Qt::KeyboardModifiers>();
        QVERIFY(!(first & Qt::ShiftModifier));
        QVERIFY(second & Qt::ShiftModifier);
        // Le cadre lui-même n'est pas contraint par la vue : c'est MainWindow::onBoxDrawn
        // qui en fait un cercle (test_main_window : drawEllipseWithShiftConstrainsToCircle).
        const QRectF rect = box.at(1).at(0).toRectF();
        QVERIFY(std::abs(rect.width() - rect.height()) > 1.0);
    }

    // Maj ne change rien au clic simple ni au glisser de panoramique hors outil de dessin.
    void shiftDragInEmptyAreaStillPans() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const QPoint before = scrollPos(view);
        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(150, 170), Qt::ShiftModifier);
        QVERIFY(scrollPos(view) != before);
    }
};

QTEST_MAIN(CanvasInputTest)
#include "test_canvas_input.moc"
