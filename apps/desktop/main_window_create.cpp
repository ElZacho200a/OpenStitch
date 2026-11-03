// SPDX-License-Identifier: Apache-2.0
// Création d'objets, auto-numérisation, réglages, conversion satin/tatami.
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

void MainWindow::segmentImage() {
    // Dialogue non modal avec aperçu en direct : voir main_window_segment_preview.cpp.
    openSegmentationDialog();
}

QPainterPath MainWindow::objectPainterPath(const document::VectorObject& object) {
    return objectScenePath(object); // contour unique partagé avec la détection de sélection
}

void MainWindow::createRunningStitchObject() {
    createRunningStitchObjectImpl(false);
}

void MainWindow::createRunningStitchObjectWithSettings() {
    createRunningStitchObjectImpl(true);
}

void MainWindow::createRunningStitchObjectImpl(bool askSettings) {
    // Une ou plusieurs formes : un objet de contour par forme. HP-UX-002 : création immédiate au
    // dernier réglage mémorisé ; le dialogue n'existe que pour « Créer avec réglages… ».
    const std::vector<ObjectId> sourceIds = selectedObjectIds();
    if (sourceIds.empty() || project_.findObject(sourceIds.front()) == nullptr) {
        return;
    }

    document::RunningStitchParams params;
    ui_memory::creation::apply(params);
    if (askSettings) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Objet de point de contour"));
        auto* layout = new QFormLayout(&dialog);
        auto* lengthSpin = new QDoubleSpinBox(&dialog);
        lengthSpin->setRange(0.5, 12.0);
        lengthSpin->setValue(to_millimeters(params.stitch_length).value);
        lengthSpin->setDecimals(1);
        lengthSpin->setSuffix(tr(" mm"));
        auto* typeCombo = new QComboBox(&dialog);
        typeCombo->addItem(tr("Point simple"), 1);
        typeCombo->addItem(tr("Aller-retour (double)"), 2);
        typeCombo->addItem(tr("Point triple"), 3);
        typeCombo->setCurrentIndex(std::clamp(params.repeats, 1, 3) - 1);
        layout->addRow(tr("Longueur de point :"), lengthSpin);
        layout->addRow(tr("Type :"), typeCombo);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        params.stitch_length = to_micrometers(Millimeters{lengthSpin->value()});
        params.repeats = typeCombo->currentData().toInt();
        ui_memory::creation::remember(document::StitchParams{params});
    }

    auto group = std::make_unique<commands::CompositeCommand>(
        tr("Créer un contour pour %1 formes").arg(sourceIds.size()).toStdString());
    for (const ObjectId sourceId : sourceIds) {
        const auto* source = project_.findObject(sourceId);
        if (source == nullptr) {
            continue;
        }
        document::EmbroideryObject object;
        object.id = project_.object_ids.next();
        object.name = tr("Contour de %1").arg(QString::fromStdString(source->name)).toStdString();
        object.source_vector = source->id;
        object.rgb = source->rgb;
        object.params = params;
        object.intent = document::EmbroideryIntent::ForcedUserChoice;
        group->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)));
    }
    undoStack_.execute(std::move(group), project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(
            tr("Points générés : %1 points, %2 saut(s)").arg(stats.stitches).arg(stats.jumps));
    }
}

void MainWindow::createTatamiObject() {
    createTatamiObjectImpl(false);
}

void MainWindow::createTatamiObjectWithSettings() {
    createTatamiObjectImpl(true);
}

void MainWindow::createTatamiObjectImpl(bool askSettings) {
    // Une ou plusieurs formes : un remplissage par forme. HP-UX-002 : création immédiate au
    // dernier réglage mémorisé ; le dialogue n'existe que pour « Créer avec réglages… ».
    const std::vector<ObjectId> sourceIds = selectedObjectIds();
    if (sourceIds.empty() || project_.findObject(sourceIds.front()) == nullptr) {
        return;
    }

    document::TatamiParams params;
    ui_memory::creation::apply(params);
    if (askSettings) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Remplissage tatami"));
        auto* layout = new QFormLayout(&dialog);
        auto* densitySpin = new QDoubleSpinBox(&dialog);
        densitySpin->setRange(0.1, 2.0);
        densitySpin->setValue(to_millimeters(params.row_spacing).value);
        densitySpin->setDecimals(2);
        densitySpin->setSuffix(tr(" mm"));
        auto* lengthSpin = new QDoubleSpinBox(&dialog);
        lengthSpin->setRange(1.0, 8.0);
        lengthSpin->setValue(to_millimeters(params.stitch_length).value);
        lengthSpin->setDecimals(1);
        lengthSpin->setSuffix(tr(" mm"));
        auto* angleSpin = new QSpinBox(&dialog);
        angleSpin->setRange(0, 179);
        angleSpin->setValue(
            static_cast<int>(std::lround(params.angle.radians * 180.0 / std::numbers::pi)) % 180);
        angleSpin->setSuffix(tr(" °"));
        layout->addRow(tr("Espacement des rangées :"), densitySpin);
        layout->addRow(tr("Longueur de point :"), lengthSpin);
        layout->addRow(tr("Angle :"), angleSpin);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addRow(buttons);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        params.row_spacing = to_micrometers(Millimeters{densitySpin->value()});
        params.stitch_length = to_micrometers(Millimeters{lengthSpin->value()});
        params.angle = Angle{angleSpin->value() * std::numbers::pi / 180.0};
        ui_memory::creation::remember(document::StitchParams{params});
    }

    auto group = std::make_unique<commands::CompositeCommand>(
        tr("Créer un remplissage pour %1 formes").arg(sourceIds.size()).toStdString());
    std::optional<ObjectId> createdId;
    for (const ObjectId sourceId : sourceIds) {
        const auto* source = project_.findObject(sourceId);
        if (source == nullptr) {
            continue;
        }
        document::EmbroideryObject object;
        object.id = project_.object_ids.next();
        object.name =
            tr("Remplissage de %1").arg(QString::fromStdString(source->name)).toStdString();
        object.source_vector = source->id;
        object.rgb = source->rgb;
        object.params = params;
        object.intent = document::EmbroideryIntent::ForcedUserChoice;
        createdId = object.id;
        group->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)));
    }
    // Forme seule : sélectionne le nouveau remplissage (APRÈS l'exécution réussie, jamais un id
    // inexistant) pour que « Orientation du remplissage… » s'applique à lui.
    undoStack_.execute(std::move(group), project_);
    if (createdId && sourceIds.size() == 1) {
        editSelection([id = *createdId](Selection& sel) { sel.embroidery = id; });
    }
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("Remplissage généré : %1 points").arg(stats.stitches));
    }
}

