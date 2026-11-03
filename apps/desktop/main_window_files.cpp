// SPDX-License-Identifier: Apache-2.0
// Fichiers et projet : nouveau, ouvrir, importer, taille du cercle, enregistrer, autosauvegarde, exports DST/DXF.
// Membres de MainWindow extraits de main_window.cpp (HP-QA-001) : déplacement pur.
#include "main_window.hpp"
#include "simulation_nav_bar.hpp"
#include "stitch_type_catalog.hpp"
#include "thread_strip.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsEllipseItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "ai_preferences.hpp"
#include "ai_preferences_dialog.hpp"
#include "ai_segmentation_dialog.hpp"
#include "app_language.hpp"
#include "app_settings.hpp"
#include "app_theme.hpp"
#include "autodigitize_presets.hpp"
#include "autosave.hpp"
#include "brightness_dialog.hpp"
#include "busy_indicator.hpp"
#include "canvas_view.hpp"
#include "command_palette_dialog.hpp"
#include "document_panel.hpp"
#include "empty_state_widget.hpp"
#include "general_preferences_dialog.hpp"
#include "generation_options_dialog.hpp"
#include "help_dialogs.hpp"
#include "help_manual.hpp"
#include "import_dialog.hpp"
#include "length_units.hpp"
#include "machine_export_dialog.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/sewing_order_commands.hpp"
#include "order_list_widget.hpp"
#include "responsive.hpp"
#include "vector_tooltip.hpp"

#include "calque_panel.hpp"
#include "claude_code_process_backend.hpp"
#include "companion_settings_dialog.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/autodigitize/line_layer.hpp"
#include "openstitch/autodigitize/vector_convert.hpp"
#include "openstitch/commands/calque_commands.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/finishing_commands.hpp"
#include "openstitch/commands/import_machine_design_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/hoop_library.hpp"
#include "openstitch/document/hoop_presets.hpp"
#include "openstitch/document/image_placement.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/export_limits.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/formats/svg_import.hpp"
#include "openstitch/geometry/cut.hpp"
#include "openstitch/geometry/offset.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/project_io/machine_file.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/machine_checks.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/satin_guides.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "properties_panel.hpp"
#include "recent_files.hpp"
#include "ruler.hpp"
#include "satin_guide_item.hpp"
#include "selection_hit_test.hpp"
#include "settings_dialog.hpp"
#include "ui_icons.hpp"
#include "ui_memory.hpp"
#include "ui_swatch.hpp"
#include "workflow_panel.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsPathItem>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "main_window_internal.hpp"

namespace openstitch::desktop {

namespace {

// Dernier cadre choisi, rappelé pour les nouveaux projets (un projet ouvert garde le sien).
document::Canvas rememberedCanvas() {
    document::Canvas c;
    c.width = Micrometers{
        ui_memory::intValue(QStringLiteral("hoop/widthUm"), c.width.value, 10'000, 500'000)};
    c.height = Micrometers{
        ui_memory::intValue(QStringLiteral("hoop/heightUm"), c.height.value, 10'000, 500'000)};
    c.margin = Micrometers{ui_memory::intValue(QStringLiteral("hoop/marginUm"), 0, 0, 100'000)};
    return c;
}

void rememberCanvas(const document::Canvas& c) {
    ui_memory::setIntValue(QStringLiteral("hoop/widthUm"), c.width.value);
    ui_memory::setIntValue(QStringLiteral("hoop/heightUm"), c.height.value);
    ui_memory::setIntValue(QStringLiteral("hoop/marginUm"), c.margin.value);
}

// Cadres enregistrés par l'utilisateur : « nom<TAB>largeur_mm<TAB>hauteur_mm ».
struct CustomHoop {
    QString name;
    double widthMm{0.0};
    double heightMm{0.0};
};

std::vector<CustomHoop> loadCustomHoops() {
    std::vector<CustomHoop> out;
    for (const QString& line : QSettings().value(QStringLiteral("hoops/custom")).toStringList()) {
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() != 3) {
            continue;
        }
        bool okW = false;
        bool okH = false;
        const double w = parts[1].toDouble(&okW);
        const double h = parts[2].toDouble(&okH);
        if (okW && okH && !parts[0].isEmpty() && w >= 10.0 && h >= 10.0 && w <= 500.0 &&
            h <= 500.0) {
            out.push_back({parts[0], w, h});
        }
    }
    return out;
}

void saveCustomHoops(const std::vector<CustomHoop>& hoops) {
    QStringList lines;
    for (const auto& h : hoops) {
        lines << QStringLiteral("%1\t%2\t%3").arg(h.name).arg(h.widthMm).arg(h.heightMm);
    }
    QSettings().setValue(QStringLiteral("hoops/custom"), lines);
}

} // namespace

void MainWindow::newProject() {
    if (!confirmDiscardChanges(
            tr("Le projet a été modifié. Enregistrer avant de créer un nouveau projet ?"))) {
        return;
    }
    // Un `Project` par défaut passe par le même chemin qu'un projet chargé :
    // aucune réinitialisation spécifique à maintenir en double ici.
    document::Project fresh;
    fresh.canvas = rememberedCanvas();
    applyLoadedProject(std::move(fresh));
    setTool(Tool::Select);
    statusBar()->showMessage(tr("Nouveau projet — ouvrez une image ou dessinez une forme."));
}

void MainWindow::openImage() {
    // Même point d'entrée pour une image ET un SVG (demande utilisateur,
    // 2026-09-11) : ce qui compte pour l'utilisateur est "ouvrir mon
    // dessin", pas la distinction interne image/vecteur -- branché sur
    // l'extension juste après le dialogue, cf. openSvg().
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Ouvrir une image ou un SVG"),
        QSettings().value(QStringLiteral("ui/lastImportDir")).toString(),
        tr("Images et SVG (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif *.webp *.svg);;"
           "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;"
           "SVG (*.svg);;Tous les fichiers (*)"));
    if (file.isEmpty()) {
        return;
    }
    QSettings().setValue(QStringLiteral("ui/lastImportDir"), QFileInfo(file).absolutePath());
    openImageFile(file);
}

void MainWindow::openAny() {
    const QString start = currentProjectPath_.isEmpty()
                              ? QSettings().value(QStringLiteral("ui/lastImportDir")).toString()
                              : QFileInfo(currentProjectPath_).absolutePath();
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Ouvrir"), start,
        tr("Tous les formats pris en charge (*.osp *.dst *.pes *.jef *.vp3 *.exp *.svg *.png *.jpg "
           "*.jpeg *.bmp *.tif *.tiff *.gif *.webp);;"
           "Projets OpenStitch (*.osp);;"
           "Fichiers de broderie (*.dst *.pes *.jef *.vp3 *.exp);;"
           "Images et SVG (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.gif *.webp *.svg);;"
           "Tous les fichiers (*)"));
    if (file.isEmpty()) {
        return;
    }
    // La garde « modifications non enregistrées » est celle de chaque chemin d'ouverture.
    openPath(file);
}

