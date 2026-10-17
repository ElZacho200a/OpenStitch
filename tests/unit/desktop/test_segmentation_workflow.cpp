// SPDX-License-Identifier: Apache-2.0
// Flux import -> segmentation -> vectorisation (audit ergonomique A) : vectorisation de toute la
// sélection, régions déjà vectorisées, mode « Fusionner avec… », survol des régions, actions
// grisées avec raison, aperçu de quantification, dialogues d'import et IA.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGraphicsItem>
#include <QHeaderView>
#include <QImage>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <array>
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
#include "openstitch/segmentation/color_merge.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "segmentation_colors_panel.hpp"

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

openstitch::image::Image syntheticMaskSource() {
    openstitch::image::Image image;
    image.width = 8;
    image.height = 4;
    image.rgba.assign(8 * 4 * 4, 255);
    return image;
}

QString maskJobsRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/OpenStitch/ai-jobs");
}

QString maskFixtureTemplate() {
    const QString root = maskJobsRoot();
    if (!QDir().mkpath(root)) {
        return {};
    }
    return root + QStringLiteral("/ux-mask-test-XXXXXX");
}

// Résultat synthétique du worker, reçu par le vrai slot du dialogue. Aucun worker ni modèle
// n'est lancé. Les PNG restent relatifs au dossier de tâche utilisé par le dialogue neuf.
class SyntheticMaskFixture {
public:
    bool loadInto(AiSegmentationDialog& dialog) {
        if (!directory_.isValid()) {
            return false;
        }
        openstitch::ai_segmentation::MaskCollection collection;
        collection.schema_version = 1;
        collection.source_file = "input.png";
        collection.model_worker_id = "synthetic";
        collection.image_width = 8;
        collection.image_height = 4;
        const std::array<int, 3> ids{0, 2, 10};
        const std::array<QRect, 3> bounds{QRect(1, 1, 1, 1), QRect(3, 1, 2, 1), QRect(6, 0, 2, 4)};
        const std::array<double, 3> ious{0.7, 0.9, 0.8};
        for (std::size_t i = 0; i < ids.size(); ++i) {
            QImage mask(8, 4, QImage::Format_RGB32);
            mask.fill(Qt::black);
            {
                QPainter painter(&mask);
                painter.fillRect(bounds[i], Qt::white);
            }
            const QString path = directory_.filePath(QStringLiteral("mask_%1.png").arg(ids[i]));
            if (!mask.save(path)) {
                return false;
            }
            openstitch::ai_segmentation::MaskEntry entry;
            entry.id = ids[i];
            entry.file = QDir(maskJobsRoot()).relativeFilePath(path).toStdString();
            entry.area_pixels = static_cast<std::size_t>(bounds[i].width() * bounds[i].height());
            entry.bbox_xywh = {bounds[i].x(), bounds[i].y(), bounds[i].width(), bounds[i].height()};
            entry.predicted_iou = ious[i];
            entry.stability_score = 0.95;
            collection.masks.push_back(entry);
        }
        const auto json = openstitch::ai_segmentation::serialize_masks_json(collection);
        QFile file(directory_.filePath(QStringLiteral("masks.json")));
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(json.data(), static_cast<qint64>(json.size())) !=
                static_cast<qint64>(json.size())) {
            return false;
        }
        file.close();
        return QMetaObject::invokeMethod(
            &dialog, "onSegmentResult", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("synthetic-result")), Q_ARG(QString, directory_.path()),
            Q_ARG(QString, QStringLiteral("masks.json")), Q_ARG(int, 3));
    }

private:
    QTemporaryDir directory_{maskFixtureTemplate()};
};

