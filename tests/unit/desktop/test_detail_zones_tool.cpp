// SPDX-License-Identifier: Apache-2.0
// Zones de détail : le pinceau de masque peint avec de VRAIS gestes souris, un trait = un pas
// d'annulation, Échap l'abandonne, la gomme retire, et le dialogue de segmentation expose les
// réglages « dans les détails ».
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QGraphicsPixmapItem>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "segmentation_dialog.hpp"

namespace {

namespace doc = openstitch::document;

// Image blanche de 100x100 px (~26 mm de côté à la résolution par défaut).
doc::Project whiteImage() {
    doc::Project project;
    project.original.width = 100;
    project.original.height = 100;
    project.original.rgba.assign(100 * 100 * 4, 255);
    return project;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    struct Fixture {
        MainWindow window;
        CanvasView* view{nullptr};
    };

    static void setup(Fixture& f) {
        f.window.resize(1200, 800);
        f.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&f.window));
        f.window.applyLoadedProject(whiteImage());
        f.window.refreshImage();
        f.view = f.window.findChild<CanvasView*>();
        QVERIFY(f.view != nullptr);
        f.view->resetTransform();
        f.view->scale(12.0, 12.0);
        f.view->centerOn(QPointF(0.0, 0.0));
        f.window.setDetailRadiusMm(3.0);
        f.window.toolDetailAct_->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(f.window.currentTool_, Tool::DetailBrush);
    }

    static QPoint px(CanvasView* view, double x, double y) {
        return view->mapFromScene(QPointF(x, y));
    }

    static void sendMouse(CanvasView* view, QEvent::Type type, QPoint at, Qt::MouseButton button,
                          Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(at), QPointF(view->viewport()->mapToGlobal(at)), button,
                          buttons, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &event);
    }

    static void stroke(CanvasView* view, QPointF from, QPointF to, bool release = true) {
        sendMouse(view, QEvent::MouseButtonPress, px(view, from.x(), from.y()), Qt::LeftButton,
                  Qt::LeftButton);
        for (int i = 1; i <= 10; ++i) {
            const QPointF p = from + (to - from) * (i / 10.0);
            sendMouse(view, QEvent::MouseMove, px(view, p.x(), p.y()), Qt::NoButton,
                      Qt::LeftButton);
        }
        if (release) {
            sendMouse(view, QEvent::MouseButtonRelease, px(view, to.x(), to.y()), Qt::LeftButton,
                      Qt::NoButton);
            QCoreApplication::processEvents();
        }
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("DetailZonesToolTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void theToolIsInThePaletteAndTheSegmentationMenu() {
        MainWindow window;
        QVERIFY(window.toolDetailAct_ != nullptr);
        QVERIFY(window.toolPalette_->actions().contains(window.toolDetailAct_));
        QVERIFY(window.segmentMenu_->actions().contains(window.toolDetailAct_));
        QCOMPARE(window.toolDetailAct_->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_D));
    }

    void aStrokePaintsTheMaskInOneUndoStep() {
        Fixture f;
        setup(f);
        QVERIFY(f.window.project_.detail_mask.empty());
        stroke(f.view, QPointF(-4.0, 0.0), QPointF(4.0, 0.0), false);
        QVERIFY(!f.window.undoStack_.canUndo()); // rien avant le relâchement
        QVERIFY2(f.window.detailOverlayItem_ != nullptr && f.window.detailOverlayItem_->isVisible(),
                 "la surcouche suit le trait");
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier,
                            px(f.view, 4.0, 0.0));
        QCoreApplication::processEvents();

        QVERIFY(f.window.project_.detail_mask.any_detail());
        QVERIFY(f.window.project_.detail_mask.matches(100, 100));
        QCOMPARE(f.window.undoStack_.undoNames().size(), std::size_t{1}); // un seul pas
        QVERIFY(!f.window.brushStroke_.has_value()); // le pinceau de déformation n'a rien fait

        const auto painted = f.window.project_.detail_mask;
        QVERIFY(f.window.undoStack_.undo(f.window.project_));
        QVERIFY(f.window.project_.detail_mask.empty());
        QVERIFY(f.window.undoStack_.redo(f.window.project_));
        QVERIFY(f.window.project_.detail_mask == painted);
    }

    void escapeAbandonsTheStroke() {
        Fixture f;
        setup(f);
        stroke(f.view, QPointF(-4.0, 0.0), QPointF(4.0, 0.0), false);
        QTest::keyClick(f.view, Qt::Key_Escape);
        QTest::mouseRelease(f.view->viewport(), Qt::LeftButton, Qt::NoModifier,
                            px(f.view, 4.0, 0.0));
        QCoreApplication::processEvents();
        QVERIFY(f.window.project_.detail_mask.empty());
        QVERIFY(!f.window.undoStack_.canUndo());
    }

    void theEraserRemovesDetailAndClearAllIsUndoable() {
        Fixture f;
        setup(f);
        stroke(f.view, QPointF(-4.0, 0.0), QPointF(4.0, 0.0));
        QVERIFY(f.window.project_.detail_mask.any_detail());

        f.window.detailEraseCheck_->setChecked(true);
        stroke(f.view, QPointF(-8.0, 0.0), QPointF(8.0, 0.0));
        QVERIFY(!f.window.project_.detail_mask.any_detail());
        QCOMPARE(f.window.undoStack_.undoNames().size(), std::size_t{2});

        f.window.detailEraseCheck_->setChecked(false);
        stroke(f.view, QPointF(-4.0, 0.0), QPointF(4.0, 0.0));
        const auto painted = f.window.project_.detail_mask;
        f.window.clearDetailZones();
        QVERIFY(f.window.project_.detail_mask.empty());
        QVERIFY(f.window.undoStack_.undo(f.window.project_));
        QVERIFY(f.window.project_.detail_mask == painted);
    }

    void leavingTheToolHidesTheRingAndTheBar() {
        Fixture f;
        setup(f);
        QTest::mouseMove(f.view->viewport(), px(f.view, 5.0, 5.0));
        QVERIFY(f.window.detailRing_ != nullptr);
        QVERIFY(f.window.detailBar_->isVisible());
        f.window.setTool(Tool::Select);
        QVERIFY(f.window.detailRing_ == nullptr);
        QVERIFY(!f.window.detailBar_->isVisible());
        QVERIFY(!f.window.view_->brushMode());
    }

    void theSegmentationDialogExposesTheDetailSettings() {
        Fixture f;
        setup(f);
        f.window.setTool(Tool::Select);
        f.window.openSegmentationDialog();
        QVERIFY(!f.window.segmentDialog_.isNull());
        QVERIFY(f.window.segmentDialog_->findChild<QSpinBox*>(
                    QStringLiteral("segmentDetailSmoothingSpin")) != nullptr);
        QCOMPARE(f.window.segmentDialog_->options().detail_min_region_px, 2);
        QCOMPARE(f.window.segmentDialog_->options().detail_smoothing_radius_px, 0);

        emit f.window.segmentDialog_->paintDetailRequested();
        QCOMPARE(f.window.currentTool_, Tool::DetailBrush);
        f.window.segmentDialog_->close();
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_detail_zones_tool.moc"