void MainWindow::openImageFile(const QString& file) {
    if (file.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
        openSvg(file);
        return;
    }

    auto loaded = image::load_image(std::filesystem::path(file.toStdWString()));
    if (!loaded) {
        showFriendlyError(this, tr("Image illisible"),
                          tr("Ce fichier n'a pas pu être ouvert comme image. Formats acceptés : "
                             "PNG, JPEG, BMP, TIFF, GIF, WebP."),
                          loaded.error().message);
        return;
    }

    const QImage preview(loaded->rgba.data(), loaded->width, loaded->height, loaded->width * 4,
                         QImage::Format_RGBA8888);
    const QSizeF hoop(to_millimeters(project_.canvas.width).value,
                      to_millimeters(project_.canvas.height).value);
    ImportDialog dialog(loaded->width, loaded->height, preview.copy(), hoop, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto placement = dialog.placement();
    if (!placement) {
        QMessageBox::warning(this, tr("Taille invalide"),
                             tr("La taille physique de l'image doit être strictement positive."));
        return;
    }
    // Garde « modifications non enregistrées » : placée après le choix de l'image et de sa
    // taille (rien n'est détruit tant que l'utilisateur peut encore annuler).
    if (!confirmDiscardChanges(
            tr("Le projet a été modifié. Enregistrer avant d'ouvrir cette image ?"))) {
        return;
    }

    auto canvas = project_.canvas; // même cadre que celui proposé dans ImportDialog
    if (dialog.hoopChanged()) {
        // HP-UX-033 : le cadre choisi dans l'import devient celui du projet (marge conservée).
        canvas.width = to_micrometers(Millimeters{dialog.hoopMm().width()});
        canvas.height = to_micrometers(Millimeters{dialog.hoopMm().height()});
        rememberCanvas(canvas);
    }
    project_ = document::Project{};
    project_.canvas = canvas;
    project_.mm_per_px = document::mm_per_pixel(*placement, loaded->width);
    project_.original = std::move(*loaded);
    resetDocumentState();

    refreshImage();
    view_->fitCanvas();
    statusBar()->showMessage(tr("%1 — %2 × %3 px")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(project_.original.width)
                                 .arg(project_.original.height));
    updateActions();
    setWindowModified(false); // nouveau document propre
}

void MainWindow::openSvg(const QString& file) {
    auto imported = formats::read_svg_file(std::filesystem::path(file.toStdWString()));
    if (!imported) {
        showFriendlyError(this, tr("SVG illisible"),
                          tr("Ce fichier n'a pas pu être importé comme dessin SVG."),
                          imported.error().message);
        return;
    }
    if (!confirmDiscardChanges(
            tr("Le projet a été modifié. Enregistrer avant d'ouvrir ce SVG ?"))) {
        return;
    }

    // Même réinitialisation "nouveau document" que openImage() ci-dessus --
    // seule la source diffère (objets vectoriels directement construits,
    // jamais d'image/segmentation/vectorisation à traverser).
    project_ = document::Project{};
    std::vector<document::VectorObject> objects;
    objects.reserve(imported->objects.size());
    for (auto& pathSet : imported->objects) {
        document::VectorObject object;
        object.id = project_.object_ids.next();
        object.name = tr("Import SVG %1").arg(objects.size() + 1).toStdString();
        object.rgb = {80, 120, 200};
        object.paths.push_back(std::move(pathSet));
        objects.push_back(std::move(object));
    }
    const std::size_t imported_count = objects.size();
    project_.vector_objects = std::move(objects);
    resetDocumentState();

    showVectorsAct_->setChecked(true);
    refreshImage();
    view_->fitCanvas();
    statusBar()->showMessage(tr("%1 — %2 objet(s) vectoriel(s) importé(s)")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(imported_count));
    updateActions();
    setWindowModified(false); // nouveau document propre
    warnAboutSkippedSvgFeatures(imported->warnings);
}

void MainWindow::convertVectorsToEmbroidery() {
    // HP-AUTO-012 : toute la classification (tatami / contour) vient de libs/autodigitize ;
    // ici seulement le déclenchement, le calcul hors du fil d'interface et la commande annulable.
    auto nextIds = project_.object_ids;
    autodigitize::AutoOptions opts;
    opts.mm_per_px = project_.mm_per_px;
    std::optional<BusyIndicator> busy;
    busy.emplace(this, tr("Conversion des formes en broderie…"),
                 std::initializer_list<QTimer*>{autosaveTimer_, simTimer_}, true);
    auto result = busy->run([&]() -> Result<autodigitize::VectorConversion> {
        return autodigitize::convert_vectors_to_embroidery(project_, nextIds, opts);
    });
    const bool cancelled = busy->cancelled();
    busy.reset();
    if (cancelled) {
        statusBar()->showMessage(tr("Conversion annulée : aucune modification."), 6000);
        return;
    }
    if (!result) {
        showFriendlyError(this, tr("Conversion impossible"),
                          tr("Aucune forme vectorielle sans broderie à convertir."),
                          result.error().message);
        return;
    }
    const std::size_t tatami = result->tatami;
    const std::size_t contour = result->contour;
    project_.object_ids = nextIds;
    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::vector<document::VectorObject>{}, std::move(result->embroideries),
                           "Convertir les vecteurs en broderie"),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 forme(s) converties en broderie : %2 tatami, %3 contour(s)")
                                 .arg(tatami + contour + result->other)
                                 .arg(tatami)
                                 .arg(contour),
                             8000);
}

void MainWindow::executeOp(image::ImageOp op) {
    if (!project_.hasImage()) {
        return;
    }
    // Validation avant mutation : une opération invalide n'entre pas dans la pile.
    auto preview = image::apply_op(processed_, op);
    if (!preview) {
        showFriendlyError(this, tr("Opération impossible"),
                          tr("Cette opération ne peut pas être appliquée à l'image."),
                          preview.error().message);
        return;
    }
    // Toute opération sur l'image invalide la segmentation (elle ne correspond
    // plus aux pixels) : on prévient au lieu de la perdre en silence.
    if (!confirmDestroySegmentation(tr("Cette opération sur l'image"))) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::AppendImageOpCommand>(std::move(op)), project_);
    refreshImage();
    updateActions();
}