void MainWindow::autoDigitize() {
    // Deux sources possibles pour la même classification AutoChoice (§24) :
    // une segmentation pixel (chemin historique), ou des objets vectoriels
    // déjà existants sans aucune segmentation -- typiquement un import SVG
    // direct (openSvg()), qui produit des VectorObject en sautant
    // délibérément image/segmentation/vectorisation. Least-surprise : une
    // seule action « Numérisation automatique », qui choisit la bonne
    // entrée plutôt que de forcer l'utilisateur à connaître la distinction.
    const bool hasSegmentation = project_.segmentation.has_value();
    if (!hasSegmentation && project_.vector_objects.empty()) {
        QMessageBox::information(
            this, tr("Numérisation automatique"),
            tr("Segmentez d'abord l'image (menu Segmentation), ou importez un fichier SVG, "
               "puis relancez."));
        return;
    }
    // Pour le chemin segmentation, un objet vectoriel existant est déjà le
    // signe qu'une numérisation a tourné une première fois (§ comportement
    // historique, inchangé). Pour le chemin SVG, les objets vectoriels SONT
    // l'entrée elle-même -- seule la présence d'un objet de broderie
    // signale une numérisation déjà effectuée.
    const bool alreadyDigitized =
        hasSegmentation ? (!project_.embroidery_objects.empty() || !project_.vector_objects.empty())
                        : !project_.embroidery_objects.empty();
    bool replacePrevious = false;
    // Chemin segmentation : la question Ajouter / Remplacer est posée DANS le dialogue d'options
    // (une seule fenêtre, HP-UX-030). Le chemin SVG n'a pas de dialogue d'options : question seule.
    if (alreadyDigitized && !hasSegmentation) {
        QMessageBox box(
            QMessageBox::Question, tr("Numérisation automatique"),
            tr("Des objets existent déjà. Ajouter les nouveaux objets à côté des existants, ou "
               "remplacer les objets créés précédemment par la numérisation automatique ?"),
            QMessageBox::NoButton, this);
        box.setInformativeText(
            tr("« Remplacer » supprime les objets de broderie à choix automatique et les formes "
               "issues de la segmentation qui leur servent de source ; les objets verrouillés, "
               "retouchés à la main ou dessinés par vous sont conservés. Un seul Ctrl+Z "
               "annule le remplacement."));
        auto* addBtn = box.addButton(tr("Ajouter"), QMessageBox::AcceptRole);
        auto* replaceBtn = box.addButton(tr("Remplacer"), QMessageBox::DestructiveRole);
        auto* cancelBtn = box.addButton(tr("Annuler"), QMessageBox::RejectRole);
        box.setDefaultButton(addBtn);
        box.setEscapeButton(cancelBtn);
        box.exec();
        if (box.clickedButton() == replaceBtn) {
            replacePrevious = true;
        } else if (box.clickedButton() != addBtn) {
            return;
        }
    }

    autodigitize::AutoOptions opts;
    opts.mm_per_px = project_.mm_per_px;
    // Chemin segmentation : les réglages du dialogue alimentent `project_ops::digitize_objects`,
    // le même cœur que `openstitch-cli digitize` (formes, contours, traits sombres, pointillé).
    project_ops::DigitizeSettings ds;
    bool contoursMode = false;
    bool stippleMode = false;
    bool skipLargest = false;

    if (hasSegmentation) {
        // Fond présumé (Lot A, audit marine plein cadre 2026-09-22) : la
        // recommandation vient de `segmentation::background_candidate`, plus
        // du seul critère « pas de canal alpha ». Historique : une image PNG
        // sans alpha n'a aucun pixel transparent (`image::load.cpp` remplit
        // l'alpha à 255), son fond devient une région opaque comme les
        // autres -- d'où l'option. Mais sur une image PLEIN CADRE (paysage,
        // carte de segmentation) la plus grande région est un vrai élément
        // du motif : cocher d'office sur « pas d'alpha » laissait le ciel
        // entier sans un point. La case n'est donc cochée que pour un fond
        // quasi blanc (L* > 90) qui encadre le motif (>= 3 bords), et la
        // couleur + la surface de la candidate sont affichées pour que
        // l'utilisateur tranche en connaissance de cause. Sans objet de pixel
        // segmenté (chemin SVG), cette notion n'a pas de sens -- la boîte de
        // dialogue ne s'affiche que dans ce cas.
        const auto candidate = segmentation::background_candidate(*project_.segmentation);
        QDialog optsDialog(this);
        optsDialog.setWindowTitle(tr("Numérisation automatique"));
        auto* optsLayout = new QVBoxLayout(&optsDialog);
        // Préréglages (HP-UX-030) : remplissent les champs ci-dessous, rien de plus.
        auto* presetRow = new QHBoxLayout;
        auto* presetCombo = new QComboBox(&optsDialog);
        presetCombo->setObjectName("autoDigitizePresetCombo");
        presetCombo->setAccessibleName(tr("Préréglage de numérisation"));
        presetCombo->addItem(tr("Personnalisé"), static_cast<int>(DigitizePreset::Custom));
        presetCombo->addItem(tr("Logo / aplats"), static_cast<int>(DigitizePreset::Logo));
        presetCombo->addItem(tr("Dessin au trait"), static_cast<int>(DigitizePreset::LineArt));
        presetCombo->addItem(tr("Photo / pointillé"), static_cast<int>(DigitizePreset::Photo));
        presetCombo->setToolTip(tr(
            "Un préréglage remplit les réglages de ce dialogue ; vous pouvez ensuite les "
            "ajuster dans « Réglages avancés » (le préréglage passe alors à « Personnalisé »)."));
        auto* presetLabel = new QLabel(tr("Type d'image :"), &optsDialog);
        presetLabel->setBuddy(presetCombo);
        presetRow->addWidget(presetLabel);
        presetRow->addWidget(presetCombo, 1);
        optsLayout->addLayout(presetRow);
        auto* skipBgCheck =
            new QCheckBox(tr("Ignorer la plus grande région (probablement le fond)"), &optsDialog);
        skipBgCheck->setObjectName("skipBackgroundCheck");
        skipBgCheck->setChecked(candidate && candidate->recommended);
        skipBgCheck->setToolTip(
            tr("Exclut la couleur de la plus grande région, et toutes les régions de cette "
               "couleur. Cochée d'office seulement pour un fond quasi blanc qui encadre le "
               "motif : sur une image plein cadre, la plus grande région (ciel, mer...) fait "
               "partie du dessin."));
        optsLayout->addWidget(skipBgCheck);
        auto* skipBgOuterCheck = new QCheckBox(
            tr("Seulement le fond extérieur (garder le blanc enfermé dans le motif)"), &optsDialog);
        skipBgOuterCheck->setObjectName("skipBackgroundOuterCheck");
        skipBgOuterCheck->setEnabled(skipBgCheck->isChecked());
        skipBgOuterCheck->setToolTip(
            tr("N'exclut que les régions de la couleur du fond qui touchent le pourtour de "
               "l'image : un soleil ou un nuage blanc entouré de motif reste brodé. Un blanc "
               "relié au fond par la quantification reste ignoré."));
        QObject::connect(skipBgCheck, &QCheckBox::toggled, skipBgOuterCheck, &QWidget::setEnabled);
        optsLayout->addWidget(skipBgOuterCheck);
        auto* satinBandsCheck =
            new QCheckBox(tr("Satin automatique (bandes étroites, petits disques)"), &optsDialog);
        satinBandsCheck->setObjectName("satinBandsCheck");
        satinBandsCheck->setChecked(
            ui_memory::intValue(QStringLiteral("autoDigitize/satinBands"), 0, 0, 1) != 0);
        satinBandsCheck->setToolTip(
            tr("Mode formes : les anneaux, rubans et traits de 0,8 à 6 mm de large sont cousus en "
               "auto-satin au lieu d'un tatami (repli tatami si le satin couvre mal la forme). "
               "Les petits disques (jusqu'à 12 mm) sont cousus en cordes parallèles ; les formes "
               "plus larges restent en tatami."));
        optsLayout->addWidget(satinBandsCheck);
        if (candidate) {
            auto* row = new QHBoxLayout;
            auto* swatch = new QLabel(&optsDialog);
            swatch->setObjectName("backgroundSwatch");
            // HP-UX-043 : pastille nette à tout DPI, taille déduite de la police.
            const int swatchSide = optsDialog.fontMetrics().height() + 8;
            swatch->setFixedSize(swatchSide, swatchSide);
            swatch->setPixmap(makeSwatch(
                QColor(candidate->rgb[0], candidate->rgb[1], candidate->rgb[2]), swatchSide));
            auto* info = new QLabel(
                tr("Région candidate : %1 % de l'image, clarté L* %2, %3 bord(s) touché(s) sur 4")
                    .arg(QLocale().toString(candidate->area_ratio * 100.0, 'f', 1))
                    .arg(QLocale().toString(candidate->lightness, 'f', 0))
                    .arg(candidate->sides_touched),
                &optsDialog);
            info->setObjectName("backgroundInfo");
            row->addWidget(swatch);
            row->addWidget(info, 1);
            optsLayout->addLayout(row);
        }
        // Palette de fils (HP-THR-011) : fusion des couleurs les plus proches (libs/autodigitize).
        auto* maxThreadsRow = new QHBoxLayout;
        maxThreadsRow->addWidget(new QLabel(tr("Limiter à :"), &optsDialog));
        auto* maxThreadsSpin = new QSpinBox(&optsDialog);
        maxThreadsSpin->setObjectName("maxThreadsSpin");
        maxThreadsSpin->setRange(0, 99);
        maxThreadsSpin->setSpecialValueText(tr("pas de limite"));
        maxThreadsSpin->setSuffix(tr(" fils"));
        maxThreadsSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/maxThreads"), 0, 0, 99));
        maxThreadsSpin->setToolTip(
            tr("Fusionne les couleurs les plus proches pour n'utiliser que ce nombre de fils "
               "(la couleur du plus grand aplat est conservée)."));
        maxThreadsRow->addWidget(maxThreadsSpin);
        maxThreadsRow->addStretch(1);
        optsLayout->addLayout(maxThreadsRow);
        // Strategie : formes pleines (historique) ou contours (dessin au trait).
        auto* shapesRadio = new QRadioButton(tr("Formes pleines (remplissages)"), &optsDialog);
        shapesRadio->setObjectName("strategyShapesRadio");
        shapesRadio->setChecked(true);
        auto* contoursRadio = new QRadioButton(tr("Contours (dessin au trait)"), &optsDialog);
        contoursRadio->setObjectName("strategyContoursRadio");
        contoursRadio->setToolTip(
            tr("Coud les traits du dessin (lignes médianes) en point droit, au lieu de remplir "
               "les formes."));
        auto* stippleRadio = new QRadioButton(tr("Pointillé (stippling)"), &optsDialog);
        stippleRadio->setObjectName("strategyStippleRadio");
        stippleRadio->setToolTip(
            tr("Coud des points répartis selon les tons de l'image (clair = peu de points, "
               "sombre = beaucoup), sans segmentation."));
        optsLayout->addWidget(shapesRadio);
        optsLayout->addWidget(contoursRadio);
        optsLayout->addWidget(stippleRadio);

        // Section « Réglages avancés », repliée par défaut ; son état est mémorisé.
        auto* advancedToggle = new QToolButton(&optsDialog);
        advancedToggle->setObjectName("autoDigitizeAdvancedToggle");
        advancedToggle->setText(tr("Réglages avancés"));
        advancedToggle->setCheckable(true);
        advancedToggle->setAutoRaise(true);
        advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        auto* advancedBox = new QWidget(&optsDialog);
        advancedBox->setObjectName("autoDigitizeAdvanced");
        auto* advancedLayout = new QVBoxLayout(advancedBox);
        advancedLayout->setContentsMargins(0, 0, 0, 0);
        const auto setAdvancedOpen = [advancedToggle, advancedBox](bool open) {
            advancedToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
            advancedBox->setVisible(open);
        };
        connect(advancedToggle, &QToolButton::toggled, &optsDialog, setAdvancedOpen);
        advancedToggle->setChecked(
            ui_memory::boolValue(QStringLiteral("autoDigitize/advancedOpen"), false));
        setAdvancedOpen(advancedToggle->isChecked());
        optsLayout->addWidget(advancedToggle);
        optsLayout->addWidget(advancedBox);

        auto* shapesPanel = new QWidget(&optsDialog);
        shapesPanel->setObjectName("shapesPanel");
        auto* shapesPanelLayout = new QVBoxLayout(shapesPanel);
        shapesPanelLayout->setContentsMargins(0, 0, 0, 0);
        auto* shapeDetailRow = new QHBoxLayout;
        auto* shapeDetailSlider = new QSlider(Qt::Horizontal, shapesPanel);
        shapeDetailSlider->setObjectName("shapeVectorizeDetailSlider");
        shapeDetailSlider->setRange(0, 100);
        shapeDetailSlider->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/shapeDetail"), 50, 0, 100));
        shapeDetailSlider->setToolTip(
            tr("Niveau de détail des formes vectorisées : bas = contours lissés ; haut = plus "
               "fidèle aux pixels segmentés."));
        auto* shapeDetailValue =
            new QLabel(QString::number(shapeDetailSlider->value()), shapesPanel);
        shapeDetailValue->setObjectName("shapeVectorizeDetailValue");
        connect(shapeDetailSlider, &QSlider::valueChanged, shapeDetailValue,
                [shapeDetailValue](int v) { shapeDetailValue->setText(QString::number(v)); });
        shapeDetailRow->addWidget(new QLabel(tr("Détail vectorisation :"), shapesPanel));
        shapeDetailRow->addWidget(new QLabel(tr("Faible"), shapesPanel));
        shapeDetailRow->addWidget(shapeDetailSlider, 1);
        shapeDetailRow->addWidget(new QLabel(tr("Élevé"), shapesPanel));
        shapeDetailRow->addWidget(shapeDetailValue);
        shapesPanelLayout->addLayout(shapeDetailRow);
        // Chevauchement des tatamis voisins (AutoOptions::fill_overlap), en µm
        // dans la mémoire des réglages.
        auto* overlapRow = new QHBoxLayout;
        auto* overlapSpin = new QDoubleSpinBox(shapesPanel);
        overlapSpin->setObjectName("fillOverlapSpin");
        overlapSpin->setRange(0.0, 1.0);
        overlapSpin->setSingleStep(0.05);
        overlapSpin->setDecimals(2);
        overlapSpin->setSuffix(tr(" mm"));
        overlapSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/fillOverlapUm"), 0, 0, 1000) / 1000.0);
        overlapSpin->setToolTip(
            tr("Débord de chaque remplissage sur ses voisins brodés, le long des bords partagés "
               "uniquement : évite les jours entre deux couleurs. 0 = contours exacts de la "
               "segmentation. Le débord fait partie de la forme vectorielle : un autre type de "
               "points choisi ensuite sur cette forme débordera aussi."));
        overlapRow->addWidget(new QLabel(tr("Chevauchement des remplissages :"), shapesPanel));
        overlapRow->addWidget(overlapSpin);
        overlapRow->addStretch(1);
        shapesPanelLayout->addLayout(overlapRow);
        // Nettoyage des fragments avant numérisation (AutoOptions::min_feature_width_mm et
        // min_region_area_mm2) : il arrondit les coins et efface les détails fins ; à 0 le
        // contour reste celui de la segmentation. Mémorisés en µm et en 1/100 mm².
        auto* featureRow = new QHBoxLayout;
        auto* featureSpin = new QDoubleSpinBox(shapesPanel);
        featureSpin->setObjectName("minFeatureWidthSpin");
        featureSpin->setRange(0.0, 5.0);
        featureSpin->setSingleStep(0.1);
        featureSpin->setDecimals(1);
        featureSpin->setSuffix(tr(" mm"));
        featureSpin->setSpecialValueText(tr("désactivé"));
        featureSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/minFeatureWidthUm"),
                                static_cast<int>(std::lround(opts.min_feature_width_mm * 1000.0)),
                                0, 5000) /
            1000.0);
        featureSpin->setToolTip(
            tr("Les détails plus étroits que cette largeur (contours fins, anneaux, pointes) sont "
               "retirés de leur forme et rendus à la voisine avant numérisation ; les coins "
               "convexes sont aussi arrondis. Baissez ou désactivez pour des contours plus "
               "fidèles à l'image."));
        featureRow->addWidget(new QLabel(tr("Largeur minimale des détails :"), shapesPanel));
        featureRow->addWidget(featureSpin);
        featureRow->addStretch(1);
        shapesPanelLayout->addLayout(featureRow);
        auto* minAreaRow = new QHBoxLayout;
        auto* minAreaSpin = new QDoubleSpinBox(shapesPanel);
        minAreaSpin->setObjectName("minRegionAreaSpin");
        minAreaSpin->setRange(0.0, 50.0);
        minAreaSpin->setSingleStep(0.5);
        minAreaSpin->setDecimals(2);
        minAreaSpin->setSuffix(tr(" mm²"));
        minAreaSpin->setSpecialValueText(tr("désactivé"));
        minAreaSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/minRegionAreaCmm2"),
                                static_cast<int>(std::lround(opts.min_region_area_mm2 * 100.0)), 0,
                                5000) /
            100.0);
        minAreaSpin->setToolTip(
            tr("Les régions plus petites que cette aire sont fusionnées avec leur voisine de plus "
               "longue frontière : elles disparaissent du résultat. 0 = toutes les régions "
               "segmentées sont conservées."));
        minAreaRow->addWidget(new QLabel(tr("Aire minimale des régions :"), shapesPanel));
        minAreaRow->addWidget(minAreaSpin);
        minAreaRow->addStretch(1);
        shapesPanelLayout->addLayout(minAreaRow);
        // Nettoyage des îlots (AutoOptions::island_area_mm2) : fusion dans la voisine de couleur la
        // plus proche. Mémorisé en 1/100 mm².
        auto* islandRow = new QHBoxLayout;
        auto* islandSpin = new QDoubleSpinBox(shapesPanel);
        islandSpin->setObjectName("islandAreaSpin");
        islandSpin->setRange(0.0, 500.0);
        islandSpin->setSingleStep(1.0);
        islandSpin->setDecimals(1);
        islandSpin->setSuffix(tr(" mm²"));
        islandSpin->setSpecialValueText(tr("désactivé"));
        islandSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/islandAreaCmm2"), 0, 0, 50000) /
            100.0);
        islandSpin->setToolTip(
            tr("Les petits îlots collés à un grand aplat sont fusionnés dans la voisine dont la "
               "couleur est la plus proche (et non celle de plus longue frontière). Les traits "
               "fins sombres sont aussi des îlots : cochez « Traits sombres » pour les garder."));
        islandRow->addWidget(new QLabel(tr("Nettoyage des îlots :"), shapesPanel));
        islandRow->addWidget(islandSpin);
        islandRow->addStretch(1);
        shapesPanelLayout->addLayout(islandRow);
        // Bloc uni (autodigitize::apply_solid_block) : aucun tissu visible entre zones voisines.
        auto* solidBlockCheck = new QCheckBox(tr("Bloc uni (sans tissu visible)"), shapesPanel);
        solidBlockCheck->setObjectName("solidBlockCheck");
        solidBlockCheck->setChecked(
            ui_memory::intValue(QStringLiteral("autoDigitize/solidBlock"), 0, 0, 1) != 0);
        solidBlockCheck->setToolTip(
            tr("Pour une broderie pleine d'un seul tenant : les remplissages débordent d'au moins "
               "0,5 mm sous leurs voisins et les petites régions sont fusionnées dans leur voisine "
               "au lieu d'être cousues en simple contour. Les réglages plus élevés ci-dessus "
               "(débord, aire minimale) sont conservés."));
        shapesPanelLayout->addWidget(solidBlockCheck);
        // Couche de traits sombres (HP-AUTO-005) : contours noirs, mâts, haubans, séparés des
        // remplissages et cousus par-dessus, en dernier.
        auto* darkLinesCheck = new QCheckBox(tr("Traits sombres cousus par-dessus"), shapesPanel);
        darkLinesCheck->setObjectName("darkLinesCheck");
        darkLinesCheck->setChecked(
            ui_memory::intValue(QStringLiteral("autoDigitize/darkLines"), 0, 0, 1) != 0);
        darkLinesCheck->setToolTip(tr(
            "Les traits sombres fins de l'image (contours noirs, mâts, haubans) sont séparés des "
            "remplissages et cousus en point droit par-dessus, en dernier ; les remplissages "
            "passent dessous sans trou. Coûte des coupes : chaque trait est un tracé séparé."));
        shapesPanelLayout->addWidget(darkLinesCheck);
        auto* darkWidthRow = new QHBoxLayout;
        auto* darkWidthSpin = new QDoubleSpinBox(shapesPanel);
        darkWidthSpin->setObjectName("darkLinesWidthSpin");
        darkWidthSpin->setRange(0.3, 6.0);
        darkWidthSpin->setSingleStep(0.1);
        darkWidthSpin->setDecimals(1);
        darkWidthSpin->setSuffix(tr(" mm"));
        darkWidthSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/darkLinesWidthUm"), 1600, 300, 6000) /
            1000.0);
        darkWidthSpin->setToolTip(tr("Largeur maximale d'un trait : au-delà, c'est un aplat."));
        auto* darkLightSpin = new QDoubleSpinBox(shapesPanel);
        darkLightSpin->setObjectName("darkLinesLightnessSpin");
        darkLightSpin->setRange(5.0, 70.0);
        darkLightSpin->setSingleStep(1.0);
        darkLightSpin->setDecimals(0);
        darkLightSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/darkLinesLightness"), 25, 5, 70));
        darkLightSpin->setToolTip(
            tr("Clarté CIELAB L* maximale d'un pixel de trait (0 = noir, 100 = blanc)."));
        darkWidthRow->addWidget(new QLabel(tr("Largeur max. :"), shapesPanel));
        darkWidthRow->addWidget(darkWidthSpin);
        darkWidthRow->addWidget(new QLabel(tr("Clarté max. (L*) :"), shapesPanel));
        darkWidthRow->addWidget(darkLightSpin);
        darkWidthRow->addStretch(1);
        shapesPanelLayout->addLayout(darkWidthRow);
        darkWidthSpin->setEnabled(darkLinesCheck->isChecked());
        darkLightSpin->setEnabled(darkLinesCheck->isChecked());
        connect(darkLinesCheck, &QCheckBox::toggled, darkWidthSpin, &QWidget::setEnabled);
        connect(darkLinesCheck, &QCheckBox::toggled, darkLightSpin, &QWidget::setEnabled);
        // Réglages avancés des traits sombres (mêmes options que la CLI : --dark-lines-*).
        auto* darkMinLenSpin = new QDoubleSpinBox(shapesPanel);
        darkMinLenSpin->setObjectName("darkLinesMinLengthSpin");
        darkMinLenSpin->setRange(0.0, 50.0);
        darkMinLenSpin->setSingleStep(0.5);
        darkMinLenSpin->setDecimals(1);
        darkMinLenSpin->setSuffix(tr(" mm"));
        darkMinLenSpin->setSpecialValueText(tr("aucune"));
        darkMinLenSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/darkLinesMinLenUm"), 0, 0, 50000) /
            1000.0);
        darkMinLenSpin->setToolTip(
            tr("Les fragments de trait isolés plus courts que cette longueur ne sont pas cousus."));
        auto* darkObjectsOnlyCheck = new QCheckBox(tr("Seulement les vrais objets"), shapesPanel);
        darkObjectsOnlyCheck->setObjectName("darkLinesObjectsOnlyCheck");
        darkObjectsOnlyCheck->setChecked(
            ui_memory::intValue(QStringLiteral("autoDigitize/darkLinesObjectsOnly"), 0, 0, 1) != 0);
        darkObjectsOnlyCheck->setToolTip(tr(
            "Ne coud que les traits qui sont de vrais objets, pas les séparations entre zones."));
        auto* ringsSatinCheck = new QCheckBox(tr("Anneaux en satin"), shapesPanel);
        ringsSatinCheck->setObjectName("ringsSatinCheck");
        ringsSatinCheck->setChecked(
            ui_memory::intValue(QStringLiteral("autoDigitize/ringsSatin"), 0, 0, 1) != 0);
        ringsSatinCheck->setToolTip(
            tr("Les cercles et ellipses épaissis (hublots, ventouses, bulles, yeux) sont reconnus "
               "sur le masque des traits sombres et cousus en auto-satin avec un contour exact."));
        auto* darkAdvRow = new QHBoxLayout;
        darkAdvRow->addWidget(new QLabel(tr("Longueur min. :"), shapesPanel));
        darkAdvRow->addWidget(darkMinLenSpin);
        darkAdvRow->addWidget(darkObjectsOnlyCheck);
        darkAdvRow->addWidget(ringsSatinCheck);
        darkAdvRow->addStretch(1);
        shapesPanelLayout->addLayout(darkAdvRow);
        for (QWidget* w : std::initializer_list<QWidget*>{darkMinLenSpin, darkObjectsOnlyCheck,
                                                          ringsSatinCheck}) {
            w->setEnabled(darkLinesCheck->isChecked());
            connect(darkLinesCheck, &QCheckBox::toggled, w, &QWidget::setEnabled);
        }
        // Courbes de Bézier, plafond de ΔE des îlots, remplissage qui suit le mouvement.
        auto* curveSpin = new QDoubleSpinBox(shapesPanel);
        curveSpin->setObjectName("curveToleranceSpin");
        curveSpin->setRange(0.0, 2.0);
        curveSpin->setSingleStep(0.05);
        curveSpin->setDecimals(2);
        curveSpin->setSuffix(tr(" mm"));
        curveSpin->setSpecialValueText(tr("polygones"));
        curveSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/curveToleranceUm"), 0, 0, 2000) /
            1000.0);
        curveSpin->setToolTip(
            tr("Contours des formes en courbes de Bézier (tolérance d'ajustement) "
               "au lieu de polygones ; 0 = polygones."));
        auto* islandDeSpin = new QDoubleSpinBox(shapesPanel);
        islandDeSpin->setObjectName("islandMaxDeltaESpin");
        islandDeSpin->setRange(0.0, 100.0);
        islandDeSpin->setSingleStep(1.0);
        islandDeSpin->setDecimals(0);
        islandDeSpin->setSpecialValueText(tr("sans limite"));
        islandDeSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/islandMaxDeltaE"), 0, 0, 100));
        islandDeSpin->setToolTip(
            tr("Avec le nettoyage des îlots : refuse une voisine plus éloignée que cet écart de "
               "couleur (ΔE CIELAB)."));
        auto* curveRow = new QHBoxLayout;
        curveRow->addWidget(new QLabel(tr("Contours :"), shapesPanel));
        curveRow->addWidget(curveSpin);
        curveRow->addWidget(new QLabel(tr("ΔE max. des îlots :"), shapesPanel));
        curveRow->addWidget(islandDeSpin);
        curveRow->addStretch(1);
        shapesPanelLayout->addLayout(curveRow);
        islandDeSpin->setEnabled(islandSpin->value() > 0.0);
        connect(islandSpin, &QDoubleSpinBox::valueChanged, islandDeSpin,
                [islandDeSpin](double v) { islandDeSpin->setEnabled(v > 0.0); });
        auto* flowFillCheck = new QCheckBox(tr("Remplissage qui suit le mouvement"), shapesPanel);
        flowFillCheck->setObjectName("flowFillCheck");
        flowFillCheck->setChecked(
            ui_memory::intValue(QStringLiteral("autoDigitize/flowFill"), 0, 0, 1) != 0);
        flowFillCheck->setToolTip(
            tr("Les grandes formes allongées (tentacules, rubans, nuages, fumée) passent en "
               "remplissage directionnel dont les guides suivent l'axe médian : les rangées "
               "suggèrent le mouvement. La couleur la plus sombre reste en tatami."));
        shapesPanelLayout->addWidget(flowFillCheck);
        connect(shapesRadio, &QRadioButton::toggled, shapesPanel, &QWidget::setEnabled);
        advancedLayout->addWidget(shapesPanel);

        auto* contoursPanel = new QWidget(&optsDialog);
        contoursPanel->setObjectName("contoursPanel");
        auto* panelLayout = new QVBoxLayout(contoursPanel);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        auto* detailRow = new QHBoxLayout;
        auto* detailSlider = new QSlider(Qt::Horizontal, contoursPanel);
        detailSlider->setObjectName("contourDetailSlider");
        detailSlider->setRange(0, 100);
        detailSlider->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/contourDetail"), 50, 0, 100));
        detailSlider->setToolTip(
            tr("Niveau de détail : bas = lignes très simplifiées, petits traits ignorés ; "
               "haut = fidèle au dessin."));
        auto* detailValue = new QLabel(QString::number(detailSlider->value()), contoursPanel);
        detailValue->setObjectName("contourDetailValue");
        connect(detailSlider, &QSlider::valueChanged, detailValue,
                [detailValue](int v) { detailValue->setText(QString::number(v)); });
        detailRow->addWidget(new QLabel(tr("Détail :"), contoursPanel));
        detailRow->addWidget(new QLabel(tr("Faible"), contoursPanel));
        detailRow->addWidget(detailSlider, 1);
        detailRow->addWidget(new QLabel(tr("Élevé"), contoursPanel));
        detailRow->addWidget(detailValue);
        panelLayout->addLayout(detailRow);
        auto* techAutoRadio = new QRadioButton(tr("Automatique"), contoursPanel);
        techAutoRadio->setObjectName("contourTechniqueAutoRadio");
        techAutoRadio->setChecked(true);
        auto* techRunningRadio = new QRadioButton(tr("Running (point droit)"), contoursPanel);
        techRunningRadio->setObjectName("contourTechniqueRunningRadio");
        auto* techRow = new QHBoxLayout;
        techRow->addWidget(techAutoRadio);
        techRow->addWidget(techRunningRadio);
        panelLayout->addLayout(techRow);
        contoursPanel->setEnabled(false);
        connect(contoursRadio, &QRadioButton::toggled, contoursPanel, &QWidget::setEnabled);
        advancedLayout->addWidget(contoursPanel);

        auto* stipplePanel = new QWidget(&optsDialog);
        stipplePanel->setObjectName("stipplePanel");
        auto* stippleLayout = new QHBoxLayout(stipplePanel);
        stippleLayout->setContentsMargins(0, 0, 0, 0);
        auto* stipplePointsSpin = new QSpinBox(stipplePanel);
        stipplePointsSpin->setObjectName("stipplePointsSpin");
        stipplePointsSpin->setRange(100, 200000);
        stipplePointsSpin->setSingleStep(100);
        stipplePointsSpin->setSuffix(tr(" points"));
        stipplePointsSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/stipplePoints"), 2000, 100, 200000));
        auto* stippleSeedSpin = new QSpinBox(stipplePanel);
        stippleSeedSpin->setObjectName("stippleSeedSpin");
        stippleSeedSpin->setRange(0, 999999);
        stippleSeedSpin->setValue(
            ui_memory::intValue(QStringLiteral("autoDigitize/stippleSeed"), 1, 0, 999999));
        stippleSeedSpin->setToolTip(tr("Graine : même graine, même répartition."));
        auto* stippleInvertCheck = new QCheckBox(tr("Inverser les tons"), stipplePanel);
        stippleInvertCheck->setObjectName("stippleInvertCheck");
        stippleInvertCheck->setChecked(
            ui_memory::intValue(QStringLiteral("autoDigitize/stippleInvert"), 0, 0, 1) != 0);
        stippleLayout->addWidget(new QLabel(tr("Nombre :"), stipplePanel));
        stippleLayout->addWidget(stipplePointsSpin);
        stippleLayout->addWidget(new QLabel(tr("Graine :"), stipplePanel));
        stippleLayout->addWidget(stippleSeedSpin);
        stippleLayout->addWidget(stippleInvertCheck);
        stippleLayout->addStretch(1);
        stipplePanel->setEnabled(false);
        connect(stippleRadio, &QRadioButton::toggled, stipplePanel, &QWidget::setEnabled);
        advancedLayout->addWidget(stipplePanel);

        // Valeurs du dialogue <-> structure de préréglage (autodigitize_presets.hpp).
        DigitizeDialogValues builtinDefaults;
        builtinDefaults.minFeatureWidthUm =
            static_cast<int>(std::lround(opts.min_feature_width_mm * 1000.0));
        builtinDefaults.minRegionAreaCmm2 =
            static_cast<int>(std::lround(opts.min_region_area_mm2 * 100.0));
        const auto collectValues = [&]() {
            DigitizeDialogValues v;
            v.satinBands = satinBandsCheck->isChecked();
            v.maxThreads = maxThreadsSpin->value();
            v.strategy = contoursRadio->isChecked()  ? DigitizeStrategy::Contours
                         : stippleRadio->isChecked() ? DigitizeStrategy::Stipple
                                                     : DigitizeStrategy::Shapes;
            v.shapeDetail = shapeDetailSlider->value();
            v.fillOverlapUm = static_cast<int>(std::lround(overlapSpin->value() * 1000.0));
            v.minFeatureWidthUm = static_cast<int>(std::lround(featureSpin->value() * 1000.0));
            v.minRegionAreaCmm2 = static_cast<int>(std::lround(minAreaSpin->value() * 100.0));
            v.islandAreaCmm2 = static_cast<int>(std::lround(islandSpin->value() * 100.0));
            v.islandMaxDeltaE = static_cast<int>(std::lround(islandDeSpin->value()));
            v.curveToleranceUm = static_cast<int>(std::lround(curveSpin->value() * 1000.0));
            v.solidBlock = solidBlockCheck->isChecked();
            v.flowFill = flowFillCheck->isChecked();
            v.darkLines = darkLinesCheck->isChecked();
            v.darkLinesWidthUm = static_cast<int>(std::lround(darkWidthSpin->value() * 1000.0));
            v.darkLinesLightness = static_cast<int>(std::lround(darkLightSpin->value()));
            v.darkLinesMinLenUm = static_cast<int>(std::lround(darkMinLenSpin->value() * 1000.0));
            v.darkLinesObjectsOnly = darkObjectsOnlyCheck->isChecked();
            v.ringsSatin = ringsSatinCheck->isChecked();
            v.contourDetail = detailSlider->value();
            v.techniqueRunning = techRunningRadio->isChecked();
            v.stipplePoints = stipplePointsSpin->value();
            v.stippleSeed = stippleSeedSpin->value();
            v.stippleInvert = stippleInvertCheck->isChecked();
            return v;
        };
        bool applyingPreset = false;
        const auto syncPresetCombo = [&] {
            if (applyingPreset) {
                return;
            }
            const auto match = matchingDigitizePreset(collectValues(), builtinDefaults);
            QSignalBlocker block(presetCombo);
            presetCombo->setCurrentIndex(presetCombo->findData(static_cast<int>(match)));
        };
        const auto applyValues = [&](const DigitizeDialogValues& v) {
            applyingPreset = true;
            satinBandsCheck->setChecked(v.satinBands);
            // La palette (nombre de fils) n'appartient à aucun préréglage : maxThreadsSpin reste.
            shapesRadio->setChecked(v.strategy == DigitizeStrategy::Shapes);
            contoursRadio->setChecked(v.strategy == DigitizeStrategy::Contours);
            stippleRadio->setChecked(v.strategy == DigitizeStrategy::Stipple);
            shapeDetailSlider->setValue(v.shapeDetail);
            overlapSpin->setValue(v.fillOverlapUm / 1000.0);
            featureSpin->setValue(v.minFeatureWidthUm / 1000.0);
            minAreaSpin->setValue(v.minRegionAreaCmm2 / 100.0);
            islandSpin->setValue(v.islandAreaCmm2 / 100.0);
            islandDeSpin->setValue(v.islandMaxDeltaE);
            curveSpin->setValue(v.curveToleranceUm / 1000.0);
            solidBlockCheck->setChecked(v.solidBlock);
            flowFillCheck->setChecked(v.flowFill);
            darkLinesCheck->setChecked(v.darkLines);
            darkWidthSpin->setValue(v.darkLinesWidthUm / 1000.0);
            darkLightSpin->setValue(v.darkLinesLightness);
            darkMinLenSpin->setValue(v.darkLinesMinLenUm / 1000.0);
            darkObjectsOnlyCheck->setChecked(v.darkLinesObjectsOnly);
            ringsSatinCheck->setChecked(v.ringsSatin);
            detailSlider->setValue(v.contourDetail);
            techRunningRadio->setChecked(v.techniqueRunning);
            techAutoRadio->setChecked(!v.techniqueRunning);
            stipplePointsSpin->setValue(v.stipplePoints);
            stippleSeedSpin->setValue(v.stippleSeed);
            stippleInvertCheck->setChecked(v.stippleInvert);
            applyingPreset = false;
        };
        connect(presetCombo, &QComboBox::currentIndexChanged, &optsDialog, [&](int index) {
            const auto preset = static_cast<DigitizePreset>(presetCombo->itemData(index).toInt());
            if (preset != DigitizePreset::Custom) {
                applyValues(digitizePresetValues(preset, builtinDefaults));
                syncPresetCombo(); // une valeur écrêtée par un champ ne doit pas passer pour exacte
            }
        });
        // Toute retouche d'un réglage ramène le préréglage affiché à « Personnalisé » (ou à celui
        // dont les valeurs sont exactement celles-ci).
        for (auto* w : optsDialog.findChildren<QDoubleSpinBox*>()) {
            connect(w, &QDoubleSpinBox::valueChanged, &optsDialog, syncPresetCombo);
        }
        for (auto* w : optsDialog.findChildren<QSpinBox*>()) {
            connect(w, &QSpinBox::valueChanged, &optsDialog, syncPresetCombo);
        }
        for (auto* w : optsDialog.findChildren<QSlider*>()) {
            connect(w, &QSlider::valueChanged, &optsDialog, syncPresetCombo);
        }
        for (auto* w : optsDialog.findChildren<QCheckBox*>()) {
            connect(w, &QCheckBox::toggled, &optsDialog, syncPresetCombo);
        }
        for (auto* w : optsDialog.findChildren<QRadioButton*>()) {
            connect(w, &QRadioButton::toggled, &optsDialog, syncPresetCombo);
        }
        syncPresetCombo(); // valeurs relues de la mémoire : affiche le préréglage correspondant

        // Boutons : « Numériser » seul, ou « Ajouter » / « Remplacer » quand des objets existent
        // déjà (la question n'est plus une seconde fenêtre).
        auto* optsButtons = new QDialogButtonBox(&optsDialog);
        if (alreadyDigitized) {
            auto* addBtn = optsButtons->addButton(tr("Ajouter"), QDialogButtonBox::AcceptRole);
            addBtn->setToolTip(tr("Ajoute les nouveaux objets à côté des objets existants."));
            auto* replaceBtn =
                optsButtons->addButton(tr("Remplacer"), QDialogButtonBox::ActionRole);
            replaceBtn->setToolTip(
                tr("Supprime les objets de broderie à choix automatique et les formes issues de "
                   "la segmentation qui leur servent de source ; les objets verrouillés, "
                   "retouchés à la main ou dessinés par vous sont conservés. Un seul Ctrl+Z "
                   "annule le remplacement."));
            replaceBtn->setAutoDefault(false);
            connect(replaceBtn, &QPushButton::clicked, &optsDialog, [&] {
                replacePrevious = true;
                optsDialog.accept();
            });
            addBtn->setDefault(true);
        } else {
            auto* goBtn = optsButtons->addButton(tr("Numériser"), QDialogButtonBox::AcceptRole);
            goBtn->setDefault(true);
        }
        optsButtons->addButton(tr("Annuler"), QDialogButtonBox::RejectRole);
        connect(optsButtons, &QDialogButtonBox::accepted, &optsDialog, &QDialog::accept);
        connect(optsButtons, &QDialogButtonBox::rejected, &optsDialog, &QDialog::reject);
        optsLayout->addWidget(optsButtons);
        if (optsDialog.exec() != QDialog::Accepted) {
            return;
        }
        ui_memory::setBoolValue(QStringLiteral("autoDigitize/advancedOpen"),
                                advancedToggle->isChecked());
        ui_memory::setIntValue(QStringLiteral("autoDigitize/shapeDetail"),
                               shapeDetailSlider->value());
        ui_memory::setIntValue(QStringLiteral("autoDigitize/contourDetail"), detailSlider->value());
        ui_memory::setIntValue(QStringLiteral("autoDigitize/maxThreads"), maxThreadsSpin->value());
        const auto remember = [](const char* key, int value) {
            ui_memory::setIntValue(QString::fromLatin1(key), value);
        };
        skipLargest = skipBgCheck->isChecked();
        ds.skip_background = !skipLargest ? 0 : (skipBgOuterCheck->isChecked() ? 2 : 1);
        ds.max_threads = static_cast<std::size_t>(maxThreadsSpin->value());
        ds.satin_bands = satinBandsCheck->isChecked();
        remember("autoDigitize/satinBands", ds.satin_bands ? 1 : 0);
        ds.simplify_tolerance_um =
            vectorize_tolerance_from_detail(shapeDetailSlider->value()).value;
        const int overlapUm = static_cast<int>(std::lround(overlapSpin->value() * 1000.0));
        remember("autoDigitize/fillOverlapUm", overlapUm);
        ds.fill_overlap_mm = overlapUm / 1000.0;
        const int featureUm = static_cast<int>(std::lround(featureSpin->value() * 1000.0));
        remember("autoDigitize/minFeatureWidthUm", featureUm);
        ds.min_feature_mm = featureUm / 1000.0;
        const int minAreaCmm2 = static_cast<int>(std::lround(minAreaSpin->value() * 100.0));
        remember("autoDigitize/minRegionAreaCmm2", minAreaCmm2);
        ds.min_region_mm2 = minAreaCmm2 / 100.0;
        const int islandCmm2 = static_cast<int>(std::lround(islandSpin->value() * 100.0));
        remember("autoDigitize/islandAreaCmm2", islandCmm2);
        ds.island_cleanup_mm2 = islandCmm2 / 100.0;
        const int islandDe = static_cast<int>(std::lround(islandDeSpin->value()));
        remember("autoDigitize/islandMaxDeltaE", islandDe);
        ds.island_max_delta_e = islandDe;
        const int curveUm = static_cast<int>(std::lround(curveSpin->value() * 1000.0));
        remember("autoDigitize/curveToleranceUm", curveUm);
        ds.curve_tolerance_mm = curveUm / 1000.0;
        ds.solid_block = solidBlockCheck->isChecked();
        remember("autoDigitize/solidBlock", ds.solid_block ? 1 : 0);
        ds.flow_fill = flowFillCheck->isChecked();
        remember("autoDigitize/flowFill", ds.flow_fill ? 1 : 0);
        ds.dark_lines = darkLinesCheck->isChecked();
        remember("autoDigitize/darkLines", ds.dark_lines ? 1 : 0);
        const int darkWidthUm = static_cast<int>(std::lround(darkWidthSpin->value() * 1000.0));
        remember("autoDigitize/darkLinesWidthUm", darkWidthUm);
        ds.dark_lines_width_mm = darkWidthUm / 1000.0;
        ds.dark_lines_lightness = darkLightSpin->value();
        remember("autoDigitize/darkLinesLightness",
                 static_cast<int>(std::lround(darkLightSpin->value())));
        const int darkMinLenUm = static_cast<int>(std::lround(darkMinLenSpin->value() * 1000.0));
        remember("autoDigitize/darkLinesMinLenUm", darkMinLenUm);
        ds.dark_lines_min_length_mm = darkMinLenUm / 1000.0;
        ds.dark_lines_objects_only = darkObjectsOnlyCheck->isChecked();
        remember("autoDigitize/darkLinesObjectsOnly", ds.dark_lines_objects_only ? 1 : 0);
        ds.rings_satin = ringsSatinCheck->isChecked();
        remember("autoDigitize/ringsSatin", ds.rings_satin ? 1 : 0);
        contoursMode = contoursRadio->isChecked();
        stippleMode = stippleRadio->isChecked();
        ds.mode = contoursMode ? "contours" : stippleMode ? "stipple" : "shapes";
        ds.detail = static_cast<double>(detailSlider->value()) / 100.0;
        ds.technique = techRunningRadio->isChecked() ? "running" : "auto";
        ds.stipple_points = stipplePointsSpin->value();
        ds.stipple_seed = stippleSeedSpin->value();
        ds.stipple_invert = stippleInvertCheck->isChecked();
        remember("autoDigitize/stipplePoints", ds.stipple_points);
        remember("autoDigitize/stippleSeed", ds.stipple_seed);
        remember("autoDigitize/stippleInvert", ds.stipple_invert ? 1 : 0);
        opts = project_ops::make_auto_options(ds, project_.mm_per_px, skipLargest);
    }

    // Le calcul asynchrone ne doit pas avancer le compteur du projet tant que le
    // résultat n'est pas appliqué. Un clic Annuler refuse le lot entier (l'arrêt
    // CPU coopératif demandera une évolution des API project_ops/autodigitize).
    auto nextIds = project_.object_ids;
    std::optional<BusyIndicator> busy;
    busy.emplace(this, tr("Numérisation automatique en cours…"),
                 std::initializer_list<QTimer*>{autosaveTimer_, simTimer_}, true);
    std::optional<autodigitize::ContourMetrics> contourMetrics;
    std::size_t flowFillCount = 0;
    auto result = busy->run([&]() -> Result<autodigitize::AutoResult> {
        if (!hasSegmentation) {
            return autodigitize::auto_digitize_vectors(project_.vector_objects, nextIds, opts);
        }
        // Même image que la segmentation (opérations appliquées) si les dimensions concordent,
        // sinon l'image d'origine (traits sombres et pointillé s'y appuient).
        const image::Image& workImage = processed_.width == project_.segmentation->width &&
                                                processed_.height == project_.segmentation->height
                                            ? processed_
                                            : project_.original;
        const project_ops::DigitizeSource source{&*project_.segmentation, &workImage,
                                                 project_.mm_per_px, &project_.detail_mask};
        auto outcome = project_ops::digitize_objects(source, nextIds, ds, skipLargest);
        if (!outcome) {
            return std::unexpected(outcome.error());
        }
        contourMetrics = outcome->contour;
        flowFillCount = outcome->flow_fill;
        return std::move(outcome->objects);
    });
    const bool cancelled = busy->cancelled();
    busy.reset();
    if (cancelled) {
        statusBar()->showMessage(tr("Numérisation annulée : aucune modification."), 6000);
        return;
    }
    if (!result) {
        showFriendlyError(this, tr("Numérisation impossible"),
                          tr("L'image n'a pas pu être numérisée automatiquement."),
                          result.error().message);
        return;
    }
    const std::size_t vecCount = result->vectors.size();
    const std::size_t embCount = result->embroideries.size();
    const std::vector<std::string> warnings = std::move(result->warnings);

    project_.object_ids = nextIds;
    auto batch = std::make_unique<commands::AddObjectBatchCommand>(std::move(result->vectors),
                                                                   std::move(result->embroideries));
    if (replacePrevious) {
        // Une seule entrée d'annulation : retraits des objets auto-créés puis
        // ajout du nouveau lot (revert dans l'ordre inverse).
        auto composite =
            std::make_unique<commands::CompositeCommand>("Numérisation automatique (remplacement)");
        std::set<ObjectId> keptSources;
        for (const auto& emb : project_.embroidery_objects) {
            const bool removable = emb.intent == document::EmbroideryIntent::AutoChoice &&
                                   emb.overrides.empty() && !emb.locked;
            if (removable) {
                composite->add(std::make_unique<commands::RemoveEmbroideryObjectCommand>(emb.id));
            } else {
                keptSources.insert(emb.source_vector);
            }
        }
        for (const auto& vec : project_.vector_objects) {
            if (vec.source_region && keptSources.count(vec.id) == 0) {
                composite->add(std::make_unique<commands::RemoveVectorObjectCommand>(vec.id));
            }
        }
        composite->add(std::move(batch));
        undoStack_.execute(std::move(composite), project_);
    } else {
        undoStack_.execute(std::move(batch), project_);
    }
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (contoursMode && contourMetrics) {
        // Metriques et avertissements viennent tels quels de la lib (aucune
        // logique metier ici) : resume en barre d'etat, details en dialogue.
        announceAutoDigitizeResult(
            tr("Contours : %1 objet(s), %2 segment(s), %3 jonction(s) — point droit %4 mm, "
               "%5 repli(s), %6 rejet(s)")
                .arg(embCount)
                .arg(contourMetrics->segments)
                .arg(contourMetrics->junctions)
                .arg(QLocale().toString(contourMetrics->running_length_mm, 'f', 1))
                .arg(contourMetrics->fallbacks)
                .arg(contourMetrics->rejected));
        if (!warnings.empty()) {
            constexpr std::size_t kMaxShown = 12;
            QString text =
                tr("Certains traits n'ont pas pu être cousus tels quels (trop courts ou trop "
                   "dégradés après nettoyage) :\n\n");
            for (std::size_t i = 0; i < warnings.size() && i < kMaxShown; ++i) {
                text += QStringLiteral("• ") + QString::fromStdString(warnings[i]) + "\n";
            }
            if (warnings.size() > kMaxShown) {
                text += tr("… et %1 de plus.").arg(warnings.size() - kMaxShown);
            }
            QMessageBox::warning(this, tr("Contours : avertissements"), text);
        }
        return;
    }
    QString summary =
        tr("Numérisation automatique : %1 objet(s) vectoriel(s), %2 objet(s) de broderie — "
           "tous éditables")
            .arg(vecCount)
            .arg(embCount);
    if (flowFillCount > 0) {
        summary += tr(" (%1 forme(s) en remplissage directionnel)").arg(flowFillCount);
    }
    announceAutoDigitizeResult(summary);
    warnAboutSkippedAutoSatinBranches(warnings);
}

