// SPDX-License-Identifier: Apache-2.0
// Outil Règle (HP-VEC-010) : glisser sur le canevas = règle temporaire, résultat dans la barre
// d'état et copiable, AUCUNE mutation du document (pas de commande, pas d'annulation).
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QLocale>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

#include <cmath>

#include "canvas_view.hpp"
#include "interaction_map.hpp"
#include "main_window.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static document::Project blankProject() {
        document::Project p;
        p.original.width = 2;
        p.original.height = 2;
        p.original.rgba.assign(2 * 2 * 4, 255);
        return p;
    }

    static void dragOn(CanvasView& view, const QPoint& from, const QPoint& to) {
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view.viewport(), (from + to) / 2);
        QTest::mouseMove(view.viewport(), to);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, to);
    }

    // Premier nombre du texte de mesure (« 12,34 mm ... »), lu avec la locale française.
    static double leadingNumber(const QString& text) {
        bool ok = false;
        const double v = QLocale(QLocale::French).toDouble(text.section(' ', 0, 0), &ok);
        return ok ? v : -1.0;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("MeasureToolTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void canvasEmitsDragThenFinishedAndNothingElse() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        view.setCanvasSizeMm(QSizeF(100.0, 100.0));
        view.resize(400, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setMeasureMode(true);
        QVERIFY(view.measureMode());
        QSignalSpy dragSpy(&view, &CanvasView::measureDragMm);
        QSignalSpy doneSpy(&view, &CanvasView::measureFinishedMm);
        QSignalSpy clickSpy(&view, &CanvasView::canvasClickedMm);
        QSignalSpy selSpy(&view, &CanvasView::selectionRectangleMm);
        dragOn(view, QPoint(100, 100), QPoint(220, 160));
        QVERIFY(dragSpy.count() >= 2); // appui + déplacements
        QCOMPARE(doneSpy.count(), 1);
        QCOMPARE(clickSpy.count(), 0); // la règle n'est pas un clic de sélection
        QCOMPARE(selSpy.count(), 0);
        const QPointF start = doneSpy.at(0).at(0).toPointF();
        const QPointF end = doneSpy.at(0).at(1).toPointF();
        QCOMPARE(start, view.mapToScene(QPoint(100, 100)));
        QCOMPARE(end, view.mapToScene(QPoint(220, 160)));
    }

    void escapeCancelsAMeasureInProgress() {
        QGraphicsScene scene;
        CanvasView view(&scene);
        view.setCanvasSizeMm(QSizeF(100.0, 100.0));
        view.resize(400, 400);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setMeasureMode(true);
        QSignalSpy cancelled(&view, &CanvasView::measureCancelled);
        QSignalSpy done(&view, &CanvasView::measureFinishedMm);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(50, 50));
        QTest::mouseMove(view.viewport(), QPoint(90, 90));
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&view, &esc);
        QCOMPARE(cancelled.count(), 1);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(90, 90));
        QCOMPARE(done.count(), 0); // le relâchement après annulation n'émet plus rien
    }

    void rulerToolMeasuresShowsLabelAndLeavesTheDocumentUntouched() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(blankProject());
        QApplication::processEvents();
        auto* undo = window.findChild<QAction*>("action_undo");
        QVERIFY(undo != nullptr);
        const bool undoBefore = undo->isEnabled();
        const bool stackBefore = window.undoStack_.canUndo();
        const auto objectsBefore = window.project_.vector_objects.size();
        const auto embBefore = window.project_.embroidery_objects.size();

        QVERIFY(window.toolMeasureAct_ != nullptr);
        QVERIFY(window.toolPalette_->actions().contains(window.toolMeasureAct_));
        QVERIFY(window.toolPalette_->actions().contains(window.copyMeasureAct_));
        window.toolMeasureAct_->trigger();
        QCOMPARE(window.currentTool_, Tool::Measure);
        QVERIFY(window.toolMeasureAct_->isChecked());
        QVERIFY(window.view_->measureMode());

        QSignalSpy done(window.view_, &CanvasView::measureFinishedMm);
        dragOn(*window.view_, QPoint(120, 140), QPoint(260, 200));
        QCOMPARE(done.count(), 1);
        QVERIFY(window.measureRuler_ != nullptr);
        QVERIFY(window.measureRuler_->scene() == window.scene_);

        // Distance attendue recalculée à partir des points émis (mm scène, pas de pixels).
        const QPointF a = done.at(0).at(0).toPointF();
        const QPointF b = done.at(0).at(1).toPointF();
        const double expected = std::hypot(b.x() - a.x(), b.y() - a.y());
        QVERIFY(expected > 1.0);
        QVERIFY2(std::abs(leadingNumber(window.lastMeasureText_) - expected) < 0.01,
                 qPrintable(window.lastMeasureText_));
        QVERIFY(window.lastMeasureText_.contains(QStringLiteral("Δx")));
        QVERIFY(window.lastMeasureText_.contains(QStringLiteral("angle")));
        QVERIFY(window.statusBar()->currentMessage().contains(window.lastMeasureText_));

        // Aucune mutation du document.
        QCOMPARE(undo->isEnabled(), undoBefore);
        QCOMPARE(window.undoStack_.canUndo(), stackBefore);
        QCOMPARE(window.project_.vector_objects.size(), objectsBefore);
        QCOMPARE(window.project_.embroidery_objects.size(), embBefore);

        // Copiable.
        QVERIFY(window.copyMeasureAct_->isEnabled());
        window.copyMeasureAct_->trigger();
        QCOMPARE(QApplication::clipboard()->text(), window.lastMeasureText_);
    }

    void measureIsExactOnAKnownHorizontalSegment() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(blankProject());
        window.setTool(Tool::Measure);
        // Segment de 10 mm exactement (dx = 10, dy = 0), injecté par le signal de la vue.
        emit window.view_->measureFinishedMm(QPointF(0.0, 0.0), QPointF(10.0, 0.0));
        QVERIFY2(window.lastMeasureText_.startsWith(QStringLiteral("10,00 mm")),
                 qPrintable(window.lastMeasureText_));
        QVERIFY(window.lastMeasureText_.contains(QStringLiteral("Δx 10,00 mm")));
        QVERIFY(window.lastMeasureText_.contains(QStringLiteral("Δy 0,00 mm")));
        QVERIFY(window.lastMeasureText_.contains(QStringLiteral("angle 0,0°")));
        // Scène Y vers le bas : 3-4-5 vers le haut de l'écran = angle antihoraire positif.
        emit window.view_->measureFinishedMm(QPointF(0.0, 0.0), QPointF(3.0, -4.0));
        QVERIFY2(window.lastMeasureText_.startsWith(QStringLiteral("5,00 mm")),
                 qPrintable(window.lastMeasureText_));
        QVERIFY(window.lastMeasureText_.contains(QStringLiteral("Δy 4,00 mm")));
        QVERIFY(window.lastMeasureText_.contains(QStringLiteral("angle 53,1°")));
    }

    void leavingTheToolRemovesTheRulerAndRestoresNormalClicks() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(blankProject());
        window.setTool(Tool::Measure);
        emit window.view_->measureFinishedMm(QPointF(0.0, 0.0), QPointF(10.0, 5.0));
        QVERIFY(window.measureRuler_ != nullptr);
        window.setTool(Tool::Select);
        QVERIFY(window.measureRuler_ == nullptr);
        QVERIFY(!window.view_->measureMode());
        QVERIFY(!window.toolMeasureAct_->isChecked());
        // Un clic avec l'outil Règle ne sélectionne rien (pas de mutation, pas de crash).
        window.setTool(Tool::Measure);
        window.onCanvasClicked(QPointF(0.0, 0.0));
        QVERIFY(!window.selectedObject_.has_value());
    }

    void gestureTableKnowsTheMeasureContext() {
        QCOMPARE(InteractionMap::contextFor(Tool::Measure, false, false), Context::Measure);
        const auto hints = InteractionMap::hintsFor(Context::Measure, Qt::NoModifier);
        QVERIFY(!hints.isEmpty());
        bool found = false;
        for (const Hint& h : hints) {
            found = found || h.label.contains(QStringLiteral("distance"), Qt::CaseInsensitive);
        }
        QVERIFY(found);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_measure_tool.moc"
