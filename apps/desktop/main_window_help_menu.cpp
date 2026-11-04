// SPDX-License-Identifier: Apache-2.0
// Menu d'aide, navigation et contexte d'interaction.
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
#include "crash_report_ticket.hpp"
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

void MainWindow::buildHelpMenu() {

    auto* helpMenu = menuBar()->addMenu(tr("Aid&e"));
    helpMenu->setObjectName(QStringLiteral("menu_help"));

    helpQuickStartAct_ = helpMenu->addAction(tr("&Guide de prise en main"));
    helpQuickStartAct_->setObjectName(QStringLiteral("action_help_quickstart"));
    helpQuickStartAct_->setToolTip(
        tr("Les six étapes de l'image au fichier DST, avec un bouton par commande"));
    helpQuickStartAct_->setStatusTip(
        tr("Guide de prise en main : de l'image au fichier DST en six étapes"));
    connect(helpQuickStartAct_, &QAction::triggered, this, &MainWindow::showQuickStartDialog);

    liveBridgeAct_ = helpMenu->addAction(tr("Pont IA &local (MCP)"));
    liveBridgeAct_->setObjectName(QStringLiteral("action_live_bridge"));
    liveBridgeAct_->setCheckable(true);
    liveBridgeAct_->setToolTip(
        tr("Autorise un agent IA local (openstitch-mcp --live) à lire le projet ouvert et à "
           "proposer des modifications. Aucun port réseau ; chaque modification demande votre "
           "approbation et s'annule en une étape. Désactivé par défaut."));
    liveBridgeAct_->setStatusTip(liveBridgeAct_->toolTip());
    liveBridgeAct_->setChecked(QSettings().value(QStringLiteral("ai/liveBridge"), false).toBool());
    connect(liveBridgeAct_, &QAction::toggled, this, [this](bool on) {
        QString error;
        if (!setLiveBridgeEnabled(on, {}, &error)) {
            QSignalBlocker block(liveBridgeAct_);
            liveBridgeAct_->setChecked(false);
            statusBar()->showMessage(tr("Pont IA local indisponible : %1").arg(error), 8000);
            return;
        }
        QSettings().setValue(QStringLiteral("ai/liveBridge"), on);
    });
    if (liveBridgeAct_->isChecked()) {
        QTimer::singleShot(0, this, [this] {
            QString error;
            if (!setLiveBridgeEnabled(true, {}, &error)) {
                QSignalBlocker block(liveBridgeAct_);
                liveBridgeAct_->setChecked(false);
            }
        });
    }

    companionSettingsAct_ = helpMenu->addAction(tr("Compagnon &IA…"));
    companionSettingsAct_->setObjectName(QStringLiteral("action_companion_settings"));
    companionSettingsAct_->setToolTip(
        tr("Fournisseur Claude, clé API (stockage sécurisé du système), données transmises, "
           "désactivation du compagnon."));
    companionSettingsAct_->setStatusTip(companionSettingsAct_->toolTip());
    connect(companionSettingsAct_, &QAction::triggered, this, &MainWindow::showCompanionSettings);

    openClaudeCodeAct_ = helpMenu->addAction(tr("Ouvrir Claude &Code…"));
    openClaudeCodeAct_->setObjectName(QStringLiteral("action_open_claude_code"));
    openClaudeCodeAct_->setToolTip(
        tr("Ouvre votre Claude Code dans un terminal, relié au projet ouvert (pont IA local, "
           "consignes et skills OpenStitch préparés). Claude Code doit être installé."));
    openClaudeCodeAct_->setStatusTip(openClaudeCodeAct_->toolTip());
    connect(openClaudeCodeAct_, &QAction::triggered, this, &MainWindow::openClaudeCode);

    helpGesturesAct_ = helpMenu->addAction(tr("Gestes &souris et clavier"));
    helpGesturesAct_->setObjectName(QStringLiteral("action_help_gestures"));
    helpGesturesAct_->setShortcut(QKeySequence(Qt::Key_F1));
    helpGesturesAct_->setToolTip(tr("Tous les gestes de la souris et les raccourcis clavier (F1)"));
    helpGesturesAct_->setStatusTip(
        tr("Tableau consultable des gestes souris et des raccourcis clavier"));
    connect(helpGesturesAct_, &QAction::triggered, this, &MainWindow::showGesturesDialog);

    helpTourAct_ = helpMenu->addAction(tr("Tutoriel : de l'image à la broderie…"));
    helpTourAct_->setObjectName(QStringLiteral("action_help_tutorial"));
    helpTourAct_->setToolTip(
        tr("Ouvre une image d'exemple et déroule le guide pas à pas jusqu'au fichier de broderie"));
    helpTourAct_->setStatusTip(
        tr("Tutoriel guidé avec une image d'exemple générée par le logiciel"));
    connect(helpTourAct_, &QAction::triggered, this, &MainWindow::startGuidedTour);

    helpManualAct_ = helpMenu->addAction(tr("Manuel d'utilisation"));
    helpManualAct_->setObjectName(QStringLiteral("action_help_manual"));
    helpManualAct_->setToolTip(tr("Le manuel complet, avec recherche, sans quitter le logiciel"));
    helpManualAct_->setStatusTip(tr("Manuel d'utilisation consultable dans l'application"));
    connect(helpManualAct_, &QAction::triggered, this, [this] { showManual(); });

    helpContextAct_ = helpMenu->addAction(tr("Aide sur l'outil ou le panneau actif"));
    helpContextAct_->setObjectName(QStringLiteral("action_help_context"));
    helpContextAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F1));
    helpContextAct_->setToolTip(
        tr("Ouvre le manuel à la section de l'outil actif ou du panneau où vous travaillez "
           "(Maj+F1)"));
    helpContextAct_->setStatusTip(tr("Aide contextuelle : section du manuel de l'outil actif"));
    connect(helpContextAct_, &QAction::triggered, this, &MainWindow::showContextualHelp);

    aboutAct_ = helpMenu->addAction(tr("À &propos"));
    aboutAct_->setObjectName(QStringLiteral("action_about"));
    aboutAct_->setToolTip(tr("Version, licence et code source"));
    aboutAct_->setStatusTip(tr("Version, licence et code source d'OpenStitch"));
    connect(aboutAct_, &QAction::triggered, this,
            [this] { QMessageBox::about(this, tr("À propos"), aboutText()); });

    installCrashReportAction(this, helpMenu); // HP-QA-002
}