bool MainWindow::confirmDestroySegmentation(const QString& action) {
    if (!project_.segmentation) {
        return true;
    }
    QMessageBox box(QMessageBox::Question, tr("Segmentation existante"),
                    tr("%1 supprime la segmentation actuelle : ses régions seront perdues. "
                       "Vous pourrez revenir en arrière avec Ctrl+Z.")
                        .arg(action),
                    QMessageBox::NoButton, this);
    auto* proceed = box.addButton(tr("Continuer"), QMessageBox::AcceptRole);
    auto* cancel = box.addButton(tr("Annuler"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    return box.clickedButton() == proceed;
}

void MainWindow::setHoopSize() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Cadre de broderie"));
    auto* layout = new QFormLayout(&dialog);
    layout->addRow(new QLabel(tr("Zone physique dans laquelle le motif doit tenir.\n"
                                 "L'analyse signale les points hors de la zone brodable."),
                              &dialog));
    const auto makeSpin = [&](double valueMm, double maxMm) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setRange(0.0, maxMm);
        spin->setDecimals(1);
        spin->setSingleStep(5.0);
        spin->setSuffix(tr(" mm"));
        spin->setValue(valueMm);
        return spin;
    };
    auto* presetCombo = new QComboBox(&dialog);
    presetCombo->setObjectName(QStringLiteral("hoopPresetCombo"));
    auto* w = makeSpin(to_millimeters(project_.canvas.width).value, 500.0);
    w->setMinimum(10.0);
    w->setObjectName(QStringLiteral("hoopWidthSpin"));
    auto* h = makeSpin(to_millimeters(project_.canvas.height).value, 500.0);
    h->setMinimum(10.0);
    h->setObjectName(QStringLiteral("hoopHeightSpin"));
    auto* margin = makeSpin(to_millimeters(project_.canvas.margin).value, 100.0);
    margin->setSingleStep(0.5);
    margin->setObjectName(QStringLiteral("hoopMarginSpin"));
    margin->setToolTip(tr("Bande de sécurité le long du cadre (pied presseur, collision avec le "
                          "cadre) : le motif ne doit pas y passer. Hachurée sur le canevas."));
    auto* usable = new QLabel(&dialog);
    usable->setObjectName(QStringLiteral("hoopUsableLabel"));
    // Forme du cadre (HP-HOOP-001) : rectangle, rond / ovale, coins arrondis.
    auto* shapeCombo = new QComboBox(&dialog);
    shapeCombo->setObjectName(QStringLiteral("hoopShapeCombo"));
    shapeCombo->addItem(tr("Rectangle"), static_cast<int>(document::HoopShape::Rectangle));
    shapeCombo->addItem(tr("Rond / ovale"), static_cast<int>(document::HoopShape::Ellipse));
    shapeCombo->addItem(tr("Coins arrondis"),
                        static_cast<int>(document::HoopShape::RoundedRectangle));
    shapeCombo->setCurrentIndex(
        std::max(0, shapeCombo->findData(static_cast<int>(project_.canvas.shape))));
    auto* radius = makeSpin(to_millimeters(project_.canvas.corner_radius).value, 100.0);
    radius->setObjectName(QStringLiteral("hoopCornerSpin"));
    radius->setSingleStep(1.0);
    radius->setToolTip(tr("Rayon des coins du cadre (forme « Coins arrondis »)."));
    const auto refreshShape = [&] {
        radius->setEnabled(shapeCombo->currentData().toInt() ==
                           static_cast<int>(document::HoopShape::RoundedRectangle));
    };
    refreshShape();

    // Entrées de la liste : « Personnalisé », modèles courants, puis cadres enregistrés.
    std::vector<CustomHoop> custom = loadCustomHoops();
    constexpr int kCustomRole = Qt::UserRole + 1;
    constexpr int kShapeRole = Qt::UserRole + 2;  // forme d'un cadre de la bibliothèque
    constexpr int kRadiusRole = Qt::UserRole + 3; // rayon des coins (mm)
    const auto fillPresets = [&] {
        presetCombo->clear();
        presetCombo->addItem(tr("Personnalisé"), QSizeF{});
        for (const auto& p : document::kHoopPresets) {
            presetCombo->addItem(
                QString::fromUtf8(p.name.data(), static_cast<qsizetype>(p.name.size())),
                QSizeF(static_cast<double>(p.width_mm), static_cast<double>(p.height_mm)));
        }
        // Cadres ronds, ovales et à coins arrondis de la bibliothèque générique (les
        // rectangles sont déjà ci-dessus).
        for (const auto& h : document::hoop_library()) {
            if (h.shape == document::HoopShape::Rectangle) {
                continue;
            }
            presetCombo->addItem(
                QString::fromUtf8(h.name.data(), static_cast<qsizetype>(h.name.size())),
                QSizeF(static_cast<double>(h.width_mm), static_cast<double>(h.height_mm)));
            presetCombo->setItemData(presetCombo->count() - 1, static_cast<int>(h.shape),
                                     kShapeRole);
            presetCombo->setItemData(presetCombo->count() - 1,
                                     static_cast<double>(h.corner_radius_mm), kRadiusRole);
        }
        for (const auto& c : custom) {
            presetCombo->addItem(tr("%1 (enregistré)").arg(c.name), QSizeF(c.widthMm, c.heightMm));
            presetCombo->setItemData(presetCombo->count() - 1, c.name, kCustomRole);
        }
    };
    fillPresets();

    const auto selectMatchingPreset = [&] {
        QSignalBlocker block(presetCombo);
        int found = 0;
        for (int i = 1; i < presetCombo->count(); ++i) {
            const QSizeF size = presetCombo->itemData(i).toSizeF();
            if (std::abs(size.width() - w->value()) < 0.05 &&
                std::abs(size.height() - h->value()) < 0.05 &&
                presetCombo->itemData(i, kShapeRole).toInt() == shapeCombo->currentData().toInt()) {
                found = i;
                break;
            }
        }
        presetCombo->setCurrentIndex(found);
    };
    const auto refreshUsable = [&] {
        const double uw = std::max(0.0, w->value() - 2.0 * margin->value());
        const double uh = std::max(0.0, h->value() - 2.0 * margin->value());
        usable->setText(tr("Zone brodable : %1 × %2 mm").arg(uw, 0, 'f', 1).arg(uh, 0, 'f', 1));
    };
    selectMatchingPreset();
    refreshUsable();

    connect(presetCombo, &QComboBox::activated, &dialog, [&](int index) {
        const QSizeF size = presetCombo->itemData(index).toSizeF();
        if (size.isEmpty()) {
            return; // « Personnalisé » : on garde les valeurs
        }
        w->setValue(size.width());
        h->setValue(size.height());
        shapeCombo->setCurrentIndex(
            std::max(0, shapeCombo->findData(presetCombo->itemData(index, kShapeRole).toInt())));
        if (presetCombo->itemData(index, kRadiusRole).isValid()) {
            radius->setValue(presetCombo->itemData(index, kRadiusRole).toDouble());
        }
        refreshShape();
        selectMatchingPreset();
    });
    connect(shapeCombo, &QComboBox::currentIndexChanged, &dialog, [&] {
        refreshShape();
        selectMatchingPreset();
    });
    for (auto* spin : {w, h, margin}) {
        connect(spin, &QDoubleSpinBox::valueChanged, &dialog, [&] {
            selectMatchingPreset();
            refreshUsable();
        });
    }

    auto* swapBtn = new QPushButton(tr("Pivoter"), &dialog);
    swapBtn->setToolTip(
        tr("Échange la largeur et la hauteur (cadre à l'horizontale ou à la verticale)."));
    connect(swapBtn, &QPushButton::clicked, &dialog, [&] {
        const double previousWidth = w->value();
        w->setValue(h->value());
        h->setValue(previousWidth);
    });
    auto* saveBtn = new QPushButton(tr("Enregistrer ce cadre…"), &dialog);
    saveBtn->setToolTip(tr("Garde cette taille dans la liste, pour les prochains projets."));
    auto* deleteBtn = new QPushButton(tr("Supprimer"), &dialog);
    deleteBtn->setToolTip(tr("Retire de la liste le cadre enregistré sélectionné."));
    const auto refreshDelete = [&] {
        deleteBtn->setEnabled(presetCombo->currentData(kCustomRole).isValid());
    };
    refreshDelete();
    connect(presetCombo, &QComboBox::currentIndexChanged, &dialog, refreshDelete);
    connect(saveBtn, &QPushButton::clicked, &dialog, [&] {
        bool ok = false;
        const QString name =
            QInputDialog::getText(&dialog, tr("Enregistrer le cadre"), tr("Nom du cadre :"),
                                  QLineEdit::Normal, QString(), &ok)
                .trimmed();
        if (!ok || name.isEmpty()) {
            return;
        }
        std::erase_if(custom, [&](const CustomHoop& c) { return c.name == name; });
        custom.push_back({name, w->value(), h->value()});
        saveCustomHoops(custom);
        fillPresets();
        selectMatchingPreset();
        refreshDelete();
    });
    connect(deleteBtn, &QPushButton::clicked, &dialog, [&] {
        const QString name = presetCombo->currentData(kCustomRole).toString();
        if (name.isEmpty()) {
            return;
        }
        std::erase_if(custom, [&](const CustomHoop& c) { return c.name == name; });
        saveCustomHoops(custom);
        fillPresets();
        selectMatchingPreset();
        refreshDelete();
    });

    layout->addRow(tr("Modèle :"), presetCombo);
    layout->addRow(tr("Largeur :"), w);
    layout->addRow(tr("Hauteur :"), h);
    layout->addRow(tr("Forme :"), shapeCombo);
    layout->addRow(tr("Rayon des coins :"), radius);
    auto* tools = new QHBoxLayout;
    tools->addWidget(swapBtn);
    tools->addWidget(saveBtn);
    tools->addWidget(deleteBtn);
    tools->addStretch(1);
    layout->addRow(tools);
    layout->addRow(tr("Marge de sécurité :"), margin);
    layout->addRow(usable);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    document::Canvas c;
    c.width = to_micrometers(Millimeters{w->value()});
    c.height = to_micrometers(Millimeters{h->value()});
    c.margin = to_micrometers(Millimeters{margin->value()});
    c.shape = static_cast<document::HoopShape>(shapeCombo->currentData().toInt());
    if (c.shape == document::HoopShape::RoundedRectangle) {
        c.corner_radius = to_micrometers(Millimeters{radius->value()});
    }
    rememberCanvas(c);
    if (c == project_.canvas) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::SetCanvasCommand>(c), project_);
    applyCanvasToView();
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Cadre : %1 × %2 mm, zone brodable %3 × %4 mm")
                                 .arg(w->value(), 0, 'f', 1)
                                 .arg(h->value(), 0, 'f', 1)
                                 .arg(to_millimeters(c.usable_width()).value, 0, 'f', 1)
                                 .arg(to_millimeters(c.usable_height()).value, 0, 'f', 1));
}

