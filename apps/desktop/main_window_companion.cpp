// SPDX-License-Identifier: Apache-2.0
// Dock compagnon (Claude Code, analyse sémantique, pont live).
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

void MainWindow::buildCompanionDock() {
    companionDock_ = new CompanionDock(this);
    addDockWidget(Qt::RightDockWidgetArea, companionDock_);
    tabifyDockWidget(analysisDock_, companionDock_);
    secretStore_ = makeSystemSecretStore();
    claudeBackend_ = new ClaudeCompanionBackend(liveService_, *secretStore_, {}, this);
    claudeBackend_->setDiagnostics(&companionLog_);
    companionLog_.set_enabled(companion_settings::diagnosticsEnabled());
    companionLog_.set_content_consent(companion_settings::diagnosticsEnabled() &&
                                      companion_settings::diagnosticsContentConsent());
    companionDock_->setBackend(claudeBackend_);
    connect(companionDock_, &CompanionDock::configureProviderRequested, this,
            &MainWindow::showCompanionSettings);
    companionDock_->hide(); // masqué par défaut : n'occupe aucune place tant qu'on ne l'ouvre pas
    // Raccourci configurable : réglage « shortcuts/companionDock » (séquence Qt), Ctrl+Maj+J par
    // défaut ; vide = aucun raccourci.
    const QString keys =
        QSettings()
            .value(QStringLiteral("shortcuts/companionDock"), QStringLiteral("Ctrl+Shift+J"))
            .toString();
    QAction* toggle = companionDock_->toggleViewAction();
    toggle->setObjectName(QStringLiteral("action_companionDock"));
    toggle->setShortcut(QKeySequence(keys));
    toggle->setToolTip(tr("Affiche ou masque le compagnon IA (%1)").arg(keys));
    toggle->setStatusTip(toggle->toolTip());
    panelsMenu_->addAction(toggle);
    connect(companionDock_, &CompanionDock::focusObjectRequested, this,
            [this](quint64 id) { focusEmbroideryObject(id); });
    connect(companionDock_, &CompanionDock::undoLastRequested, this, &MainWindow::undo);
    connect(companionDock_, &CompanionDock::enableLiveBridgeRequested, this, [this] {
        if (liveBridgeAct_ != nullptr) {
            liveBridgeAct_->setChecked(true);
        }
    });
    connect(companionDock_, &CompanionDock::openClaudeCodeRequested, this,
            &MainWindow::openClaudeCode);
    connect(companionDock_, &CompanionDock::useClaudeCodeInDockRequested, this,
            &MainWindow::useClaudeCodeInDock);
    // Les approbations de l'agent s'affichent dans le dock (boîte modale seulement en repli).
    liveGate_.setChoiceDelegate([this](const mcp::ChoiceRequest& request) {
        companionDock_->show();
        companionDock_->raise();
        QStringList labels;
        QList<quint64> focus;
        for (const auto& option : request.options) {
            labels << QString::fromStdString(option.label);
            focus << (option.object_ids.empty() ? 0 : option.object_ids.front());
        }
        return companionDock_->askChoice(QString::fromStdString(request.question), labels, focus);
    });
    liveGate_.setDelegate([this](const mcp::ApprovalRequest& request) {
        companionDock_->show();
        companionDock_->raise();
        CompanionProposal proposal;
        proposal.title = tr("Changement proposé par un agent IA");
        proposal.summary = tr("« %1 » : %n opération(s). Annulable en une étape (Ctrl+Z).", "",
                              static_cast<int>(request.items.size()))
                               .arg(QString::fromStdString(request.label));
        for (const auto& item : request.items) {
            proposal.items.push_back({item.object_id, QString::fromStdString(item.text)});
        }
        proposal.beforePng = QByteArray::fromStdString(request.before_png);
        proposal.afterPng = QByteArray::fromStdString(request.after_png);
        QStringList metrics;
        for (const auto& metric : request.metrics) {
            metrics << QString::fromStdString(metric);
        }
        if (!metrics.isEmpty()) {
            proposal.summary += QStringLiteral("\n") + metrics.join(QStringLiteral("\n"));
        }
        for (const auto& warning : request.warnings) {
            proposal.warnings << QString::fromStdString(warning);
        }
        // Mode fantôme : pendant la décision, les objets concernés sont mis en évidence sur le
        // canevas (halo de sélection existant) ; la sélection de l'utilisateur est restaurée après.
        const Selection saved = currentSelection();
        std::vector<quint64> ids;
        for (const auto& item : request.items) {
            if (item.object_id != 0) {
                ids.push_back(item.object_id);
            }
        }
        showGhostSelection(ids);
        const bool approved = companionDock_->askApproval(proposal);
        restoreGhostSelection(saved);
        return approved;
    });
}

