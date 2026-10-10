// SPDX-License-Identifier: Apache-2.0
// Flux import -> segmentation -> vectorisation (audit ergonomique A) : vectorisation de toute la
// sélection, régions déjà vectorisées, mode « Fusionner avec… », survol des régions, actions
// grisées avec raison, aperçu de quantification, dialogues d'import et IA.
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>
#include <memory>
#include <variant>
#include <vector>

#include "ai_segmentation_dialog.hpp"
#include "canvas_view.hpp"
#include "import_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/segmentation/segmentation.hpp"

using openstitch::RegionId;
using openstitch::desktop::AiPreferences;
using openstitch::desktop::AiSegmentationDialog;
using openstitch::desktop::ImportDialog;
using openstitch::desktop::MainWindow;
using openstitch::desktop::SelectMode;

namespace {

// Image w x h : trois bandes verticales rouge / verte / bleue, segmentée en trois régions.
openstitch::document::Project threeBandProject() {
    constexpr int kW = 60;
    constexpr int kH = 40;
    openstitch::document::Project project;
    project.original.width = kW;
    project.original.height = kH;
    project.original.source_had_alpha = false;
    project.original.rgba.resize(std::size_t{kW} * kH * 4);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            auto* px = project.original.rgba.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
            const int band = x * 3 / kW;
            px[0] = band == 0 ? 220 : 20;
            px[1] = band == 1 ? 200 : 20;
            px[2] = band == 2 ? 220 : 20;
            px[3] = 255;
        }
    }
    auto seg =
        openstitch::segmentation::segment(project.original, {.max_colors = 3, .min_region_px = 1});
    project.segmentation = std::move(*seg);
    return project;
}

// Pas de réponse en ligne possible aux boîtes modales (exec() bloque) : un petit scénario sonde la
// fenêtre modale active et lui applique l'étape suivante.
void runModalScript(QObject* owner, std::vector<std::function<void(QWidget*)>> steps) {
    struct Script {
        std::vector<std::function<void(QWidget*)>> steps;
        std::size_t next{0};
    };
    auto script = std::make_shared<Script>();
    script->steps = std::move(steps);
    auto* timer = new QTimer(owner);
    timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, timer, [script, timer] {
        if (script->next >= script->steps.size()) {
            timer->stop();
            timer->deleteLater();
            return;
        }
        if (QWidget* modal = QApplication::activeModalWidget()) {
            script->steps[script->next++](modal);
        }
    });
    timer->start();
}

std::function<void(QWidget*)> clickButton(const QString& text) {
    return [text](QWidget* modal) {
        for (auto* button : modal->findChildren<QAbstractButton*>()) {
            if (button->text() == text) {
                button->click();
                return;
            }
        }
        modal->close(); // bouton introuvable : le test échouera sur ses assertions, sans bloquer
    };
}