void MainWindow::saveProject() {
    // Document déjà rattaché à un fichier : Ctrl+S écrit dessus, sans dialogue
    // (le sélecteur systématique était le défaut corrigé par HP-FILE-002).
    if (currentProjectPath_.isEmpty()) {
        saveProjectAs();
        return;
    }
    (void)saveProjectToPath(currentProjectPath_);
}

void MainWindow::saveProjectAs() {
    // Un design importé (DST) est un contenu à part entière du document.
    if (!hasDocumentContent()) {
        QMessageBox::information(this, tr("Rien à enregistrer"),
                                 tr("Ouvrez une image et créez des objets d'abord."));
        return;
    }
    // Le dialogue repart du fichier courant quand il y en a un (dossier et nom
    // présélectionnés), sinon d'un nom suggéré dans le dernier dossier utilisé.
    const QString filter = tr("Projet OpenStitch (*.osp)");
    const QString start = currentProjectPath_.isEmpty() ? suggestedFilePath(QStringLiteral("osp"))
                                                        : currentProjectPath_;
    QString file =
        QFileDialog::getSaveFileName(this, tr("Enregistrer le projet sous"), start, filter);
    if (file.isEmpty()) {
        return;
    }
    // Certaines plateformes ne complètent pas l'extension du filtre choisi ;
    // sans elle, le fichier ne se retrouve plus par le filtre à l'ouverture.
    if (QFileInfo(file).suffix().isEmpty()) {
        file += QStringLiteral(".osp");
    }
    (void)saveProjectToPath(file);
}

QString MainWindow::suggestedFilePath(const QString& suffix) const {
    // Dossier : celui du projet courant, sinon le dernier dossier d'enregistrement
    // ou d'export, sinon Documents. Nom : celui du projet, sinon « sans-titre ».
    QString dir;
    QString base = tr("sans-titre");
    if (!currentProjectPath_.isEmpty()) {
        const QFileInfo info(currentProjectPath_);
        dir = info.absolutePath();
        base = info.completeBaseName();
    } else {
        QSettings settings;
        dir = settings.value(QStringLiteral("files/lastDir")).toString();
        if (dir.isEmpty()) { // HP-UX-007 : dossier de travail des préférences générales
            dir = settings.value(QStringLiteral("prefs/workingDirectory")).toString();
        }
    }
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    return QDir(dir).filePath(base + QLatin1Char('.') + suffix);
}

void MainWindow::rememberLastDirectory(const QString& file) {
    QSettings settings;
    settings.setValue(QStringLiteral("files/lastDir"), QFileInfo(file).absolutePath());
}

void MainWindow::offerRevealInFolder(const QString& file, const QString& message) {
    // Message d'état persistant + action « Ouvrir le dossier » (QPushButton
    // temporaire dans la barre d'état) : le fichier exporté se retrouve en un clic.
    statusBar()->showMessage(message);
    auto* button = new QPushButton(tr("Ouvrir le dossier"), this);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setObjectName(QStringLiteral("action_revealExport"));
    statusBar()->addWidget(button);
    const auto remove = [this, button] {
        statusBar()->removeWidget(button);
        button->deleteLater();
    };
    connect(button, &QPushButton::clicked, this, [file, remove] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
        remove();
    });
    QTimer::singleShot(20000, button, remove);
}

bool MainWindow::saveProjectToPath(const QString& file) {
    const std::filesystem::path path(file.toStdWString());
    // Premier enregistrement par-dessus un fichier migré depuis un ancien
    // format : copie de sécurité `.vN.osp.bak` (l'ancien format n'est plus
    // lisible par les versions précédentes une fois réécrit).
    if (migratedFromVersion_ > 0 &&
        QFileInfo(file).canonicalFilePath() == QFileInfo(migratedFromPath_).canonicalFilePath()) {
        const auto backup = project_io::backup_before_migrated_save(path, migratedFromVersion_);
        if (!backup) {
            showFriendlyError(this, tr("Copie de sécurité impossible"),
                              tr("L'ancien fichier n'a pas pu être sauvegardé avant migration. "
                                 "L'enregistrement est annulé pour ne pas l'écraser."),
                              backup.error().message);
            return false;
        }
    }
    // L'écriture atomique (temporaire + renommage) est assurée par
    // project_io::save_project : un échec en cours d'écriture laisse le
    // fichier précédent intact.
    const auto written = project_io::save_project(path, project_);
    if (!written) {
        showFriendlyError(this, tr("Enregistrement impossible"),
                          tr("Le projet n'a pas pu être enregistré dans « %1 ». Vérifiez "
                             "l'espace disque et que le fichier n'est pas ouvert ailleurs.")
                              .arg(QFileInfo(file).fileName()),
                          written.error().message);
        return false;
    }
    migratedFromVersion_ = 0;
    migratedFromPath_.clear();
    setCurrentProjectPath(file);
    rememberLastDirectory(file);
    undoStack_.markClean();
    savedAt_ = QTime::currentTime();
    // Les données sont désormais dans le fichier : la sauvegarde automatique
    // issue d'une récupération (conservée jusqu'ici) n'a plus de raison d'être.
    discardPendingRecoverySlot();
    statusBar()->showMessage(tr("Projet enregistré : %1").arg(QFileInfo(file).fileName()));
    setWindowModified(false);
    updateSavedIndicator(); // la sauvegarde peut partir d’un état déjà « propre »
    return true;
}

void MainWindow::loadProject() {
    // Même garde que « Nouveau projet » et les Récents : ouvrir un projet
    // remplace le document en cours.
    if (!confirmDiscardChanges(
            tr("Le projet a été modifié. Enregistrer avant d'ouvrir un autre projet ?"))) {
        return;
    }
    const QString start =
        currentProjectPath_.isEmpty() ? QString() : QFileInfo(currentProjectPath_).absolutePath();
    const QString file = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"), start,
                                                      tr("Projet OpenStitch (*.osp)"));
    if (file.isEmpty()) {
        return;
    }
    (void)openProjectFile(file);
}

bool MainWindow::openProjectFile(const QString& file) {
    project_io::LoadInfo info;
    auto loaded = project_io::load_project(std::filesystem::path(file.toStdWString()), &info);
    if (!loaded) {
        showFriendlyError(this, tr("Ouverture impossible"),
                          loaded.error().category == ErrorCategory::UnsupportedFormat
                              ? QString::fromStdString(loaded.error().message)
                              : tr("Le fichier « %1 » n'a pas pu être ouvert comme projet "
                                   "OpenStitch.")
                                    .arg(QFileInfo(file).fileName()),
                          loaded.error().message);
        // Purge immédiate (AD-S11-2) : si `file` provenait d'un item
        // Récents, l'échec prouve qu'il n'existe plus -- pruneMissingRecentFiles
        // (appliqué par refreshRecentFilesUi) le retire sans attendre une
        // prochaine reconstruction du menu/de l'écran d'accueil.
        refreshRecentFilesUi(/*prune=*/true);
        return false;
    }
    applyLoadedProject(std::move(*loaded));
    // Après applyLoadedProject : sa réinitialisation efface la cible
    // d'enregistrement (tout remplacement de document l'oublie), c'est
    // l'ouverture qui en installe une nouvelle.
    setCurrentProjectPath(file);
    rememberLastDirectory(file);
    QString message = tr("Projet ouvert : %1 — %2 objet(s) vectoriel(s), %3 objet(s) de "
                         "broderie")
                          .arg(QFileInfo(file).fileName())
                          .arg(project_.vector_objects.size())
                          .arg(project_.embroidery_objects.size());
    if (info.migrated) {
        migratedFromVersion_ = info.fileVersion;
        migratedFromPath_ = file;
        message += tr(" — converti depuis le format v%1 (copie .v%1.osp.bak créée à "
                      "l'enregistrement)")
                       .arg(info.fileVersion);
    }
    statusBar()->showMessage(message);
    return true;
}

