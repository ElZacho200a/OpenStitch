// SPDX-License-Identifier: Apache-2.0
// Entrées souris/clavier de CanvasView (lot L0 : caractérisation, lot L5 : modèle
// d'interaction, docs/ui-audit-2026-10.md §3 bis et §4, specs/plans/ui-interaction-model.md).
// Les tests « isolés » (sans setBaseContext) figent le comportement historique
// (ScrollHandDrag) ; les tests « modèle activé » appellent setBaseContext(Select)
// + setSelectionRectangleEnabled(true). Évènements injectés (QTest::mouse*,
// QWheelEvent), aucun sleep, aucune comparaison de pixels.
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

#include "canvas_view.hpp"
#include "native_gesture_helper.hpp"

using openstitch::desktop::CanvasView;
using openstitch::desktop::Context;
using openstitch::desktop::InteractionMap;
using openstitch::desktop::Preset;
using openstitch::desktop::SelectMode;

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

// Vue en mode « modèle d'interaction » (outil Sélection).
void prepareSelectView(CanvasView& view) {
    prepareView(view);
    view.setBaseContext(Context::Select);
    view.setSelectionRectangleEnabled(true);
}

// Évènement souris explicite (modificateurs maîtrisés, état clavier global ignoré).
void sendMouse(CanvasView& view, QEvent::Type type, const QPoint& at, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) {
    QMouseEvent event(type, QPointF(at), QPointF(view.viewport()->mapToGlobal(at)), button, buttons,
                      mods);
    QApplication::sendEvent(view.viewport(), &event);
}

bool nearlyEqual(const QPointF& a, const QPointF& b, double tol) {
    return std::abs(a.x() - b.x()) <= tol && std::abs(a.y() - b.y()) <= tol;
}