void MainWindow::warnAboutSkippedAutoSatinBranches(const std::vector<std::string>& warnings) {
    if (warnings.empty()) {
        return;
    }
    // Plafonné à l'affichage (comme les autres listes de diagnostics de cette
    // fenêtre) : une image très fragmentée peut produire beaucoup de rejets,
    // une boîte de dialogue interminable serait aussi peu lisible qu'aucun
    // avertissement du tout.
    constexpr std::size_t kMaxShown = 12;
    QString text =
        tr("Le squelette auto-satin a rejeté une portion de forme sur certaines zones "
           "(souvent trop large pour du satin) : elles ont reçu un remplissage tatami de "
           "repli à la place, pour ne rien laisser sans point, mais le résultat mérite "
           "d'être vérifié (aspect, densité) — ou redécoupé manuellement (ligne de coupe, "
           "outil Décaler) puis renumérisé si le tatami ne convient pas.\n\n");
    for (std::size_t i = 0; i < warnings.size() && i < kMaxShown; ++i) {
        text += QStringLiteral("• ") + QString::fromStdString(warnings[i]) + "\n";
    }
    if (warnings.size() > kMaxShown) {
        text += tr("… et %1 de plus.").arg(warnings.size() - kMaxShown);
    }
    QMessageBox::warning(this, tr("Zones remplies en tatami de repli"), text);
}