void MainWindow::setCurrentProjectPath(const QString& file) {
    // HP-FILE-004 : le créneau autosave suit l'identité du document -- un
    // changement d'identité abandonne le créneau de l'ancienne (slotFor()
    // est pure, recalculée depuis currentProjectPath_ à chaque usage, donc
    // rien à recalculer pour la nouvelle avant qu'un tick n'en ait besoin).
    // Doit lire l'ANCIEN chemin avant qu'il soit écrasé ci-dessous.
    flushAutosave();
    discardAutosave(slotFor(currentProjectPath_));
    currentProjectPath_ = file;
    updateWindowTitle();
    if (!file.isEmpty()) {
        // Seul site d'ajout aux récents (HP-FILE-003) : couvre à la fois
        // saveProjectToPath et openProjectFile, qui appellent tous deux
        // cette fonction.
        recentFiles_ = addRecentFile(std::move(recentFiles_), file);
        saveRecentFiles(recentFiles_);
        refreshRecentFilesUi();
    }
    updateSavedIndicator(); // chargement/sauvegarde sans changement d’état « modifié »
}

void MainWindow::openRecentFile(const QString& path) {
    if (confirmDiscardChanges(
            tr("Le projet a été modifié. Enregistrer avant d'ouvrir un autre projet ?"))) {
        (void)openProjectFile(path);
    }
}

void MainWindow::refreshRecentFilesUi(bool prune) {
    // Synchrone : recentFiles_ doit être à jour dès le retour de cet appel,
    // pas seulement après un cycle d'évènements -- setCurrentProjectPath()
    // (et tout appelant futur) le relit et le persiste immédiatement.
    // La purge des fichiers disparus (QFileInfo::exists, bloquant sur un
    // lecteur réseau) n'a lieu qu'à l'ouverture du menu ou après un échec
    // d'ouverture, pas à chaque enregistrement.
    recentFiles_ = loadRecentFiles();
    if (prune) {
        recentFiles_ = pruneMissingRecentFiles(std::move(recentFiles_));
    }
    saveRecentFiles(recentFiles_);

    // Seule la reconstruction des widgets est différée (réentrance : ne pas
    // détruire, depuis son propre gestionnaire de clic, le QAction ou le
    // QPushButton qui vient de déclencher cet appel).
    QTimer::singleShot(0, this, [this] { rebuildRecentUi(); });
}

void MainWindow::rebuildRecentUi() {
    recentMenu_->clear();
    recentMenu_->setEnabled(!recentFiles_.isEmpty());
    recentMenu_->setToolTipsVisible(true);
    clearRecentAct_->setEnabled(!recentFiles_.isEmpty());
    int index = 1;
    for (const QString& path : recentFiles_) {
        // Mnémonique 1-9 (puis 0) : accès clavier sans souris ; le chemin
        // complet est en info-bulle (deux projets peuvent porter le même nom).
        QString name = QFileInfo(path).fileName();
        name.replace(QLatin1Char('&'), QStringLiteral("&&"));
        const QString label = index <= 9    ? QStringLiteral("&%1 %2").arg(index).arg(name)
                              : index == 10 ? QStringLiteral("1&0 %1").arg(name)
                                            : name;
        auto* action = recentMenu_->addAction(label);
        action->setToolTip(QDir::toNativeSeparators(path));
        action->setStatusTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] { openRecentFile(path); });
        ++index;
    }
    emptyState_->setRecentFiles(recentFiles_);
}

void MainWindow::onAutosaveTick() {
    // Même garde que updateEmptyState() : un document vide n'a rien à
    // protéger, et écrire n'apporterait qu'un fichier autosave inutile.
    if (!isWindowModified() || !hasDocumentContent()) {
        return;
    }
    if (autosaveThread_ != nullptr) {
        return; // l'ecriture precedente n'est pas finie : le prochain tick reprendra
    }
    const AutosaveSlot slot = slotFor(currentProjectPath_);
    // Revendique le créneau : une autre instance vivante ne le proposera pas à la
    // récupération comme s'il s'agissait d'un reste de plantage.
    claimAutosaveSlot(slot);
    // Instantané (copie) pris sur le fil d'interface ; l'écriture (sérialisation, zip, disque)
    // se fait dans un fil de travail, sans jamais lire `project_` (HP-PERF-032).
    auto snapshot = std::make_shared<const document::Project>(project_);
    auto error = std::make_shared<std::string>();
    const QString original = currentProjectPath_;
    autosaveError_ = error;
    autosaveOspPath_ = slot.osp_path;
    QThread* thread = QThread::create([slot, snapshot, original, error] {
        const auto written = writeAutosave(slot, *snapshot, original);
        if (!written) {
            *error = written.error().message.empty() ? std::string("erreur d'écriture")
                                                     : written.error().message;
        }
    });
    autosaveThread_ = thread;
    connect(thread, &QThread::finished, this, [this, thread] {
        if (thread == autosaveThread_) {
            finishAutosave();
        }
    });
    thread->start();
}

void MainWindow::flushAutosave() {
    if (autosaveThread_ == nullptr) {
        return;
    }
    QThread* thread = autosaveThread_;
    thread->wait();
    disconnect(thread, nullptr, this, nullptr);
    finishAutosave();
}

void MainWindow::finishAutosave() {
    QThread* thread = autosaveThread_;
    autosaveThread_ = nullptr;
    if (thread != nullptr) {
        thread->wait();
        thread->deleteLater();
    }
    const QString ospPath = autosaveOspPath_;
    const std::string error = autosaveError_ ? *autosaveError_ : std::string{};
    if (!error.empty()) {
        // Best-effort (jamais bloquant) : message transitoire, même gabarit
        // que saveProjectToPath pour un échec d'enregistrement normal.
        statusBar()->showMessage(
            tr("Sauvegarde automatique impossible : %1").arg(QString::fromStdString(error)));
        return;
    }
    // Première sauvegarde automatique réussie depuis une récupération : les données
    // récupérées sont désormais couvertes par le créneau courant, l'ancien peut partir.
    if (QDir::cleanPath(pendingRecoveryOsp_) != QDir::cleanPath(ospPath)) {
        discardPendingRecoverySlot();
    } else {
        pendingRecoveryOsp_.clear();
        pendingRecoverySidecar_.clear();
    }
    const QString when = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    statusBar()->showMessage(tr("Sauvegarde automatique à %1").arg(when), 4000);
}

void MainWindow::discardPendingRecoverySlot() {
    if (pendingRecoveryOsp_.isEmpty()) {
        return;
    }
    discardAutosave(AutosaveSlot{pendingRecoveryOsp_, pendingRecoverySidecar_});
    pendingRecoveryOsp_.clear();
    pendingRecoverySidecar_.clear();
}