std::function<void(QWidget*)> acceptDialog() {
    return [](QWidget* modal) {
        if (auto* dialog = qobject_cast<QDialog*>(modal)) {
            dialog->accept();
        }
    };
}

} // namespace

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    // Centre du pixel (x, y) de l'image de travail, en mm de scène (image centrée sur le cadre).
    static QPointF pixelMm(const MainWindow& window, int x, int y) {
        const double mm = window.project_.mm_per_px.value;
        return {-window.processed_.width * mm / 2.0 + (x + 0.5) * mm,
                -window.processed_.height * mm / 2.0 + (y + 0.5) * mm};
    }
    static RegionId regionAt(const MainWindow& window, int x, int y) {
        return *segmentation::region_at(*window.project_.segmentation, x, y);
    }

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("SegmentationWorkflowTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }
    void init() { QSettings().clear(); }

    // ---- vectorisation de toute la sélection (audit A4/A5) --------------------------------

    void vectorizeAllSelectedRegionsInOneUndoStep() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.selectRegions({regionAt(window, 5, 5), regionAt(window, 30, 5)},
                             SelectMode::Replace);
        QCOMPARE(window.selectedRegionIds().size(), std::size_t{2});

        runModalScript(&window, {acceptDialog()}); // dialogue de détail
        window.vectorizeSelectedRegion();

        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2});
        QCOMPARE(window.selectedObjectIds().size(), std::size_t{2}); // les deux créés sont choisis
        window.undo();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{0}); // UN seul pas
        window.redo();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2});
    }

    void vectorizeRemembersDetailBetweenRuns() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.selectRegions({regionAt(window, 5, 5)}, SelectMode::Replace);
        runModalScript(&window, {[](QWidget* modal) {
            modal->findChild<QSlider*>("vectorizeDetailSlider")->setValue(80);
            static_cast<QDialog*>(modal)->accept();
        }});
        window.vectorizeSelectedRegion();
        window.selectRegions({regionAt(window, 30, 5)}, SelectMode::Replace);
        int seen = -1;
        runModalScript(&window, {[&seen](QWidget* modal) {
            seen = modal->findChild<QSlider*>("vectorizeDetailSlider")->value();
            static_cast<QDialog*>(modal)->reject();
        }});
        window.vectorizeSelectedRegion();
        QCOMPARE(seen, 80);
    }

    void vectorizingAnAlreadyVectorizedRegionDoesNotDuplicate() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.selectRegions({regionAt(window, 5, 5)}, SelectMode::Replace);
        runModalScript(&window, {acceptDialog()});
        window.vectorizeSelectedRegion();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        const ObjectId first = window.project_.vector_objects.front().id;

        // Seconde vectorisation, réponse « Sélectionner l'objet existant » : aucun doublon.
        window.selectRegions({regionAt(window, 5, 5)}, SelectMode::Replace);
        runModalScript(&window, {clickButton(QStringLiteral("Sélectionner l'objet existant"))});
        window.vectorizeSelectedRegion();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QVERIFY(window.selectedObject_.has_value());
        QCOMPARE(*window.selectedObject_, first);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("déjà vectorisée")));

        // « Remplacer » : un seul objet, nouveau, dont l'annulation rend l'ancien.
        window.selectRegions({regionAt(window, 5, 5)}, SelectMode::Replace);
        runModalScript(&window, {clickButton(QStringLiteral("Remplacer")), acceptDialog()});
        window.vectorizeSelectedRegion();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QVERIFY(window.project_.vector_objects.front().id != first);
        window.undo();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.vector_objects.front().id, first);
    }

    void vectorizeIgnoreKeepsOnlyTheNewRegions() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.selectRegions({regionAt(window, 5, 5)}, SelectMode::Replace);
        runModalScript(&window, {acceptDialog()});
        window.vectorizeSelectedRegion();

        window.selectRegions({regionAt(window, 5, 5), regionAt(window, 30, 5)},
                             SelectMode::Replace);
        runModalScript(&window,
                       {clickButton(QStringLiteral("Ignorer ces régions")), acceptDialog()});
        window.vectorizeSelectedRegion();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2}); // 1 ancien + 1 nouveau
    }

    // ---- mode « Fusionner avec… » (audit A7) ------------------------------------------

    void mergeModeAnnouncesItselfAndRejectsASelectedTarget() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.showSegAct_->setChecked(true);
        window.showVectorsAct_->setChecked(false);
        const RegionId a = regionAt(window, 5, 5);
        const RegionId b = regionAt(window, 30, 5);
        window.selectRegions({a}, SelectMode::Replace);

        window.mergeAct_->setChecked(true);
        QVERIFY(window.mergeMode_);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("CIBLE")));
        QCOMPARE(window.view_->viewport()->cursor().shape(), Qt::PointingHandCursor);

        // Clic sur la région déjà sélectionnée : message, mode conservé, rien de fusionné.
        window.onCanvasClicked(pixelMm(window, 5, 5));
        QVERIFY(window.mergeMode_);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("NON sélectionnée")));
        QCOMPARE(window.project_.segmentation->region_count(), std::size_t{3});

        // Clic sur une autre région : fusion, mode terminé, curseur normal.
        window.onCanvasClicked(pixelMm(window, 30, 5));
        QVERIFY(!window.mergeMode_);
        QCOMPARE(window.project_.segmentation->region_count(), std::size_t{2});
        QVERIFY(window.project_.segmentation->find(a) == nullptr);
        QVERIFY(window.project_.segmentation->find(b) != nullptr);
        QCOMPARE(window.view_->viewport()->cursor().shape(), Qt::ArrowCursor);
    }

    // ---- régions déjà vectorisées : pas d'objet orphelin (audit A29) -----------------------

    void mergingAVectorizedRegionAsksWhatToDoWithItsObject() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        const RegionId a = regionAt(window, 5, 5);
        const RegionId b = regionAt(window, 30, 5);
        window.selectRegions({a}, SelectMode::Replace);
        runModalScript(&window, {acceptDialog()});
        window.vectorizeSelectedRegion();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});

        // Annuler : rien ne change.
        runModalScript(&window, {clickButton(QStringLiteral("Annuler"))});
        QVERIFY(!window.mergeRegions({a}, b));
        QCOMPARE(window.project_.segmentation->region_count(), std::size_t{3});
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});

        // Supprimer les objets : fusion ET retrait en un seul pas d'annulation.
        runModalScript(&window, {clickButton(QStringLiteral("Supprimer les objets"))});
        QVERIFY(window.mergeRegions({a}, b));
        QCOMPARE(window.project_.segmentation->region_count(), std::size_t{2});
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{0});
        window.undo();
        QCOMPARE(window.project_.segmentation->region_count(), std::size_t{3});
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});

        // Conserver les objets : la fusion a lieu, l'objet reste.
        runModalScript(&window, {clickButton(QStringLiteral("Conserver les objets"))});
        QVERIFY(window.mergeRegions({a}, b));
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
    }

    void deletingRegionsReportsTheCount() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.selectRegions({regionAt(window, 5, 5), regionAt(window, 30, 5)},
                             SelectMode::Replace);
        window.deleteSelectedRegions();
        QVERIFY(
            window.statusBar()->currentMessage().contains(QStringLiteral("2 régions supprimées")));
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Ctrl+Z")));
    }

    // ---- survol des régions et opacité de la carte (audit A8) ----------------------------

    void hoveringARegionHighlightsItUnlessSelected() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.showSegAct_->setChecked(true);
        window.showVectorsAct_->setChecked(false);
        const RegionId a = regionAt(window, 5, 5);
        window.selectRegions({a}, SelectMode::Replace);

        window.updateRegionHover(pixelMm(window, 30, 5)); // autre région : surlignée
        QVERIFY(window.regionHoverItem_ != nullptr);
        QVERIFY(window.regionHoverItem_->isVisible());
        QCOMPARE(window.regionHoverId_.value().value, regionAt(window, 30, 5).value);

        window.updateRegionHover(pixelMm(window, 5, 5)); // région sélectionnée : déjà éclaircie
        QVERIFY(!window.regionHoverItem_->isVisible());

        window.updateRegionHover(pixelMm(window, 30, 5));
        QVERIFY(window.regionHoverItem_->isVisible());
        window.updateRegionHover(std::nullopt);
        QVERIFY(!window.regionHoverItem_->isVisible());
    }

    void regionMapOpacityIsAdjustableAndRemembered() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.showSegAct_->setChecked(true);
        const auto mapOpacity = [&window] {
            for (QGraphicsItem* item : window.baseItems_) {
                if (item->data(0).toString() == QLatin1String("regionMap")) {
                    return item->opacity();
                }
            }
            return -1.0;
        };
        QCOMPARE(mapOpacity(), 0.9);
        window.setRegionMapOpacity(0.4);
        QCOMPARE(mapOpacity(), 0.4);
        window.displayImage(window.processed_); // un nouveau rendu garde l'opacité choisie
        QCOMPARE(mapOpacity(), 0.4);
        window.setRegionMapOpacity(0.0); // bornée : la carte ne disparaît jamais
        QCOMPARE(mapOpacity(), 0.2);
        MainWindow other;
        QCOMPARE(other.regionMapOpacity_, 0.2); // mémorisée d'une fenêtre à l'autre
    }

    // ---- cadre de sélection quand les objets sont affichés (audit A10) -----------------

    void rectangleSelectsRegionsWhenItTouchesNoVectorObject() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.showSegAct_->setChecked(true);
        window.selectRegions({regionAt(window, 5, 5)}, SelectMode::Replace);
        runModalScript(&window, {acceptDialog()});
        window.vectorizeSelectedRegion(); // affiche les objets vectoriels
        QVERIFY(window.showVectorsAct_->isChecked());

        // Cadre sur la bande de droite, où aucun objet n'existe : il choisit la région.
        const double mm = window.project_.mm_per_px.value;
        const QPointF c = pixelMm(window, 50, 20);
        window.onSelectionRectangle(QRectF(c.x() - 2 * mm, c.y() - 2 * mm, 4 * mm, 4 * mm),
                                    SelectMode::Replace, /*crossing=*/true);
        QCOMPARE(window.selectedRegionIds().size(), std::size_t{1});
        QCOMPARE(window.selectedRegion_->value, regionAt(window, 50, 20).value);

        // Cadre sur l'objet : c'est lui qui est saisi, et un message explique pour les régions.
        const QPointF o = pixelMm(window, 5, 5);
        window.onSelectionRectangle(QRectF(o.x() - 2 * mm, o.y() - 2 * mm, 4 * mm, 4 * mm),
                                    SelectMode::Replace, /*crossing=*/true);
        QVERIFY(window.selectedObject_.has_value());
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Masquez")));
    }

    // ---- actions grisées avec raison, nombre de régions (audit A23/A40) ---------------------

    void actionsAreDisabledWithAReasonAndRegionCountIsShown() {
        MainWindow window;
        QVERIFY(!window.aiSegmentAct_->isEnabled());
        QVERIFY(window.aiSegmentAct_->statusTip().contains(QStringLiteral("image")));
        QVERIFY(!window.autoDigitizeAct_->isEnabled());

        auto project = threeBandProject();
        auto segmentation = std::move(project.segmentation);
        project.segmentation.reset();
        window.applyLoadedProject(project);
        QVERIFY(window.aiSegmentAct_->isEnabled());
        QVERIFY(!window.autoDigitizeAct_->isEnabled());
        QVERIFY(window.autoDigitizeAct_->statusTip().contains(QStringLiteral("Segmentez")));
        QVERIFY(window.regionCountLabel_ == nullptr || !window.regionCountLabel_->isVisible());

        window.undoStack_.execute(
            std::make_unique<commands::SetSegmentationCommand>(std::move(segmentation)),
            window.project_);
        window.updateActions();
        QVERIFY(window.autoDigitizeAct_->isEnabled());
        QVERIFY(window.regionCountLabel_ != nullptr);
        QVERIFY(!window.regionCountLabel_->isHidden());
        QCOMPARE(window.regionCountLabel_->text(), QStringLiteral("3 région(s)"));
    }

    void workflowStepsRunTheActionOrExplainWhy() {
        MainWindow window;
        // Étape « Broderie » sans rien de segmenté : la raison, avec le vrai nom du menu.
        window.onWorkflowStepClicked(3);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Segmentez")));
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Numérisation automatique")));
        window.onWorkflowStepClicked(5); // export sans points
        QVERIFY(window.statusBar()->currentMessage().contains(
            QStringLiteral("Exporter une broderie machine")));

        // Étape « Régions » avec une image : lance « Segmenter l'image… » (dialogue refusé ici).
        window.applyLoadedProject(threeBandProject());
        window.project_.segmentation.reset();
        window.updateActions();
        bool dialogSeen = false;
        runModalScript(&window, {[&dialogSeen](QWidget* modal) {
            dialogSeen = modal->windowTitle() == QStringLiteral("Segmenter l'image");
            static_cast<QDialog*>(modal)->reject();
        }});
        window.onWorkflowStepClicked(1);
        QVERIFY(dialogSeen);
    }

    void segmentationDialogRemembersItsSettingsAndShowsMmSquared() {
        MainWindow window;
        auto project = threeBandProject();
        project.segmentation.reset();
        window.applyLoadedProject(project);
        QString mmText;
        runModalScript(&window, {[&mmText](QWidget* modal) {
            modal->findChild<QSpinBox*>()->setValue(5);
            mmText = modal->findChild<QLabel*>("segmentMinSizeMm")->text();
            static_cast<QDialog*>(modal)->accept();
        }});
        window.segmentImage();
        QVERIFY(mmText.contains(QStringLiteral("mm²")));
        QVERIFY(window.project_.segmentation.has_value());

        int remembered = -1;
        runModalScript(&window, {[&remembered](QWidget* modal) {
            remembered = modal->findChild<QSpinBox*>()->value();
            static_cast<QDialog*>(modal)->reject();
        }});
        window.segmentImage();
        QCOMPARE(remembered, 5);
    }

    // ---- recadrage, quantification (audit A9/A21) -----------------------------------------

    void cropToolAnnouncesWhatTheDragWillDo() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.setTool(Tool::Rect);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("recadrée")));
        QVERIFY(window.toolRectAct_->text().contains(QStringLiteral("Recadrer")));
    }

    void cropWithExistingObjectsAsksFirst() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        window.selectRegions({regionAt(window, 5, 5)}, SelectMode::Replace);
        runModalScript(&window, {acceptDialog()});
        window.vectorizeSelectedRegion();
        const auto before = window.project_.ops.size();
        runModalScript(&window, {[](QWidget* modal) {
            static_cast<QMessageBox*>(modal)->button(QMessageBox::No)->click();
        }});
        const double mm = window.project_.mm_per_px.value;
        window.onCropSelected(QRectF(-10 * mm, -10 * mm, 20 * mm, 20 * mm));
        QCOMPARE(window.project_.ops.size(), before); // refusé : image intacte
    }

    void quantizeDialogPreviewsAndRemembersTheChoice() {
        MainWindow window;
        auto project = threeBandProject();
        project.segmentation.reset();
        window.applyLoadedProject(project);
        runModalScript(&window, {[](QWidget* modal) {
            auto* spin = modal->findChild<QSpinBox*>("quantizeColorsSpin");
            spin->setValue(5);
            QTest::qWait(250); // laisse passer l'aperçu différé
            static_cast<QDialog*>(modal)->accept();
        }});
        window.quantizeColors();
        QCOMPARE(window.project_.ops.size(), std::size_t{1});
        QCOMPARE(std::get<image::QuantizeOp>(window.project_.ops.back()).colors, 5);

        int seen = -1;
        runModalScript(&window, {[&seen](QWidget* modal) {
            seen = modal->findChild<QSpinBox*>("quantizeColorsSpin")->value();
            static_cast<QDialog*>(modal)->reject();
        }});
        window.quantizeColors();
        QCOMPARE(seen, 5);
        QCOMPARE(window.project_.ops.size(), std::size_t{1}); // annulé : rien d'ajouté
    }

    // ---- dialogue d'import (audit A19/A20) ---------------------------------------------------

    void importDialogDefaultsToTheHoopAndFitsOnRequest() {
        const QImage preview(4, 4, QImage::Format_RGB32);
        ImportDialog dialog(3000, 2000, preview, QSizeF(100.0, 100.0));
        auto* width = dialog.findChild<QDoubleSpinBox*>("importWidthSpin");
        auto* height = dialog.findChild<QDoubleSpinBox*>("importHeightSpin");
        QVERIFY(width != nullptr && height != nullptr);
        QVERIFY(width->value() <= 100.0 + 1e-6); // 96 dpi donnerait ~ 794 mm
        QVERIFY(height->value() <= 100.0 + 1e-6);
        QCOMPARE(width->value(), 100.0);
        QVERIFY(std::abs(height->value() - 66.7) < 0.06);

        width->setValue(40.0);
        auto* fit = dialog.findChild<QPushButton*>("importFitButton");
        QVERIFY(fit != nullptr);
        fit->click();
        QCOMPARE(width->value(), 100.0);
    }

    void importDialogKeepsTheRatioWithinRangeAndReportsDistortion() {
        const QImage preview(4, 4, QImage::Format_RGB32);
        ImportDialog dialog(4000, 100, preview, QSizeF(1000.0, 1000.0)); // bandeau 40:1
        auto* width = dialog.findChild<QDoubleSpinBox*>("importWidthSpin");
        auto* height = dialog.findChild<QDoubleSpinBox*>("importHeightSpin");
        // Ratio verrouillé : la largeur est bornée pour que la hauteur reste dans [1, 1000].
        QCOMPARE(width->minimum(), 40.0);
        QCOMPARE(height->maximum(), 25.0);
        width->setValue(1000.0);
        QCOMPARE(height->value(), 25.0);

        auto* keep = dialog.findChild<QCheckBox*>("importKeepRatioCheck");
        auto* ratio = dialog.findChild<QLabel*>("importRatioLabel");
        QVERIFY(ratio->isHidden());
        keep->setChecked(false); // plages libres, ratio indiqué dès qu'il change
        QCOMPARE(width->minimum(), 1.0);
        height->setValue(250.0);
        QVERIFY(!ratio->isHidden());
        QVERIFY(ratio->text().contains(QStringLiteral("proportions")));
    }

    // ---- dialogue IA (audit A13-A17) ------------------------------------------------------------

    void aiDialogExplainsItsColumnsAndSortsNumerically() {
        image::Image img;
        img.width = 4;
        img.height = 4;
        img.rgba.assign(4 * 4 * 4, 255);
        AiSegmentationDialog dialog(img, Millimeters{0.2}, AiPreferences{});
        auto* table = dialog.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QVERIFY(table->isSortingEnabled());
        for (const int column : {3, 4, 5}) { // IoU, Stabilité, Protéger
            QVERIFY(!table->horizontalHeaderItem(column)->toolTip().isEmpty());
        }
        QVERIFY(table->horizontalHeaderItem(3)->toolTip().contains(QStringLiteral("Confiance")));
        QVERIFY(
            table->horizontalHeaderItem(5)->toolTip().contains(QStringLiteral("jamais absorbé")));
        // Rien à perdre (aucun masque) : fermer ne demande aucune confirmation.
        dialog.show();
        dialog.reject();
        QVERIFY(!dialog.isVisible());
        QVERIFY(dialog.output() == AiSegmentationDialog::Output::EditableRegions);
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_segmentation_workflow.moc"