void MainWindow::warnAboutSkippedSvgFeatures(const std::vector<std::string>& warnings) {
    if (warnings.empty()) {
        return;
    }
    constexpr std::size_t kMaxShown = 12;
    QString text =
        tr("Certains éléments de ce fichier SVG n'ont pas pu être importés (texte, "
           "dégradés, filtres, motifs... — non pris en charge dans cette première version) "
           "ou ont été simplifiés. Le reste du dessin a bien été importé.\n\n");
    for (std::size_t i = 0; i < warnings.size() && i < kMaxShown; ++i) {
        text += QStringLiteral("• ") + QString::fromStdString(warnings[i]) + "\n";
    }
    if (warnings.size() > kMaxShown) {
        text += tr("… et %1 de plus.").arg(warnings.size() - kMaxShown);
    }
    QMessageBox::warning(this, tr("Import SVG partiel"), text);
}

void MainWindow::openAiPreferences() {
    AiPreferencesDialog dialog(this);
    dialog.setPreferences(loadAiPreferences());
    if (dialog.exec() == QDialog::Accepted) {
        saveAiPreferences(dialog.preferences());
    }
}

void MainWindow::openSettings() {
    SettingsDialog dialog(this);
    dialog.setSettings(loadEditSettings());
    const QString previousLanguage = loadLanguagePreference();
    dialog.setLanguage(previousLanguage);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const EditSettings applied = dialog.settings();
    saveEditSettings(applied);
    if (dialog.language() != previousLanguage) {
        saveLanguagePreference(dialog.language());
        QMessageBox::information(this, tr("Langue"),
                                 tr("La nouvelle langue sera appliquée au prochain démarrage."));
    }
    // Les réglages d'action (Unir, Couteau) sont relus à chaque geste ; ceux-ci sont portés par
    // l'état de la fenêtre.
    snapNodesAct_->setChecked(applied.snapNodesOnDrag);
    setRegionMapOpacity(applied.regionMapOpacity);
}