void MainWindow::checkAutosaveRecovery() {
    // Différé après le premier passage de la boucle d'évènements qui suit la
    // construction : à ce point le document est normalement le défaut neuf. La
    // garde confirmDiscardChanges reste appliquée avant tout remplacement, au cas
    // où l'utilisateur aurait déjà commencé à travailler.
    //
    // Un seul candidat récupéré par session : une 2e récupération écraserait la
    // 1re ; les autres restent proposés au prochain démarrage. « Décider plus
    // tard » (et Échap / croix) ne supprime jamais rien : seule l'action
    // explicite « Supprimer » détruit une sauvegarde.
    const auto candidates = scanForRecoverableAutosaves();
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& candidate = candidates[i];
        const QString when =
            candidate.saved_at.isValid()
                ? QLocale().toString(candidate.saved_at.toLocalTime(), QLocale::ShortFormat)
                : tr("date inconnue");
        QMessageBox box(QMessageBox::Warning, tr("Récupération après un arrêt anormal"),
                        candidate.original_path.isEmpty()
                            ? tr("Un projet sans nom non enregistré a été retrouvé (%1).").arg(when)
                            : tr("Une sauvegarde automatique de « %1 » a été retrouvée (%2).")
                                  .arg(QFileInfo(candidate.original_path).fileName(), when),
                        QMessageBox::NoButton, this);
        if (candidates.size() > 1) {
            box.setInformativeText(
                tr("%1 sauvegarde(s) automatique(s) au total. Une seule est récupérée par "
                   "session ; les autres seront reproposées au prochain démarrage.")
                    .arg(candidates.size()));
        }
        auto* recoverBtn = box.addButton(tr("Récupérer"), QMessageBox::AcceptRole);
        recoverBtn->setObjectName(QStringLiteral("action_autosaveRecover"));
        auto* laterBtn = box.addButton(tr("Décider plus tard"), QMessageBox::RejectRole);
        laterBtn->setObjectName(QStringLiteral("action_autosaveLater"));
        auto* deleteBtn =
            box.addButton(tr("Supprimer la sauvegarde"), QMessageBox::DestructiveRole);
        deleteBtn->setObjectName(QStringLiteral("action_autosaveDiscard"));
        box.setDefaultButton(recoverBtn);
        box.setEscapeButton(laterBtn);
        box.exec();
        if (box.clickedButton() == deleteBtn) {
            discardAutosave(candidate.slot);
            continue; // décision explicite : on passe au candidat suivant
        }
        if (box.clickedButton() != recoverBtn) {
            return; // « plus tard », Échap ou croix : tout est conservé
        }
        if (!confirmDiscardChanges(
                tr("Le projet en cours a été modifié. Enregistrer avant de récupérer la "
                   "sauvegarde automatique ?"))) {
            return;
        }
        auto loaded =
            project_io::load_project(std::filesystem::path(candidate.slot.osp_path.toStdWString()));
        if (!loaded) {
            showFriendlyError(this, tr("Récupération impossible"),
                              tr("La sauvegarde automatique n'a pas pu être relue. Elle est "
                                 "conservée : vous pourrez réessayer au prochain démarrage."),
                              loaded.error().message);
            return;
        }
        applyLoadedProject(std::move(*loaded));
        undoStack_.markDirty(); // la récupération n'est pas un enregistrement du document
        // Le projet récupéré reprend le chemin d'origine quand il est connu : un
        // Ctrl+S réécrit alors le bon fichier (et pas un « Sans titre »). Le chemin est
        // posé sans passer par setCurrentProjectPath() : une récupération ne doit ni
        // alimenter les Récents ni supprimer le créneau qu'on vient de lire.
        if (!candidate.original_path.isEmpty()) {
            currentProjectPath_ = candidate.original_path;
            updateWindowTitle();
        }
        setWindowModified(true);
        // Le créneau n'est PAS supprimé ici : il reste le seul exemplaire des
        // données tant qu'un enregistrement ou une sauvegarde automatique n'a pas
        // réussi (onAutosaveTick / saveProjectToPath le libèrent).
        pendingRecoveryOsp_ = candidate.slot.osp_path;
        pendingRecoverySidecar_ = candidate.slot.sidecar_path;
        if (candidates.size() > i + 1) {
            statusBar()->showMessage(
                tr("Sauvegarde récupérée. %1 autre(s) seront proposées au prochain démarrage.")
                    .arg(candidates.size() - i - 1));
        } else {
            statusBar()->showMessage(
                tr("Sauvegarde automatique récupérée — enregistrez le projet pour la conserver."));
        }
        return;
    }
}

void MainWindow::updateWindowTitle() {
    // Le marqueur [*] est obligatoire pour que setWindowModified() ait un
    // effet : il est remplacé par l'indicateur de modification de la plateforme.
    QString document = tr("Sans titre");
    if (!currentProjectPath_.isEmpty()) {
        document = QFileInfo(currentProjectPath_).fileName();
    }
    setWindowTitle(QStringLiteral("%1[*] — %2").arg(document, QString::fromUtf8(kAppName)));
}

void MainWindow::applyLoadedProject(document::Project project) {
    project_ = std::move(project);
    resetDocumentState();
    refreshImage();
    view_->fitCanvas();
    updateActions();
    setWindowModified(false); // projet fraîchement chargé = propre
}

