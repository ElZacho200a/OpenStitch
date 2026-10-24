// SPDX-License-Identifier: Apache-2.0
// Outil Clic-pour-broder (HP-AUTO-003) : un clic sur une zone de couleur de l'image crée l'objet
// brodé (commande annulable) ; la tolerance et le type se reglent dans la barre de l'outil ; la
// zone qui sera brodee est apercue au survol.
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QGraphicsPixmapItem>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

#include <variant>

#include "canvas_view.hpp"
#include "interaction_map.hpp"
#include "main_window.hpp"
#include "ui_memory.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static document::Project emptyProjectWithImage() {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        return project;
    }

    static void paint(MainWindow& window, int x0, int y0, int x1, int y1,
                      std::array<std::uint8_t, 3> rgb) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                auto* p =
                    &window.processed_
                         .rgba[(static_cast<std::size_t>(y) * 20U + static_cast<std::size_t>(x)) *
                               4U];
                p[0] = rgb[0];
                p[1] = rgb[1];
                p[2] = rgb[2];
                p[3] = 255;
            }
        }
    }

    // Image de travail 20 x 20 px, 1 mm par pixel : fond blanc, bloc rouge A (x 2..9, y 4..15)
    // et bloc B de rouge voisin (x 10..17, y 4..15).
    static void setTwoRedBlocks(MainWindow& window) {
        window.processed_.width = 20;
        window.processed_.height = 20;
        window.processed_.rgba.assign(20 * 20 * 4, 255);
        paint(window, 2, 4, 9, 15, {220, 30, 30});
        paint(window, 10, 4, 17, 15, {200, 40, 40});
        window.project_.mm_per_px = Millimeters{1.0};
    }

    // Centre du pixel (px, py) en coordonnees scene (mm, Y vers le bas) : l'image est centree.
    static QPointF sceneOfPixel(int px, int py) {
        return QPointF(px - 10.0 + 0.5, py - 10.0 + 0.5);
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("ClickToStitchToolTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void init() { QSettings().clear(); }

    void toolIsInThePaletteWithItsContextAndSettingsBar() {
        MainWindow window;
        window.show();
        QVERIFY(window.toolClickToStitchAct_ != nullptr);
        QVERIFY(window.toolPalette_->actions().contains(window.toolClickToStitchAct_));
        QVERIFY(window.clickToStitchBar_ != nullptr);
        QVERIFY(window.clickToStitchBar_->isHidden());
        window.toolClickToStitchAct_->trigger();
        QCOMPARE(window.currentTool_, Tool::ClickToStitch);
        QVERIFY(window.toolClickToStitchAct_->isChecked());
        QVERIFY(!window.clickToStitchBar_->isHidden());
        QCOMPARE(InteractionMap::contextFor(Tool::ClickToStitch, false, false),
                 Context::ClickToStitch);
        QVERIFY(!InteractionMap::hintsFor(Context::ClickToStitch, Qt::NoModifier).isEmpty());
        window.setTool(Tool::Select);
        QVERIFY(!window.toolClickToStitchAct_->isChecked());
        QVERIFY(window.clickToStitchBar_->isHidden());
    }

    void clickOnAColorAreaCreatesAnUndoableEmbroideredObject() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(emptyProjectWithImage());
        setTwoRedBlocks(window);
        window.setClickToStitchTolerance(0); // couleur exacte : le bloc A seulement
        window.setTool(Tool::ClickToStitch);
        window.onCanvasClicked(sceneOfPixel(5, 8));
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
        const auto& emb = window.project_.embroidery_objects.front();
        QVERIFY(emb.is_tatami());
        QCOMPARE(emb.source_vector, window.project_.vector_objects.front().id);
        QCOMPARE(emb.rgb, (std::array<std::uint8_t, 3>{220, 30, 30}));
        QVERIFY(window.selectedEmbroidery_.has_value());
        QCOMPARE(*window.selectedEmbroidery_, emb.id);
        QVERIFY(window.undoStack_.canUndo());

        // Un seul pas d'annulation retire les deux objets, refaire les remet.
        const ObjectId embId = emb.id;
        QVERIFY(window.undoStack_.undo(window.project_));
        QVERIFY(window.project_.vector_objects.empty());
        QVERIFY(window.project_.embroidery_objects.empty());
        QVERIFY(window.undoStack_.redo(window.project_));
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.embroidery_objects.front().id, embId);
    }

    void toleranceControlsStayInSyncAndAreRemembered() {
        MainWindow window;
        window.show();
        window.clickToStitchToleranceSpin_->setValue(30);
        QCOMPARE(window.clickToStitchTolerance_, 30);
        QCOMPARE(window.clickToStitchToleranceSlider_->value(), 30);
        window.clickToStitchToleranceSlider_->setValue(7);
        QCOMPARE(window.clickToStitchToleranceSpin_->value(), 7);
        QCOMPARE(ui_memory::intValue(QStringLiteral("clickToStitch/tolerance"), -1, -1, 100), 7);
        // Un reglage hors bornes est ramene dans la plage.
        window.setClickToStitchTolerance(500);
        QCOMPARE(window.clickToStitchTolerance_, 60);
    }

    void aLargerToleranceMergesNeighbouringShadesIntoOneObject() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(emptyProjectWithImage());
        setTwoRedBlocks(window);
        window.setTool(Tool::ClickToStitch);

        window.setClickToStitchTolerance(0);
        window.updateClickToStitchPreview(sceneOfPixel(5, 8));
        QCOMPARE(window.clickToStitchPreviewPixels_, std::size_t{8 * 12});
        QVERIFY(window.clickToStitchPreviewItem_ != nullptr);
        QVERIFY(window.clickToStitchPreviewItem_->isVisible());

        window.setClickToStitchTolerance(60);
        window.updateClickToStitchPreview(sceneOfPixel(5, 8));
        QCOMPARE(window.clickToStitchPreviewPixels_, std::size_t{16 * 12}); // blanc exclu

        // Le survol d'un pixel hors de l'image masque l'aperçu.
        window.updateClickToStitchPreview(QPointF(80.0, 0.0));
        QVERIFY(!window.clickToStitchPreviewItem_->isVisible());
        QCOMPARE(window.clickToStitchPreviewPixels_, std::size_t{0});
    }

    void kindChoiceSelectsContourOnlyOrFill() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(emptyProjectWithImage());
        setTwoRedBlocks(window);
        window.setClickToStitchTolerance(0);
        window.setTool(Tool::ClickToStitch);

        window.clickToStitchKindCombo_->setCurrentIndex(3); // Contour seul
        QCOMPARE(window.clickToStitchKind_, 3);
        window.onCanvasClicked(sceneOfPixel(5, 8));
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
        QVERIFY(std::holds_alternative<document::RunningStitchParams>(
            window.project_.embroidery_objects.back().params));

        setTwoRedBlocks(window); // refreshImage a pu recalculer l'image de travail
        window.clickToStitchKindCombo_->setCurrentIndex(1); // Remplissage
        window.onCanvasClicked(sceneOfPixel(12, 8));
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{2});
        QVERIFY(window.project_.embroidery_objects.back().is_tatami());
        // Deux clics = deux pas d'annulation.
        QVERIFY(window.undoStack_.undo(window.project_));
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
    }

    void clickOutsideTheImageOrWithoutImageCreatesNothing() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(emptyProjectWithImage());
        window.processed_ = {};
        window.setTool(Tool::ClickToStitch);
        window.onCanvasClicked(QPointF(0.0, 0.0));
        QVERIFY(window.project_.embroidery_objects.empty());
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("image")));

        setTwoRedBlocks(window);
        window.onCanvasClicked(QPointF(80.0, 0.0)); // hors de l'image de 20 mm
        QVERIFY(window.project_.embroidery_objects.empty());
        QVERIFY(!window.undoStack_.canUndo());
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_click_to_stitch_tool.moc"