void MainWindow::createSatinObject() {
    // HP-UX-002 : ni dialogue de réglages ni question de confirmation (annulable en un pas).
    createAutoSatin(/*askParameters=*/false, /*confirm=*/false);
}

void MainWindow::createSatinObjectWithSettings() {
    createAutoSatin(/*askParameters=*/true);
}

document::EmbroideryObject* MainWindow::currentFillObject() {
    if (selectedEmbroidery_) {
        if (auto* emb = project_.findEmbroidery(*selectedEmbroidery_);
            emb != nullptr && emb->is_tatami()) {
            return emb;
        }
    }
    if (selectedObject_ && !hasMultiSelection()) {
        for (auto& emb : project_.embroidery_objects) {
            if (emb.source_vector == *selectedObject_ && emb.is_tatami()) {
                return &emb;
            }
        }
    }
    return nullptr;
}

std::optional<ObjectId> MainWindow::objectAt(QPointF posMm) const {
    std::optional<ObjectId> hit;
    for (const auto& object : project_.vector_objects) {
        if (object.visible && calqueShowsShape(object) && !project_.shapeEditLocked(object.id) &&
            objectPainterPath(object).contains(posMm)) {
            hit = object.id; // le dernier dessiné (au-dessus) gagne
        }
    }
    return hit;
}

