// SPDX-License-Identifier: Apache-2.0
// Affichage par passe (HP-VIEW-003), zoom rectangle et 1:1 (HP-VIEW-007), grille et guides
// (HP-VIEW-010), aperçu en direct (HP-VIEW-012). Headless : ni pixels ni sleep ; les gestes
// souris sont injectés sur le viewport.
#include <QAction>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cmath>

#include "canvas_view.hpp"
#include "grid_settings_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "properties_panel.hpp"
#include "view_display_state.hpp"

using openstitch::Micrometers;
using openstitch::Vec2um;
using openstitch::desktop::CanvasView;
using openstitch::desktop::GridSettingsDialog;
using openstitch::desktop::MainWindow;
using openstitch::desktop::ViewDisplayState;
namespace sr = openstitch::stitch_render;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

doc::Project squareProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    doc::VectorObject vector;
    vector.id = project.object_ids.next();
    vector.name = "Carre";
    geo::Path path;
    path.closed = true;
    for (const auto& [x, y] :
         {std::pair{0, 0}, std::pair{10'000, 0}, std::pair{10'000, 10'000}, std::pair{0, 10'000}}) {
        path.nodes.push_back({Vec2um{Micrometers{x}, Micrometers{y}}, geo::NodeType::Corner,
                              std::nullopt, std::nullopt});
    }
    vector.paths.push_back({std::move(path), {}});
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vector.id;
    emb.name = "Carre";
    doc::TatamiParams tatami;
    tatami.underlay_mode = doc::UnderlayMode::Auto;
    emb.params = tatami;
    project.vector_objects.push_back(std::move(vector));
    project.embroidery_objects.push_back(std::move(emb));
    return project;
}

} // namespace

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    // Nombre d'éléments de tracé cousus (z = 20) : 0 si rien n'est dessiné.
    static int sewnElements(const MainWindow& window) {
        int n = 0;
        for (const QGraphicsItem* item : window.stitchItems_) {
            if (item->zValue() == 20.0) {
                if (const auto* p = qgraphicsitem_cast<const QGraphicsPathItem*>(item)) {
                    n += p->path().elementCount();
                }
            }
        }
        return n;
    }
    static QAction* action(MainWindow& window, const char* name) {
        auto* act = window.findChild<QAction*>(QString::fromLatin1(name));
        return act;
    }
    static int markerItems(const MainWindow& window) {
        int n = 0;
        for (const QGraphicsItem* item : window.scene_->items()) {
            n += item->data(0).toString() == QStringLiteral("functionMarkers") ? 1 : 0;
        }
        return n;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("ViewDisplayUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    void passToggleHidesOnlyThatPass() {
        MainWindow window;
        window.applyLoadedProject(squareProject());
        QVERIFY(window.sequence_.has_value());
        const auto counts = sr::count_passes(*window.sequence_);
        QVERIFY(counts.stitches[static_cast<std::size_t>(stitch::StitchPass::Underlay)] > 0);
        QVERIFY(counts.stitches[static_cast<std::size_t>(stitch::StitchPass::TopStitch)] > 0);

        window.renderStitches();
        const int all = sewnElements(window);
        QVERIFY(all > 0);

        QAction* underlay = action(window, "action_passUnderlay");
        QAction* top = action(window, "action_passTop");
        QVERIFY(underlay != nullptr && top != nullptr);
        QVERIFY(underlay->isChecked() && top->isChecked());

        underlay->setChecked(false);
        const int withoutUnderlay = sewnElements(window);
        QVERIFY(withoutUnderlay > 0);
        QVERIFY(withoutUnderlay < all);

        underlay->setChecked(true);
        QCOMPARE(sewnElements(window), all);

        // Plus aucune passe cousue : rien n'est dessiné.
        for (const char* name : {"action_passUnderlay", "action_passTop", "action_passLock",
                                 "action_passManual", "action_passTravel"}) {
            action(window, name)->setChecked(false);
        }
        QCOMPARE(sewnElements(window), 0);
    }

    void passDisplayNeverTouchesTheDocument() {
        MainWindow window;
        window.applyLoadedProject(squareProject());
        const auto undoBefore = window.undoStack_.undoNames().size();
        const auto commandsBefore = window.sequence_->commands.size();
        action(window, "action_passUnderlay")->setChecked(false);
        action(window, "action_passDistinct")->setChecked(true);
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QCOMPARE(window.sequence_->commands.size(), commandsBefore);
    }

    void distinctColorsUseThePassTint() {
        MainWindow window;
        window.applyLoadedProject(squareProject());
        action(window, "action_passDistinct")->setChecked(true);
        const auto tint = sr::pass_tint(stitch::StitchPass::TopStitch);
        bool found = false;
        for (const QGraphicsItem* item : window.stitchItems_) {
            if (const auto* p = qgraphicsitem_cast<const QGraphicsPathItem*>(item);
                p != nullptr && p->zValue() == 20.0 && p->pen().color().red() == tint[0] &&
                p->pen().color().green() == tint[1] && p->pen().color().blue() == tint[2]) {
                found = true;
            }
        }
        QVERIFY(found);
    }

    void functionMarkersAreOptionalAndNeverCaptureHits() {
        MainWindow window;
        window.applyLoadedProject(squareProject());
        QCOMPARE(markerItems(window), 0);
        action(window, "action_passFunctions")->setChecked(true);
        QVERIFY(markerItems(window) > 0);
        for (const QGraphicsItem* item : window.scene_->items()) {
            if (item->data(0).toString() == QStringLiteral("functionMarkers")) {
                QVERIFY(item->shape().isEmpty());
                QCOMPARE(item->acceptedMouseButtons(), Qt::MouseButtons{});
            }
        }
        action(window, "action_passFunctions")->setChecked(false);
        QCOMPARE(markerItems(window), 0);
    }

    void displayStateIsRemembered() {
        {
            MainWindow window;
            action(window, "action_passLock")->setChecked(false);
            window.viewDisplay_.guides = {sr::Guide{sr::GuideAxis::Vertical, Micrometers{12'000}}};
            window.viewDisplay_.grid.step_mm = 2.5;
            window.applyViewDisplay();
        }
        const ViewDisplayState loaded = ViewDisplayState::load();
        QVERIFY(!loaded.passes.lock);
        QVERIFY(loaded.passes.underlay);
        QCOMPARE(loaded.grid.step_mm, 2.5);
        QCOMPARE(loaded.guides.size(), std::size_t{1});
        QCOMPARE(loaded.guides[0].position.value, 12'000);

        MainWindow again;
        QVERIFY(!action(again, "action_passLock")->isChecked());
        QCOMPARE(again.view_->gridSettings().step_mm, 2.5);
        QCOMPARE(again.view_->guides().size(), std::size_t{1});
        QCOMPARE(again.gridStepMm_, 2.5); // l'accrochage suit la grille dessinée
    }

    void gridAndGuidesReachTheCanvas() {
        MainWindow window;
        QVERIFY(window.view_->gridSettings().visible);
        action(window, "action_gridShow")->setChecked(false);
        QVERIFY(!window.view_->gridSettings().visible);
        action(window, "action_guidesShow")->setChecked(false);
        QVERIFY(!window.view_->guidesVisible());
        window.viewDisplay_.guides = {sr::Guide{sr::GuideAxis::Horizontal, Micrometers{-4'000}}};
        window.applyViewDisplay();
        QCOMPARE(window.view_->guides().size(), std::size_t{1});
        action(window, "action_guidesClear")->trigger();
        QVERIFY(window.view_->guides().empty());
    }

    void gridDialogReturnsBoundedSettings() {
        sr::GridSettings initial;
        GridSettingsDialog dialog(initial);
        QVERIFY(dialog.settings().visible);
        QCOMPARE(dialog.settings().step_mm, 0.0);             // automatique par défaut
        QCOMPARE(dialog.settings().rgba[3], std::uint8_t{0}); // couleur du thème

        sr::GridSettings custom;
        custom.step_mm = 2.0;
        custom.major_every = 4;
        GridSettingsDialog dialog2(custom);
        dialog2.setColor(QColor(10, 20, 30, 128));
        const sr::GridSettings out = dialog2.settings();
        QCOMPARE(out.step_mm, 2.0);
        QCOMPARE(out.major_every, 4);
        QCOMPARE(out.rgba[0], std::uint8_t{10});
        QCOMPARE(out.rgba[3], std::uint8_t{128});
    }

    void zoomRectangleFramesTheDraggedArea() {
        MainWindow window;
        CanvasView* view = window.view_;
        window.resize(1000, 700);
        window.show();
        QAction* act = action(window, "action_zoomRect");
        QVERIFY(act != nullptr);
        act->setChecked(true);
        QVERIFY(view->zoomRectMode());
        const double before = view->pixelsPerMm();
        const QPoint c = view->viewport()->rect().center();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, c - QPoint(20, 15));
        QTest::mouseMove(view->viewport(), c);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, c + QPoint(20, 15));
        QVERIFY(!view->zoomRectMode()); // le mode s'éteint après un cadre
        QVERIFY(!act->isChecked());
        QVERIFY(view->pixelsPerMm() > before);
    }

    void zoomRectangleEscapeCancelsWithoutZooming() {
        MainWindow window;
        CanvasView* view = window.view_;
        window.resize(1000, 700);
        window.show();
        action(window, "action_zoomRect")->setChecked(true);
        const double before = view->pixelsPerMm();
        const QPoint c = view->viewport()->rect().center();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, c);
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!view->zoomRectMode());
        QCOMPARE(view->pixelsPerMm(), before);
    }

    void actualSizeUsesTheCalibration() {
        MainWindow window;
        CanvasView* view = window.view_;
        QVERIFY(view->actualSizePxPerMm() > 0.0);
        view->setActualSizeCalibration(4.0);
        QCOMPARE(view->actualSizePxPerMm(), 4.0);
        view->zoomActualSize();
        QVERIFY(std::abs(view->pixelsPerMm() - 4.0) < 0.05);
        // Valeur absurde : retombe sur l'automatique.
        view->setActualSizeCalibration(5000.0);
        QCOMPARE(view->actualSizeCalibration(), 0.0);
    }

    void livePreviewIsOnForInspectorSpins() {
        MainWindow window;
        window.applyLoadedProject(squareProject());
        window.propertiesPanel_->showEmbroidery(window.project_.embroidery_objects.front());
        const auto spins = window.propertiesPanel_->findChildren<QDoubleSpinBox*>();
        QVERIFY(!spins.isEmpty());
        for (const QDoubleSpinBox* spin : spins) {
            if (spin->suffix() == QStringLiteral(" mm")) {
                QVERIFY(spin->keyboardTracking());
            }
        }
        // Désactivé : la frappe n'est prise en compte qu'à la validation.
        action(window, "action_liveTyping")->setChecked(false);
        window.propertiesPanel_->showEmbroidery(window.project_.embroidery_objects.front());
        for (const QDoubleSpinBox* spin :
             window.propertiesPanel_->findChildren<QDoubleSpinBox*>()) {
            if (spin->suffix() == QStringLiteral(" mm")) {
                QVERIFY(!spin->keyboardTracking());
            }
        }
    }

    void smallDesignRefreshesWithAShortDelay() {
        MainWindow window;
        window.applyLoadedProject(squareProject());
        window.scheduleParamsRefresh();
        QVERIFY(window.paramsRefreshTimer_->interval() < MainWindow::kParamsRefreshDelayMs);
        window.paramsRefreshTimer_->stop();
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_view_display_ui.moc"
