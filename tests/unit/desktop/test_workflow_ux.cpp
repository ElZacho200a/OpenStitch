// SPDX-License-Identifier: Apache-2.0
// Lot C (suite) de l'audit UX : cadre dans l'import et en barre d'état (HP-UX-033), export rapide
// et décision explicite avant export (HP-UX-032), préréglages de numérisation (HP-UX-030), dock
// Analyse ouvert seul et « Corriger tout » (HP-UX-031). Headless, sans pixel ni sleep.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "autodigitize_presets.hpp"
#include "font_catalog.hpp"
#include "import_dialog.hpp"
#include "machine_export_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "workflow_panel.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;
namespace sa = openstitch::stitch_analysis;
namespace pio = openstitch::project_io;
using openstitch::Micrometers;
using openstitch::Vec2um;
using namespace openstitch::desktop;

// Trois contours de 10 mm, rouge / bleu / rouge, dans le cadre par défaut (finitions inactives :
// les codecs à grille 0,1 mm ne perdent aucun point).
doc::Project contourProject() {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    project.finishing.enabled = false;
    for (int i = 0; i < 3; ++i) {
        doc::VectorObject vector;
        vector.id = project.object_ids.next();
        vector.name = "Carre " + std::to_string(i + 1);
        geo::Path path;
        path.closed = true;
        const std::int32_t x = i * 15'000;
        for (const auto& pos :
             std::array<Vec2um, 4>{Vec2um{Micrometers{x}, Micrometers{0}},
                                   Vec2um{Micrometers{x + 10'000}, Micrometers{0}},
                                   Vec2um{Micrometers{x + 10'000}, Micrometers{10'000}},
                                   Vec2um{Micrometers{x}, Micrometers{10'000}}}) {
            path.nodes.push_back({pos, geo::NodeType::Corner, std::nullopt, std::nullopt});
        }
        vector.paths.push_back({std::move(path), {}});
        doc::EmbroideryObject embroidery;
        embroidery.id = project.object_ids.next();
        embroidery.source_vector = vector.id;
        embroidery.name = "Contour " + std::to_string(i + 1);
        doc::RunningStitchParams params;
        params.stitch_length = Micrometers{1'000};
        embroidery.params = params;
        embroidery.rgb = i == 1 ? std::array<std::uint8_t, 3>{20, 60, 210}
                                : std::array<std::uint8_t, 3>{220, 20, 30};
        project.vector_objects.push_back(std::move(vector));
        project.embroidery_objects.push_back(std::move(embroidery));
    }
    return project;
}

// Un carré de côté `side`, décalé de `offsetX`, cousu en point droit de `stitchLength`.
void addSquare(doc::Project& project, std::int32_t offsetX, std::int32_t stitchLength,
               std::int32_t side) {
    doc::VectorObject vector;
    vector.id = project.object_ids.next();
    vector.name = "Carre";
    geo::Path path;
    path.closed = true;
    for (const auto& [x, y] :
         {std::pair{0, 0}, std::pair{side, 0}, std::pair{side, side}, std::pair{0, side}}) {
        path.nodes.push_back({Vec2um{Micrometers{offsetX + x}, Micrometers{y}},
                              geo::NodeType::Corner, std::nullopt, std::nullopt});
    }
    vector.paths.push_back({std::move(path), {}});
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vector.id;
    emb.name = "Contour";
    doc::RunningStitchParams params;
    params.stitch_length = Micrometers{stitchLength};
    emb.params = params;
    project.vector_objects.push_back(std::move(vector));
    project.embroidery_objects.push_back(std::move(emb));
}

// Projet opaque 40 x 30 : `bg` partout sauf le rectangle `fg`, réellement segmenté.
doc::Project opaqueSegmentedProject(std::array<std::uint8_t, 3> bg, std::array<std::uint8_t, 3> fg,
                                    int x0, int y0, int x1, int y1) {
    constexpr int kW = 40;
    constexpr int kH = 30;
    doc::Project project;
    project.original.width = kW;
    project.original.height = kH;
    project.original.source_had_alpha = false;
    project.original.rgba.resize(std::size_t{kW} * kH * 4);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            const auto& c = inside ? fg : bg;
            auto* px = project.original.rgba.data() + (std::size_t(y) * kW + std::size_t(x)) * 4;
            px[0] = c[0];
            px[1] = c[1];
            px[2] = c[2];
            px[3] = 255;
        }
    }
    auto seg =
        openstitch::segmentation::segment(project.original, {.max_colors = 2, .min_region_px = 1});
    project.segmentation = std::move(*seg);
    return project;
}

QString clickDialogRole(QDialog* dialog, QDialogButtonBox::ButtonRole role) {
    auto* box = dialog->findChild<QDialogButtonBox*>();
    if (box != nullptr) {
        for (auto* button : box->buttons()) {
            if (box->buttonRole(button) == role && button->isEnabled()) {
                button->click();
                return {};
            }
        }
    }
    return QStringLiteral("Bouton de dialogue absent ou désactivé");
}

using ModalStep = std::function<QString(QDialog*)>;

// Pilote des exec() imbriqués : chaque étape contrôle le dialogue reçu, une boîte inattendue ou un
// délai dépassé fait échouer le test sans jamais rester bloqué dans exec().
class ModalScript final : public QObject {
public:
    explicit ModalScript(std::vector<ModalStep> steps) : steps_(std::move(steps)) {
        elapsed_.start();
        timer_.setInterval(10);
        connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
        timer_.start();
    }

    bool completed() const { return error_.isEmpty() && next_ == steps_.size(); }
    QString diagnostic() const {
        return error_.isEmpty()
                   ? QStringLiteral("Dialogues traités : %1 / %2").arg(next_).arg(steps_.size())
                   : error_;
    }

private:
    void advance() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (elapsed_.elapsed() > 8'000 && error_.isEmpty()) {
            error_ = QStringLiteral("Délai modal dépassé : %1 / %2").arg(next_).arg(steps_.size());
        }
        if (dialog == nullptr) {
            return;
        }
        if (!error_.isEmpty()) {
            dialog->reject();
            return;
        }
        if (dialog == handled_) {
            return;
        }
        handled_ = dialog;
        if (next_ >= steps_.size()) {
            error_ =
                QStringLiteral("Dialogue supplémentaire inattendu : %1").arg(dialog->windowTitle());
        } else {
            error_ = steps_[next_++](dialog);
        }
        if (!error_.isEmpty()) {
            dialog->reject();
        }
    }

    std::vector<ModalStep> steps_;
    std::size_t next_{0};
    QString error_;
    QPointer<QDialog> handled_;
    QElapsedTimer elapsed_;
    QTimer timer_;
};

ModalStep fileChoice(const QString& path) {
    return [path](QDialog* dialog) -> QString {
        auto* file = qobject_cast<QFileDialog*>(dialog);
        if (file == nullptr) {
            return QStringLiteral("QFileDialog attendu, reçu : %1").arg(dialog->windowTitle());
        }
        file->setDirectory(QFileInfo(path).absolutePath());
        file->selectFile(QFileInfo(path).fileName());
        // selectFile ne touche pas au champ de nom quand il a le focus (fenêtre active) : le nom
        // dépendait du timing d'activation. On le fixe directement.
        if (auto* name = file->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
            name->setText(QFileInfo(path).fileName());
        }
        return clickDialogRole(file, QDialogButtonBox::AcceptRole);
    };
}

ModalStep machineSummary(const QString& format) {
    return [format](QDialog* dialog) -> QString {
        auto* summary = qobject_cast<MachineExportDialog*>(dialog);
        auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("combo_exportFormat"));
        if (summary == nullptr || combo == nullptr || combo->findData(format) < 0) {
            return QStringLiteral("Résumé machine ou format demandé absent");
        }
        combo->setCurrentIndex(combo->findData(format));
        return clickDialogRole(summary, QDialogButtonBox::AcceptRole);
    };
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

MachineExportDialog::SummaryFn summaryWith(MachineExportDialog::Summary summary, int* calls) {
    return [summary, calls](const openstitch::formats::FormatInfo&,
                            const openstitch::formats::MachineExportOptions&,
                            const pio::ExportOptimizations&) {
        if (calls != nullptr) {
            ++*calls;
        }
        return summary;
    };
}

// Valeurs du dialogue de numérisation relues dans ses widgets (par nom d'objet).
DigitizeDialogValues readDigitizeValues(QDialog* dlg) {
    DigitizeDialogValues v;
    const auto dbl = [&](const char* name) {
        return dlg->findChild<QDoubleSpinBox*>(name)->value();
    };
    const auto um = [&](const char* name) {
        return static_cast<int>(std::lround(dbl(name) * 1000.0));
    };
    v.satinBands = dlg->findChild<QCheckBox*>("satinBandsCheck")->isChecked();
    v.maxThreads = dlg->findChild<QSpinBox*>("maxThreadsSpin")->value();
    v.strategy = dlg->findChild<QRadioButton*>("strategyContoursRadio")->isChecked()
                     ? DigitizeStrategy::Contours
                 : dlg->findChild<QRadioButton*>("strategyStippleRadio")->isChecked()
                     ? DigitizeStrategy::Stipple
                     : DigitizeStrategy::Shapes;
    v.shapeDetail = dlg->findChild<QSlider*>("shapeVectorizeDetailSlider")->value();
    v.fillOverlapUm = um("fillOverlapSpin");
    v.minFeatureWidthUm = um("minFeatureWidthSpin");
    v.minRegionAreaCmm2 = static_cast<int>(std::lround(dbl("minRegionAreaSpin") * 100.0));
    v.islandAreaCmm2 = static_cast<int>(std::lround(dbl("islandAreaSpin") * 100.0));
    v.islandMaxDeltaE = static_cast<int>(std::lround(dbl("islandMaxDeltaESpin")));
    v.curveToleranceUm = um("curveToleranceSpin");
    v.solidBlock = dlg->findChild<QCheckBox*>("solidBlockCheck")->isChecked();
    v.flowFill = dlg->findChild<QCheckBox*>("flowFillCheck")->isChecked();
    v.darkLines = dlg->findChild<QCheckBox*>("darkLinesCheck")->isChecked();
    v.darkLinesWidthUm = um("darkLinesWidthSpin");
    v.darkLinesLightness = static_cast<int>(std::lround(dbl("darkLinesLightnessSpin")));
    v.darkLinesMinLenUm = um("darkLinesMinLengthSpin");
    v.darkLinesObjectsOnly = dlg->findChild<QCheckBox*>("darkLinesObjectsOnlyCheck")->isChecked();
    v.ringsSatin = dlg->findChild<QCheckBox*>("ringsSatinCheck")->isChecked();
    v.contourDetail = dlg->findChild<QSlider*>("contourDetailSlider")->value();
    v.techniqueRunning = dlg->findChild<QRadioButton*>("contourTechniqueRunningRadio")->isChecked();
    v.stipplePoints = dlg->findChild<QSpinBox*>("stipplePointsSpin")->value();
    v.stippleSeed = dlg->findChild<QSpinBox*>("stippleSeedSpin")->value();
    v.stippleInvert = dlg->findChild<QCheckBox*>("stippleInvertCheck")->isChecked();
    return v;
}

// Tout ce que la numérisation mémorise : sert à comparer deux chemins d'obtention des mêmes
// options.
std::map<QString, QVariant> memorizedDigitizeOptions() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("ui/memory/autoDigitize"));
    std::map<QString, QVariant> all;
    for (const QString& key : settings.allKeys()) {
        all[key] = settings.value(key);
    }
    return all;
}

QDialog* activeDialog() {
    return qobject_cast<QDialog*>(QApplication::activeModalWidget());
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    // Ouvre le dialogue de numérisation, y applique `inspect`, puis l'annule.
    void withDigitizeDialog(MainWindow& window, const std::function<void(QDialog*)>& inspect) {
        QTimer::singleShot(0, &window, [&] {
            if (auto* dlg = activeDialog()) {
                inspect(dlg);
                dlg->reject();
            }
        });
        window.autoDigitize();
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("WorkflowUX-") +
                                             QFileInfo(settingsDir_.path()).fileName());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        FontCatalog::instance().setSystemFontDirsForTesting({});
        QVERIFY(QSettings().fileName().startsWith(settingsDir_.path()));
    }

    void init() { QSettings().clear(); }

    // HP-UX-039 : le workflow décrit la source réelle (raster, SVG, fichier machine),
    // sans faire croire qu'une segmentation est nécessaire pour un SVG ou un DST.
    void workflowMarksInapplicableStagesAsSkipped() {
        using S = WorkflowPanel::State;
        MainWindow window;
        QCOMPARE(window.workflowPanel_->currentState(0), S::NotStarted);

        doc::Project raster;
        raster.original.width = 2;
        raster.original.height = 2;
        raster.original.rgba.assign(16, 255);
        window.applyLoadedProject(std::move(raster));
        QCOMPARE(window.workflowPanel_->currentState(0), S::Done);
        QCOMPARE(window.workflowPanel_->currentState(1), S::Available);
        QCOMPARE(window.workflowPanel_->currentState(2), S::NotStarted);

        doc::Project svg;
        addSquare(svg, 0, 1'000, 10'000);
        svg.embroidery_objects.clear(); // SVG : formes sans couture, pas de raster
        window.applyLoadedProject(std::move(svg));
        QCOMPARE(window.workflowPanel_->currentState(0), S::Done);
        QCOMPARE(window.workflowPanel_->currentState(1), S::Skipped);
        QCOMPARE(window.workflowPanel_->currentState(2), S::Done);
        QCOMPARE(window.workflowPanel_->currentState(3), S::Available);
        window.onWorkflowStepClicked(1);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("non requise")));

        doc::Project machine;
        doc::ImportedDesign design;
        design.source_format = "dst";
        design.sequence.commands.push_back({.pos = Vec2um{Micrometers{0}, Micrometers{0}},
                                            .type = openstitch::stitch::CommandType::Stitch});
        machine.imported_design = std::move(design);
        window.applyLoadedProject(std::move(machine));
        QCOMPARE(window.workflowPanel_->currentState(0), S::Done);
        QCOMPARE(window.workflowPanel_->currentState(1), S::Skipped);
        QCOMPARE(window.workflowPanel_->currentState(2), S::Skipped);
        QCOMPARE(window.workflowPanel_->currentState(3), S::Done);
        QCOMPARE(window.workflowPanel_->currentState(4), S::Available);
        QCOMPARE(window.workflowPanel_->currentState(5), S::Available);
    }

    void workflowSourceActionAndLabelsAreContextual() {
        MainWindow window;
        auto buttons = window.workflowPanel_->findChildren<QToolButton*>();
        QCOMPARE(buttons.size(), WorkflowPanel::kStepCount);
        QCOMPARE(buttons[0]->text(), QStringLiteral("Source"));
        QCOMPARE(window.openAnyAct_->shortcut(), QKeySequence::Open);

        doc::Project svg;
        addSquare(svg, 0, 1'000, 10'000);
        svg.embroidery_objects.clear();
        window.applyLoadedProject(std::move(svg));
        QCOMPARE(buttons[1]->toolTip(),
                 QStringLiteral("Cette étape n’est pas nécessaire pour cette source."));
        QVERIFY(buttons[2]->toolTip().isEmpty());
    }

    // ---- HP-UX-033 : cadre dans l'import et en barre d'état ---------------------------------

    void importDialogOffersTheHoopPresets() {
        const QImage preview(4, 4, QImage::Format_RGB32);
        ImportDialog dialog(3000, 2000, preview, QSizeF(100.0, 100.0));
        auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("importHoopCombo"));
        QVERIFY(combo != nullptr);
        QVERIFY(!dialog.hoopChanged());
        QCOMPARE(dialog.hoopMm(), QSizeF(100.0, 100.0));

        int standard = -1;
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toSizeF() == QSizeF(130.0, 180.0)) {
                standard = i;
            }
        }
        QVERIFY(standard > 0);
        combo->setCurrentIndex(standard);
        QVERIFY(dialog.hoopChanged());
        QCOMPARE(dialog.hoopMm(), QSizeF(130.0, 180.0));
        // Revenir au cadre du projet : plus de changement à appliquer.
        combo->setCurrentIndex(0);
        QVERIFY(!dialog.hoopChanged());
        QCOMPARE(dialog.hoopMm(), QSizeF(100.0, 100.0));
    }

    void hoopChosenInTheImportBecomesTheProjectHoopAndCancelKeepsTheOldOne() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString png = dir.filePath(QStringLiteral("image.png"));
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(png));

        const auto pickHoop = [](QDialogButtonBox::ButtonRole role) {
            return [role](QDialog* dialog) -> QString {
                auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("importHoopCombo"));
                if (qobject_cast<ImportDialog*>(dialog) == nullptr || combo == nullptr) {
                    return QStringLiteral("ImportDialog attendu");
                }
                int index = -1;
                for (int i = 1; i < combo->count(); ++i) {
                    if (combo->itemData(i).toSizeF() == QSizeF(130.0, 180.0)) {
                        index = i;
                    }
                }
                if (index < 0) {
                    return QStringLiteral("Modèle 130 x 180 absent");
                }
                combo->setCurrentIndex(index);
                return clickDialogRole(dialog, role);
            };
        };

        {
            MainWindow window;
            const auto before = window.project_.canvas;
            ModalScript script({pickHoop(QDialogButtonBox::RejectRole)});
            window.openImageFile(png);
            QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
            QVERIFY(window.project_.canvas == before); // Annuler restaure l'ancien cadre
            QVERIFY(!window.project_.hasImage());
        }
        {
            MainWindow window;
            ModalScript script({pickHoop(QDialogButtonBox::AcceptRole)});
            window.openImageFile(png);
            QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
            QVERIFY(window.project_.hasImage());
            QCOMPARE(window.project_.canvas.width.value, 130'000);
            QCOMPARE(window.project_.canvas.height.value, 180'000);
            QVERIFY(window.hoopStatusButton_->text().contains(QStringLiteral("130")));
            QVERIFY(window.hoopStatusButton_->text().contains(QStringLiteral("180")));
        }
    }

    void hoopSizeInTheStatusBarIsClickableAndFollowsTheCanvas() {
        MainWindow window;
        auto* button = window.hoopStatusButton_;
        QVERIFY(button != nullptr);
        QVERIFY2(button->text().contains(QStringLiteral("100")), qPrintable(button->text()));
        QVERIFY(!button->toolTip().isEmpty());

        window.project_.canvas.width = Micrometers{160'000};
        window.project_.canvas.height = Micrometers{260'000};
        window.applyCanvasToView();
        QVERIFY2(button->text().contains(QStringLiteral("160")), qPrintable(button->text()));
        QVERIFY2(button->text().contains(QStringLiteral("260")), qPrintable(button->text()));

        ModalScript script({[](QDialog* dialog) -> QString {
            if (dialog->windowTitle() != QStringLiteral("Cadre de broderie")) {
                return QStringLiteral("Dialogue du cadre attendu, reçu : %1")
                    .arg(dialog->windowTitle());
            }
            return clickDialogRole(dialog, QDialogButtonBox::RejectRole);
        }});
        button->click();
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
    }

    // ---- HP-UX-032 : décision explicite avant export ------------------------------------------

    void defaultButtonIsTheFileChoiceOnlyWhenNothingIsAtRisk() {
        MachineExportDialog clean(summaryWith({}, nullptr), QStringLiteral("dst"), nullptr);
        auto* choose = clean.findChild<QPushButton*>(QStringLiteral("action_exportChoose"));
        auto* problems = clean.findChild<QPushButton*>(QStringLiteral("action_exportViewProblems"));
        QVERIFY(choose != nullptr && problems != nullptr);
        QVERIFY(choose->isDefault());
        QVERIFY(problems->isHidden());

        MachineExportDialog::Summary warned;
        warned.has_warnings = true;
        warned.problems = QStringLiteral("Avertissement : le motif dépasse le cadre.");
        MachineExportDialog risky(summaryWith(warned, nullptr), QStringLiteral("dst"), nullptr);
        auto* riskyChoose = risky.findChild<QPushButton*>(QStringLiteral("action_exportChoose"));
        auto* riskyProblems =
            risky.findChild<QPushButton*>(QStringLiteral("action_exportViewProblems"));
        QVERIFY(!riskyChoose->isDefault()); // Entrée n'exporte pas à la légère
        QVERIFY(riskyProblems->isDefault());
        QVERIFY(!riskyProblems->isHidden());
        QVERIFY(riskyChoose->isEnabled()); // l'export reste possible, il est seulement moins direct

        MachineExportDialog::Summary blocked;
        blocked.blocking = true;
        MachineExportDialog refused(summaryWith(blocked, nullptr), QStringLiteral("dst"), nullptr);
        auto* refusedChoose =
            refused.findChild<QPushButton*>(QStringLiteral("action_exportChoose"));
        QVERIFY(!refusedChoose->isDefault());
        QVERIFY(!refusedChoose->isEnabled());
        refused.accept(); // un résumé bloquant ne se valide pas
        QVERIFY(refused.result() != QDialog::Accepted);
    }

    void problemsAreShownWithoutClosingTheDialog() {
        MachineExportDialog::Summary warned;
        warned.has_warnings = true;
        warned.problems = QStringLiteral("Avertissement : le motif dépasse le cadre.");
        MachineExportDialog dialog(summaryWith(warned, nullptr), QStringLiteral("dst"), nullptr);
        dialog.show();
        auto* view = dialog.findChild<QPlainTextEdit*>(QStringLiteral("text_exportProblems"));
        auto* button = dialog.findChild<QPushButton*>(QStringLiteral("action_exportViewProblems"));
        QVERIFY(view != nullptr && button != nullptr);
        QVERIFY(view->isHidden());
        button->click();
        QVERIFY(!view->isHidden());
        QVERIFY(view->toPlainText().contains(QStringLiteral("dépasse le cadre")));
        QVERIFY(dialog.isVisible()); // le dialogue reste ouvert
        QVERIFY(!dialog.viewProblemsRequested());
        button->click();
        QVERIFY(view->isHidden());

        // « Ouvrir l'analyse » conserve l'ancien comportement : fermer et ouvrir le panneau.
        auto* analysis =
            dialog.findChild<QPushButton*>(QStringLiteral("action_exportOpenAnalysis"));
        QVERIFY(analysis != nullptr && !analysis->isHidden());
        analysis->click();
        QVERIFY(dialog.viewProblemsRequested());
        QVERIFY(!dialog.isVisible());
    }

    void saveProjectFirstKeepsTheDialogOpen() {
        MachineExportDialog dialog(summaryWith({}, nullptr), QStringLiteral("dst"), nullptr);
        dialog.show();
        auto* save = dialog.findChild<QPushButton*>(QStringLiteral("action_exportSaveProject"));
        QVERIFY(save != nullptr && !save->isHidden());
        QSignalSpy spy(&dialog, &MachineExportDialog::saveProjectRequested);
        save->click();
        QCOMPARE(spy.count(), 1);
        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.result() != QDialog::Accepted);
    }

    void previewIsRecomputedOnceAfterABurstOfChanges() {
        int calls = 0;
        MachineExportDialog dialog(summaryWith({}, &calls), QStringLiteral("dst"), nullptr);
        QCOMPARE(calls, 1); // aperçu initial
        const QStringList names{"check_exportReduceTrims", "check_exportRoutesBetween",
                                "check_exportRoutesWithin", "check_exportRoutesUnderlay",
                                "check_exportRoutesRegions"};
        for (const QString& name : names) {
            auto* box = dialog.findChild<QCheckBox*>(name);
            QVERIFY(box != nullptr);
            box->toggle();
        }
        QCOMPARE(calls, 1); // 5 cases cochées d'affilée : rien n'est encore recalculé
        QTRY_COMPARE_WITH_TIMEOUT(calls, 2, 3000);
        QCoreApplication::processEvents();
        QCOMPARE(calls, 2); // et un seul calcul au total

        // Valider tout de suite ne laisse pas un aperçu périmé : le calcul en attente est fait.
        dialog.findChild<QCheckBox*>(QStringLiteral("check_exportRoutesUnderlay"))->toggle();
        QCOMPARE(calls, 2);
        dialog.flushPendingRefresh();
        QCOMPARE(calls, 3);
    }

    void quickExportIsAShortcutAndAFileNameWithIdenticalBytes_data() {
        QTest::addColumn<QString>("formatId");
        for (const char* id : {"dst", "pes", "jef", "exp"}) {
            QTest::newRow(id) << QString::fromLatin1(id);
        }
    }

    void quickExportIsAShortcutAndAFileNameWithIdenticalBytes() {
        QFETCH(QString, formatId);
        QTemporaryDir referenceDir;
        QTemporaryDir quickDir;
        QTemporaryDir dialogDir;
        QVERIFY(referenceDir.isValid() && quickDir.isValid() && dialogDir.isValid());
        MainWindow window;
        window.applyLoadedProject(contourProject());
        QVERIFY(window.sequence_ && window.exportDstQuickAct_->isEnabled());
        QCOMPARE(window.exportDstQuickAct_->shortcut(),
                 QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
        QSettings().setValue(QStringLiteral("export/format"), formatId);

        // Référence indépendante de l'interface : le moteur d'export avec les réglages mémorisés.
        const auto saved = MachineExportDialog::loadSavedExport();
        QCOMPARE(saved.formatId, formatId);
        const auto* format = openstitch::formats::find_format(formatId.toStdString());
        QVERIFY(format != nullptr);
        const QString ext = QString::fromStdString(format->extensions.front());
        auto options = saved.options;
        options.design_name = "broderie";
        auto companion = saved.companion;
        const QString reference = referenceDir.filePath(QStringLiteral("broderie.") + ext);
        const auto written =
            pio::export_machine_file(window.project_, formatId.toStdString(),
                                     std::filesystem::path(reference.toStdWString()), options,
                                     saved.optimizations, nullptr, &companion);
        QVERIFY2(written.has_value(), written ? "" : written.error().message.c_str());
        const QByteArray expected = readFile(reference);
        QVERIFY(!expected.isEmpty());

        // Export rapide : la seule fenêtre est le choix du nom de fichier.
        const QString quick = quickDir.filePath(QStringLiteral("broderie"));
        {
            ModalScript quickScript({fileChoice(quick)});
            window.exportDstQuickAct_->trigger();
            QVERIFY2(quickScript.completed(), qPrintable(quickScript.diagnostic()));
        }
        const QByteArray quickBytes = readFile(quick + QLatin1Char('.') + ext);
        QVERIFY(!quickBytes.isEmpty());
        QCOMPARE(QCryptographicHash::hash(quickBytes, QCryptographicHash::Sha256),
                 QCryptographicHash::hash(expected, QCryptographicHash::Sha256));
        QCOMPARE(quickBytes, expected);

        // Et le parcours par le dialogue complet écrit exactement les mêmes octets.
        const QString viaDialog = dialogDir.filePath(QStringLiteral("broderie"));
        {
            ModalScript dialogScript({machineSummary(formatId), fileChoice(viaDialog)});
            window.exportDstAct_->trigger();
            QVERIFY2(dialogScript.completed(), qPrintable(dialogScript.diagnostic()));
        }
        QCOMPARE(readFile(viaDialog + QLatin1Char('.') + ext), expected);
        // Ni l'un ni l'autre ne modifie le document.
        QVERIFY(window.undoStack_.undoNames().empty());
    }

    void exportOpensTheQualityDockBeforeTheSummary() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        window.applyLoadedProject(contourProject());
        window.show();
        QVERIFY(!window.analysisDock_->isVisible());
        ModalScript script({[](QDialog* dialog) -> QString {
            return qobject_cast<MachineExportDialog*>(dialog) != nullptr
                       ? clickDialogRole(dialog, QDialogButtonBox::RejectRole)
                       : QStringLiteral("Résumé d'export attendu");
        }});
        window.exportDstAct_->trigger();
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        QVERIFY(window.analysisDock_->isVisible());
        QVERIFY(window.analysisHasResult_);
    }

    void generationOptionsAndExportUseTheSameVocabulary() {
        MachineExportDialog dialog(summaryWith({}, nullptr), QStringLiteral("pes"), nullptr);
        auto* trims = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportTrims"));
        auto* reduce = dialog.findChild<QCheckBox*>(QStringLiteral("check_exportReduceTrims"));
        QVERIFY(trims != nullptr && reduce != nullptr);
        QVERIFY(trims->text().contains(QStringLiteral("ce fichier")));
        QVERIFY(reduce->text().contains(QStringLiteral("ce fichier")));
        QVERIFY(trims->toolTip().contains(QStringLiteral("projet")));
    }

    // ---- HP-UX-030 : préréglages de numérisation ------------------------------------------------

    void presetsOnlyFillExistingOptionsAndAreDistinct() {
        DigitizeDialogValues defaults;
        defaults.minFeatureWidthUm = 1200;
        defaults.minRegionAreaCmm2 = 300;
        const auto logo = digitizePresetValues(DigitizePreset::Logo, defaults);
        const auto line = digitizePresetValues(DigitizePreset::LineArt, defaults);
        const auto photo = digitizePresetValues(DigitizePreset::Photo, defaults);
        QCOMPARE(digitizePresetValues(DigitizePreset::Custom, defaults), defaults);
        QVERIFY(logo.strategy == DigitizeStrategy::Shapes && logo.satinBands);
        QVERIFY(line.strategy == DigitizeStrategy::Contours);
        QVERIFY(photo.strategy == DigitizeStrategy::Stipple);
        QVERIFY(!(logo == line) && !(line == photo) && !(logo == photo));
        // Les seuils du moteur sont ceux des défauts, jamais écrasés par un préréglage.
        for (const auto& v : {logo, line, photo}) {
            QCOMPARE(v.minFeatureWidthUm, 1200);
            QCOMPARE(v.minRegionAreaCmm2, 300);
        }
        QVERIFY(matchingDigitizePreset(logo, defaults) == DigitizePreset::Logo);
        QVERIFY(matchingDigitizePreset(line, defaults) == DigitizePreset::LineArt);
        QVERIFY(matchingDigitizePreset(photo, defaults) == DigitizePreset::Photo);
        QVERIFY(matchingDigitizePreset(defaults, defaults) == DigitizePreset::Custom);
        auto tweaked = logo;
        tweaked.shapeDetail += 1;
        QVERIFY(matchingDigitizePreset(tweaked, defaults) == DigitizePreset::Custom);
        // La palette n'appartient à aucun préréglage.
        auto threads = logo;
        threads.maxThreads = 8;
        QVERIFY(matchingDigitizePreset(threads, defaults) == DigitizePreset::Logo);
    }

    void choosingAPresetFillsTheSameFieldsAsTypingThemByHand_data() {
        QTest::addColumn<int>("preset");
        QTest::newRow("logo") << static_cast<int>(DigitizePreset::Logo);
        QTest::newRow("trait") << static_cast<int>(DigitizePreset::LineArt);
        QTest::newRow("photo") << static_cast<int>(DigitizePreset::Photo);
    }

    void choosingAPresetFillsTheSameFieldsAsTypingThemByHand() {
        QFETCH(int, preset);
        const auto kind = static_cast<DigitizePreset>(preset);
        DigitizeDialogValues initial;
        DigitizeDialogValues viaPreset;
        bool seen = false;
        {
            MainWindow window;
            window.applyLoadedProject(
                opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
            withDigitizeDialog(window, [&](QDialog* dlg) {
                auto* combo = dlg->findChild<QComboBox*>(QStringLiteral("autoDigitizePresetCombo"));
                if (combo == nullptr) {
                    return;
                }
                seen = true;
                initial = readDigitizeValues(dlg);
                combo->setCurrentIndex(combo->findData(preset));
                viaPreset = readDigitizeValues(dlg);
            });
        }
        QVERIFY(seen);
        QVERIFY(matchingDigitizePreset(initial, initial) == DigitizePreset::Custom);
        // Les champs du dialogue valent exactement les options du préréglage.
        QCOMPARE(viaPreset, digitizePresetValues(kind, initial));

        // Saisis à la main (champ par champ), les mêmes valeurs rendent le même préréglage.
        const DigitizeDialogValues wanted = digitizePresetValues(kind, initial);
        int comboData = -1;
        DigitizeDialogValues viaHand;
        {
            MainWindow window;
            window.applyLoadedProject(
                opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
            withDigitizeDialog(window, [&](QDialog* dlg) {
                dlg->findChild<QCheckBox*>("satinBandsCheck")->setChecked(wanted.satinBands);
                dlg->findChild<QSlider*>("shapeVectorizeDetailSlider")
                    ->setValue(wanted.shapeDetail);
                dlg->findChild<QDoubleSpinBox*>("fillOverlapSpin")
                    ->setValue(wanted.fillOverlapUm / 1000.0);
                dlg->findChild<QDoubleSpinBox*>("curveToleranceSpin")
                    ->setValue(wanted.curveToleranceUm / 1000.0);
                dlg->findChild<QSlider*>("contourDetailSlider")->setValue(wanted.contourDetail);
                dlg->findChild<QSpinBox*>("stipplePointsSpin")->setValue(wanted.stipplePoints);
                dlg->findChild<QRadioButton*>("strategyContoursRadio")
                    ->setChecked(wanted.strategy == DigitizeStrategy::Contours);
                dlg->findChild<QRadioButton*>("strategyStippleRadio")
                    ->setChecked(wanted.strategy == DigitizeStrategy::Stipple);
                comboData =
                    dlg->findChild<QComboBox*>("autoDigitizePresetCombo")->currentData().toInt();
                viaHand = readDigitizeValues(dlg);
            });
        }
        QCOMPARE(viaHand, wanted);
        QCOMPARE(comboData, preset); // l'affichage retrouve le préréglage correspondant
    }

    void aPresetAndTheEquivalentManualValuesMemorizeTheSameOptions() {
        const auto run = [this](const std::function<void(QDialog*)>& fill) {
            QSettings().clear();
            MainWindow window;
            window.applyLoadedProject(
                opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
            QTimer::singleShot(0, &window, [&] {
                if (auto* dlg = activeDialog()) {
                    fill(dlg);
                    dlg->accept();
                }
            });
            window.autoDigitize();
            return std::pair{memorizedDigitizeOptions(), window.project_.embroidery_objects.size()};
        };
        const auto viaPreset = run([](QDialog* dlg) {
            auto* combo = dlg->findChild<QComboBox*>(QStringLiteral("autoDigitizePresetCombo"));
            combo->setCurrentIndex(combo->findData(static_cast<int>(DigitizePreset::Logo)));
        });
        const auto viaHand = run([](QDialog* dlg) {
            dlg->findChild<QCheckBox*>("satinBandsCheck")->setChecked(true);
            dlg->findChild<QSlider*>("shapeVectorizeDetailSlider")->setValue(60);
            dlg->findChild<QDoubleSpinBox*>("fillOverlapSpin")->setValue(0.15);
            dlg->findChild<QDoubleSpinBox*>("curveToleranceSpin")->setValue(0.1);
        });
        QVERIFY(!viaPreset.first.empty());
        QVERIFY(viaPreset.first == viaHand.first);
        QCOMPARE(viaPreset.second, viaHand.second);
        QVERIFY(viaPreset.second > 0);
    }

    void advancedSectionIsCollapsedAndRemembered() {
        bool foldedAtFirst = false;
        bool openedByToggle = false;
        {
            MainWindow window;
            window.applyLoadedProject(
                opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
            QTimer::singleShot(0, &window, [&] {
                auto* dlg = activeDialog();
                if (dlg == nullptr) {
                    return;
                }
                auto* toggle = dlg->findChild<QToolButton*>("autoDigitizeAdvancedToggle");
                auto* box = dlg->findChild<QWidget*>("autoDigitizeAdvanced");
                if (toggle != nullptr && box != nullptr) {
                    foldedAtFirst = !toggle->isChecked() && box->isHidden(); // replié d'emblée
                    toggle->setChecked(true);
                    openedByToggle = !box->isHidden();
                }
                dlg->accept();
            });
            window.autoDigitize();
        }
        QVERIFY(foldedAtFirst);
        QVERIFY(openedByToggle);
        MainWindow reopened;
        reopened.applyLoadedProject(
            opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
        bool open = false;
        withDigitizeDialog(reopened, [&](QDialog* dlg) {
            open = dlg->findChild<QToolButton*>("autoDigitizeAdvancedToggle")->isChecked() &&
                   !dlg->findChild<QWidget*>("autoDigitizeAdvanced")->isHidden();
        });
        QVERIFY(open);
    }

    void addOrReplaceIsAskedInsideTheOptionsDialogNotInASecondWindow() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(
            opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
        // Segmentation neuve : une seule modale, sans question « Ajouter / Remplacer ».
        int modalsSeen = 0;
        bool hasAddOrReplace = true;
        QTimer::singleShot(0, &window, [&] {
            if (auto* dlg = activeDialog()) {
                ++modalsSeen;
                hasAddOrReplace = false;
                for (auto* b : dlg->findChildren<QAbstractButton*>()) {
                    hasAddOrReplace = hasAddOrReplace || b->text() == QStringLiteral("Remplacer");
                }
                dlg->accept();
            }
        });
        window.autoDigitize();
        QCOMPARE(modalsSeen, 1);
        QVERIFY(!hasAddOrReplace);
        const std::size_t first = window.project_.embroidery_objects.size();
        QVERIFY(first > 0);

        // Des objets existent : la question est dans la MÊME fenêtre (boutons Ajouter / Remplacer).
        int secondModals = 0;
        bool isMessageBox = true;
        bool hasBoth = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto* dlg = activeDialog()) {
                ++secondModals;
                isMessageBox = qobject_cast<QMessageBox*>(dlg) != nullptr;
                QAbstractButton* replace = nullptr;
                bool add = false;
                for (auto* b : dlg->findChildren<QAbstractButton*>()) {
                    add = add || b->text() == QStringLiteral("Ajouter");
                    if (b->text() == QStringLiteral("Remplacer")) {
                        replace = b;
                    }
                }
                hasBoth = add && replace != nullptr;
                if (replace != nullptr) {
                    replace->click();
                } else {
                    dlg->reject();
                }
            }
        });
        window.autoDigitize();
        QCOMPARE(secondModals, 1);
        QVERIFY(!isMessageBox);
        QVERIFY(hasBoth);
        QCOMPARE(window.project_.embroidery_objects.size(), first); // remplacé, pas doublé
    }

    // ---- HP-UX-030 / 031 : résumé chiffré et dock Analyse
    // ----------------------------------------

    void autoDigitizeEndsWithAFigureSummaryAndOpensTheQualityDock() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(
            opaqueSegmentedProject({250, 250, 250}, {200, 30, 30}, 10, 8, 30, 22));
        QVERIFY(!window.analysisDock_->isVisible());
        QTimer::singleShot(0, &window, [&] {
            if (auto* dlg = activeDialog()) {
                dlg->accept();
            }
        });
        window.autoDigitize();
        const QString message = window.statusBar()->currentMessage();
        QVERIFY2(message.contains(QStringLiteral("durée estimée")), qPrintable(message));
        QVERIFY2(message.contains(QStringLiteral("fil(s)")), qPrintable(message));
        QVERIFY2(message.contains(QStringLiteral("problème(s)")), qPrintable(message));
        QVERIFY2(message.contains(QStringLiteral("points")), qPrintable(message));
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("action_autoDigitizeSeeResult")) !=
                nullptr);
        QVERIFY(window.analysisDock_->isVisible()); // HP-UX-031 étape 1
        QVERIFY(window.analysisHasResult_);
    }

    // ---- HP-UX-031 : « Corriger tout ce qui est sûr »
    // -----------------------------------------------

    void fixAllAppliesTheSafeFixesInOneUndoStep() {
        doc::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(16, 255);
        addSquare(project, 100'000, 1'000, 10'000); // hors du cadre
        addSquare(project, 0, 9'000, 10'000);       // point trop long
        MainWindow window;
        window.applyLoadedProject(project);
        const doc::Project before = window.project_;
        window.runAnalysis();
        auto* fixAll =
            window.analysisDock_->findChild<QPushButton*>(QStringLiteral("analysisFixAllButton"));
        QVERIFY(fixAll != nullptr);
        QVERIFY(fixAll->isEnabled());
        const auto undoBefore = window.undoStack_.undoNames().size();
        fixAll->click();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore + 1); // une seule annulation
        QVERIFY(window.analysisIsStale_);
        QVERIFY(!fixAll->isEnabled());

        // Nouveau calcul : plus aucune correction sûre à proposer.
        window.runAnalysis();
        for (int i = 0; i < window.analysisList_->count(); ++i) {
            const int kind = window.analysisList_->item(i)->data(Qt::UserRole + 10).toInt();
            QVERIFY(kind != static_cast<int>(sa::FixKind::MoveIntoHoop));
            QVERIFY(kind != static_cast<int>(sa::FixKind::ClampStitchLength));
        }
        QVERIFY(!fixAll->isEnabled());

        // Un Ctrl+Z rend les deux objets tels qu'avant.
        window.undo();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QCOMPARE(window.project_.vector_objects.size(), before.vector_objects.size());
        for (std::size_t i = 0; i < before.vector_objects.size(); ++i) {
            const auto& nodes = window.project_.vector_objects[i].paths.front().outer.nodes;
            const auto& expected = before.vector_objects[i].paths.front().outer.nodes;
            QCOMPARE(nodes.size(), expected.size());
            for (std::size_t j = 0; j < nodes.size(); ++j) {
                QCOMPARE(nodes[j].pos.x.value, expected[j].pos.x.value);
                QCOMPARE(nodes[j].pos.y.value, expected[j].pos.y.value);
            }
        }
        const auto* running =
            std::get_if<doc::RunningStitchParams>(&window.project_.embroidery_objects[1].params);
        QVERIFY(running != nullptr);
        QCOMPARE(running->stitch_length.value, 9'000);
    }

    void reportsAreTabsOfTheAnalysisDock() {
        MainWindow window;
        auto* tabs =
            window.analysisDock_->findChild<QTabWidget*>(QStringLiteral("analysisReportTabs"));
        QVERIFY(tabs != nullptr);
        QCOMPARE(tabs->count(), 3);
        auto* run =
            window.analysisDock_->findChild<QPushButton*>(QStringLiteral("reportDiagnoseRun"));
        auto* text =
            window.analysisDock_->findChild<QPlainTextEdit*>(QStringLiteral("reportDiagnoseText"));
        auto* density =
            window.analysisDock_->findChild<QPushButton*>(QStringLiteral("reportDensityRun"));
        auto* densityText =
            window.analysisDock_->findChild<QPlainTextEdit*>(QStringLiteral("reportDensityText"));
        QVERIFY(run != nullptr && text != nullptr && density != nullptr && densityText != nullptr);

        // Sans objet de broderie : un message, jamais une fenêtre modale.
        run->click();
        QVERIFY(text->toPlainText().contains(QStringLiteral("aucun objet")));

        window.applyLoadedProject(contourProject());
        run->click();
        QVERIFY(!text->toPlainText().isEmpty());
        QVERIFY(!text->toPlainText().contains(QStringLiteral("aucun objet")));
        density->click();
        QVERIFY(!densityText->toPlainText().isEmpty());
    }

    void fixAllIsDisabledWhenNothingIsSafelyFixable() {
        MainWindow window;
        window.applyLoadedProject(contourProject());
        window.runAnalysis();
        auto* fixAll =
            window.analysisDock_->findChild<QPushButton*>(QStringLiteral("analysisFixAllButton"));
        QVERIFY(fixAll != nullptr);
        bool anyFix = false;
        for (int i = 0; i < window.analysisList_->count(); ++i) {
            const int kind = window.analysisList_->item(i)->data(Qt::UserRole + 10).toInt();
            anyFix = anyFix || kind == static_cast<int>(sa::FixKind::MoveIntoHoop) ||
                     kind == static_cast<int>(sa::FixKind::ClampStitchLength) ||
                     kind == static_cast<int>(sa::FixKind::SplitLongStitches) ||
                     kind == static_cast<int>(sa::FixKind::EnableFinishing);
        }
        QCOMPARE(fixAll->isEnabled(), anyFix);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_workflow_ux.moc"