void MainWindow::showGhostSelection(const std::vector<quint64>& embroideryIds) {
    Selection ghost;
    for (const quint64 id : embroideryIds) {
        ObjectId target;
        target.value = id;
        if (const auto* e = project_.findEmbroidery(target)) {
            if (project_.findObject(e->source_vector) != nullptr) {
                ghost.objects.push_back(e->source_vector);
            }
        }
    }
    if (ghost.objects.empty()) {
        return;
    }
    setSelection(ghost);
    refreshImage();
    updateActions();
}

void MainWindow::restoreGhostSelection(Selection saved) {
    // Les objets n'existent peut-être plus (modification appliquée) : seuls les présents sont
    // gardés.
    std::vector<ObjectId> kept;
    for (const ObjectId id : saved.objects) {
        if (project_.findObject(id) != nullptr) {
            kept.push_back(id);
        }
    }
    saved.objects = std::move(kept);
    if (saved.embroidery && project_.findEmbroidery(*saved.embroidery) == nullptr) {
        saved.embroidery.reset();
    }
    setSelection(saved);
    refreshImage();
    updateActions();
}

void MainWindow::updateCompanionContext() {
    if (companionDock_ == nullptr) {
        return;
    }
    const auto info = liveDocumentInfo();
    companionDock_->setContext(QString::fromStdString(info.name),
                               static_cast<int>(info.selection.size()), hasDocumentContent());
    companionDock_->setLiveBridgeActive(liveServer_ != nullptr);
    companionDock_->setUndoAvailable(undoStack_.canUndo(),
                                     QString::fromStdString(undoStack_.undoName()));
}

void MainWindow::focusEmbroideryObject(quint64 id) {
    ObjectId target;
    target.value = id;
    for (const auto& e : project_.embroidery_objects) {
        if (e.id == target) {
            Selection selection;
            selection.embroidery = target;
            setSelection(selection);
            refreshImage();
            updateActions();
            return;
        }
    }
}

void MainWindow::showCompanionSettings() {
    CompanionSettingsDialog dialog(*secretStore_, companionLog_, this);
    if (dialog.exec() == QDialog::Accepted) {
        applyCompanionSettings();
    }
}

void MainWindow::useClaudeCodeInDock() {
    if (companion_settings::fullyDisabled()) {
        QMessageBox::information(this, tr("Claude Code"),
                                 tr("Le compagnon IA est desactive dans les preferences."));
        return;
    }
    QString executable;
    QString workspace;
    const auto ready = ClaudeCodeLauncher::prepare(launcherEnv_, executable, workspace);
    if (ready != ClaudeCodeLauncher::Result::Started) {
        QMessageBox::warning(this, tr("Claude Code"), ClaudeCodeLauncher::describe(ready));
        return;
    }
    if (liveBridgeAct_ != nullptr && !liveBridgeAct_->isChecked()) {
        liveBridgeAct_->setChecked(true);
    }
    if (liveServer_ == nullptr) {
        QMessageBox::warning(this, tr("Claude Code"), tr("Le pont MCP local n'a pas pu demarrer."));
        return;
    }
    // Un seul processus Claude Code par session de conversation : changement de mode explicite.
    if (codeBackend_ != nullptr) {
        codeBackend_->resetConversation();
        codeBackend_->deleteLater();
    }
    codeBackend_ = new ClaudeCodeProcessBackend(executable, workspace, this);
    companionDock_->setBackend(codeBackend_);
    companionDock_->show();
    companionDock_->raise();
    maybeStartSemanticAnalysis();
}