// Point de scène sous un point du viewport, en flottant (pas d'arrondi entier).
QPointF sceneUnder(const CanvasView& view, const QPointF& viewportPos) {
    return view.viewportTransform().inverted().map(viewportPos);
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

    // Ctrl + molette zoome comme la molette seule (G13 : le pincement Windows arrive
    // ainsi). Maj/Alt + molette défilent désormais (G6/G7, tests dédiés).
    void wheelWithCtrlZoomsLikePlainWheel() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double initial = view.pixelsPerMm();
        sendWheel(view, QPoint(200, 200), QPoint(0, 120), QPoint(), Qt::ControlModifier);
        QVERIFY(view.pixelsPerMm() > initial);
        QVERIFY(std::abs(view.pixelsPerMm() / initial - 1.15) < 1e-6);
    }

    // G6 : Maj + molette = défilement horizontal, SANS changement de zoom.
    void shiftWheelScrollsHorizontallyWithoutZoom() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double zoom = view.pixelsPerMm();
        const QPoint before = scrollPos(view);
        sendWheel(view, QPoint(200, 200), QPoint(0, 120), QPoint(), Qt::ShiftModifier);
        QCOMPARE(view.pixelsPerMm(), zoom);
        QVERIFY(scrollPos(view).x() < before.x()); // cran vers le haut : vers la gauche
        QCOMPARE(scrollPos(view).y(), before.y());
    }

    // G7 : Alt + molette = défilement vertical.
    void altWheelScrollsVerticallyWithoutZoom() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double zoom = view.pixelsPerMm();
        const QPoint before = scrollPos(view);
        sendWheel(view, QPoint(200, 200), QPoint(0, -120), QPoint(), Qt::AltModifier);
        QCOMPARE(view.pixelsPerMm(), zoom);
        QVERIFY(scrollPos(view).y() > before.y());
        QCOMPARE(scrollPos(view).x(), before.x());
    }

    // Qt échange x/y avec Alt sur certaines plates-formes : delta porté par x seul.
    void altWheelWithXOnlyAngleDeltaScrollsVertically() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double zoom = view.pixelsPerMm();
        const QPoint before = scrollPos(view);
        sendWheel(view, QPoint(200, 200), QPoint(120, 0), QPoint(), Qt::AltModifier);
        QCOMPARE(view.pixelsPerMm(), zoom);
        QVERIFY(scrollPos(view).y() < before.y());
        QCOMPARE(scrollPos(view).x(), before.x());
    }

    // G1 : zoom AVANT ancré au curseur (<= 1 px), sans dépendre de QCursor::pos().
    void wheelZoomInKeepsAnchor() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const QPoint at(120, 300);
        const QPointF before = sceneUnder(view, QPointF(at));
        sendWheel(view, at, QPoint(0, 120));
        const QPointF after = sceneUnder(view, QPointF(at));
        QVERIFY(nearlyEqual(before, after, 1.0 / view.pixelsPerMm()));
    }

    // Deltas < 120 (pavés tactiles Windows) : zoom proportionnel.
    void fractionalAngleDeltaZoomsProportionally() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double initial = view.pixelsPerMm();
        sendWheel(view, QPoint(200, 200), QPoint(0, 30));
        QVERIFY(std::abs(view.pixelsPerMm() / initial - std::pow(1.15, 0.25)) < 1e-6);
    }

    // Un coup de molette violent est plafonné à 3 crans.
    void hugeAngleDeltaIsCapped() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double initial = view.pixelsPerMm();
        sendWheel(view, QPoint(200, 200), QPoint(0, 12000));
        QVERIFY(std::abs(view.pixelsPerMm() / initial - std::pow(1.15, 3.0)) < 1e-6);
    }

    // G13 : Ctrl + molette zoome dans les deux préréglages.
    void ctrlWheelZoomsInBothPresets() {
        const Preset original = InteractionMap::preset();
        const auto restore = qScopeGuard([&] { InteractionMap::setPreset(original); });
        for (const Preset preset : {Preset::OpenStitch, Preset::Touchpad}) {
            InteractionMap::setPreset(preset);
            QGraphicsScene scene;
            CanvasView view(&scene);
            prepareView(view);
            const double initial = view.pixelsPerMm();
            sendWheel(view, QPoint(200, 200), QPoint(0, 120), QPoint(), Qt::ControlModifier);
            QVERIFY(view.pixelsPerMm() > initial);
        }
    }

    // G8 (macOS / Wayland ; Windows ne livre pas de pixelDelta) : défilement à
    // deux doigts = panoramique, jamais zoom.
    void pixelDeltaWheelPansNotZooms() {
#ifdef Q_OS_WIN
        QSKIP("Windows ne livre pas pixelDelta pour le pavé tactile");
#endif
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double zoom = view.pixelsPerMm();
        const QPoint before = scrollPos(view);
        sendWheel(view, QPoint(200, 200), QPoint(0, 120), QPoint(-30, 20));
        QCOMPARE(view.pixelsPerMm(), zoom);
        QCOMPARE(scrollPos(view), before + QPoint(30, -20));
    }

    // G9 : pincement natif (macOS / Wayland) = zoom ancré au curseur.
    void nativePinchZoomsAtCursor() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const QPoint at(130, 270);
        const double initial = view.pixelsPerMm();
        const QPointF before = sceneUnder(view, QPointF(at));
        const auto zoomEvent = makeNativeGesture(Qt::ZoomNativeGesture, QPointF(at), 0.5);
        QApplication::sendEvent(view.viewport(), zoomEvent.get());
        QVERIFY(std::abs(view.pixelsPerMm() / initial - 1.5) < 1e-6);
        QVERIFY(nearlyEqual(before, sceneUnder(view, QPointF(at)), 1.0 / view.pixelsPerMm()));

        const QPoint scrollBefore = scrollPos(view);
        const auto panEvent =
            makeNativeGesture(Qt::PanNativeGesture, QPointF(at), 0.0, QPointF(10, 5));
        QApplication::sendEvent(view.viewport(), panEvent.get());
        QCOMPARE(scrollPos(view), scrollBefore - QPoint(10, 5));
    }

    // Pavé tactile : un évènement qui ne porte que pixelDelta (angleDelta nul) panoramique
    // sans jamais zoomer.
    void pixelDeltaOnlyWheelPans() {
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
        QVERIFY(scrollPos(view) != before);
    }

    // ---- clic molette et Espace ------------------------------------------------------

    void middleButtonDragPansAndDoesNotClick() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint before = scrollPos(view);

        drag(view, Qt::MiddleButton, QPoint(200, 200), QPoint(150, 170));

        QCOMPARE(clicked.count(), 0);
        QVERIFY(scrollPos(view) != before);
        QVERIFY2(scrollPos(view).x() > before.x(), "glisser vers la gauche -> défilement +");
        QVERIFY2(scrollPos(view).y() > before.y(), "glisser vers le haut -> défilement +");
    }

    // Vue isolée : Espace + glisser panoramique exactement comme le glisser seul
    // (ScrollHandDrag historique) -> même déplacement.
    void spaceDragPansLikePlainDragOnIsolatedView() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setFocus();
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint start = scrollPos(view);

        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(160, 180));
        const QPoint plainDelta = scrollPos(view) - start;
        QCOMPARE(clicked.count(), 1); // le glisser seul émet encore son « clic »
        view.horizontalScrollBar()->setValue(start.x());
        view.verticalScrollBar()->setValue(start.y());

        QTest::keyPress(&view, Qt::Key_Space);
        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(160, 180));
        QTest::keyRelease(&view, Qt::Key_Space);
        QCOMPARE(scrollPos(view) - start, plainDelta);
        QCOMPARE(clicked.count(), 1); // Espace + glisser : aucun clic de plus
    }

    // Avec un outil de dessin actif (ici le cadre élastique), Espace + glisser
    // panoramique au lieu de dessiner.
    void spaceDragDuringBoxDrawToolPansInsteadOfDrawing() {
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

        QCOMPARE(box.count(), 0);
        QVERIFY(scrollPos(view) != before);
    }

    // ---- Espace : filtre applicatif, focus, remise à zéro ---------------------------------

    void spaceDragPansViewAndResetsOnFocusOut() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setBoxDrawMode(true);
        view.setFocus();
        QSignalSpy box(&view, &CanvasView::boxDrawnMm);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint before = scrollPos(view);

        QTest::keyPress(&view, Qt::Key_Space);
        QVERIFY(view.spaceHeld());
        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(150, 170));
        QVERIFY(scrollPos(view) != before);
        QCOMPARE(box.count(), 0);
        QCOMPARE(clicked.count(), 0);

        // Perte de focus : la touche n'est plus considérée comme tenue.
        QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
        QApplication::sendEvent(&view, &out);
        QVERIFY(!view.spaceHeld());
        drag(view, Qt::LeftButton, QPoint(100, 100), QPoint(220, 200));
        QCOMPARE(box.count(), 1);
    }

    void spaceHeldIsClearedWhenViewIsHidden() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setFocus();
        QTest::keyPress(&view, Qt::Key_Space);
        QVERIFY(view.spaceHeld());
        view.hide();
        QVERIFY(!view.spaceHeld());
    }

    void spaceDoesNotClickFocusedButton() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* view = new CanvasView(new QGraphicsScene(&host));
        auto* button = new QPushButton(QStringLiteral("ok"));
        layout->addWidget(view, 1);
        layout->addWidget(button);
        view->setCanvasSizeMm(QSizeF(100.0, 100.0));
        host.resize(400, 500);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        button->setFocus();
        QSignalSpy clicks(button, &QPushButton::clicked);
        QEvent initialLeave(QEvent::Leave); // l'offscreen peut avoir envoyé un Enter à l'affichage
        QApplication::sendEvent(view->viewport(), &initialLeave);

        // Curseur hors du viewport : Espace clique le bouton (comportement Qt normal).
        QTest::keyClick(button, Qt::Key_Space);
        QCOMPARE(clicks.count(), 1);

        // Curseur sur le viewport : Espace est consommé, le bouton ne reçoit rien.
        QEnterEvent enter(QPointF(50, 50), QPointF(50, 50), QPointF(50, 50));
        QApplication::sendEvent(view->viewport(), &enter);
        QTest::keyPress(button, Qt::Key_Space);
        QVERIFY(view->spaceHeld());
        QTest::keyRelease(button, Qt::Key_Space);
        QVERIFY(!view->spaceHeld());
        QCOMPARE(clicks.count(), 1);

        // Le curseur quitte le viewport : le comportement normal revient.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(view->viewport(), &leave);
        QTest::keyClick(button, Qt::Key_Space);
        QCOMPARE(clicks.count(), 2);
    }

    void spaceIgnoredWhenSpinBoxHasFocus() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* view = new CanvasView(new QGraphicsScene(&host));
        auto* spin = new QSpinBox;
        layout->addWidget(view, 1);
        layout->addWidget(spin);
        view->setCanvasSizeMm(QSizeF(100.0, 100.0));
        host.resize(400, 500);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        spin->setFocus();
        QEnterEvent enter(QPointF(50, 50), QPointF(50, 50), QPointF(50, 50));
        QApplication::sendEvent(view->viewport(), &enter);

        QTest::keyPress(spin, Qt::Key_Space);
        QVERIFY(!view->spaceHeld());
        QTest::keyRelease(spin, Qt::Key_Space);
    }

    void spaceFilterRemovedWithView() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* view = new CanvasView(new QGraphicsScene(&host));
        auto* button = new QPushButton(QStringLiteral("ok"));
        layout->addWidget(view, 1);
        layout->addWidget(button);
        host.resize(400, 500);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        button->setFocus();
        QEnterEvent enter(QPointF(50, 50), QPointF(50, 50), QPointF(50, 50));
        QApplication::sendEvent(view->viewport(), &enter);
        delete view;

        QSignalSpy clicks(button, &QPushButton::clicked);
        QTest::keyClick(button, Qt::Key_Space);
        QCOMPARE(clicks.count(), 1);
    }

    // ---- clic molette : zoom continu, double-clic -----------------------------------------

    void ctrlShiftMiddleDragZoomsContinuously() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint anchor(200, 200);
        const double initial = view.pixelsPerMm();
        const QPointF before = sceneUnder(view, QPointF(anchor));

        // Vers le haut : zoom avant, ancré au point d'appui.
        drag(view, Qt::MiddleButton, anchor, QPoint(200, 150),
             Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(view.pixelsPerMm() > initial);
        QVERIFY(std::abs(view.pixelsPerMm() / initial - std::exp(0.5)) < 1e-6);
        QVERIFY(nearlyEqual(before, sceneUnder(view, QPointF(anchor)), 2.0 / view.pixelsPerMm()));
        QCOMPARE(clicked.count(), 0);

        const double zoomed = view.pixelsPerMm();
        drag(view, Qt::MiddleButton, anchor, QPoint(200, 250),
             Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(view.pixelsPerMm() < zoomed);
    }

    void middleDoubleClickFitsCanvasWithoutGhostPan() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double initial = view.pixelsPerMm();
        CanvasView reference(new QGraphicsScene);
        prepareView(reference);
        reference.fitCanvas();

        const QPoint at(200, 200);
        sendMouse(view, QEvent::MouseButtonPress, at, Qt::MiddleButton, Qt::MiddleButton,
                  Qt::NoModifier);
        sendMouse(view, QEvent::MouseButtonRelease, at, Qt::MiddleButton, Qt::NoButton,
                  Qt::NoModifier);
        QCOMPARE(view.pixelsPerMm(), initial); // le premier press/release ne bouge rien
        sendMouse(view, QEvent::MouseButtonDblClick, at, Qt::MiddleButton, Qt::MiddleButton,
                  Qt::NoModifier);
        const double fitted = view.pixelsPerMm();
        const QPoint fittedScroll = scrollPos(view);
        QVERIFY(fitted != initial);
        QVERIFY(std::abs(fitted / reference.pixelsPerMm() - 1.0) < 0.05);

        sendMouse(view, QEvent::MouseButtonRelease, at, Qt::MiddleButton, Qt::NoButton,
                  Qt::NoModifier);
        // Un mouvement sans bouton ensuite ne doit rien déplacer (pas de pan fantôme).
        sendMouse(view, QEvent::MouseMove, QPoint(260, 240), Qt::NoButton, Qt::NoButton,
                  Qt::NoModifier);
        QCOMPARE(view.pixelsPerMm(), fitted);
        QCOMPARE(scrollPos(view), fittedScroll);
        delete reference.scene();
    }

    // ---- outil Pan et NoDrag ---------------------------------------------------------------

    void isolatedViewKeepsScrollHandDrag() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        QVERIFY(!view.inputModelEnabled());
        QCOMPARE(view.dragMode(), QGraphicsView::ScrollHandDrag);
        view.setBaseContext(Context::Select);
        QVERIFY(view.inputModelEnabled());
        QCOMPARE(view.dragMode(), QGraphicsView::NoDrag);
    }

    void panToolLeftDragPansWithNoDragMode() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setBaseContext(Context::Pan);
        QCOMPARE(view.dragMode(), QGraphicsView::NoDrag);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint before = scrollPos(view);

        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(150, 170));

        QVERIFY(scrollPos(view).x() > before.x());
        QVERIFY(scrollPos(view).y() > before.y());
        QCOMPARE(clicked.count(), 0);
    }

    void panWorksWhileFreeformToolActive() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setFreeformDrawMode(true);
        QSignalSpy points(&view, &CanvasView::freeformPointMm);
        QSignalSpy finished(&view, &CanvasView::freeformStrokeFinished);
        const QPoint before = scrollPos(view);

        drag(view, Qt::MiddleButton, QPoint(200, 200), QPoint(150, 170));

        QVERIFY(scrollPos(view) != before);
        QCOMPARE(points.count(), 0);
        QCOMPARE(finished.count(), 0);
    }

    void panWorksWhileBezierToolActive() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setBezierDrawMode(true);
        QSignalSpy dragging(&view, &CanvasView::bezierPointDraggingMm);
        QSignalSpy committed(&view, &CanvasView::bezierPointCommittedMm);
        const QPoint before = scrollPos(view);

        drag(view, Qt::MiddleButton, QPoint(200, 200), QPoint(150, 170));

        QVERIFY(scrollPos(view) != before);
        QCOMPARE(dragging.count(), 0);
        QCOMPARE(committed.count(), 0);
    }

    // ---- sélection (contexte Select, modèle activé) ---------------------------------------

    void plainClickEmitsCanvasClickedMm() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        QSignalSpy added(&view, &CanvasView::selectionClickedMm);
        const QPoint at(150, 120);
        const QPointF expected = view.mapToScene(at);

        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, at);

        QCOMPARE(clicked.count(), 1);
        QVERIFY(nearlyEqual(clicked.at(0).at(0).toPointF(), expected, 0.5));
        QCOMPARE(added.count(), 0);
    }

    void clickIsDeferredToReleaseInSelectContext() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
        QCOMPARE(clicked.count(), 0);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
        QCOMPARE(clicked.count(), 1);
    }

    void shiftClickEmitsSelectionClickedAdd() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        QSignalSpy added(&view, &CanvasView::selectionClickedMm);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, QPoint(150, 120));
        QCOMPARE(clicked.count(), 0);
        QCOMPARE(added.count(), 1);
        QCOMPARE(added.at(0).at(1).value<SelectMode>(), SelectMode::Add);
    }

    void ctrlClickEmitsSelectionClickedToggle() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy added(&view, &CanvasView::selectionClickedMm);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier, QPoint(150, 120));
        QCOMPARE(added.count(), 1);
        QCOMPARE(added.at(0).at(1).value<SelectMode>(), SelectMode::Toggle);
    }

    void ctrlShiftClickIsToggle() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy added(&view, &CanvasView::selectionClickedMm);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier,
                          QPoint(150, 120));
        QCOMPARE(added.count(), 1);
        QCOMPARE(added.at(0).at(1).value<SelectMode>(), SelectMode::Toggle);
    }

    void altClickEmitsSelectBelow() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        QSignalSpy below(&view, &CanvasView::selectBelowRequested);
        const QPoint at(150, 120);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::AltModifier, at);
        QCOMPARE(clicked.count(), 0);
        QVERIFY(below.wait(500)); // émission différée (file d'évènements)
        QCOMPARE(below.count(), 1);
        QVERIFY(nearlyEqual(below.at(0).at(0).toPointF(), view.mapToScene(at), 0.5));
        QCOMPARE(below.at(0).at(1).toPoint(), view.viewport()->mapToGlobal(at));
        QCOMPARE(below.at(0).at(2).value<SelectMode>(), SelectMode::Replace);
    }

    void altReleaseIsSwallowedAfterAltClick() {
        struct Counter : QObject {
            int releases = 0;
            bool eventFilter(QObject*, QEvent* e) override {
                if (e->type() == QEvent::KeyRelease) {
                    ++releases;
                }
                return false;
            }
        };
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        view.setFocus();
        Counter counter;
        view.installEventFilter(&counter);

        // Sans geste Alt : le relâchement d'Alt est transmis normalement.
        QTest::mouseMove(view.viewport(), QPoint(150, 120));
        QTest::keyRelease(&view, Qt::Key_Alt);
        QCOMPARE(counter.releases, 1);

        // Après un Alt + clic : avalé (sinon Windows active la barre de menus).
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::AltModifier, QPoint(150, 120));
        QTest::keyRelease(&view, Qt::Key_Alt);
        QCOMPARE(counter.releases, 1);
        // Un seul relâchement avalé par geste.
        QTest::keyRelease(&view, Qt::Key_Alt);
        QCOMPARE(counter.releases, 2);
    }

    void longPressEmitsSelectBelow() {
        InteractionMap::setLongPressMsForTesting(1);
        const auto restore = qScopeGuard([] { InteractionMap::setLongPressMsForTesting(-1); });
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        QSignalSpy below(&view, &CanvasView::selectBelowRequested);

        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
        QVERIFY(below.wait(500));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));

        QCOMPARE(below.count(), 1);
        QCOMPARE(clicked.count(), 0); // l'appui long consommé n'émet pas de clic
    }

    void longPressDelayIsReadFromSettings() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QSettings::Format oldFormat = QSettings::defaultFormat();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        const auto restore = qScopeGuard([&] { QSettings::setDefaultFormat(oldFormat); });
        {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                               QStringLiteral("OpenStitch"), QStringLiteral("OpenStitch Studio"));
            settings.setValue(QStringLiteral("navigation/longPressMs"), 300);
        }
        QCOMPARE(InteractionMap::longPressMs(), 300);

        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy below(&view, &CanvasView::selectBelowRequested);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
        QVERIFY(below.wait(1500));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
        QCOMPARE(below.count(), 1);
    }

    void dragRightwardEmitsWindowRectangle() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy rect(&view, &CanvasView::selectionRectangleMm);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        const QPoint from(100, 100);
        const QPoint to(220, 200);
        const QRectF expected = view.mapToScene(QRect(from, to).normalized()).boundingRect();

        drag(view, Qt::LeftButton, from, to);

        QCOMPARE(rect.count(), 1);
        const QRectF got = rect.at(0).at(0).toRectF();
        QVERIFY(nearlyEqual(got.topLeft(), expected.topLeft(), 0.5));
        QVERIFY(nearlyEqual(got.bottomRight(), expected.bottomRight(), 0.5));
        QCOMPARE(rect.at(0).at(1).value<SelectMode>(), SelectMode::Replace);
        QCOMPARE(rect.at(0).at(2).toBool(), false);
        QCOMPARE(clicked.count(), 0); // dragDoesNotEmitCanvasClicked
    }

    void dragLeftwardEmitsCrossingRectangle() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy rect(&view, &CanvasView::selectionRectangleMm);
        drag(view, Qt::LeftButton, QPoint(220, 200), QPoint(100, 100));
        QCOMPARE(rect.count(), 1);
        QCOMPARE(rect.at(0).at(2).toBool(), true);
    }

    void shiftDragRectangleIsAdd() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy rect(&view, &CanvasView::selectionRectangleMm);
        drag(view, Qt::LeftButton, QPoint(100, 100), QPoint(220, 200), Qt::ShiftModifier);
        QCOMPARE(rect.count(), 1);
        QCOMPARE(rect.at(0).at(1).value<SelectMode>(), SelectMode::Add);
    }

    void ctrlDragRectangleIsToggle() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy rect(&view, &CanvasView::selectionRectangleMm);
        drag(view, Qt::LeftButton, QPoint(100, 100), QPoint(220, 200), Qt::ControlModifier);
        QCOMPARE(rect.count(), 1);
        QCOMPARE(rect.at(0).at(1).value<SelectMode>(), SelectMode::Toggle);
    }

    void shiftPressOnSelectedBodyTogglesInsteadOfDragging() {
        QGraphicsScene scene;
        auto* body = new QGraphicsRectItem(-10, -10, 20, 20);
        body->setFlag(QGraphicsItem::ItemIsMovable);
        scene.addItem(body);
        CanvasView view(&scene);
        prepareSelectView(view);
        const QPoint center = view.mapFromScene(QPointF(0, 0));
        QSignalSpy added(&view, &CanvasView::selectionClickedMm);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);

        // Maj + clic sur le corps : sélection (ajout), pas de glisser d'item.
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, center);
        QCOMPARE(added.count(), 1);
        QCOMPARE(body->pos(), QPointF(0, 0));

        // Maj + glisser depuis le corps : rectangle, l'item ne bouge pas.
        QSignalSpy rect(&view, &CanvasView::selectionRectangleMm);
        drag(view, Qt::LeftButton, center, center + QPoint(40, 30), Qt::ShiftModifier);
        QCOMPARE(rect.count(), 1);
        QCOMPARE(body->pos(), QPointF(0, 0));

        // Sans modificateur : glisser d'objet inchangé (M1), aucun signal de sélection.
        drag(view, Qt::LeftButton, center, center + QPoint(40, 30));
        QVERIFY(body->pos() != QPointF(0, 0));
        QCOMPARE(clicked.count(), 0);
        QCOMPARE(added.count(), 1);
        QCOMPARE(rect.count(), 1);
    }

    void selectionBehaviourIsInertWithoutBaseContext() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        view.setSelectionRectangleEnabled(true); // sans setBaseContext : jamais d'effet
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        QSignalSpy rect(&view, &CanvasView::selectionRectangleMm);
        drag(view, Qt::LeftButton, QPoint(200, 200), QPoint(150, 170));
        QCOMPARE(rect.count(), 0);
        QCOMPARE(clicked.count(), 1);
    }

    // ---- modificateurs et curseur ---------------------------------------------------------

    void modifiersComeFromEventNotGlobalState() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy mods(&view, &CanvasView::modifiersChanged);

        sendMouse(view, QEvent::MouseMove, QPoint(100, 100), Qt::NoButton, Qt::NoButton,
                  Qt::ShiftModifier);
        QCOMPARE(mods.count(), 1);
        QCOMPARE(mods.at(0).at(0).value<Qt::KeyboardModifiers>(),
                 Qt::KeyboardModifiers(Qt::ShiftModifier));

        // Appui sur Ctrl : la touche elle-même est comptée, même si l'évènement ne
        // porte pas son propre modificateur.
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Control, Qt::ShiftModifier);
        QApplication::sendEvent(&view, &press);
        QCOMPARE(mods.count(), 2);
        QCOMPARE(mods.at(1).at(0).value<Qt::KeyboardModifiers>(),
                 Qt::KeyboardModifiers(Qt::ShiftModifier | Qt::ControlModifier));
    }

    void modifiersChangedEmittedOnlyOnChange() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy mods(&view, &CanvasView::modifiersChanged);
        sendMouse(view, QEvent::MouseMove, QPoint(100, 100), Qt::NoButton, Qt::NoButton,
                  Qt::NoModifier);
        QCOMPARE(mods.count(), 0);
        sendMouse(view, QEvent::MouseMove, QPoint(101, 100), Qt::NoButton, Qt::NoButton,
                  Qt::ShiftModifier);
        sendMouse(view, QEvent::MouseMove, QPoint(102, 100), Qt::NoButton, Qt::NoButton,
                  Qt::ShiftModifier);
        QCOMPARE(mods.count(), 1);
        sendMouse(view, QEvent::MouseMove, QPoint(103, 100), Qt::NoButton, Qt::NoButton,
                  Qt::NoModifier);
        QCOMPARE(mods.count(), 2);
    }

    void cursorFollowsModifierInSelectContext() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        const Qt::CursorShape base = view.viewport()->cursor().shape();
        QVERIFY(base != Qt::BitmapCursor);

        sendMouse(view, QEvent::MouseMove, QPoint(100, 100), Qt::NoButton, Qt::NoButton,
                  Qt::ShiftModifier);
        QCOMPARE(view.viewport()->cursor().shape(), Qt::BitmapCursor); // « + »
        sendMouse(view, QEvent::MouseMove, QPoint(101, 100), Qt::NoButton, Qt::NoButton,
                  Qt::ShiftModifier | Qt::ControlModifier);
        QCOMPARE(view.viewport()->cursor().shape(), Qt::BitmapCursor); // « ± »
        sendMouse(view, QEvent::MouseMove, QPoint(102, 100), Qt::NoButton, Qt::NoButton,
                  Qt::NoModifier);
        QCOMPARE(view.viewport()->cursor().shape(), base);

        view.setFocus();
        QTest::keyPress(&view, Qt::Key_Space);
        QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(&view, Qt::Key_Space);
        QCOMPARE(view.viewport()->cursor().shape(), base);
    }

    // ---- revue T2 : Espace hors contexte, boutons concurrents, jours de fenêtre -------------

    void spaceNotConsumedWhilePopupModalOrInactive() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        auto* view = new CanvasView(new QGraphicsScene(&host));
        auto* button = new QPushButton(QStringLiteral("ok"));
        layout->addWidget(view, 1);
        layout->addWidget(button);
        host.resize(400, 500);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        host.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&host));
        button->setFocus();
        QEnterEvent enter(QPointF(50, 50), QPointF(50, 50), QPointF(50, 50));
        QApplication::sendEvent(view->viewport(), &enter);

        // Contrôle : actif, curseur sur le viewport -> consommé.
        QTest::keyPress(button, Qt::Key_Space);
        QVERIFY(view->spaceHeld());
        QTest::keyRelease(button, Qt::Key_Space);
        QVERIFY(!view->spaceHeld());

        // Popup ouvert : Espace appartient au popup.
        {
            QMenu menu;
            menu.addAction(QStringLiteral("a"));
            menu.popup(host.mapToGlobal(QPoint(10, 10)));
            QVERIFY(QTest::qWaitForWindowExposed(&menu));
            QVERIFY(QApplication::activePopupWidget() != nullptr);
            QTest::keyPress(button, Qt::Key_Space);
            QVERIFY(!view->spaceHeld());
            QTest::keyRelease(button, Qt::Key_Space);
            menu.close();
        }

        // Boîte modale ouverte.
        {
            QDialog dialog;
            dialog.setModal(true);
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            QVERIFY(QApplication::activeModalWidget() != nullptr);
            QTest::keyPress(button, Qt::Key_Space);
            QVERIFY(!view->spaceHeld());
            QTest::keyRelease(button, Qt::Key_Space);
            dialog.close();
        }

        // Fenêtre inactive.
        {
            QWidget other;
            other.show();
            QVERIFY(QTest::qWaitForWindowExposed(&other));
            other.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&other));
            QVERIFY(!host.isActiveWindow());
            QTest::keyPress(button, Qt::Key_Space);
            QVERIFY(!view->spaceHeld());
            QTest::keyRelease(button, Qt::Key_Space);
        }
    }

    void secondButtonDuringMiddlePanIsIgnored() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        QSignalSpy rect(&view, &CanvasView::selectionRectangleMm);
        InteractionMap::setLongPressMsForTesting(1);
        const auto restore = qScopeGuard([] { InteractionMap::setLongPressMsForTesting(-1); });
        QSignalSpy below(&view, &CanvasView::selectBelowRequested);

        sendMouse(view, QEvent::MouseButtonPress, QPoint(200, 200), Qt::MiddleButton,
                  Qt::MiddleButton, Qt::NoModifier);
        sendMouse(view, QEvent::MouseButtonPress, QPoint(200, 200), Qt::LeftButton,
                  Qt::MiddleButton | Qt::LeftButton, Qt::NoModifier);
        QTest::qWait(30); // un appui long armé à tort aurait tiré
        sendMouse(view, QEvent::MouseButtonRelease, QPoint(200, 200), Qt::LeftButton,
                  Qt::MiddleButton, Qt::NoModifier);
        sendMouse(view, QEvent::MouseButtonRelease, QPoint(200, 200), Qt::MiddleButton,
                  Qt::NoButton, Qt::NoModifier);
        QCOMPARE(clicked.count(), 0);
        QCOMPARE(rect.count(), 0);
        QCOMPARE(below.count(), 0);

        // Le pan est terminé proprement : un clic gauche normal refonctionne.
        InteractionMap::setLongPressMsForTesting(-1);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
        QCOMPARE(clicked.count(), 1);
    }

    void middlePressDuringRubberBandIsIgnored() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy rect(&view, &CanvasView::selectionRectangleMm);
        const QPoint before = scrollPos(view);

        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
        QTest::mouseMove(view.viewport(), QPoint(160, 140));
        QTest::mouseMove(view.viewport(), QPoint(220, 200));
        sendMouse(view, QEvent::MouseButtonPress, QPoint(220, 200), Qt::MiddleButton,
                  Qt::LeftButton | Qt::MiddleButton, Qt::NoModifier);
        QTest::mouseMove(view.viewport(), QPoint(260, 240)); // ne doit pas panoramiquer
        sendMouse(view, QEvent::MouseButtonRelease, QPoint(260, 240), Qt::MiddleButton,
                  Qt::LeftButton, Qt::NoModifier);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(260, 240));

        QCOMPARE(scrollPos(view), before);
        QCOMPARE(rect.count(), 1);
    }

    void wheelWithUndefinedModifiersStillZooms() {
        for (const Qt::KeyboardModifiers mod :
             {Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier),
              Qt::KeyboardModifiers(Qt::MetaModifier)}) {
            QGraphicsScene scene;
            CanvasView view(&scene);
            prepareView(view);
            const double initial = view.pixelsPerMm();
            sendWheel(view, QPoint(200, 200), QPoint(0, 120), QPoint(), mod);
            QVERIFY(view.pixelsPerMm() > initial);
        }
        // Alt + autre modificateur : ignoré (ni zoom ni défilement).
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareView(view);
        const double initial = view.pixelsPerMm();
        const QPoint before = scrollPos(view);
        sendWheel(view, QPoint(200, 200), QPoint(0, 120), QPoint(),
                  Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(view.pixelsPerMm(), initial);
        QCOMPARE(scrollPos(view), before);
    }

    // Le slot d'un QMenu::exec() tourne dans sa propre boucle : l'état de sélection
    // doit déjà être soldé quand il s'exécute (émission différée).
    void selectBelowSlotRunsAfterStateIsCleaned() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        QSignalSpy clicked(&view, &CanvasView::canvasClickedMm);
        int slotClicks = -1;
        QObject::connect(&view, &CanvasView::selectBelowRequested, &view, [&] {
            // Re-entrance : un clic normal pendant le « menu » doit fonctionner.
            QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
            slotClicks = static_cast<int>(clicked.count());
        });
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::AltModifier, QPoint(150, 120));
        QCOMPARE(slotClicks, -1); // pas émis de façon synchrone
        QTRY_COMPARE(slotClicks, 1);
    }

    void twoCanvasViewsHaveIndependentSpaceState() {
        QGraphicsScene sceneA;
        QGraphicsScene sceneB;
        auto* a = new CanvasView(&sceneA);
        CanvasView b(&sceneB);
        prepareView(*a);
        prepareView(b);
        QEnterEvent enter(QPointF(50, 50), QPointF(50, 50), QPointF(50, 50));
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(b.viewport(), &leave);
        QApplication::sendEvent(a->viewport(), &enter);
        a->setFocus();
        QTest::keyPress(a, Qt::Key_Space);
        QVERIFY(a->spaceHeld());
        QVERIFY(!b.spaceHeld());
        QTest::keyRelease(a, Qt::Key_Space);
        delete a; // le filtre de B reste fonctionnel
        QApplication::sendEvent(b.viewport(), &enter);
        b.setFocus();
        QTest::keyPress(&b, Qt::Key_Space);
        QVERIFY(b.spaceHeld());
        QTest::keyRelease(&b, Qt::Key_Space);
    }

    void leavingViewportResetsModifierCacheAndCursor() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        const Qt::CursorShape base = view.viewport()->cursor().shape();
        QSignalSpy mods(&view, &CanvasView::modifiersChanged);
        sendMouse(view, QEvent::MouseMove, QPoint(100, 100), Qt::NoButton, Qt::NoButton,
                  Qt::ShiftModifier);
        QCOMPARE(view.viewport()->cursor().shape(), Qt::BitmapCursor);
        QCOMPARE(mods.count(), 1);

        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(view.viewport(), &leave);
        QCOMPARE(mods.count(), 2);
        QCOMPARE(mods.at(1).at(0).value<Qt::KeyboardModifiers>(), Qt::KeyboardModifiers());
        QCOMPARE(view.viewport()->cursor().shape(), base);
    }

    void cursorRestoredAfterSetBaseContextDuringSpace() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        prepareSelectView(view);
        const Qt::CursorShape base = view.viewport()->cursor().shape();
        view.setFocus();
        QTest::keyPress(&view, Qt::Key_Space);
        QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);
        view.setBaseContext(Context::NodeEdit); // changement de contexte en plein Espace
        QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);
        QTest::keyRelease(&view, Qt::Key_Space);
        QCOMPARE(view.viewport()->cursor().shape(), base);
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