void MainWindow::showGesturesDialog() {
    if (gesturesDialog_ != nullptr) {
        gesturesDialog_->refresh();
        gesturesDialog_->show();
        gesturesDialog_->raise();
        gesturesDialog_->activateWindow();
        return;
    }
    // Tas + WA_DeleteOnClose : non modal, une seule instance (QPointer).
    auto* dialog = new GesturesDialog(this, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &GesturesDialog::presetChanged, this, [this](Preset preset) {
        // Le dialogue a déjà appliqué et enregistré le préréglage : on aligne le menu.
        QAction* act = preset == Preset::Touchpad ? navTouchpadAct_ : navOpenStitchAct_;
        if (act != nullptr) {
            act->setChecked(true);
        }
        refreshHints();
    });
    gesturesDialog_ = dialog;
    dialog->show();
}

void MainWindow::showQuickStartDialog() {
    if (quickStartDialog_ != nullptr) {
        quickStartDialog_->show();
        quickStartDialog_->raise();
        quickStartDialog_->activateWindow();
        return;
    }
    // Étapes construites à partir des QAction MEMBRES (libellés non dupliqués) ;
    // les textes viennent du dialogue.
    std::vector<QuickStartStep> steps = QuickStartDialog::defaultSteps();
    const std::vector<QAction*> primary = {openImageAct_,    segmentAct_, vectorizeRegionAct_,
                                           createTatamiAct_, analyzeAct_, exportDstAct_};
    for (std::size_t i = 0; i < steps.size() && i < primary.size(); ++i) {
        steps[i].action = primary[i];
        steps[i].informative = false;
    }
    if (steps.size() > 2) {
        steps[2].actionNames = {QStringLiteral("action_autoDigitize")};
    }
    if (steps.size() > 3) {
        steps[3].actionNames = {QStringLiteral("action_createStitch")};
    }
    auto* dialog = new QuickStartDialog(this, std::move(steps), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    quickStartDialog_ = dialog;
    dialog->show();
}

void MainWindow::buildNavigationMenu(QMenu* viewMenu) {
    // Préréglage persistant (QSettings « navigation/preset »), appliqué avant de
    // cocher l'entrée correspondante.
    InteractionMap::loadPreset();
    auto* navMenu = viewMenu->addMenu(tr("&Navigation"));
    navMenu->setObjectName(QStringLiteral("menu_navigation"));
    navMenu->setToolTipsVisible(true);
    navigationGroup_ = new QActionGroup(this);
    navigationGroup_->setExclusive(true);
    const auto addPreset = [&](const QString& label, const QString& objectName, const QString& tip,
                               Preset preset) {
        auto* act = navMenu->addAction(label);
        act->setObjectName(objectName);
        act->setCheckable(true);
        act->setChecked(InteractionMap::preset() == preset);
        act->setToolTip(tip);
        act->setStatusTip(tip);
        navigationGroup_->addAction(act);
        connect(act, &QAction::triggered, this, [this, preset] { applyNavigationPreset(preset); });
        return act;
    };
    navOpenStitchAct_ =
        addPreset(tr("&OpenStitch"), QStringLiteral("navPresetOpenStitch"),
                  tr("Souris à trois boutons : clic molette = panoramique, molette = zoom"),
                  Preset::OpenStitch);
    navTouchpadAct_ =
        addPreset(tr("Pavé &tactile"), QStringLiteral("navPresetTouchpad"),
                  tr("Sans clic molette : Espace + glisser = panoramique, Ctrl + molette = zoom"),
                  Preset::Touchpad);
}

void MainWindow::applyNavigationPreset(Preset preset) {
    InteractionMap::setPreset(preset);
    InteractionMap::savePreset();
    QAction* act = preset == Preset::Touchpad ? navTouchpadAct_ : navOpenStitchAct_;
    if (act != nullptr && !act->isChecked()) {
        act->setChecked(true);
    }
    refreshHints();
    if (gesturesDialog_ != nullptr) {
        gesturesDialog_->refresh();
    }
}

Context MainWindow::interactionContext() const {
    // Même priorité que CanvasView::currentContext : un outil de dessin / recadrage prime sur les
    // modes d'édition, l'outil Déplacer la vue est toujours Pan ; seuls les modes d'édition de
    // nœuds / de points passent devant l'outil Sélection.
    if (currentTool_ == Tool::Pan) {
        return Context::Pan;
    }
    if (currentTool_ != Tool::Select) {
        return InteractionMap::contextFor(currentTool_, false, false);
    }
    const bool nodeEdit =
        (satinEditModeAct_ != nullptr && satinEditModeAct_->isChecked()) ||
        (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked()) ||
        (railEditModeAct_ != nullptr && railEditModeAct_->isChecked()) ||
        (directionGuideModeAct_ != nullptr && directionGuideModeAct_->isChecked()) ||
        (gradientEditModeAct_ != nullptr && gradientEditModeAct_->isChecked());
    const bool stitchEdit = stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked();
    return InteractionMap::contextFor(Tool::Select, nodeEdit, stitchEdit);
}

void MainWindow::updateInteractionContext() {
    if (view_ == nullptr) {
        return;
    }
    // Le canevas dérive lui-même Crop/Draw* de ses booléens de mode : on ne lui donne que le
    // contexte de base (Select, Pan, NodeEdit, StitchEdit) ; pour les autres outils : Select.
    const Context logical = interactionContext();
    const bool baseContext = logical == Context::Select || logical == Context::Pan ||
                             logical == Context::NodeEdit || logical == Context::StitchEdit;
    view_->setBaseContext(baseContext ? logical : Context::Select);
    view_->setSelectionRectangleEnabled(currentTool_ == Tool::Select);
    refreshHints();
}

} // namespace openstitch::desktop
