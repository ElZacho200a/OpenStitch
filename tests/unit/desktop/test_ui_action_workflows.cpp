// SPDX-License-Identifier: Apache-2.0
// Parcours d'actions avec effets documentaires, fichiers relus et couche points.
// MainWindowTest réutilise le friend existant ; les seams ne préparent que les fixtures.
// Les repros texte, cadre d'import et récupération attendent les correctifs du chef.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "autosave.hpp"
#include "canvas_view.hpp"
#include "font_catalog.hpp"
#include "import_dialog.hpp"
#include "machine_export_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "properties_panel.hpp"
#include "realistic_render_dialog.hpp"
#include "text_dialog.hpp"
#include "thread_panel.hpp"

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;
using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using namespace openstitch::desktop;

// Trois contours de 10 mm, rouge / bleu / rouge, tous dans le cadre par défaut.
// Longueurs entières en mm : les codecs à grille 0,1 mm ne perdent aucun point.
doc::Project contourProject(int count = 3) {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    project.finishing.enabled = false;
    for (int i = 0; i < count; ++i) {
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

int realisticPixmapCount(const QGraphicsScene* scene) {
    int count = 0;
    for (const auto* item : scene->items()) {
        if (item->zValue() == 20.0 &&
            qgraphicsitem_cast<const QGraphicsPixmapItem*>(item) != nullptr) {
            ++count;
        }
    }
    return count;
}

double sewnPathLength(const QGraphicsScene* scene) {
    double length = 0.0;
    for (const auto* item : scene->items()) {
        const auto* path = qgraphicsitem_cast<const QGraphicsPathItem*>(item);
        if (path != nullptr && item->zValue() == 20.0 && path->pen().style() == Qt::SolidLine) {
            length += path->path().length();
        }
    }
    return length;
}

using ModalStep = std::function<QString(QDialog*)>;

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

// Pilote des exec() imbriqués sans singleShot fragile ni acceptation aveugle.
// Chaque étape contrôle le type de dialogue. Une boîte inattendue ou un délai
// dépassé est rejeté et fait échouer le test, jamais rester bloqué dans exec().
// Aucun QVERIFY dans les callbacks : un early-return laisserait le modal ouvert.
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
        if (elapsed_.elapsed() > 5'000 && error_.isEmpty()) {
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

ModalStep fileChoice(const QString& path, bool accept = true) {
    return [path, accept](QDialog* dialog) -> QString {
        auto* file = qobject_cast<QFileDialog*>(dialog);
        if (file == nullptr) {
            return QStringLiteral("QFileDialog attendu, reçu : %1").arg(dialog->windowTitle());
        }
        if (!accept) {
            return clickDialogRole(file, QDialogButtonBox::RejectRole);
        }
        file->setDirectory(QFileInfo(path).absolutePath());
        file->selectFile(QFileInfo(path).fileName());
        return clickDialogRole(file, QDialogButtonBox::AcceptRole);
    };
}

ModalStep machineSummary(const QString& format, bool accept = true) {
    return [format, accept](QDialog* dialog) -> QString {
        auto* summary = qobject_cast<MachineExportDialog*>(dialog);
        auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("combo_exportFormat"));
        if (summary == nullptr || combo == nullptr || combo->findData(format) < 0) {
            return QStringLiteral("Résumé machine ou format demandé absent");
        }
        combo->setCurrentIndex(combo->findData(format));
        return clickDialogRole(summary, accept ? QDialogButtonBox::AcceptRole
                                               : QDialogButtonBox::RejectRole);
    };
}

ModalStep cancelUnsavedChanges() {
    return [](QDialog* dialog) -> QString {
        auto* box = qobject_cast<QMessageBox*>(dialog);
        auto* button = box != nullptr ? box->button(QMessageBox::Cancel) : nullptr;
        if (button == nullptr || box->button(QMessageBox::Save) == nullptr ||
            box->button(QMessageBox::Discard) == nullptr) {
            return QStringLiteral("Confirmation des modifications attendue");
        }
        button->click();
        return {};
    };
}

} // namespace

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        // Namespace unique : aucun ancien autosave / nuancier de test ne peut
        // ouvrir un modal non prévu, même après une exécution interrompue.
        QCoreApplication::setApplicationName(QStringLiteral("UIActionWorkflows-") +
                                             QFileInfo(settingsDir_.path()).fileName());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        FontCatalog::instance().setSystemFontDirsForTesting({});
        QVERIFY(QSettings().fileName().startsWith(settingsDir_.path()));
    }

    void init() { QSettings().clear(); }

    void realisticDialogDisableRemovesPixmapAndRestoresLines() {
        MainWindow window;
        window.resize(900, 700);
        window.show();
        window.applyLoadedProject(contourProject(1));
        QCoreApplication::processEvents();
        window.view_->resetTransform();
        window.view_->scale(12.0, 12.0);
        window.view_->centerOn(5.0, -5.0);
        auto* activate = window.findChild<QAction*>(QStringLiteral("action_realisticView"));
        auto* settings = window.findChild<QAction*>(QStringLiteral("action_realisticSettings"));
        QVERIFY(activate && settings);
        activate->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(realisticPixmapCount(window.scene_), 1, 2'000);
        QCOMPARE(sewnPathLength(window.scene_), 0.0);
        settings->trigger();
        auto* dialog = window.findChild<RealisticRenderDialog*>();
        QVERIFY(dialog);
        auto* enabled = dialog->findChild<QCheckBox*>(QStringLiteral("realisticEnabled"));
        QVERIFY(enabled && enabled->isChecked());
        QTRY_VERIFY_WITH_TIMEOUT(!window.realistic_.timer->isActive(), 2'000);
        QCOMPARE(realisticPixmapCount(window.scene_), 1);

        // Repro #6 : ne pas appeler renderStitches / refreshImage ici. Sur le
        // snapshot, le timer est lancé puis ignore la désactivation : pixmap=1.
        QTest::mouseClick(enabled, Qt::LeftButton, Qt::NoModifier,
                          QPoint(8, enabled->height() / 2));
        QVERIFY(!activate->isChecked());
        QVERIFY(!loadRealisticPreferences().enabled);
        QTRY_COMPARE_WITH_TIMEOUT(realisticPixmapCount(window.scene_), 0, 2'000);
        QVERIFY(sewnPathLength(window.scene_) > 0.0);

        // Le même dialogue réactive le rendu différé ; le menu le désactive.
        QTest::mouseClick(enabled, Qt::LeftButton, Qt::NoModifier,
                          QPoint(8, enabled->height() / 2));
        QTRY_COMPARE_WITH_TIMEOUT(realisticPixmapCount(window.scene_), 1, 2'000);
        activate->trigger();
        QCOMPARE(realisticPixmapCount(window.scene_), 0);
        QVERIFY(sewnPathLength(window.scene_) > 0.0);
    }

    void inspectorTextButtonEditsOrCancels_data() {
        QTest::addColumn<bool>("accept");
        QTest::newRow("accept-undo-redo") << true;
        QTest::newRow("cancel-keeps-document") << false;
    }

    void inspectorTextButtonEditsOrCancels() {
        QFETCH(bool, accept);
        MainWindow window;
        window.applyLoadedProject(contourProject(0));
        document::TextObject text;
        text.text = "A";
        text.font.builtin = "vera-sans-bold";
        text.fill = document::TextFill::Contour;
        QVERIFY(window.applyText(text, true)); // fixture seulement
        const auto before = window.project_.text_objects.front();
        const auto pointsBefore = window.sequence_->commands;
        const auto historyBefore = window.undoStack_.undoNames().size();
        auto* button = window.propertiesPanel_->findChild<QPushButton*>("text_edit_button");
        QVERIFY(button && button->isEnabled());
        ModalScript script({[accept](QDialog* dialog) -> QString {
            auto* textDialog = qobject_cast<TextDialog*>(dialog);
            auto* edit = dialog->findChild<QPlainTextEdit*>("text_edit");
            if (textDialog == nullptr || edit == nullptr) {
                return QStringLiteral("TextDialog attendu depuis le bouton inspecteur");
            }
            edit->setPlainText(QStringLiteral("AB"));
            return clickDialogRole(dialog, accept ? QDialogButtonBox::AcceptRole
                                                  : QDialogButtonBox::RejectRole);
        }});
        button->click();
        // Repro #2 : bouton -> signal existe, mais aucun dialogue ne s'ouvre.
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        if (!accept) {
            QVERIFY(window.project_.text_objects.front() == before);
            QVERIFY(window.sequence_->commands == pointsBefore);
            QCOMPARE(window.undoStack_.undoNames().size(), historyBefore);
            return;
        }
        QCOMPARE(window.project_.text_objects.front().id, before.id);
        QCOMPARE(window.project_.text_objects.front().text, std::string("AB"));
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{2});
        QCOMPARE(window.undoStack_.undoNames().size(), historyBefore + 1);
        QVERIFY(window.sequence_->commands != pointsBefore);
        window.undoAct_->trigger();
        QCOMPARE(window.project_.text_objects.front().text, std::string("A"));
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
        window.redoAct_->trigger();
        QCOMPARE(window.project_.text_objects.front().text, std::string("AB"));
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{2});
    }

    void imageImportFitKeepsCurrentHoop() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("image.png"));
        QImage image(40, 30, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        QVERIFY(image.save(path));
        auto project = contourProject();
        project.canvas.width = Micrometers{200'000};
        project.canvas.height = Micrometers{150'000};
        project.canvas.margin = Micrometers{3'000};
        MainWindow window;
        window.applyLoadedProject(project);
        ModalScript script({fileChoice(path), [](QDialog* dialog) -> QString {
                                auto* importDialog = qobject_cast<ImportDialog*>(dialog);
                                auto* fit = dialog->findChild<QPushButton*>("importFitButton");
                                auto* width = dialog->findChild<QDoubleSpinBox*>("importWidthSpin");
                                auto* height =
                                    dialog->findChild<QDoubleSpinBox*>("importHeightSpin");
                                if (importDialog == nullptr || fit == nullptr || width == nullptr ||
                                    height == nullptr) {
                                    return QStringLiteral("ImportDialog incomplet");
                                }
                                fit->click();
                                if (width->value() != 200.0 || height->value() != 150.0) {
                                    return QStringLiteral("Ajustement attendu à 200 × 150 mm");
                                }
                                return clickDialogRole(dialog, QDialogButtonBox::AcceptRole);
                            }});
        window.openImageAct_->trigger();
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        QCOMPARE(window.project_.original.width, 40);
        QCOMPARE(window.project_.original.height, 30);
        QVERIFY(window.project_.vector_objects.empty());
        QCOMPARE(window.project_.mm_per_px.value, 5.0);
        // Repro #4 : Project{} remet le cadre à 100 × 100 après validation.
        QCOMPARE(window.project_.canvas.width.value, 200'000);
        QCOMPARE(window.project_.canvas.height.value, 150'000);
        QCOMPARE(window.project_.canvas.margin.value, 3'000);
    }

    void imageImportHeightSetsTheActualProportionalSize() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("height.png"));
        QImage image(40, 20, QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(source));
        MainWindow window;
        ModalScript script({[](QDialog* modal) -> QString {
            if (qobject_cast<ImportDialog*>(modal) == nullptr) {
                return QStringLiteral("Dialogue d'import attendu");
            }
            auto* keep = modal->findChild<QCheckBox*>(QStringLiteral("importKeepRatioCheck"));
            auto* height = modal->findChild<QDoubleSpinBox*>(QStringLiteral("importHeightSpin"));
            if (!keep || !height || keep->isEnabled() || !keep->isChecked()) {
                return QStringLiteral("Import proportionnel obligatoire attendu");
            }
            height->setValue(30.0);
            qobject_cast<ImportDialog*>(modal)->accept();
            return {};
        }});
        window.openImageFile(source);
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        QCOMPARE(window.processed_.width * window.project_.mm_per_px.value, 60.0);
        QCOMPARE(window.processed_.height * window.project_.mm_per_px.value, 30.0);
    }

    void imageImportCancelKeepsDocumentAndHistory() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("image.png"));
        QImage image(40, 30, QImage::Format_RGBA8888);
        image.fill(Qt::blue);
        QVERIFY(image.save(path));
        MainWindow window;
        window.applyLoadedProject(contourProject());
        const auto before = window.sequence_->commands;
        ModalScript script({fileChoice(path), [](QDialog* dialog) -> QString {
                                if (qobject_cast<ImportDialog*>(dialog) == nullptr) {
                                    return QStringLiteral("ImportDialog attendu avant annulation");
                                }
                                return clickDialogRole(dialog, QDialogButtonBox::RejectRole);
                            }});
        window.openImageAct_->trigger();
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        QCOMPARE(window.project_.original.width, 2);
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{3});
        QVERIFY(window.sequence_->commands == before);
        QVERIFY(window.undoStack_.undoNames().empty());
        QVERIFY(!window.isWindowModified());
    }

    void recoveredDocumentUndoStillGuardsNewProject() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        QCoreApplication::processEvents(); // scan initial encore vide
        const auto slot = slotFor(dir.filePath(QStringLiteral("crashed.osp")));
        const auto cleanup = qScopeGuard([slot] { discardAutosave(slot); });
        QVERIFY(writeAutosave(slot, contourProject(), QString()).has_value());
        {
            ModalScript recover({[](QDialog* dialog) -> QString {
                auto* box = qobject_cast<QMessageBox*>(dialog);
                auto* button = dialog->findChild<QPushButton*>("action_autosaveRecover");
                if (box == nullptr || button == nullptr) {
                    return QStringLiteral("Dialogue de récupération attendu");
                }
                button->click();
                return {};
            }});
            window.checkAutosaveRecovery(); // seam de l'entrée différée au démarrage
            QVERIFY2(recover.completed(), qPrintable(recover.diagnostic()));
        }
        QVERIFY(window.isWindowModified());
        QVERIFY(QFile::exists(slot.osp_path));
        const auto pointsBefore = window.sequence_->commands;
        const ObjectId vectorId = window.project_.vector_objects.front().id;
        window.setSelection(
            {.region = std::nullopt, .embroidery = std::nullopt, .objects = {vectorId}});
        window.selectionChanged();
        QVERIFY(window.duplicateSelectionAct_->isEnabled());
        window.duplicateSelectionAct_->trigger();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{4});
        window.undoAct_->trigger();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{3});
        QVERIFY(window.sequence_->commands == pointsBefore);

        // Repro #1 : undo ramène la pile à son faux marqueur propre ; Nouveau
        // détruit alors le contenu sans poser la question que le script attend.
        ModalScript cancel({cancelUnsavedChanges()});
        window.newProjectAct_->trigger();
        QVERIFY2(cancel.completed(), qPrintable(cancel.diagnostic()));
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{3});
        QVERIFY(window.sequence_->commands == pointsBefore);
        QVERIFY(window.isWindowModified());
        QVERIFY(QFile::exists(slot.osp_path));
    }

    void machineExportWritesDecodableFile_data() {
        QTest::addColumn<QString>("formatId");
        for (const char* id : {"dst", "pes", "jef", "exp"}) {
            QTest::newRow(id) << QString::fromLatin1(id);
        }
    }

    void machineExportWritesDecodableFile() {
        QFETCH(QString, formatId);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        window.applyLoadedProject(contourProject());
        QVERIFY(window.sequence_ && window.exportDstAct_->isEnabled());
        const auto expected = stitch::compute_stats(*window.sequence_);
        const auto before = window.sequence_->commands;
        // Le chemin sans suffixe doit être complété par le vrai gestionnaire UI.
        const QString base = dir.filePath(QStringLiteral("broderie"));
        ModalScript script({machineSummary(formatId), fileChoice(base)});
        window.exportDstAct_->trigger();
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        const QString path = base + QLatin1Char('.') + formatId;
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
        const QByteArray bytes = file.readAll();
        QVERIFY(!bytes.isEmpty());
        const auto* format = formats::find_format(formatId.toStdString());
        QVERIFY(format && format->decode_ex);
        const auto decoded = format->decode_ex(
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                          static_cast<std::size_t>(bytes.size())));
        QVERIFY2(decoded.has_value(), decoded ? "" : decoded.error().message.c_str());
        const auto actual = stitch::compute_stats(decoded->sequence);
        QCOMPARE(actual.stitches, expected.stitches);
        QCOMPARE(actual.color_changes, std::size_t{2});
        QCOMPARE(actual.bounds.min.x.value, expected.bounds.min.x.value);
        QCOMPARE(actual.bounds.min.y.value, expected.bounds.min.y.value);
        QCOMPARE(actual.bounds.max.x.value, expected.bounds.max.x.value);
        QCOMPARE(actual.bounds.max.y.value, expected.bounds.max.y.value);
        if (format->carries_colors) {
            QCOMPARE(decoded->block_colors.size(), std::size_t{3});
            QVERIFY(decoded->block_colors[0] == decoded->block_colors[2]);
            QVERIFY(decoded->block_colors[0] != decoded->block_colors[1]);
        }
        QVERIFY(window.sequence_->commands == before);
        QVERIFY(window.undoStack_.undoNames().empty());
        QVERIFY(window.currentProjectPath_.isEmpty());
    }

    void machineExportCancellationWritesNothing_data() {
        QTest::addColumn<bool>("cancelSummary");
        QTest::newRow("cancel-summary") << true;
        QTest::newRow("cancel-file-choice") << false;
    }

    void machineExportCancellationWritesNothing() {
        QFETCH(bool, cancelSummary);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        window.applyLoadedProject(contourProject());
        QSettings().setValue(QStringLiteral("files/lastDir"), dir.path());
        const auto before = window.sequence_->commands;
        std::vector<ModalStep> steps{machineSummary(QStringLiteral("dst"), !cancelSummary)};
        if (!cancelSummary) {
            steps.push_back(fileChoice(dir.filePath(QStringLiteral("cancel.dst")), false));
        }
        ModalScript script(std::move(steps));
        window.exportDstAct_->trigger();
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
        QVERIFY(window.sequence_->commands == before);
        QVERIFY(!window.isWindowModified());
        QVERIFY(window.findChild<QPushButton*>("action_revealExport") == nullptr);
    }

    void dxfExportWritesDecodableGeometry() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        window.applyLoadedProject(contourProject());
        const auto before = window.sequence_->commands;
        const QString base = dir.filePath(QStringLiteral("dessin"));
        ModalScript script({fileChoice(base)});
        QVERIFY(window.exportDxfAct_->isEnabled());
        window.exportDxfAct_->trigger();
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        const auto decoded = formats::read_dxf_file(
            std::filesystem::path((base + QStringLiteral(".dxf")).toStdWString()));
        QVERIFY2(decoded.has_value(), decoded ? "" : decoded.error().message.c_str());
        QCOMPARE(decoded->size(), std::size_t{3});
        for (std::size_t i = 0; i < decoded->size(); ++i) {
            const auto& expected = window.project_.vector_objects[i].paths.front().outer;
            QVERIFY((*decoded)[i].closed);
            QCOMPARE((*decoded)[i].nodes.size(), expected.nodes.size());
            for (std::size_t j = 0; j < expected.nodes.size(); ++j) {
                QCOMPARE((*decoded)[i].nodes[j].pos, expected.nodes[j].pos);
            }
        }
        QVERIFY(window.sequence_->commands == before);
        QVERIFY(!window.isWindowModified());
    }

    void saveAsThenSaveWritesReadableCurrentProject() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        window.applyLoadedProject(contourProject());
        const QString base = dir.filePath(QStringLiteral("projet"));
        {
            ModalScript script({fileChoice(base)});
            window.saveProjectAsAct_->trigger();
            QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        }
        const QString path = base + QStringLiteral(".osp");
        QCOMPARE(window.currentProjectPath_, path);
        auto saved = project_io::load_project(std::filesystem::path(path.toStdWString()));
        QVERIFY(saved.has_value());
        QCOMPARE(saved->vector_objects.size(), std::size_t{3});
        QVERIFY(!window.isWindowModified());
        window.setSelection({.region = std::nullopt,
                             .embroidery = std::nullopt,
                             .objects = {window.project_.vector_objects.front().id}});
        window.selectionChanged();
        window.duplicateSelectionAct_->trigger();
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{4});
        {
            ModalScript noDialog({}); // protège aussi contre un sélecteur indésirable
            window.saveProjectAct_->trigger();
            QVERIFY2(noDialog.completed(), qPrintable(noDialog.diagnostic()));
        }
        saved = project_io::load_project(std::filesystem::path(path.toStdWString()));
        QVERIFY(saved.has_value());
        QCOMPARE(saved->vector_objects.size(), std::size_t{4});
        QVERIFY(!window.isWindowModified());
        window.undoAct_->trigger();
        QVERIFY(window.isWindowModified()); // le fichier contient encore la copie
        window.redoAct_->trigger();
        QVERIFY(!window.isWindowModified());
    }

    void simulationPlayPauseResumeAndSeekChangesRenderedPoints() {
        MainWindow window;
        window.resize(900, 700);
        window.show();
        window.applyLoadedProject(contourProject());
        QCoreApplication::processEvents();
        QVERIFY(window.simPlayAct_->isEnabled());
        const double fullLength = sewnPathLength(window.scene_);
        QVERIFY(fullLength > 0.0);
        window.simSpeedCombo_->setCurrentIndex(window.simSpeedCombo_->findData(0.25));
        window.simPlayAct_->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.simStep_ > 2, 2'000);
        QVERIFY(sewnPathLength(window.scene_) > 0.0);
        QVERIFY(sewnPathLength(window.scene_) < fullLength);
        QVERIFY(window.simMarker_ != nullptr);
        window.simPlayAct_->trigger();
        QVERIFY(!window.simTimer_->isActive());
        const int paused = window.simStep_;
        const double pausedLength = sewnPathLength(window.scene_);
        QElapsedTimer events;
        events.start();
        QTRY_VERIFY_WITH_TIMEOUT(events.elapsed() >= 80, 1'000);
        QCOMPARE(window.simStep_, paused);
        QCOMPARE(sewnPathLength(window.scene_), pausedLength);
        window.simSpeedCombo_->setCurrentIndex(window.simSpeedCombo_->findData(4.0));
        window.simPlayAct_->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.simStep_ >= paused + 4, 2'000);
        QVERIFY(sewnPathLength(window.scene_) > pausedLength);
        window.simPlayAct_->trigger();
        QVERIFY(!window.simTimer_->isActive());

        QTest::keyClick(window.simSlider_, Qt::Key_End);
        QCOMPARE(window.simStep_, window.simSlider_->maximum());
        QVERIFY(std::abs(sewnPathLength(window.scene_) - fullLength) < 0.001);
        QTest::keyClick(window.simSlider_, Qt::Key_Home);
        QCOMPARE(window.simStep_, 0);
        QCOMPARE(sewnPathLength(window.scene_), 0.0);
        // Le curseur montre la position du premier mouvement, même sans couture encore tracée.
        QVERIFY(window.simMarker_ != nullptr);
    }

    void threadChartComboAndReplaceButtonChangeAllMatchingObjects() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(contourProject());
        window.threadDock_->show();
        window.threadDock_->raise();
        QCoreApplication::processEvents(); // le contenu du dock est volontairement chargé à
                                           // l'affichage
        const auto red = window.project_.embroidery_objects[0].rgb;
        const auto blue = window.project_.embroidery_objects[1].rgb;
        thread_palette::ThreadChart chartA{"workflow_a", "Workflow A", "Fictif", {}};
        chartA.threads.push_back({{"workflow_a", "A1"}, "Test", "", "Vert", {10, 180, 60}});
        thread_palette::ThreadChart chartB{"workflow_b", "Workflow B", "Fictif", {}};
        chartB.threads.push_back({{"workflow_b", "B1"}, "Test", "", "Orange", {240, 100, 10}});
        QVERIFY(window.threadLibrary_.add_chart(chartA).has_value());
        QVERIFY(window.threadLibrary_.add_chart(chartB).has_value());
        window.threadPanel_->refreshCharts(); // données de fixture, pas l'action testée
        auto* combo = window.threadPanel_->findChild<QComboBox*>("threadChartCombo");
        auto* catalog = window.threadPanel_->findChild<QListWidget*>("threadCatalogList");
        auto* usage = window.threadPanel_->findChild<QListWidget*>("threadUsageList");
        auto* replace = window.threadPanel_->findChild<QPushButton*>("threadReplaceUsageBtn");
        QVERIFY(combo && catalog && usage && replace);
        for (const auto& chart : {chartA, chartB}) {
            const int index = combo->findData(QString::fromStdString(chart.chart_id));
            QVERIFY(index >= 0);
            combo->setCurrentIndex(index);
            QCOMPARE(catalog->count(), 1);
            catalog->setCurrentRow(0);
            usage->setCurrentRow(0);
            QVERIFY(replace->isEnabled());
            replace->click();
            const auto& target = chart.threads.front();
            for (const int row : {0, 2}) {
                const auto& embroidery = window.project_.embroidery_objects[row];
                QVERIFY(embroidery.thread && *embroidery.thread == target.key);
                QVERIFY(embroidery.rgb == target.rgb);
            }
            QVERIFY(window.project_.embroidery_objects[1].rgb == blue);
            QCOMPARE(usage->count(), 2);
            QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
            window.undoAct_->trigger();
            QVERIFY(window.project_.embroidery_objects[0].rgb == red);
            QVERIFY(window.project_.embroidery_objects[2].rgb == red);
            QVERIFY(!window.project_.embroidery_objects[0].thread);
            QVERIFY(!window.project_.embroidery_objects[2].thread);
        }
    }

    void filmButtonsSelectMoveMergeAndUndoRealBlocks() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(contourProject());
        window.threadDock_->show();
        window.threadDock_->raise();
        QCoreApplication::processEvents();
        const auto id1 = window.project_.embroidery_objects[0].id;
        const auto id2 = window.project_.embroidery_objects[1].id;
        const auto id3 = window.project_.embroidery_objects[2].id;
        const auto vector2 = window.project_.embroidery_objects[1].source_vector;
        auto* film = window.threadPanel_->findChild<QListWidget*>("threadFilmList");
        auto* select = window.threadPanel_->findChild<QPushButton*>("threadFilmSelectBtn");
        auto* down = window.threadPanel_->findChild<QPushButton*>("threadFilmDownBtn");
        auto* merge = window.threadPanel_->findChild<QPushButton*>("threadFilmMergeBtn");
        QVERIFY(film && select && down && merge);
        QCOMPARE(film->count(), 3);
        film->setCurrentRow(1);
        select->click();
        QCOMPARE(window.selectedObjectIds(), (std::vector<ObjectId>{vector2}));
        film->setCurrentRow(0);
        QVERIFY(down->isEnabled());
        down->click();
        QCOMPARE(window.project_.embroidery_objects[0].id, id2);
        QCOMPARE(window.project_.embroidery_objects[1].id, id1);
        QCOMPARE(window.project_.embroidery_objects[2].id, id3);
        QCOMPARE(stitch::compute_stats(*window.sequence_).color_changes, std::size_t{1});
        window.undoAct_->trigger();
        QCOMPARE(window.project_.embroidery_objects[0].id, id1);
        QCOMPARE(window.project_.embroidery_objects[1].id, id2);
        QCOMPARE(film->count(), 3);
        QVERIFY(merge->isEnabled());
        merge->click();
        QCOMPARE(window.project_.embroidery_objects[0].id, id1);
        QCOMPARE(window.project_.embroidery_objects[1].id, id3);
        QCOMPARE(window.project_.embroidery_objects[2].id, id2);
        QCOMPARE(film->count(), 2);
        QCOMPARE(stitch::compute_stats(*window.sequence_).color_changes, std::size_t{1});
        window.undoAct_->trigger();
        QCOMPARE(window.project_.embroidery_objects[0].id, id1);
        QCOMPARE(window.project_.embroidery_objects[1].id, id2);
        QCOMPARE(window.project_.embroidery_objects[2].id, id3);
        QCOMPARE(film->count(), 3);
    }

    void threadListExportWritesColorsObjectsAndPointCounts() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        window.applyLoadedProject(contourProject());
        const QString path = dir.filePath(QStringLiteral("fils.csv"));
        auto* action = window.findChild<QAction*>("action_exportThreadList");
        QVERIFY(action && action->isEnabled());
        ModalScript script({fileChoice(path)});
        action->trigger();
        QVERIFY2(script.completed(), qPrintable(script.diagnostic()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto lines = file.readAll().trimmed().split('\n');
        QCOMPARE(lines.size(), qsizetype{3});
        QCOMPARE(
            lines[0].trimmed(),
            QByteArray(
                "ordre;marque;nuancier;reference;nom;couleur;objets;points;longueur_mm;duree_s"));
        const auto red = lines[1].trimmed().split(';');
        const auto blue = lines[2].trimmed().split(';');
        QCOMPARE(red.size(), qsizetype{10});
        QCOMPARE(blue.size(), qsizetype{10});
        QCOMPARE(red[5].toUpper(), QByteArray("#DC141E"));
        QCOMPARE(blue[5].toUpper(), QByteArray("#143CD2"));
        QCOMPARE(red[6].toInt(), 2);
        QCOMPARE(blue[6].toInt(), 1);
        QVERIFY(red[7].toULongLong() > 0 && blue[7].toULongLong() > 0);
        QCOMPARE(red[7].toULongLong() + blue[7].toULongLong(),
                 static_cast<qulonglong>(stitch::compute_stats(*window.sequence_).stitches));
        QVERIFY(red[8].toDouble() > 0.0 && blue[8].toDouble() > 0.0);
        QVERIFY(window.undoStack_.undoNames().empty());
    }

    void analysisActionReportsOverflowAndFilterHidesOtherSeverities() {
        auto project = contourProject(1);
        // Fixture délibérément hors cadre, sans changer le moteur ni ses seuils.
        for (auto& node : project.vector_objects.front().paths.front().outer.nodes) {
            node.pos.x += Micrometers{100'000};
        }
        const ObjectId embroideryId = project.embroidery_objects.front().id;
        MainWindow window;
        window.show();
        window.applyLoadedProject(project);
        QVERIFY(window.analyzeAct_->isEnabled());
        window.analyzeAct_->trigger();
        QVERIFY(window.analysisHasResult_ && !window.analysisIsStale_);
        QVERIFY(window.analysisDock_->isVisible());
        QVERIFY(window.analysisList_->count() > 0);
        int errorRow = -1;
        for (int row = 0; row < window.analysisList_->count(); ++row) {
            const auto* item = window.analysisList_->item(row);
            if (item->data(Qt::UserRole + 1).toInt() == 2 &&
                item->data(Qt::UserRole + 2).toULongLong() == embroideryId.value) {
                errorRow = row;
                break;
            }
        }
        QVERIFY(errorRow >= 0);
        window.analysisFilter_->setCurrentIndex(window.analysisFilter_->findData(2));
        QVERIFY(!window.analysisList_->item(errorRow)->isHidden());
        window.analysisFilter_->setCurrentIndex(window.analysisFilter_->findData(0));
        QVERIFY(window.analysisList_->item(errorRow)->isHidden());
        QVERIFY(!window.analysisSummary_->text().isEmpty());
        QVERIFY(window.undoStack_.undoNames().empty());
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    openstitch::desktop::MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_ui_action_workflows.moc"