document::EmbroideryObject* MainWindow::satinEmbroideryAt(QPointF posMm) {
    document::EmbroideryObject* hit = nullptr;
    for (auto& emb : project_.embroidery_objects) {
        if (!emb.visible || !emb.is_satin()) {
            continue;
        }
        const auto& satin = std::get<document::SatinParams>(emb.params);
        const auto flatA = geometry::flatten(satin.rail_a, Micrometers{100});
        const auto flatB = geometry::flatten(satin.rail_b, Micrometers{100});
        if (flatA.points.empty() || flatB.points.empty()) {
            continue;
        }
        QPainterPath ribbon;
        ribbon.moveTo(modelToSceneMm(flatA.points.front()));
        for (const auto& p : flatA.points) {
            ribbon.lineTo(modelToSceneMm(p));
        }
        for (auto it = flatB.points.rbegin(); it != flatB.points.rend(); ++it) {
            ribbon.lineTo(modelToSceneMm(*it));
        }
        ribbon.closeSubpath();
        if (ribbon.contains(posMm)) {
            hit = &emb; // le dernier de la liste gagne, comme objectAt
        }
    }
    return hit;
}

document::EmbroideryObject* MainWindow::embroideryForVector(ObjectId vectorId) {
    // Une forme à plusieurs couches : un clic au canevas vise celle du DESSUS (la dernière
    // cousue, donc la visible), comme un clic dans un éditeur de calques ; les autres se
    // choisissent dans la liste Document ou le sélecteur des Propriétés.
    document::EmbroideryObject* top = nullptr;
    for (auto& emb : project_.embroidery_objects) {
        if (emb.source_vector == vectorId) {
            top = &emb;
        }
    }
    return top;
}