void MainWindow::exportDst() {
    flushPendingRefresh(); // HP-PERF-009 : jamais d'export d'une séquence périmée
    if (!sequence_) {
        return;
    }
    openQualityDock(); // HP-UX-031 : l'état de qualité est visible avant de décider d'exporter
    const auto stats = stitch::compute_stats(*sequence_);
    const double wMm = to_millimeters(stats.bounds.max.x - stats.bounds.min.x).value;
    const double hMm = to_millimeters(stats.bounds.max.y - stats.bounds.min.y).value;
    const auto& cv = project_.canvas;
    const bool overflow = stats.bounds.min.x.value < -cv.usable_width().value / 2 ||
                          stats.bounds.max.x.value > cv.usable_width().value / 2 ||
                          stats.bounds.min.y.value < -cv.usable_height().value / 2 ||
                          stats.bounds.max.y.value > cv.usable_height().value / 2;

    // HP-SEQ-006 : séquence réellement exportée quand l'utilisateur demande la réduction des
    // coupes. Le calcul n'est refait que si les réglages changent ; `project_io::export_sequence`
    // est le chemin même de l'écriture du fichier, donc le résumé décrit exactement ce qui sera
    // écrit.
    struct OptimizedCache {
        bool valid{false};
        project_io::ExportOptimizations key;
        stitch_generation::FinishResult value;
    };
    auto cache = std::make_shared<OptimizedCache>();
    const auto optimizedFor =
        [this, cache](const project_io::ExportOptimizations& opt) -> const OptimizedCache* {
        if (!opt.reduce_trims) {
            return nullptr;
        }
        if (!cache->valid || !(cache->key == opt)) {
            QGuiApplication::setOverrideCursor(Qt::WaitCursor);
            auto computed = project_io::export_sequence(project_, opt);
            QGuiApplication::restoreOverrideCursor();
            if (!computed) {
                cache->valid = false;
                return nullptr;
            }
            cache->key = opt;
            cache->value = std::move(*computed);
            cache->valid = true;
        }
        return cache.get();
    };

    // Résumé pré-export : décision réelle, recalculé pour le format et les options choisis
    // (HP-FMT-002..005 : limites propres au format = `formats::check_export_limits`).
    const auto summarize = [&](const formats::FormatInfo& format,
                               const formats::MachineExportOptions& options,
                               const project_io::ExportOptimizations& optimizations) {
        MachineExportDialog::Summary result;
        const auto* optimized = optimizedFor(optimizations);
        const stitch::StitchSequence& shown = optimized ? optimized->value.sequence : *sequence_;
        const auto shownStats = stitch::compute_stats(shown);
        QString summary = tr("Dimensions : %1 × %2 mm\nPoints : %3\nSauts : %4\nCoupes : %5\n"
                             "Changements de couleur : %6\nFil estimé : %7 m\nCadre : %8 × %9 mm")
                              .arg(wMm, 0, 'f', 1)
                              .arg(hMm, 0, 'f', 1)
                              .arg(shownStats.stitches)
                              .arg(shownStats.jumps)
                              .arg(shownStats.trims)
                              .arg(shownStats.color_changes)
                              .arg(shownStats.thread_length_um / 1e9, 0, 'f', 2)
                              .arg(to_millimeters(cv.width).value, 0, 'f', 0)
                              .arg(to_millimeters(cv.height).value, 0, 'f', 0);
        if (optimized != nullptr) {
            const auto& r = optimized->value.report;
            summary += tr("\nCoupes supprimées : %1 sur %2 écart(s) possible(s) (%3 saut(s) "
                          "recouvert(s), %4 pont(s) cousu(s), +%5 mm de fil).")
                           .arg(r.trims_removed)
                           .arg(r.candidate_gaps)
                           .arg(r.hidden_jumps)
                           .arg(r.bridges)
                           .arg(r.bridge_length_um / 1000.0, 0, 'f', 1);
            if (r.region_groups > 0) {
                summary +=
                    tr("\nRégions : %1 déplacée(s) dans %2 groupe(s), %3 mm de saut en moins.")
                        .arg(r.regions_moved)
                        .arg(r.region_groups)
                        .arg(r.region_jump_saved_um / 1000.0, 0, 'f', 0);
            }
            if (r.underlay_blocks > 0) {
                summary +=
                    tr("\nSous-couche : %1 groupe(s) de passes réordonné(s), %2 mm de saut en "
                       "moins.")
                        .arg(r.underlay_blocks)
                        .arg(r.underlay_jump_saved_um / 1000.0, 0, 'f', 0);
            }
        } else if (optimizations.reduce_trims) {
            summary += tr("\nL'optimisation des coupes n'a pas pu être calculée.");
        }
        // Résultat de l'analyse AVANT l'export : l'utilisateur voit les erreurs sans avoir à
        // penser à lancer « Analyser le motif ».
        const auto findings = stitch_analysis::analyze_project(project_, shown).findings;
        std::size_t errors = 0;
        std::size_t warnings = 0;
        QStringList problemLines;
        constexpr int kMaxProblemLines = 40;
        for (const auto& f : findings) {
            errors += f.severity == stitch_analysis::Severity::Error ? 1 : 0;
            warnings += f.severity == stitch_analysis::Severity::Warning ? 1 : 0;
            if (f.severity == stitch_analysis::Severity::Info ||
                problemLines.size() >= kMaxProblemLines) {
                continue;
            }
            const auto* emb = project_.findEmbroidery(f.object);
            const QString where = (emb != nullptr && !emb->name.empty())
                                      ? tr("« %1 » : ").arg(QString::fromStdString(emb->name))
                                      : QString();
            problemLines << (f.severity == stitch_analysis::Severity::Error
                                 ? tr("Erreur : ")
                                 : tr("Avertissement : ")) +
                                where + QString::fromStdString(f.message);
        }
        if (errors + warnings > static_cast<std::size_t>(problemLines.size())) {
            problemLines << tr("… et %1 de plus (panneau Analyse).")
                                .arg(errors + warnings -
                                     static_cast<std::size_t>(problemLines.size()));
        }
        result.has_warnings = warnings > 0;
        summary += errors + warnings == 0
                       ? tr("\n\nAnalyse : aucun problème détecté.")
                       : tr("\n\nAnalyse : %1 erreur(s), %2 avertissement(s) — voir Analyse ▸ "
                            "Analyser le motif.")
                             .arg(errors)
                             .arg(warnings);
        result.has_analysis_errors = errors > 0;
        if (overflow) {
            summary += tr("\n\nAttention : le motif dépasse le cadre.");
            result.has_warnings = true;
            problemLines << tr("Avertissement : le motif dépasse le cadre.");
        }
        // Limites du format cible (couleurs, étendue, cadres de la marque, déplacements découpés).
        QString limits;
        // Le saut d'approche vers le point de départ compte dans l'étendue et les déplacements.
        const auto exported = project_io::with_start_point(shown, options.start);
        for (const auto& issue : formats::check_export_limits(exported, format, options)) {
            const QString text = QString::fromStdString(issue.message);
            switch (issue.severity) {
            case formats::ExportIssueSeverity::Error:
                limits += tr("\nImpossible : %1").arg(text);
                result.blocking = true;
                problemLines << tr("Impossible : %1").arg(text);
                break;
            case formats::ExportIssueSeverity::Warning:
                limits += tr("\nAttention : %1").arg(text);
                result.has_warnings = true;
                problemLines << tr("Avertissement : %1").arg(text);
                break;
            case formats::ExportIssueSeverity::Info:
                limits += tr("\nÀ noter : %1").arg(text);
                break;
            }
        }
        // HP-HOOP-003 : format d'export non accepté par la machine du projet.
        for (const auto& f : stitch_analysis::check_machine(project_, shown, format.id)) {
            if (f.category != "machine-format") {
                continue; // les autres constats machine sont déjà dans l'analyse ci-dessus
            }
            const QString text = QString::fromStdString(f.message);
            limits += tr("\nAttention : %1").arg(text);
            result.has_warnings = true;
            problemLines << tr("Avertissement : %1").arg(text);
        }
        if (!limits.isEmpty()) {
            summary += QStringLiteral("\n") + limits;
        }
        summary += tr("\n\nLe format %1 ne conserve pas les objets éditables.")
                       .arg(QString::fromStdString(format.display_name));
        if (format.carries_colors) {
            summary += tr(" Les couleurs sont approchées par la palette du format.");
        }
        if (!project_.embroidery_objects.empty()) {
            summary += tr(" Pensez à enregistrer aussi le projet (.osp).");
        }
        // HP-FMT-022 : blocs de couleur dans l'ordre de couture de la séquence exportée.
        {
            const auto film = stitch_analysis::color_film(project_, shown);
            int rank = 0;
            for (const auto& block : film) {
                ++rank;
                const unsigned rgb = (static_cast<unsigned>(block.identity.rgb[0]) << 16) |
                                     (static_cast<unsigned>(block.identity.rgb[1]) << 8) |
                                     static_cast<unsigned>(block.identity.rgb[2]);
                result.blocks.push_back({rgb, tr("%1. #%2 — %3 point(s), %4 mm")
                                                  .arg(rank)
                                                  .arg(rgb, 6, 16, QLatin1Char('0'))
                                                  .arg(block.stitch_count)
                                                  .arg(block.length_mm, 0, 'f', 0)});
            }
        }
        result.text = summary;
        result.problems = problemLines.join(QLatin1Char('\n'));
        return result;
    };

    MachineExportDialog dialog(summarize, QString(), this);
    // « Enregistrer le projet d'abord » : sans fermer le dialogue d'export.
    connect(&dialog, &MachineExportDialog::saveProjectRequested, this, [this] { saveProject(); });
    dialog.exec();
    if (dialog.viewProblemsRequested()) {
        runAnalysis();
        return;
    }
    if (dialog.result() != QDialog::Accepted) {
        return;
    }
    MachineExportRequest request;
    request.format = dialog.format();
    request.options = dialog.options();
    request.optimizations = dialog.optimizations();
    request.companion = dialog.companion(); // HP-FMT-009 : fichier de couleurs voisin (DST, EXP)
    // Nombre de points du fichier réellement écrit : celui de la séquence optimisée si demandée.
    const auto* exportedSeq =
        cache->valid && cache->key == request.optimizations && request.optimizations.reduce_trims
            ? &cache->value.sequence
            : nullptr;
    request.writtenStitches =
        exportedSeq ? stitch::compute_stats(*exportedSeq).stitches : stats.stitches;
    runMachineExport(request);
}

bool MainWindow::runMachineExport(const MachineExportRequest& request) {
    const formats::FormatInfo* format = request.format;
    if (format == nullptr || format->extensions.empty()) {
        return false;
    }
    const QString ext = QString::fromStdString(format->extensions.front());
    QString file = request.file;
    if (file.isEmpty()) {
        file = QFileDialog::getSaveFileName(
            this, tr("Exporter en %1").arg(QString::fromStdString(format->display_name)),
            suggestedFilePath(ext),
            tr("%1 (*.%2)").arg(QString::fromStdString(format->display_name), ext));
        if (file.isEmpty()) {
            return false;
        }
    }
    // Extension forcée : sans elle la machine ne reconnaît pas le fichier.
    if (QFileInfo(file).suffix().compare(ext, Qt::CaseInsensitive) != 0) {
        file += QLatin1Char('.') + ext;
    }

    // AD-03 : composition projet -> fichier machine générique (AI-03b), partagée avec la CLI --
    // recalcule sa propre `effective_sequence`, garantie identique à `*sequence_`.
    auto options = request.options;
    options.design_name = QFileInfo(file).completeBaseName().toStdString();
    const auto& optimizations = request.optimizations;
    stitch_generation::TrimReport trimReport;
    auto companion = request.companion;
    const auto written = project_io::export_machine_file(
        project_, format->id, std::filesystem::path(file.toStdWString()), options, optimizations,
        &trimReport, &companion);
    if (!written) {
        showFriendlyError(
            this, tr("Export impossible"),
            tr("Le fichier %1 n'a pas pu être écrit dans « %2 ». L'ancien fichier, "
               "s'il existait, est intact.")
                .arg(QString::fromStdString(format->display_name), QFileInfo(file).fileName()),
            written.error().message);
        return false;
    }
    rememberLastDirectory(file);
    QString message =
        tr("%1 exporté : %2")
            .arg(QString::fromStdString(format->display_name), QFileInfo(file).fileName());
    if (request.writtenStitches) {
        message += tr(" (%1 points)").arg(*request.writtenStitches);
    }
    message += QLatin1Char('.');
    if (optimizations.reduce_trims) {
        message += tr(" Coupes supprimées : %1.").arg(trimReport.trims_removed);
    }
    if (!companion.written.empty()) {
        message += tr(" Fichier de couleurs : %1.")
                       .arg(QString::fromStdWString(companion.written.filename().wstring()));
    } else if (!companion.error.empty()) {
        message += tr(" Attention : le fichier de couleurs n'a pas pu être écrit (%1).")
                       .arg(QString::fromStdString(companion.error));
    }
    offerRevealInFolder(file, message);
    return true;
}