void MainWindow::maybeStartSemanticAnalysis() {
    // Une seule fois par document ouvert, uniquement sur un projet non vide, et si l'utilisateur
    // ne l'a pas désactivée (Aide ▸ Compagnon IA…).
    if (!companion_settings::autoSemanticAnalysis() || project_.embroidery_objects.empty() ||
        semanticAnalysisGeneration_ == documentGeneration_) {
        return;
    }
    bool allCached = true;
    for (const auto& object : project_.embroidery_objects) {
        const auto* shape = project_.findObject(object.source_vector);
        if (!shape || !object.semantic ||
            object.semantic->geometry_fingerprint !=
                document::semantic_geometry_fingerprint(*shape)) {
            allCached = false;
            break;
        }
    }
    if (allCached) {
        semanticAnalysisGeneration_ = documentGeneration_;
        return; // zéro appel modèle : tout est déjà connu dans le .osp
    }
    const QString prompt =
        tr("Lis live_context : réutilise les objets semantic.status=cached. "
           "Examine seulement ceux sans semantic ou avec semantic.status=stale. "
           "Utilise project_render si nécessaire et live_object_views sur au plus 5 lots "
           "de 8 objets à analyser, en priorité les plus importants de chaque fil. "
           "Laisse les objets incertains sans annotation. "
           "Propose set_semantic (role, family, confidence), live_preview puis "
           "project_apply avec mon approbation. Ne renomme pas les objets.");
    if (companionDock_->sendProgrammatic(tr("Analyse sémantique automatique du projet…"), prompt)) {
        semanticAnalysisGeneration_ = documentGeneration_;
    }
}

void MainWindow::openClaudeCode() {
    if (companion_settings::fullyDisabled()) {
        QMessageBox::information(this, tr("Claude Code"),
                                 tr("Le compagnon IA est désactivé (Aide ▸ Compagnon IA…)."));
        return;
    }
    // Retour a l'etat initial si la CLI n'est pas installee ou ne demarre pas.
    const bool wasActive = liveBridgeAct_ != nullptr && liveBridgeAct_->isChecked();
    if (liveBridgeAct_ != nullptr && !wasActive) {
        liveBridgeAct_->setChecked(true);
    }
    const auto result = ClaudeCodeLauncher::launch(launcherEnv_);
    if (result != ClaudeCodeLauncher::Result::Started) {
        if (liveBridgeAct_ != nullptr && !wasActive) {
            liveBridgeAct_->setChecked(false);
        }
        QMessageBox::warning(this, tr("Claude Code"), ClaudeCodeLauncher::describe(result));
        return;
    }
    statusBar()->showMessage(tr("Claude Code ouvert dans un terminal."), 6000);
}

void MainWindow::applyCompanionSettings() {
    const bool off = companion_settings::fullyDisabled();
    if (off) {
        if (liveBridgeAct_ != nullptr) {
            liveBridgeAct_->setChecked(false);
        }
        setLiveBridgeEnabled(false);
    }
    if (liveBridgeAct_ != nullptr) {
        liveBridgeAct_->setEnabled(!off);
    }
    if (claudeBackend_ != nullptr) {
        claudeBackend_->refresh();
    }
    // Valider les preferences de l'API revient explicitement au fournisseur API.
    if (companionDock_->backend() != claudeBackend_) {
        if (codeBackend_ != nullptr) {
            codeBackend_->resetConversation();
        }
        companionDock_->setBackend(claudeBackend_);
    }
    companionDock_->setFullyDisabled(off);
    companionDock_->refresh();
}

bool MainWindow::setLiveBridgeEnabled(bool on, const std::filesystem::path& dir, QString* error) {
    if (!on) {
        if (liveServer_) {
            // Invalider les calculs MCP deja en cours, y compris pendant une approbation.
            // Un agent ne doit jamais pouvoir appliquer apres la desactivation du pont.
            if (!liveBridge_.closed()) {
                liveBridge_.reset();
            }
            liveServer_->stop();
            liveServer_.reset();
        }
        return true;
    }
    if (liveServer_) {
        return true;
    }
    auto server = std::make_unique<LiveServer>(liveService_, this);
    if (!server->start(dir.empty() ? mcp::live_directory() : dir, error)) {
        return false;
    }
    liveServer_ = std::move(server);
    return true;
}

mcp::LiveDocumentInfo MainWindow::liveDocumentInfo() const {
    mcp::LiveDocumentInfo info;
    // Nom seul, jamais le chemin complet : rien de plus que nécessaire n'est transmis à l'agent.
    info.name = currentProjectPath_.isEmpty()
                    ? std::string("(sans titre)")
                    : QFileInfo(currentProjectPath_).fileName().toStdString();
    info.dirty = isWindowModified();
    info.never_saved = currentProjectPath_.isEmpty();
    for (const ObjectId id : selectedEmbroideryIds()) {
        info.selection.push_back(id.value);
    }
    return info;
}

} // namespace openstitch::desktop