void MainWindow::autoConvertToSatin() {
    createAutoSatin(false);
}

void MainWindow::changeFillAngle() {
    auto* object = currentFillObject();
    if (object == nullptr) {
        return;
    }
    const auto& tatami = std::get<document::TatamiParams>(object->params);

    // Angle stocké en radians ; présenté en degrés. C'est l'orientation d'une
    // droite, donc modulo 180° (0° = rangées horizontales).
    const double currentDeg = tatami.angle.radians * 180.0 / std::numbers::pi;
    int startDeg = ((static_cast<int>(std::lround(currentDeg)) % 180) + 180) % 180;

    bool ok = false;
    const int deg = QInputDialog::getInt(this, tr("Orientation du remplissage"),
                                         tr("Angle des fils (°) :"), startDeg, 0, 179, 1, &ok);
    if (!ok) {
        return;
    }

    const Angle next{deg * std::numbers::pi / 180.0};
    undoStack_.execute(std::make_unique<commands::SetFillAngleCommand>(object->id, next), project_);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(
            tr("Orientation du remplissage : %1° — %2 points").arg(deg).arg(stats.stitches));
    }
}

void MainWindow::convertSatinsToTatami() {
    std::vector<ObjectId> satins;
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.is_satin()) {
            satins.push_back(emb.id);
        }
    }
    if (satins.empty()) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Convertir en tatami"),
        tr("%1 colonne(s) satin automatique(s) vont devenir des remplissages tatami "
           "(découpés sur leur région, sans débordement). L'orientation restera réglable. "
           "Continuer ?")
            .arg(satins.size()));
    if (answer != QMessageBox::Yes) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::ConvertFillsToTatamiCommand>(std::move(satins)),
                       project_);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("Satins convertis en tatami : %1 points").arg(stats.stitches));
    }
}

} // namespace openstitch::desktop