// Ctrl+Maj+E : refait l'export avec le format et les options du dernier export (mémorisés par
// poste), sans résumé ni choix : seul le nom du fichier est demandé, dans le dernier dossier.
// Si les réglages mémorisés sont refusés par le format, on rouvre le dialogue complet plutôt que
// d'écrire un fichier que la machine refuserait.
void MainWindow::exportDstQuick() {
    if (!sequence_) {
        return;
    }
    const auto saved = MachineExportDialog::loadSavedExport();
    MachineExportRequest request;
    request.format = formats::find_format(saved.formatId.toStdString());
    if (request.format == nullptr) {
        request.format = formats::find_format("dst");
    }
    request.options = saved.options;
    request.optimizations = saved.optimizations;
    request.companion = saved.companion;
    if (request.format == nullptr || !request.format->can_write) {
        exportDst();
        return;
    }
    const auto exported = project_io::with_start_point(*sequence_, request.options.start);
    for (const auto& issue :
         formats::check_export_limits(exported, *request.format, request.options)) {
        if (issue.severity == formats::ExportIssueSeverity::Error) {
            statusBar()->showMessage(
                tr("Export rapide impossible avec les derniers réglages (%1) : résumé ouvert.")
                    .arg(QString::fromStdString(issue.message)),
                8000);
            exportDst();
            return;
        }
    }
    if (!request.optimizations.reduce_trims) {
        request.writtenStitches = stitch::compute_stats(*sequence_).stitches;
    }
    runMachineExport(request);
}

void MainWindow::importDst() {
    // Filtres construits depuis le registre de formats (HP-FMT-003..005) : tous les formats
    // lisibles d'un coup, puis chacun séparément.
    QStringList all;
    QStringList each;
    for (const auto& f : formats::registered_formats()) {
        if (!f.can_read || f.decode == nullptr) {
            continue;
        }
        QStringList patterns;
        for (const auto& e : f.extensions) {
            patterns << QStringLiteral("*.") + QString::fromStdString(e);
        }
        all << patterns;
        each << tr("%1 (%2)").arg(QString::fromStdString(f.display_name), patterns.join(' '));
    }
    const QString filter = (QStringList{tr("Broderies machine (%1)").arg(all.join(' '))} + each)
                               .join(QStringLiteral(";;"));
    const QString file =
        QFileDialog::getOpenFileName(this, tr("Importer une broderie machine"), QString(), filter);
    if (file.isEmpty()) {
        return;
    }
    importDstFile(file);
}

void MainWindow::importDstFile(const QString& file) {
    // AD-04 : « quelque chose à perdre » se lit maintenant sur le document
    // réel (le design importé en fait partie), plus sur `sequence_` (qui
    // n'est qu'une copie d'affichage, cf. main_window.hpp).
    if (hasDocumentContent()) {
        if (!confirmDiscardChanges(
                tr("L'import remplace le document en cours. Enregistrer le projet avant "
                   "d'importer cette broderie ?"))) {
            return;
        }
    }
    // AD-03 : composition fichier machine -> design importé générique
    // (AI-03b), partagée avec la CLI.
    auto imported = project_io::import_machine_file(std::filesystem::path(file.toStdWString()));
    if (!imported) {
        showFriendlyError(this, tr("Import impossible"),
                          tr("Ce fichier n'a pas pu être lu comme broderie machine (DST, PES, "
                             "JEF ou EXP)."),
                          imported.error().message);
        return;
    }

    // HP-HOOP-006 : le design arrive centré dans le cadre (l'origine), comme Hatch.
    document::center_imported_design(*imported);

    project_ = document::Project{};
    resetDocumentState();
    // AD-04 (Tier 0) : le design importé entre dans le document par
    // `ICommand`, jamais une affectation directe -- remplace l'ancienne
    // exception desktop `sequenceImported_`/`sequence_` hors document (§17).
    // Poussée sur la pile juste vidée par `resetDocumentState` : un Ctrl+Z
    // immédiat après cet import redonne un document sans design importé.
    undoStack_.execute(std::make_unique<commands::SetImportedDesignCommand>(std::move(*imported)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage(); // regenere sequence_ via effective_sequence, qui voit project_.imported_design
    view_->fitCanvas();
    updateActions();
    // Le design importé EST l'état de départ du document : propre, comme un projet ouvert.
    undoStack_.markClean();
    savedAt_ = QTime::currentTime();
    setWindowModified(false);

    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("%1 — %2 points, %3 saut(s), %4 changement(s) de fil")
                                     .arg(QFileInfo(file).fileName())
                                     .arg(stats.stitches)
                                     .arg(stats.jumps)
                                     .arg(stats.color_changes));
    }
}

void MainWindow::importDxf() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Importer un DXF"), QString(),
                                                      tr("Dessin AutoCAD (*.dxf)"));
    if (file.isEmpty()) {
        return;
    }
    auto paths = formats::read_dxf_file(std::filesystem::path(file.toStdWString()));
    if (!paths) {
        QMessageBox::warning(this, tr("Import impossible"),
                             QString::fromStdString(paths.error().message));
        return;
    }

    std::vector<document::VectorObject> objects;
    objects.reserve(paths->size());
    for (auto& path : *paths) {
        document::VectorObject object;
        object.id = project_.object_ids.next();
        object.name = tr("Import DXF %1").arg(objects.size() + 1).toStdString();
        object.rgb = {80, 120, 200};
        object.paths.push_back(geometry::PathSet{std::move(path), {}});
        objects.push_back(std::move(object));
    }
    const std::size_t imported = objects.size();
    undoStack_.execute(
        std::make_unique<commands::AddObjectBatchCommand>(
            std::move(objects), std::vector<document::EmbroideryObject>{}, "Import DXF"),
        project_);
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("%1 — %2 tracé(s) importé(s)").arg(QFileInfo(file).fileName()).arg(imported));
}

void MainWindow::exportDxf() {
    if (project_.vector_objects.empty()) {
        QMessageBox::information(this, tr("Exporter en DXF"),
                                 tr("Aucun objet vectoriel à exporter."));
        return;
    }
    QString file = QFileDialog::getSaveFileName(this, tr("Exporter en DXF"),
                                                suggestedFilePath(QStringLiteral("dxf")),
                                                tr("Dessin AutoCAD (*.dxf)"));
    if (file.isEmpty()) {
        return;
    }
    if (QFileInfo(file).suffix().compare(QStringLiteral("dxf"), Qt::CaseInsensitive) != 0) {
        file += QStringLiteral(".dxf");
    }
    std::vector<geometry::Path> paths;
    for (const auto& object : project_.vector_objects) {
        for (const auto& pathSet : object.paths) {
            paths.push_back(pathSet.outer);
            for (const auto& hole : pathSet.holes) {
                paths.push_back(hole);
            }
        }
    }
    const auto result = formats::write_dxf_file(std::filesystem::path(file.toStdWString()), paths);
    if (!result) {
        showFriendlyError(
            this, tr("Export impossible"),
            tr("Le fichier DXF n'a pas pu être écrit dans « %1 ».").arg(QFileInfo(file).fileName()),
            result.error().message);
        return;
    }
    rememberLastDirectory(file);
    offerRevealInFolder(
        file, tr("%1 — %2 tracé(s) exporté(s)").arg(QFileInfo(file).fileName()).arg(paths.size()));
}

} // namespace openstitch::desktop