QPushButton* aiDialogButton(AiSegmentationDialog& dialog, const QString& text) {
    for (auto* button : dialog.findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

QLabel* selectedMaskPreview(AiSegmentationDialog& dialog) {
    QLabel* preview = nullptr;
    for (auto* label : dialog.findChildren<QLabel*>()) {
        if (label->frameShape() == QFrame::StyledPanel) {
            preview = label; // le second cadre est l'aperçu de sélection
        }
    }
    return preview;
}

void disableMaskCleanup(AiSegmentationDialog& dialog) {
    for (auto* spin : dialog.findChildren<QDoubleSpinBox*>()) {
        spin->setValue(0.0);
    }
}

void selectMaskRows(QTableWidget& table, const QTableWidgetItem* first,
                    const QTableWidgetItem* second) {
    table.clearSelection();
    for (const auto* item : {first, second}) {
        table.selectionModel()->select(table.model()->index(item->row(), 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
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

    void importDialogKeepsTheRatioWithinRangeAndExplainsTheConstraint() {
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
        QVERIFY(keep->isChecked());
        QVERIFY(!keep->isEnabled()); // pas d'étirement silencieusement ignoré à l'import
        QVERIFY(!keep->toolTip().isEmpty());
        height->setValue(20.0);
        QCOMPARE(width->value(), 800.0);
        QVERIFY(ratio->isHidden());
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

    void aiDialogNumericMaskCellsAreReadOnly_data() {
        QTest::addColumn<bool>("mergeFirst");
        QTest::newRow("loaded-masks") << false;
        QTest::newRow("merged-mask") << true;
    }

    void aiDialogNumericMaskCellsAreReadOnly() {
        QFETCH(bool, mergeFirst);
        SyntheticMaskFixture fixture;
        AiSegmentationDialog dialog(syntheticMaskSource(), Millimeters{0.2}, AiPreferences{});
        QVERIFY(fixture.loadInto(dialog));
        auto* table = dialog.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 3);
        if (mergeFirst) {
            auto* merge = aiDialogButton(dialog, QStringLiteral("Fusionner la sélection"));
            QVERIFY(merge != nullptr);
            selectMaskRows(*table, table->item(0, 1), table->item(1, 1));
            merge->click();
            QCOMPARE(table->rowCount(), 2);
        }
        dialog.show();
        QApplication::processEvents();
        for (int row = 0; row < table->rowCount(); ++row) {
            for (const int column : {1, 2, 3, 4}) {
                auto* item = table->item(row, column);
                QVERIFY(item != nullptr);
                QVERIFY(!item->flags().testFlag(Qt::ItemIsEditable));
                const QString original = item->text();
                table->setCurrentItem(item);
                QTest::keyClick(table, Qt::Key_F2);
                QVERIFY(table->findChild<QLineEdit*>() == nullptr);
                QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                                   table->visualItemRect(item).center());
                QVERIFY(table->findChild<QLineEdit*>() == nullptr);
                QCOMPARE(item->text(), original);
            }
            for (const int column : {0, 5}) {
                QVERIFY(table->item(row, column)->flags().testFlag(Qt::ItemIsUserCheckable));
            }
        }
    }

    void aiDialogSelectionUsesStableMaskIdentity() {
        SyntheticMaskFixture fixture;
        AiSegmentationDialog dialog(syntheticMaskSource(), Millimeters{0.2}, AiPreferences{});
        QVERIFY(fixture.loadInto(dialog));
        auto* table = dialog.findChild<QTableWidget*>();
        auto* preview = selectedMaskPreview(dialog);
        QVERIFY(table != nullptr && preview != nullptr);
        QCOMPARE(table->rowCount(), 3);
        auto* id = table->item(0, 1);       // masque 0 : un pixel, distinct des deux autres
        id->setText(QStringLiteral("777")); // l'affichage n'est jamais l'identité
        table->sortItems(2, Qt::DescendingOrder);
        table->selectRow(id->row());
        QVERIFY(!preview->pixmap().isNull());
        QVERIFY(id->data(Qt::UserRole).isValid());
        QCOMPARE(id->data(Qt::UserRole).toInt(), 0);
    }

    void aiDialogValidationUsesStableMaskIdentityAfterSorting() {
        QSettings().setValue(QStringLiteral("ui/aiSegmentation/colorRefine"), false);
        SyntheticMaskFixture fixture;
        AiSegmentationDialog dialog(syntheticMaskSource(), Millimeters{0.2}, AiPreferences{});
        QVERIFY(fixture.loadInto(dialog));
        disableMaskCleanup(dialog);
        auto* table = dialog.findChild<QTableWidget*>();
        auto* validate = aiDialogButton(dialog, QStringLiteral("Valider"));
        QVERIFY(table != nullptr && validate != nullptr);
        QCOMPARE(table->rowCount(), 3);
        auto* first = table->item(0, 1);
        auto* excluded = table->item(1, 1);
        table->item(first->row(), 5)->setCheckState(Qt::Checked);
        table->item(excluded->row(), 0)->setCheckState(Qt::Unchecked);
        first->setText(QStringLiteral("777"));
        table->sortItems(2, Qt::DescendingOrder);
        QCOMPARE(table->item(first->row(), 5)->checkState(), Qt::Checked);
        QCOMPARE(table->item(excluded->row(), 0)->checkState(), Qt::Unchecked);
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        validate->click();
        QCOMPARE(accepted.count(), 1);
        auto result = dialog.takeSegmentation();
        QVERIFY(result.has_value());
        QCOMPARE(result->region_count(), std::size_t{2});
        const auto* firstRegion = result->find(RegionId{1});
        const auto* lastRegion = result->find(RegionId{2});
        QVERIFY(firstRegion != nullptr && lastRegion != nullptr);
        QCOMPARE(firstRegion->pixel_count, std::size_t{1});
        QCOMPARE(lastRegion->pixel_count, std::size_t{8});
        QCOMPARE(firstRegion->rgb[0], std::uint8_t{60}); // couleur de l'ID 0, pas de "777"
        QCOMPARE(lastRegion->rgb[0], std::uint8_t{230}); // couleur de l'ID 10
    }

    void aiDialogMergeUsesStableMaskIdentityAfterSorting() {
        QSettings().setValue(QStringLiteral("ui/aiSegmentation/colorRefine"), false);
        SyntheticMaskFixture fixture;
        AiSegmentationDialog dialog(syntheticMaskSource(), Millimeters{0.2}, AiPreferences{});
        QVERIFY(fixture.loadInto(dialog));
        disableMaskCleanup(dialog);
        auto* table = dialog.findChild<QTableWidget*>();
        auto* merge = aiDialogButton(dialog, QStringLiteral("Fusionner la sélection"));
        auto* validate = aiDialogButton(dialog, QStringLiteral("Valider"));
        QVERIFY(table != nullptr && merge != nullptr && validate != nullptr);
        QCOMPARE(table->rowCount(), 3);
        auto* first = table->item(0, 1);
        auto* second = table->item(1, 1);
        first->setText(QStringLiteral("777"));
        table->sortItems(3, Qt::DescendingOrder);
        selectMaskRows(*table, first, second);
        merge->click();
        QCOMPARE(table->rowCount(), 2);
        table->sortItems(1, Qt::AscendingOrder);
        QCOMPARE(table->item(1, 1)->text(), QStringLiteral("11"));
        QCOMPARE(table->item(1, 2)->text().toDouble(), 0.12);
        QVERIFY(table->item(1, 1)->data(Qt::UserRole).isValid());
        QCOMPARE(table->item(1, 1)->data(Qt::UserRole).toInt(), 11);
        validate->click();
        auto result = dialog.takeSegmentation();
        QVERIFY(result.has_value());
        QCOMPARE(result->region_count(), std::size_t{2});
        const auto* merged = result->find(RegionId{2});
        QVERIFY(merged != nullptr);
        QCOMPARE(merged->pixel_count, std::size_t{3});
    }

    void aiDialogMergePreservesProtection_data() {
        QTest::addColumn<bool>("protectFirst");
        QTest::addColumn<bool>("protectSecond");
        QTest::newRow("neither-protected") << false << false;
        QTest::newRow("first-protected") << true << false;
        QTest::newRow("second-protected") << false << true;
        QTest::newRow("both-protected") << true << true;
    }

    void aiDialogMergePreservesProtection() {
        QFETCH(bool, protectFirst);
        QFETCH(bool, protectSecond);
        QSettings().setValue(QStringLiteral("ui/aiSegmentation/colorRefine"), false);
        SyntheticMaskFixture fixture;
        AiSegmentationDialog dialog(syntheticMaskSource(), Millimeters{0.2}, AiPreferences{});
        QVERIFY(fixture.loadInto(dialog));
        if (!protectFirst && !protectSecond) {
            disableMaskCleanup(dialog);
        }
        auto* table = dialog.findChild<QTableWidget*>();
        auto* merge = aiDialogButton(dialog, QStringLiteral("Fusionner la sélection"));
        auto* validate = aiDialogButton(dialog, QStringLiteral("Valider"));
        QVERIFY(table != nullptr && merge != nullptr && validate != nullptr);
        QCOMPARE(table->rowCount(), 3);
        auto* first = table->item(0, 1);
        auto* second = table->item(1, 1);
        auto* untouched = table->item(2, 1);
        table->item(first->row(), 5)->setCheckState(protectFirst ? Qt::Checked : Qt::Unchecked);
        table->item(second->row(), 5)->setCheckState(protectSecond ? Qt::Checked : Qt::Unchecked);
        table->item(untouched->row(), 0)->setCheckState(Qt::Unchecked);
        table->item(untouched->row(), 5)->setCheckState(Qt::Checked);
        table->sortItems(5, Qt::DescendingOrder);
        selectMaskRows(*table, first, second);
        merge->click();
        QCOMPARE(table->rowCount(), 2);
        table->sortItems(1, Qt::AscendingOrder);
        QCOMPARE(table->item(1, 1)->text(), QStringLiteral("11"));
        QCOMPARE(table->item(1, 5)->checkState(),
                 protectFirst || protectSecond ? Qt::Checked : Qt::Unchecked);
        QCOMPARE(table->item(untouched->row(), 0)->checkState(), Qt::Unchecked);
        QCOMPARE(table->item(untouched->row(), 5)->checkState(), Qt::Checked);
        validate->click();
        auto result = dialog.takeSegmentation();
        QVERIFY(result.has_value());
        QCOMPARE(result->region_count(), std::size_t{1});
        const auto* merged = result->find(RegionId{1});
        QVERIFY(merged != nullptr);
        QCOMPARE(merged->pixel_count, std::size_t{3}); // les deux îlots survivent au nettoyage
    }

    void aiDialogRejectsMissingMaskIdentity_data() {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QVariant>("identity");
        for (const QString operation :
             {QStringLiteral("preview"), QStringLiteral("merge"), QStringLiteral("validate")}) {
            QTest::newRow(qPrintable(operation + QStringLiteral("-absent")))
                << operation << QVariant{};
            QTest::newRow(qPrintable(operation + QStringLiteral("-unknown")))
                << operation << QVariant{1234};
        }
    }

    void aiDialogRejectsMissingMaskIdentity() {
        QFETCH(QString, operation);
        QFETCH(QVariant, identity);
        QSettings().setValue(QStringLiteral("ui/aiSegmentation/colorRefine"), false);
        SyntheticMaskFixture fixture;
        AiSegmentationDialog dialog(syntheticMaskSource(), Millimeters{0.2}, AiPreferences{});
        QVERIFY(fixture.loadInto(dialog));
        disableMaskCleanup(dialog);
        auto* table = dialog.findChild<QTableWidget*>();
        QVERIFY(table != nullptr);
        QCOMPARE(table->rowCount(), 3);
        auto* first = table->item(0, 1);
        auto* invalid = table->item(1, 1);
        invalid->setData(Qt::UserRole, identity);
        table->sortItems(2, Qt::DescendingOrder);
        if (operation == QLatin1String("preview")) {
            auto* preview = selectedMaskPreview(dialog);
            QVERIFY(preview != nullptr);
            table->selectRow(first->row());
            QVERIFY(!preview->pixmap().isNull());
            table->selectRow(invalid->row());
            QVERIFY(preview->pixmap().isNull());
        } else if (operation == QLatin1String("merge")) {
            auto* merge = aiDialogButton(dialog, QStringLiteral("Fusionner la sélection"));
            QVERIFY(merge != nullptr);
            selectMaskRows(*table, first, invalid);
            merge->click();
            QCOMPARE(table->rowCount(), 3); // aucun retrait partiel ni fusion avec le masque 0
        } else {
            auto* validate = aiDialogButton(dialog, QStringLiteral("Valider"));
            QVERIFY(validate != nullptr);
            for (int row = 0; row < table->rowCount(); ++row) {
                table->item(row, 0)->setCheckState(row == invalid->row() ? Qt::Checked
                                                                         : Qt::Unchecked);
            }
            QSignalSpy accepted(&dialog, &QDialog::accepted);
            validate->click();
            QCOMPARE(accepted.count(), 0);
            QVERIFY(!dialog.hasResult());
            QVERIFY(validate->isEnabled());
        }
    }

    // ---- fusion de couleurs (panneau « Couleurs ») ------------------------------------------

    void colorMergePreviewNeverTouchesTheDocument() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        const auto labels = window.project_.segmentation->labels;
        const auto plan = openstitch::segmentation::plan_reduce_colors(
            *window.project_.segmentation, 2, 0.0, true);
        QVERIFY(!plan.empty());

        window.previewColorMerge(plan);
        QVERIFY(window.previewSegmentation_.has_value());
        // L'aperçu n'a plus que deux teintes ; le document garde ses trois : c'est une copie.
        QCOMPARE(openstitch::segmentation::color_classes(*window.previewSegmentation_).size(),
                 std::size_t{2});
        QCOMPARE(openstitch::segmentation::color_classes(*window.project_.segmentation).size(),
                 std::size_t{3});
        QCOMPARE(window.project_.segmentation->labels, labels);
        QVERIFY(window.undoStack_.isClean());

        window.clearSegmentationPreview(true);
        QVERIFY(!window.previewSegmentation_.has_value());
        QCOMPARE(window.project_.segmentation->labels, labels);
    }

    void colorMergeApplyIsOneUndoStepAndRefreshesThePanel() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        const auto labels = window.project_.segmentation->labels;
        auto* open = window.findChild<QAction*>(QStringLiteral("action_mergeColors"));
        QVERIFY(open != nullptr);
        open->trigger();
        QVERIFY(window.colorsPanel_ != nullptr);
        auto* list = window.colorsPanel_->findChild<QListWidget*>(QStringLiteral("segColorsList"));
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);

        const auto plan = openstitch::segmentation::plan_reduce_colors(
            *window.project_.segmentation, 2, 0.0, true);
        window.previewColorMerge(plan);
        window.applyColorMergePlan(plan);
        QVERIFY(!window.previewSegmentation_.has_value()); // l'aperçu cède la place au résultat
        const auto distinctColors = [&window] {
            return openstitch::segmentation::color_classes(*window.project_.segmentation).size();
        };
        QCOMPARE(distinctColors(), std::size_t{2});
        QCOMPARE(list->count(), 2); // la liste suit le document

        window.undo(); // UN seul pas pour toute la fusion
        QCOMPARE(distinctColors(), std::size_t{3});
        QCOMPARE(window.project_.segmentation->region_count(), std::size_t{3});
        QCOMPARE(window.project_.segmentation->labels, labels);
        QCOMPARE(list->count(), 3);
        window.redo();
        QCOMPARE(distinctColors(), std::size_t{2});
    }

    void selectingAColorSelectsAllItsRegions() {
        MainWindow window;
        window.applyLoadedProject(threeBandProject());
        const auto classes = openstitch::segmentation::color_classes(*window.project_.segmentation);
        window.selectRegionsOfColors({classes.front().rgb});
        QCOMPARE(window.selectedRegionIds().size(), classes.front().regions.size());
        window.selectRegionsOfColors({});
        QVERIFY(window.selectedRegionIds().empty());
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_segmentation_workflow.moc"
