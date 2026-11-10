// SPDX-License-Identifier: Apache-2.0
#include "shell_window.hpp"
#include "direction_guides.hpp"
#include "display_filters.hpp"
#include "file_io.hpp"
#include "floating_action_bar.hpp"
#include "radial_menu_widget.hpp"
#include "render_quality_dialog.hpp"
#include "simulation_nav.hpp"
#include "spool_rail_widget.hpp"
#include "studio_theme.hpp"
#include "thread_palette_service.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <map>
#include <utility>

#include "openstitch/commands/placement_ops.hpp"

#include "action_dialog.hpp"
#include "analysis_overlays.hpp"
#include "calibration_dialog.hpp"
#include "calque_panel.hpp"
#include "chat_panel.hpp"
#include "command_palette.hpp"
#include "companion_panel.hpp"
#include "diagnostics_panel.hpp"
#include "export_options_dialog.hpp"
#include "export_settings.hpp"
#include "feedback_cue.hpp"
#include "import_dialog.hpp"
#include "inspector_model.hpp"
#include "inspector_panel.hpp"
#include "live_server.hpp"
#include "node_edit_controller.hpp"
#include "object_labels.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "optimize_panel.hpp"
#include "preview_canvas.hpp"
#include "preview_service.hpp"
#include "pull_calibration.hpp"
#include "region_edit_controller.hpp"
#include "region_panel.hpp"
#include "satin_column.hpp"
#include "segmentation_preview.hpp"
#include "thread_palette_dialog.hpp"
#include "thread_panel.hpp"
#include "tool_icon.hpp"
#include "transform_batch.hpp"
#include "welcome_page.hpp"
#include <QComboBox>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyleHints>

namespace openstitch::desktop_v2 {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

// Outils réellement migrés : les autres sont déclarés dans le registre mais désactivés.
bool isToolImplemented(const std::string& id) {
    return id == "select" || id == "pan" || id == "draw.rectangle" || id == "draw.ellipse" ||
           id == "draw.polygon" || id == "measure" || id == "eyedropper" || id == "knife" ||
           id == "click-to-stitch" || id == "detail-brush" || id == "stitch.points" ||
           id == "satin.column" || id == "node.edit" || id == "fade.axis" || id == "draw.bezier" ||
           id == "draw.freeform" || id == "region.edit" || id == "region.brush" ||
           id == "fill.direction-guide" || id == "fill.break-line";
}

// Ordre d'affichage ET des raccourcis Ctrl+1..5.
constexpr Workspace kWorkspaces[] = {Workspace::Design, Workspace::Digitize, Workspace::Zones,
                                     Workspace::Optimize, Workspace::Produce};

QString workspaceLabel(Workspace w) {
    switch (w) {
    case Workspace::Design:
        return QObject::tr("Formes");
    case Workspace::Zones:
        return QObject::tr("Zones");
    case Workspace::Digitize:
        return QObject::tr("Numérisation");
    case Workspace::Optimize:
        return QObject::tr("Optimisation");
    case Workspace::Produce:
        return QObject::tr("Production");
    }
    return {};
}

QString stitchTypeText(const document::EmbroideryObject& object) {
    const auto id = stitchTypeId(object);
    if (id == "running")
        return QObject::tr("Contour");
    if (id == "tatami")
        return QObject::tr("Tatami");
    if (id == "satin")
        return QObject::tr("Satin");
    if (id == "auto_satin")
        return QObject::tr("Satin auto");
    if (id == "directional")
        return QObject::tr("Directionnel");
    if (id == "blanket")
        return QObject::tr("Boutonnière");
    if (id == "applique")
        return QObject::tr("Appliqué");
    return QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

// Raccourcis par défaut des commandes qui n'en avaient pas. Jamais de Ctrl+Alt (AltGr sur clavier
// AZERTY tape des caractères) ; Ctrl+Maj+lettre ou touches de fonction, sans doublon (test).
const std::pair<const char*, const char*> kCommandShortcuts[] = {
    {"analysis.run", "F5"},
    {"analysis.density", "F6"},
    {"view.render", "F4"},
    {"view.show-all", "Ctrl+Shift+H"},
    {"view.zoom-in", "Ctrl+="},
    {"view.zoom-out", "Ctrl+-"},
    {"view.zoom-actual", "Ctrl+0"},
    {"view.zoom-selection", "Ctrl+9"},
    {"view.zoom-previous", "Ctrl+Backspace"},
    {"image.resegment", "F3"},
    {"file.import-machine", "Ctrl+Shift+M"},
    {"region.invert", "Ctrl+I"},
    {"region.merge", "Ctrl+M"},
    {"region.regenerate", "Ctrl+R"},
    {"region.select-color", "Ctrl+Shift+O"},
    {"region.select-neighbours", "Ctrl+Shift+N"},
    {"region.objects", "Ctrl+Shift+V"},
    {"region.recolor", "Ctrl+Shift+K"},
    {"analysis.fabric", "F7"},
    {"optimize.order", "F8"},
    {"export.check", "F9"},
    {"export.options", "Ctrl+Shift+E"},
    {"thread.palette", "Ctrl+Shift+P"},
    {"chat.open", "Ctrl+J"},
    {"radial.open", "F2"},
    {"companion.toggle", "Ctrl+Shift+J"},
    {"text.edit", "Ctrl+Shift+X"},
    {"vector.curves", "Ctrl+Shift+B"},
    {"vector.smooth", "Ctrl+Shift+L"},
    {"satin.rung.insert", "Ctrl+Shift+I"},
    {"satin.rung.remove", "Ctrl+Shift+R"},
    {"satin.diagnose", "Ctrl+Shift+D"},
    {"action.satin.auto", "Ctrl+Shift+U"},
    {"action.fill.guides", "Ctrl+Shift+F"},
};

QKeySequence keyFor(const std::string& shortcut) {
    QString s = fromUtf8(shortcut);
    s.replace(QStringLiteral("Maj+"), QStringLiteral("Shift+"));
    return QKeySequence(s);
}

} // namespace

ShellWindow::ShellWindow(QWidget* parent)
    : QMainWindow(parent), registry_(ToolRegistry::builtin()) {
    setObjectName(QStringLiteral("ShellWindow"));
    setWindowTitle(tr("OpenStitch Desktop V2"));
    resize(1280, 800);

    canvas_ = new PreviewCanvas(this);
    canvas_->setObjectName(QStringLiteral("previewCanvas"));
    buildCentral();
    floatingBar_ = new FloatingActionBar(canvas_->viewport());
    canvas_->viewport()->installEventFilter(this);
    connect(floatingBar_, &FloatingActionBar::chosen, this,
            [this](const QString& id) { runCommand(id); });

    buildMenus();
    buildWorkspaceBar();
    buildToolRail();
    buildInspector();
    buildStatusArea();

    sessionToken_ = session_.subscribe([this](ChangeKind k) { onDocumentChanged(k); });
    selectionToken_ = session_.selection().subscribe([this](const SelectionModel&) {
        if (!syncingTree_) {
            syncTreeFromSelection();
        }
        updateSelectionBoxes();
        updatePointMarkers();
        refreshFloatingBar();
    });
    regions_ = std::make_unique<RegionEditController>(
        RegionEditController::Host{
            session_, *canvas_, frame_, tasks_, [this] { return activeTool_; },
            [this](const QString& message, bool error) { setNotice(message, error); },
            [this]() -> const image::Image* { return ensureProcessed(); },
            [this] { return lastDigitizeSettings_; },
            [this](const QString& question) {
                if (confirmHook_) {
                    return confirmHook_(question);
                }
                return QMessageBox::question(this, tr("Remplacer des retouches ?"), question,
                                             QMessageBox::Yes | QMessageBox::Cancel,
                                             QMessageBox::Cancel) == QMessageBox::Yes;
            },
            [this] { updateGuide(); }},
        regionPanel_, this);
    overlays_ = std::make_unique<AnalysisOverlays>(AnalysisOverlays::Host{
        session_, *canvas_, frame_, tasks_,
        [this](const QString& message, bool error) { setNotice(message, error); }});
    nodes_ = std::make_unique<NodeEditController>(
        NodeEditController::Host{
            session_, *canvas_, frame_, [this] { return activeTool_; },
            [this] { return documentOpen_; }, [this] { return pointTarget(); },
            [this](const QString& message, bool error) { setNotice(message, error); }},
        this);
    nodes_->connectCanvas();
    connect(canvas_, &PreviewCanvas::clickedPx, this, &ShellWindow::onCanvasClick);
    connect(canvas_, &PreviewCanvas::rectPx, this, &ShellWindow::onCanvasRect);
    connect(canvas_, &PreviewCanvas::boxDrawnPx, this, &ShellWindow::onBoxDrawn);
    connect(canvas_, &PreviewCanvas::measuredPx, this, &ShellWindow::onMeasured);
    connect(canvas_, &PreviewCanvas::lineDrawnPx, this, &ShellWindow::onKnife);
    connect(canvas_, &PreviewCanvas::strokeDrawnPx, this, &ShellWindow::onStroke);
    connect(canvas_, &PreviewCanvas::contextMenuRequested, this, &ShellWindow::showContextMenu);
    connect(canvas_, &PreviewCanvas::moveStartRequested, this, &ShellWindow::onMoveStart);
    connect(canvas_, &PreviewCanvas::nudgeRequested, this, &ShellWindow::nudgeSelection);
    connect(canvas_, &PreviewCanvas::moveDraggedPx, this, &ShellWindow::onMoveDragged);
    connect(canvas_, &PreviewCanvas::scaleDraggedPx, this, &ShellWindow::onScaleDragged);
    canvas_->setMoveHitTest([this](const QPointF& p) { return pickAtPx(p).has_value(); });
    canvas_->setMoveSnap([this](QPointF delta) { return snapDelta(delta); });
    connect(canvas_, &PreviewCanvas::polygonDrawnPx, this, &ShellWindow::onPolygonDrawn);
    connect(canvas_, &PreviewCanvas::penShapeDrawnPx, this,
            [this](QVector<QPointF> positions, QVector<QPointF> handles) {
                if (frame_.px_per_mm <= 0.0) {
                    return;
                }
                const auto toModel = [this](const QPointF& p) {
                    return screenToModel(frame_.pxToMmX(p.x()), frame_.pxToMmY(p.y()));
                };
                std::vector<PenNode> nodes;
                for (qsizetype i = 0; i < positions.size(); ++i) {
                    PenNode node;
                    node.pos = toModel(positions[i]);
                    if (handles[i] != positions[i]) {
                        node.handleOut = toModel(handles[i]);
                    }
                    nodes.push_back(node);
                }
                const auto r = createPenShape(session_, nodes);
                if (!r.ok) {
                    setNotice(tr("Plume : %1").arg(fromUtf8(r.message)), true);
                    return;
                }
                session_.selection().replace({{TargetKind::EmbroideryObject, r.objectId}});
                setNotice(r.curved ? tr("Courbe créée : modifiez-la avec l'outil Nœuds (B).")
                                   : tr("Forme créée."),
                          false);
            });
    applyToolToCanvas();

    // Un aperçu par rafale de modifications, pas un par modification (le rendu n'est pas
    // interruptible en cours de calcul).
    previewDebounce_ = new QTimer(this);
    previewDebounce_->setSingleShot(true);
    previewDebounce_->setInterval(120);
    connect(previewDebounce_, &QTimer::timeout, this, &ShellWindow::requestPreview);

    pump_ = new QTimer(this);
    pump_->setInterval(33);
    connect(pump_, &QTimer::timeout, this, [this] {
        tasks_.pump();
        refreshTaskStatus();
    });
    pump_->start();

    setWorkspace(Workspace::Design);
    refreshThemedIcons();
    setThemeMode(QStringLiteral("system"));
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
                if (themeMode_ == QLatin1String("system")) {
                    applyStudioTheme(*qApp, themeMode_);
                }
            });
    refreshActionStates();
}

ShellWindow::~ShellWindow() {
    // Les abonnements pointent vers `this` : on les retire avant que les widgets disparaissent, et
    // on annule les tâches avant que la session (membre) soit détruite.
    pump_->stop();
    previewDebounce_->stop();
    liveServer_.reset(); // plus aucun agent ne peut atteindre le document
    if (claudeBackend_ != nullptr) {
        // Avant le pont : une demande en cours ne doit plus pouvoir atteindre le document.
        claudeBackend_->cancel();
        chatPanel_->setBackend(nullptr);
        delete claudeBackend_;
        claudeBackend_ = nullptr;
        chatBackend_ = nullptr;
    }
    if (companion_) {
        companion_->close();
    }
    if (autosave_ && !session_.dirty()) {
        autosave_->clear(); // sortie propre : rien à récupérer au prochain démarrage
    }
    tasks_.cancelAll();
    // Les widgets enfants sont détruits APRÈS les membres de cette classe : ceux qui référencent la
    // session (l'inspecteur) doivent disparaître tant qu'elle existe.
    delete inspector_;
    inspector_ = nullptr;
    delete calques_;
    calques_ = nullptr;
    delete threads_;
    threads_ = nullptr;
    delete spoolRail_;
    spoolRail_ = nullptr;
    session_.unsubscribe(sessionToken_);
    session_.selection().unsubscribe(selectionToken_);
}

// --- Construction ----------------------------------------------------------------------------

void ShellWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&Fichier"));
    auto* newDoc = file->addAction(tr("&Nouveau projet"));
    newDoc->setShortcut(QKeySequence::New);
    connect(newDoc, &QAction::triggered, this, &ShellWindow::newProject);
    auto* open = file->addAction(tr("&Ouvrir une image ou un projet…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &ShellWindow::chooseAndOpen);
    auto* importMachine = file->addAction(tr("&Importer une broderie machine…"));
    importMachine->setObjectName(QStringLiteral("action_file_import_machine"));
    connect(importMachine, &QAction::triggered, this, &ShellWindow::chooseAndImportMachine);
    auto* importDxf = file->addAction(tr("Importer un &DXF…"));
    importDxf->setObjectName(QStringLiteral("action_file_import_dxf"));
    connect(importDxf, &QAction::triggered, this, &ShellWindow::chooseAndImportDxf);
    recentMenu_ = file->addMenu(tr("Fichiers &récents"));
    recentMenu_->setObjectName(QStringLiteral("menu_recent"));
    saveAction_ = file->addAction(tr("&Enregistrer"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] { (void)saveCurrentProject(); });
    auto* saveAs = file->addAction(tr("Enregistrer &sous…"));
    saveAs->setObjectName(QStringLiteral("action_file_save_as"));
    saveAs->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    connect(saveAs, &QAction::triggered, this, [this] { (void)saveProjectAs(); });
    exportAction_ = file->addAction(tr("E&xporter pour la machine…"));
    exportAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction_, &QAction::triggered, this, [this] {
        // Un filtre par format machine inscrit (DST, PES, JEF, EXP, VP3…), le DST en premier.
        QStringList filters;
        for (const auto& f : exportFormats()) {
            filters << QStringLiteral("%1 (*.%2)").arg(fromUtf8(f.label), fromUtf8(f.extension));
        }
        const QString path = QFileDialog::getSaveFileName(this, tr("Exporter pour la machine"),
                                                          QString(), filters.join(";;"));
        if (!path.isEmpty()) {
            exportDst(std::filesystem::path(path.toStdU16String()));
        }
    });
    auto* exportAgainAction = file->addAction(tr("Exporter de nouveau (derniers réglages)"));
    exportAgainAction->setObjectName(QStringLiteral("action_file_export_again"));
    exportAgainAction->setShortcut(QKeySequence(QStringLiteral("F10")));
    connect(exportAgainAction, &QAction::triggered, this, [this] { (void)exportAgain(); });
    auto* exportThreadsAction = file->addAction(tr("Exporter la liste des &fils (CSV)…"));
    exportThreadsAction->setObjectName(QStringLiteral("action_file_export_threads"));
    connect(exportThreadsAction, &QAction::triggered, this, &ShellWindow::chooseAndExportThreads);
    auto* importChartAction = file->addAction(tr("Importer un &nuancier de fils (CSV, JSON)…"));
    importChartAction->setObjectName(QStringLiteral("action_file_import_chart"));
    connect(importChartAction, &QAction::triggered, this, &ShellWindow::chooseAndImportChart);
    auto* chartFormatAction = file->addAction(tr("Format d'import des nuanciers…"));
    chartFormatAction->setObjectName(QStringLiteral("action_file_chart_format"));
    connect(chartFormatAction, &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, tr("Format d'import des nuanciers"),
            tr("<p>Les cartes de fils des marques ne sont pas fournies avec OpenStitch : importez "
               "les vôtres.</p><p><b>CSV</b> (UTF-8, séparateur « , » ou « ; », en-tête "
               "obligatoire) "
               ":<br><code>code,name,hex</code> ou <code>code,name,r,g,b</code><br>colonnes "
               "facultatives : <code>brand</code>, <code>range</code>.</p><p><b>JSON</b> :<br>"
               "<code>{\"name\":\"Mes fils\",\"threads\":[{\"code\":\"1001\",\"name\":"
               "\"Rouge\",\"rgb\":\"#C8102E\"}]}</code></p><p>Le nom du fichier (CSV) ou « name » "
               "(JSON) donne le nom du nuancier ; les codes doivent être uniques.</p>"));
    });
    auto* removeChartAction = file->addAction(tr("Retirer un nuancier importé…"));
    removeChartAction->setObjectName(QStringLiteral("action_file_remove_chart"));
    connect(removeChartAction, &QAction::triggered, this, &ShellWindow::removeImportedChart);
    auto* exportDxfAction = file->addAction(tr("Exporter les contours en D&XF…"));
    exportDxfAction->setObjectName(QStringLiteral("action_file_export_dxf"));
    connect(exportDxfAction, &QAction::triggered, this, &ShellWindow::chooseAndExportDxf);
    auto* exportImageAction = file->addAction(tr("Exporter l'&image (PNG)…"));
    connect(exportImageAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exporter l'image"), QString(),
                                                          tr("Image PNG (*.png)"));
        if (!path.isEmpty()) {
            exportImage(std::filesystem::path(path.toStdU16String()));
        }
    });
    auto* sheetAction = file->addAction(tr("&Fiche de production (PDF)…"));
    sheetAction->setObjectName(QStringLiteral("action_file_production_sheet"));
    connect(sheetAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Fiche de production"),
                                                          QString(), tr("Document PDF (*.pdf)"));
        if (!path.isEmpty()) {
            exportProductionSheet(std::filesystem::path(path.toStdU16String()));
        }
    });
    file->addSeparator();
    auto* quit = file->addAction(tr("&Quitter"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(tr("É&dition"));
    undoAction_ = edit->addAction(tr("&Annuler"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] { session_.undo(); });
    redoAction_ = edit->addAction(tr("&Rétablir"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] { session_.redo(); });
    historyMenu_ = edit->addMenu(tr("&Historique"));
    historyMenu_->setObjectName(QStringLiteral("menu_history"));
    connect(historyMenu_, &QMenu::aboutToShow, this, [this] { rebuildHistoryMenu(); });
    edit->addSeparator();
    auto* all = edit->addAction(tr("Tout &sélectionner"));
    all->setShortcut(QKeySequence::SelectAll);
    connect(all, &QAction::triggered, this, [this] {
        if (regions_ && regions_->active()) {
            regions_->selectAll(); // avec l'outil Régions, Ctrl+A choisit les régions
        } else if (activeTool_ == QStringLiteral("stitch.points")) {
            selectAllPoints();
        } else {
            selectAllObjects();
        }
    });
    auto* none = edit->addAction(tr("&Désélectionner"));
    none->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    connect(none, &QAction::triggered, this, [this] {
        if (regions_ && regions_->active()) {
            regions_->clearSelection();
        } else if (activeTool_ == QStringLiteral("stitch.points")) {
            clearPointSelection();
        } else {
            session_.selection().clear();
        }
    });
    edit->addSeparator();
    const auto addEdit = [this](QMenu* menu, const QString& text, const char* id,
                                const QKeySequence& key = QKeySequence()) {
        auto* a = menu->addAction(text);
        if (!key.isEmpty()) {
            a->setShortcut(key);
        }
        connect(a, &QAction::triggered, this,
                [this, id] { editSelection(QString::fromLatin1(id)); });
    };
    addEdit(edit, tr("&Supprimer"), "edit.delete", QKeySequence::Delete);
    addEdit(edit, tr("D&upliquer"), "edit.duplicate", QKeySequence(QStringLiteral("Ctrl+D")));
    auto* discard = edit->addAction(tr("Effacer les retouches de &points"));
    discard->setObjectName(QStringLiteral("action_discard_points"));
    connect(discard, &QAction::triggered, this, [this] { discardPointEdits(); });
    auto* transform = edit->addMenu(tr("&Transformer"));
    addEdit(transform, tr("Pivoter de 90° à &droite"), "transform.rotate.cw");
    addEdit(transform, tr("Pivoter de 90° à &gauche"), "transform.rotate.ccw");
    addEdit(transform, tr("Miroir &horizontal"), "transform.mirror.h");
    addEdit(transform, tr("Miroir &vertical"), "transform.mirror.v");
    auto* arrange = edit->addMenu(tr("&Aligner"));
    addEdit(arrange, tr("À &gauche"), "arrange.align.left");
    addEdit(arrange, tr("Centrer &horizontalement"), "arrange.align.centerh");
    addEdit(arrange, tr("À &droite"), "arrange.align.right");
    addEdit(arrange, tr("En &haut"), "arrange.align.top");
    addEdit(arrange, tr("Centrer &verticalement"), "arrange.align.centerv");
    addEdit(arrange, tr("En &bas"), "arrange.align.bottom");
    auto* distribute = edit->addMenu(tr("&Répartir"));
    addEdit(distribute, tr("&Horizontalement"), "arrange.distribute.h");
    addEdit(distribute, tr("&Verticalement"), "arrange.distribute.v");
    addEdit(edit, tr("&Grouper"), "arrange.group", QKeySequence(QStringLiteral("Ctrl+G")));
    addEdit(edit, tr("Dé&grouper"), "arrange.ungroup",
            QKeySequence(QStringLiteral("Ctrl+Shift+G")));
    addEdit(edit, tr("Position et &taille…"), "arrange.place");
    auto* order = edit->addMenu(tr("&Ordre de couture"));
    addEdit(order, tr("Premier plan"), "order.front");
    addEdit(order, tr("Avancer"), "order.forward");
    addEdit(order, tr("Reculer"), "order.backward");
    addEdit(order, tr("Arrière-plan"), "order.back");
    edit->addSeparator();
    auto* palette = edit->addAction(tr("&Palette de commandes…"));
    palette->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(palette, &QAction::triggered, this, &ShellWindow::showCommandPalette);
    // Les commandes à raccourci par défaut sont atteignables au clavier même sans entrée de menu ;
    // `runCommand` refuse (sans effet) quand la commande est indisponible.
    for (const auto& [id, key] : kCommandShortcuts) {
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(QString::fromLatin1(key)));
        a->setShortcutContext(Qt::WindowShortcut);
        const QString commandId = QString::fromLatin1(id);
        connect(a, &QAction::triggered, this, [this, commandId] { runCommand(commandId); });
        addAction(a);
    }

    auto* view = menuBar()->addMenu(tr("&Affichage"));
    // Menu « Outils » : une entrée par action du catalogue, groupées ; le dialogue de réglages est
    // généré depuis les champs de l'action.
    auto* toolsMenu = menuBar()->addMenu(tr("&Outils"));
    std::map<std::string, QMenu*> groups;
    for (const auto& def : actionCatalog()) {
        QMenu*& group = groups[def.group];
        if (group == nullptr) {
            group = toolsMenu->addMenu(fromUtf8(def.group));
        }
        auto* a = group->addAction(fromUtf8(def.label));
        a->setObjectName(QStringLiteral("action_") + fromUtf8(def.id));
        const QString id = fromUtf8(def.id);
        connect(a, &QAction::triggered, this, [this, id] { runAction(id); });
    }
    toolsMenu->addSeparator();
    // Correction de la segmentation après numérisation : supprimer / fusionner des régions, puis
    // régénérer les objets. Le bouton du rail d'outils est loin en bas : ce menu le rend trouvable.
    auto* regionsAction = toolsMenu->addAction(tr("Corriger la segmentation (régions)… [X]"));
    regionsAction->setObjectName(QStringLiteral("action_region_edit"));
    connect(regionsAction, &QAction::triggered, this,
            [this] { (void)runCommand(QStringLiteral("tool.region.edit")); });
    auto* imageMenu = menuBar()->addMenu(tr("&Image"));
    const auto addPrep = [this, imageMenu](const QString& text, const char* id) {
        auto* a = imageMenu->addAction(text);
        a->setObjectName(QStringLiteral("action_") + QString::fromLatin1(id));
        connect(a, &QAction::triggered, this,
                [this, id] { runImagePrep(QString::fromLatin1(id)); });
    };
    addPrep(tr("&Luminosité et contraste…"), "image.prep.bc");
    addPrep(tr("&Niveaux de gris"), "image.prep.gray");
    addPrep(tr("N&etteté…"), "image.prep.sharpen");
    addPrep(tr("Contraste &local (CLAHE)…"), "image.prep.clahe");
    imageMenu->addSeparator();
    addPrep(tr("Pivoter à &droite"), "image.prep.rotate.r");
    addPrep(tr("Pivoter à &gauche"), "image.prep.rotate.l");
    addPrep(tr("Pivoter de 180°"), "image.prep.rotate.180");
    addPrep(tr("Retourner &horizontalement"), "image.prep.flip.h");
    addPrep(tr("Retourner &verticalement"), "image.prep.flip.v");
    addPrep(tr("&Recadrer…"), "image.prep.crop");
    presetMenu_ = menuBar()->addMenu(tr("&Préréglages"));
    auto* helpMenu = menuBar()->addMenu(tr("A&ide"));
    auto* shortcuts = helpMenu->addAction(tr("&Raccourcis et commandes…"));
    shortcuts->setShortcut(QKeySequence::HelpContents);
    shortcuts->setObjectName(QStringLiteral("action_help_shortcuts"));
    connect(shortcuts, &QAction::triggered, this, [this] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Raccourcis et commandes"));
        auto* layout = new QVBoxLayout(&dialog);
        auto* browser = new QTextBrowser(&dialog);
        browser->setPlainText(helpText());
        browser->setMinimumSize(520, 480);
        layout->addWidget(browser);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(close);
        dialog.exec();
    });
    auto* about = helpMenu->addAction(tr("À &propos…"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("À propos"),
                           tr("OpenStitch Studio — numérisation de broderie machine libre "
                              "(Apache-2.0).\nDesktop V2."));
    });
    connect(presetMenu_, &QMenu::aboutToShow, this, &ShellWindow::rebuildPresetMenu);
    auto* fit = view->addAction(tr("&Ajuster à la fenêtre"));
    connect(fit, &QAction::triggered, canvas_, &PreviewCanvas::fitAll);
    auto* zoomMenu = view->addMenu(tr("&Zoom"));
    zoomMenu->setObjectName(QStringLiteral("zoomMenu"));
    const auto zoomAction = [&](const QString& text, const char* name, auto slot) {
        auto* a = zoomMenu->addAction(text);
        a->setObjectName(QString::fromLatin1(name));
        connect(a, &QAction::triggered, this, slot);
    };
    zoomAction(tr("Zoom &avant"), "zoom_in", [this] { zoomIn(); });
    zoomAction(tr("Zoom a&rrière"), "zoom_out", [this] { zoomOut(); });
    zoomAction(tr("Zoom sur la &sélection"), "zoom_selection", [this] { zoomToSelection(); });
    zoomAction(tr("&Taille réelle (100 %)"), "zoom_actual", [this] { zoomActual(); });
    zoomAction(tr("Zoom &précédent"), "zoom_previous", [this] { zoomBack(); });
    auto* guideMenu = view->addMenu(tr("G&uides"));
    guideMenu->setObjectName(QStringLiteral("guideMenu"));
    const auto guideAction = [&](const QString& text, const char* name, auto slot) {
        auto* a = guideMenu->addAction(text);
        a->setObjectName(QString::fromLatin1(name));
        connect(a, &QAction::triggered, this, slot);
    };
    guideAction(tr("Ajouter un guide &vertical…"), "guide_add_v", [this] { askGuide(true); });
    guideAction(tr("Ajouter un guide &horizontal…"), "guide_add_h", [this] { askGuide(false); });
    guideAction(tr("&Effacer les guides"), "guide_clear", [this] { clearGuides(); });
    guideAction(tr("&Couleur des guides…"), "guide_colour", [this] {
        setGuideColor(QColorDialog::getColor(guideColor_, this, tr("Couleur des guides")));
    });
    auto* source = view->addAction(tr("&Image source"));
    source->setCheckable(true);
    source->setObjectName(QStringLiteral("action_view_source"));
    connect(source, &QAction::toggled, this, [this](bool on) { setSourceVisible(on); });
    auto* hoop = view->addAction(tr("Cadre de &broderie"));
    hoop->setCheckable(true);
    hoop->setChecked(true);
    connect(hoop, &QAction::toggled, this, [this](bool on) { canvas_->setHoopVisible(on); });
    auto* gridMenu = view->addMenu(tr("&Grille"));
    auto* gridGroup = new QActionGroup(this);
    for (const double mm : {0.0, 5.0, 10.0, 20.0}) {
        auto* g = gridMenu->addAction(mm == 0.0 ? tr("Aucune") : tr("%1 mm").arg(mm, 0, 'f', 0));
        g->setCheckable(true);
        g->setChecked(mm == 0.0);
        gridGroup->addAction(g);
        connect(g, &QAction::triggered, this, [this, mm] { setGridSpacing(mm); });
    }
    view->addSeparator();
    compactToolsAction_ = view->addAction(tr("Barre d'outils compacte"));
    compactToolsAction_->setObjectName(QStringLiteral("action_view_compact_tools"));
    compactToolsAction_->setCheckable(true);
    compactToolsAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
    compactToolsAction_->setToolTip(
        tr("Réduire le rail pour laisser davantage d'espace au canevas"));
    connect(compactToolsAction_, &QAction::toggled, this,
            [this](bool compact) { setCompactToolRail(compact); });
    auto* themeMenu = view->addMenu(tr("&Thème"));
    auto* themeGroup = new QActionGroup(this);
    for (const auto& [mode, label] :
         {std::pair{"system", tr("Système")}, std::pair{"light", tr("Clair")},
          std::pair{"dark", tr("Sombre")}}) {
        auto* t = themeMenu->addAction(label);
        t->setObjectName(QStringLiteral("theme_") + QLatin1String(mode));
        t->setCheckable(true);
        t->setChecked(QLatin1String(mode) == QLatin1String("system"));
        themeGroup->addAction(t);
        connect(t, &QAction::triggered, this,
                [this, mode] { setThemeMode(QString::fromLatin1(mode)); });
    }
    auto* densityMenu = view->addMenu(tr("&Densité de l'interface"));
    auto* densityGroup = new QActionGroup(this);
    for (const auto d : {UiDensity::Compact, UiDensity::Normal, UiDensity::Spacious}) {
        auto* a = densityMenu->addAction(fromUtf8(densityLabel(d)));
        a->setObjectName(QStringLiteral("density_") + fromUtf8(densityId(d)));
        a->setCheckable(true);
        a->setChecked(d == UiDensity::Normal);
        densityGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, d] { setDensity(d); });
    }
    auto* renderMenu = view->addMenu(tr("&Qualité du rendu réaliste"));
    renderMenu->setObjectName(QStringLiteral("renderMenu"));
    auto* renderGroup = new QActionGroup(this);
    for (const char* id : {"draft", "balanced", "high", "photo"}) {
        const auto label = renderPresetLabel(id);
        auto* a = renderMenu->addAction(fromUtf8(label));
        a->setObjectName(QStringLiteral("renderPreset_") + QString::fromLatin1(id));
        a->setCheckable(true);
        renderGroup->addAction(a);
        const std::string presetId = id;
        connect(a, &QAction::triggered, this, [this, presetId] {
            if (const auto p = renderPreset(presetId)) {
                setRenderSettings(*p);
            }
        });
    }
    renderMenu->addSeparator();
    auto* renderDetails = renderMenu->addAction(tr("Réglages détaillés…"));
    renderDetails->setObjectName(QStringLiteral("renderDetails"));
    connect(renderDetails, &QAction::triggered, this, [this] { showRenderQuality(); });
    reducedMotionAction_ = view->addAction(tr("Réduire les animations"));
    reducedMotionAction_->setObjectName(QStringLiteral("reduceMotion"));
    reducedMotionAction_->setToolTip(
        tr("Affiche immédiatement les confirmations, sans animation ni effet continu."));
    reducedMotionAction_->setCheckable(true);
    connect(reducedMotionAction_, &QAction::triggered, this, [this](bool reduced) {
        feedbackCue_->setReducedMotion(reduced);
        if (!stateDir_.empty()) {
            QSettings settings(QString::fromStdU16String((stateDir_ / "motion.ini").u16string()),
                               QSettings::IniFormat);
            settings.setValue(QStringLiteral("visual/reducedMotion"), reduced);
        }
    });
    connect(reducedMotionAction_, &QAction::triggered, canvas_, &PreviewCanvas::setReducedMotion);
}

void ShellWindow::buildWorkspaceBar() {
    workspaceBar_ = addToolBar(tr("Espaces de travail"));
    workspaceBar_->setObjectName(QStringLiteral("workspaceBar"));
    workspaceBar_->setMovable(false);
    workspaceGroup_ = new QActionGroup(this);
    workspaceGroup_->setExclusive(true);
    int n = 1;
    for (const auto w : kWorkspaces) {
        auto* a = workspaceBar_->addAction(workspaceLabel(w));
        a->setCheckable(true);
        a->setData(static_cast<int>(w));
        a->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(n++)));
        workspaceGroup_->addAction(a);
        workspaceActions_.push_back(a);
        connect(a, &QAction::triggered, this, [this, w] { setWorkspace(w); });
    }
    // Accès permanent à tous les outils, y compris ceux cachés lorsque le rail vertical
    // déborde sur un écran peu haut. Le menu suit l'espace actif, sans raccourcis doublés.
    auto* allToolsButton = new QToolButton(workspaceBar_);
    allToolsButton->setObjectName(QStringLiteral("allToolsButton"));
    allToolsButton->setText(tr("Tous les outils"));
    allToolsButton->setIcon(toolIcon("select", palette().color(QPalette::ButtonText)));
    allToolsButton->setIconSize(QSize(16, 16)); // même hauteur que les boutons d'espace
    allToolsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    allToolsButton->setPopupMode(QToolButton::InstantPopup);
    allToolsButton->setToolTip(tr("Tous les outils, quel que soit l'espace actif"));
    allToolsButton->setAccessibleName(tr("Afficher tous les outils de broderie"));
    allToolsMenu_ = new QMenu(allToolsButton);
    allToolsMenu_->setObjectName(QStringLiteral("allToolsMenu"));
    allToolsButton->setMenu(allToolsMenu_);
    workspaceBar_->addWidget(allToolsButton);
    connect(allToolsMenu_, &QMenu::aboutToShow, this, [this] {
        for (auto* action : allToolsMenu_->actions()) {
            action->setChecked(action->data().toString() == activeTool_);
        }
    });

    auto* spacer = new QWidget(workspaceBar_);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    workspaceBar_->addWidget(spacer);
    workspaceBar_->addWidget(new QLabel(tr("Hauteur visée (mm) : "), workspaceBar_));
    heightSpin_ = new QDoubleSpinBox(workspaceBar_);
    heightSpin_->setObjectName(QStringLiteral("targetHeight"));
    heightSpin_->setRange(0.0, 1000.0);
    heightSpin_->setDecimals(1);
    heightSpin_->setSpecialValueText(tr("native"));
    heightSpin_->setToolTip(tr("Hauteur physique du motif à l'import ; « native » = 96 dpi."));
    workspaceBar_->addWidget(heightSpin_);
}

void ShellWindow::buildToolRail() {
    toolRail_ = new QToolBar(tr("Outils"), this);
    toolRail_->setObjectName(QStringLiteral("toolRail"));
    toolRail_->setOrientation(Qt::Vertical);
    toolRail_->setMovable(false);
    toolRail_->setIconSize(QSize(20, 20));
    toolRail_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolRail_->setFixedWidth(52);
    addToolBar(Qt::LeftToolBarArea, toolRail_);
    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);
}

void ShellWindow::buildInspector() {
    auto* dock = new QDockWidget(tr("Document"), this);
    dock->setObjectName(QStringLiteral("documentDock"));
    auto* body = new QWidget(dock);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(4, 4, 4, 4);
    objectTree_ = new QTreeWidget(body);
    objectTree_->setObjectName(QStringLiteral("objectTree"));
    objectTree_->setColumnCount(2);
    objectTree_->setHeaderLabels({tr("Objet"), tr("Type")});
    objectTree_->setRootIsDecorated(false);
    objectTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    objectTree_->setAccessibleName(tr("Objets de broderie"));
    objectTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(objectTree_, 1);
    inspector_ = new InspectorPanel(session_, body);
    connect(inspector_, &InspectorPanel::refused, this,
            [this](const QString& message) { setNotice(message, true); });
    // L'inspecteur peut changer de hauteur selon la sélection (réglages par type de point) : dans
    // une zone défilante, il ne modifie jamais la taille minimale de la fenêtre, donc ne
    // redimensionne pas le canevas en plein geste (un clic qui fait « sauter » la vue serait
    // inutilisable).
    auto* inspectorScroll = new QScrollArea(body);
    inspectorScroll->setObjectName(QStringLiteral("inspectorScroll"));
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFrameShape(QFrame::NoFrame);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspectorScroll->setMinimumHeight(160);
    inspectorScroll->setWidget(inspector_);
    layout->addWidget(inspectorScroll, 1);
    summary_ = new QLabel(body);
    summary_->setObjectName(QStringLiteral("summary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    dock->setWidget(body);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    docDock_ = dock;

    // Diagnostic : second onglet du même dock, mis en avant dans l'espace Optimisation.
    diagDock_ = new QDockWidget(tr("Diagnostic"), this);
    diagDock_->setObjectName(QStringLiteral("diagnosticDock"));
    diagnostics_ = new DiagnosticsPanel(diagDock_);
    diagDock_->setWidget(diagnostics_);
    addDockWidget(Qt::RightDockWidgetArea, diagDock_);
    tabifyDockWidget(docDock_, diagDock_);
    auto* calqueDock = new QDockWidget(tr("Calques"), this);
    calqueDock->setObjectName(QStringLiteral("calqueDock"));
    calques_ = new CalquePanel(session_, calqueDock);
    calqueDock->setWidget(calques_);
    addDockWidget(Qt::RightDockWidgetArea, calqueDock);
    tabifyDockWidget(diagDock_, calqueDock);
    connect(calques_, &CalquePanel::refused, this,
            [this](const QString& message) { setNotice(message, true); });
    auto* threadDock = new QDockWidget(tr("Fils"), this);
    threadDock->setObjectName(QStringLiteral("threadDock"));
    threads_ = new ThreadPanel(session_, threadDock);
    threadDock->setWidget(threads_);
    addDockWidget(Qt::RightDockWidgetArea, threadDock);
    tabifyDockWidget(calqueDock, threadDock);
    connect(threads_, &ThreadPanel::refused, this,
            [this](const QString& message) { setNotice(message, true); });
    auto* railDock = new QDockWidget(tr("Bobines"), this);
    railDock->setObjectName(QStringLiteral("spoolRailDock"));
    spoolRail_ = new SpoolRailWidget(session_, railDock);
    railDock->setWidget(spoolRail_);
    addDockWidget(Qt::BottomDockWidgetArea, railDock);
    connect(spoolRail_, &SpoolRailWidget::summaryChanged, railDock, [railDock](const QString& s) {
        railDock->setWindowTitle(QObject::tr("Bobines — %1").arg(s));
    });
    railDock->setWindowTitle(tr("Bobines — %1").arg(spoolRail_->summaryText()));
    connect(spoolRail_, &SpoolRailWidget::refused, this,
            [this](const QString& message) { setNotice(message, true); });
    auto* optimizeDock = new QDockWidget(tr("Optimisation"), this);
    optimizeDock->setObjectName(QStringLiteral("optimizeDock"));
    optimizePanel_ = new OptimizePanel(optimizeDock);
    optimizeDock->setWidget(optimizePanel_);
    addDockWidget(Qt::RightDockWidgetArea, optimizeDock);
    tabifyDockWidget(calqueDock, optimizeDock);
    connect(optimizePanel_, &OptimizePanel::proposeRequested, this,
            [this](PathStrategy strategy, bool improve) { runOptimization(strategy, improve); });
    connect(optimizePanel_, &OptimizePanel::acceptRequested, this,
            [this] { (void)acceptOptimization(); });
    connect(optimizePanel_, &OptimizePanel::refuseRequested, this,
            [this] { refuseOptimization(); });
    auto* regionDock = new QDockWidget(tr("Régions"), this);
    regionDock->setObjectName(QStringLiteral("regionDock"));
    regionPanel_ = new RegionPanel(regionDock);
    regionDock->setWidget(regionPanel_);
    addDockWidget(Qt::RightDockWidgetArea, regionDock);
    tabifyDockWidget(calqueDock, regionDock);
    auto* companionDock = new QDockWidget(tr("Compagnon IA"), this);
    companionDock->setObjectName(QStringLiteral("companionDock"));
    companionPanel_ = new CompanionPanel(companionDock);
    companionDock->setWidget(companionPanel_);
    addDockWidget(Qt::RightDockWidgetArea, companionDock);
    tabifyDockWidget(calqueDock, companionDock);
    connect(companionPanel_, &CompanionPanel::toggleRequested, this, [this](bool enable) {
        QString error;
        if (!setLiveBridgeEnabled(enable, &error)) {
            companionPanel_->setError(error);
            setNotice(tr("Compagnon IA : %1").arg(error), true);
        }
    });
    connect(companionPanel_, &CompanionPanel::undoRequested, this, [this] { session_.undo(); });
    chatDock_ = new QDockWidget(tr("Discussion IA"), this);
    chatDock_->setObjectName(QStringLiteral("chatDock"));
    chatPanel_ = new ChatPanel(chatDock_);
    chatDock_->setWidget(chatPanel_);
    addDockWidget(Qt::RightDockWidgetArea, chatDock_);
    tabifyDockWidget(calqueDock, chatDock_);
    connect(chatPanel_, &ChatPanel::configureRequested, this, [this] { showCompanionSettings(); });
    connect(chatPanel_, &ChatPanel::resetRequested, this, [this] {
        if (claudeBackend_ != nullptr) {
            claudeBackend_->resetConversation();
        }
    });
    docDock_->raise();
    polishDockTabs();
    // Affichage › Panneaux : rouvrir à la main n'importe quel panneau, quel que soit l'espace.
    for (auto* action : menuBar()->actions()) {
        if (action->menu() != nullptr && action->text() == tr("&Affichage")) {
            auto* panels = action->menu()->addMenu(tr("&Panneaux"));
            panels->setObjectName(QStringLiteral("panelsMenu"));
            for (auto* panelDock : findChildren<QDockWidget*>()) {
                panels->addAction(panelDock->toggleViewAction());
            }
        }
    }
    connect(diagnostics_, &DiagnosticsPanel::analyseRequested, this, &ShellWindow::runDiagnostics);
    connect(diagnostics_, &DiagnosticsPanel::findingActivated, this,
            &ShellWindow::onFindingActivated);

    connect(objectTree_, &QTreeWidget::itemSelectionChanged, this,
            &ShellWindow::onSelectionChangedInTree);
    // Édition directe dans l'arbre : nom (colonne 0) et visibilité (case) passent par le MÊME lot
    // d'opérations que la CLI et le compagnon IA -- une étape d'annulation chacun.
    connect(objectTree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        if (syncingTree_ || item == nullptr) {
            return;
        }
        const auto id = item->data(0, kIdRole).toULongLong();
        const bool visible = item->checkState(0) == Qt::Checked;
        const std::string name = item->text(0).toStdString();
        const auto* object = [&]() -> const document::EmbroideryObject* {
            for (const auto& o : session_.project().embroidery_objects) {
                if (o.id.value == id) {
                    return &o;
                }
            }
            return nullptr;
        }();
        if (object == nullptr) {
            return;
        }
        std::string ops;
        std::string label;
        if (object->visible != visible) {
            ops = R"({"op":"set_visible","object":)" + std::to_string(id) + R"(,"visible":)" +
                  (visible ? "true" : "false") + "}";
            label = visible ? tr("Afficher").toStdString() : tr("Masquer").toStdString();
        } else if (object->name != name && !name.empty()) {
            // JSON : on échappe guillemets et barres obliques inverses du nom saisi.
            std::string escaped;
            for (const char c : name) {
                if (c == '"' || c == '\\') {
                    escaped.push_back('\\');
                }
                escaped.push_back(c);
            }
            ops = R"({"op":"rename","object":)" + std::to_string(id) + R"(,"name":")" + escaped +
                  R"("})";
            label = tr("Renommer").toStdString();
        } else {
            return;
        }
        const auto result = session_.applyBatch(
            R"({"schema":"openstitch.batch/1","operations":[)" + ops + "]}", label);
        if (!result.committed) {
            const auto& outcomes = result.report.outcomes;
            setNotice(outcomes.empty() ? tr("Modification refusée")
                                       : fromUtf8(outcomes.back().message),
                      true);
            rebuildObjectList(); // rétablit l'affichage du document
        }
    });
}

void ShellWindow::buildStatusArea() {
    // Simulation de couture (espace Production) : curseur sur la séquence, lecture automatique.
    simBar_ = addToolBar(tr("Simulation"));
    simBar_->setObjectName(QStringLiteral("simulationBar"));
    simBar_->setMovable(false);
    simBar_->addWidget(new QLabel(tr("Simulation : "), simBar_));
    simSlider_ = new QSlider(Qt::Horizontal, simBar_);
    simSlider_->setObjectName(QStringLiteral("simulationSlider"));
    simSlider_->setAccessibleName(tr("Position dans la séquence de couture"));
    simSlider_->setMinimumWidth(320);
    simBar_->addWidget(simSlider_);
    simLabel_ = new QLabel(simBar_);
    simLabel_->setObjectName(QStringLiteral("simulationLabel"));
    simLabel_->setMinimumWidth(110);
    simBar_->addWidget(simLabel_);
    auto* play = simBar_->addAction(tr("Lecture"));
    play->setCheckable(true);
    const auto nav = [&](const QString& text, const QString& tip, const char* id) {
        auto* a = simBar_->addAction(text);
        a->setObjectName(QString::fromLatin1(id));
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this,
                [this, id] { (void)simulationJump(QString::fromLatin1(id)); });
    };
    nav(tr("⏮ Couleur"), tr("Début de la couleur précédente"), "sim.prev-colour");
    nav(tr("⏮ Objet"), tr("Début de l'objet précédent"), "sim.prev-object");
    nav(tr("◀ Point"), tr("Un point en arrière"), "sim.prev-point");
    nav(tr("Point ▶"), tr("Un point en avant"), "sim.next-point");
    nav(tr("Objet ⏭"), tr("Début de l'objet suivant"), "sim.next-object");
    nav(tr("Couleur ⏭"), tr("Début de la couleur suivante"), "sim.next-colour");
    nav(tr("⏹ Fin"), tr("Aller à la fin de la séquence"), "sim.end");
    simTimer_ = new QTimer(this);
    simTimer_->setInterval(120);
    connect(simSlider_, &QSlider::valueChanged, this, [this](int v) { setSimulationPosition(v); });
    connect(play, &QAction::toggled, this, [this](bool on) {
        if (on) {
            if (!simLimit_ || simulationPosition() >= simulationTotal()) {
                setSimulationPosition(0); // repart du début
            }
            simTimer_->start();
        } else {
            simTimer_->stop();
        }
    });
    connect(simTimer_, &QTimer::timeout, this, [this, play] {
        const int step = std::max(1, simulationTotal() / 150);
        const int next = simulationPosition() + step;
        setSimulationPosition(next);
        if (next >= simulationTotal()) {
            play->setChecked(false);
        }
    });
    simBar_->hide();

    // Réglages des outils d'image (clic-pour-broder : tolérance, type ; pinceau : rayon).
    imageBar_ = addToolBar(tr("Outils d'image"));
    imageBar_->setObjectName(QStringLiteral("imageToolBar"));
    imageBar_->setMovable(false);
    imageBar_->addWidget(new QLabel(tr(" Tolérance : "), imageBar_));
    tolSpin_ = new QSpinBox(imageBar_);
    tolSpin_->setObjectName(QStringLiteral("clickTolerance"));
    tolSpin_->setRange(0, 100);
    tolSpin_->setValue(clickTol_);
    tolSpin_->setToolTip(tr("Écart de couleur accepté autour du pixel cliqué (CIELAB)."));
    imageBar_->addWidget(tolSpin_);
    imageBar_->addWidget(new QLabel(tr("  Type : "), imageBar_));
    kindCombo_ = new QComboBox(imageBar_);
    kindCombo_->setObjectName(QStringLiteral("clickKind"));
    kindCombo_->addItems({tr("Automatique"), tr("Remplissage"), tr("Satin"), tr("Contour seul")});
    imageBar_->addWidget(kindCombo_);
    imageBar_->addWidget(new QLabel(tr("  Pinceau : "), imageBar_));
    auto* radius = new QDoubleSpinBox(imageBar_);
    radius->setObjectName(QStringLiteral("brushRadius"));
    radius->setRange(0.2, 50.0);
    radius->setValue(brushMm_);
    radius->setSuffix(tr(" mm"));
    radius->setToolTip(tr("Rayon du pinceau des zones de détail (Ctrl = gomme)."));
    imageBar_->addWidget(radius);
    connect(tolSpin_, &QSpinBox::valueChanged, this,
            [this](int v) { setClickToStitch(v, clickKind_); });
    connect(kindCombo_, &QComboBox::currentIndexChanged, this,
            [this](int i) { setClickToStitch(clickTol_, static_cast<ClickKind>(i)); });
    connect(radius, &QDoubleSpinBox::valueChanged, this, [this](double v) { setBrushRadiusMm(v); });
    imageBar_->hide();
    feedbackCue_ = new FeedbackCue(this);
    statusBar()->addWidget(feedbackCue_);
    notice_ = new QLabel(this);
    notice_->setObjectName(QStringLiteral("notice"));
    notice_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->addWidget(notice_, 1);
    activeToolStatus_ = new QLabel(tr("Sélection"), this);
    activeToolStatus_->setObjectName(QStringLiteral("activeToolStatus"));
    activeToolStatus_->setAccessibleName(tr("Outil actif"));
    statusBar()->addPermanentWidget(activeToolStatus_);
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("taskProgress"));
    progress_->setMaximumWidth(220);
    progress_->setTextVisible(true);
    progress_->hide();
    statusBar()->addPermanentWidget(progress_);
    cancelButton_ = new QToolButton(this);
    cancelButton_->setObjectName(QStringLiteral("cancelTasks"));
    cancelButton_->setText(tr("Annuler"));
    cancelButton_->setToolTip(tr("Annule les calculs en cours (le document n'est pas modifié)"));
    cancelButton_->hide();
    connect(cancelButton_, &QToolButton::clicked, this, [this] { tasks_.cancelAll(); });
    statusBar()->addPermanentWidget(cancelButton_);
    setNotice(tr("Ouvrez une image (Ctrl+O) pour la numériser."), false);
}

// --- Espaces et outils -----------------------------------------------------------------------

void ShellWindow::setCompactToolRail(bool compact) {
    compactTools_ = compact;
    toolRail_->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    toolRail_->setFixedWidth(compact ? 52 : 180);
    if (compactToolsAction_ != nullptr) {
        compactToolsAction_->setChecked(compact);
    }
    if (!stateDir_.empty()) {
        QSettings settings(QString::fromStdU16String((stateDir_ / "ui.ini").u16string()),
                           QSettings::IniFormat);
        settings.setValue(QStringLiteral("tools/compact"), compact);
    }
}

// Chaque espace montre SES panneaux et masque ceux des autres : Formes (document, calques), Zones
// (régions, document), Numérisation (document, fils), Optimisation (diagnostic, ordre), Production
// (fils, document). Les panneaux IA restent où l'utilisateur les a mis. Affichage › Panneaux
// rouvre n'importe lequel ; la sélection et l'historique ne bougent jamais.
void ShellWindow::applyWorkspaceDocks(Workspace workspace) {
    struct Layout {
        Workspace workspace;
        std::vector<const char*> docks; // le premier passe au premier plan
    };
    static const Layout kLayouts[] = {
        {Workspace::Design, {"documentDock", "calqueDock"}},
        {Workspace::Digitize, {"documentDock", "threadDock"}},
        {Workspace::Zones, {"regionDock", "documentDock"}},
        {Workspace::Optimize, {"diagnosticDock", "optimizeDock", "documentDock"}},
        {Workspace::Produce, {"documentDock", "threadDock"}},
    };
    static const char* const kPrimary[] = {"documentDock",   "calqueDock",   "threadDock",
                                           "diagnosticDock", "optimizeDock", "regionDock"};
    const Layout* layout = nullptr;
    for (const auto& l : kLayouts) {
        if (l.workspace == workspace) {
            layout = &l;
        }
    }
    if (layout == nullptr) {
        return;
    }
    const auto wanted = [&](const char* name) {
        return std::ranges::any_of(layout->docks,
                                   [&](const char* d) { return std::string_view(d) == name; });
    };
    for (const char* name : kPrimary) {
        if (auto* dock = findChild<QDockWidget*>(QString::fromLatin1(name))) {
            dock->setVisible(wanted(name));
        }
    }
    if (auto* first = findChild<QDockWidget*>(QString::fromLatin1(layout->docks.front()))) {
        first->raise();
    }
}

void ShellWindow::setWorkspace(Workspace workspace) {
    // Rechoisir l'espace courant ne doit ni reconstruire le rail (et perdre le focus),
    // ni interrompre une interaction en cours. Le premier appel initialise le rail.
    if (workspace_ == workspace && !toolRail_->actions().isEmpty()) {
        return;
    }
    // Rebuilding the rail deletes its tool buttons. If one held keyboard focus,
    // return focus to the permanent canvas instead of leaving it on a deleted widget.
    QWidget* focused = QApplication::focusWidget();
    const bool focusWasInRail =
        focused != nullptr && (focused == toolRail_ || toolRail_->isAncestorOf(focused));
    // Move focus before removing toolbar actions: deleting the focused tool button
    // otherwise lets Qt choose an arbitrary successor during deferred destruction.
    if (focusWasInRail) {
        canvas_->setFocus(Qt::OtherFocusReason);
    }
    workspace_ = workspace;
    for (auto* a : workspaceActions_) {
        a->setChecked(a->data().toInt() == static_cast<int>(workspace));
    }
    rebuildToolRail();
    applyWorkspaceDocks(workspace);
    simBar_->setVisible(workspace == Workspace::Produce);
    if (workspace != Workspace::Produce && simLimit_) {
        setSimulationPosition(simulationTotal()); // quitter Production termine la simulation
    }
    refreshWindowTitle();
    if (focusWasInRail) {
        // Raising the dock can focus its first child after the old toolbar button
        // was removed. Restore focus synchronously once all workspace UI changes
        // are complete; do not steal focus when it originally belonged elsewhere.
        canvas_->setFocus(Qt::OtherFocusReason);
    }
}

void ShellWindow::rebuildToolRail() {
    allToolsMenu_->clear();
    for (auto* a : toolGroup_->actions()) {
        toolGroup_->removeAction(a);
        toolRail_->removeAction(a);
        a->deleteLater();
    }
    bool activeStillThere = false;
    for (const auto* tool : registry_.forWorkspace(workspace_)) {
        // Seuls Sélection et Déplacer la vue sont migrés dans ce lot : les autres sont AFFICHÉS
        // mais désactivés, avec la raison, pour ne pas faire croire à une parité qui n'existe pas.
        const bool implemented = isToolImplemented(tool->id);
        const QString label = fromUtf8(tool->label);
        // Les libellés sont visibles en mode étendu, et les pictogrammes restent présents
        // en mode compact. Les raccourcis sont des commandes, jamais les seuls noms d'outils.
        // Nom court dans le rail (le nom complet reste dans l'infobulle et la palette de
        // commandes).
        static const std::pair<const char*, const char*> kShort[] = {
            {"crop", "Recadrer"},
            {"draw.polygon-regular", "Polyg. régulier"},
            {"draw.bezier", "Plume"},
            {"draw.freeform", "Crayon"},
            {"warp-brush", "Déformation"},
            {"node.edit", "Nœuds"},
            {"fade.axis", "Axe du fondu"},
            {"stitch.points", "Points"},
            {"click-to-stitch", "Clic-broder"},
            {"detail-brush", "Zones détail"},
            {"fill.direction-guide", "Guide direction"},
            {"fill.break-line", "Ligne rupture"}};
        QString railLabel = label;
        for (const auto& [id, text] : kShort) {
            if (tool->id == id) {
                railLabel = QString::fromUtf8(text);
            }
        }
        auto* a = new QAction(toolIcon(tool->id, toolRail_->palette().color(QPalette::ButtonText)),
                              railLabel, toolGroup_);
        a->setCheckable(true);
        a->setProperty("fullLabel", label);
        a->setData(fromUtf8(tool->id));
        a->setEnabled(implemented);
        QString tip = tool->shortcut.empty()
                          ? label
                          : label + QStringLiteral(" (%1)").arg(fromUtf8(tool->shortcut));
        if (!implemented) {
            tip += tr(" — pas encore migré (docs/desktop-v2/02-inventaire-et-parite.md)");
        } else if (!tool->shortcut.empty()) {
            a->setShortcut(keyFor(tool->shortcut));
        }
        a->setToolTip(tip);
        toolRail_->addAction(a);
        if (auto* button = toolRail_->widgetForAction(a)) {
            button->setAccessibleName(label);
            if (!compactTools_) {
                // Colonne régulière : tous les boutons ont la même largeur (le rail fait 180 px).
                button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                button->setMinimumWidth(164);
            }
        }
        if (fromUtf8(tool->id) == activeTool_ && implemented) {
            a->setChecked(true);
            activeStillThere = true;
        }
        connect(a, &QAction::triggered, this, [this, a] {
            activeTool_ = a->data().toString();
            activeToolStatus_->setText(a->property("fullLabel").toString());
            applyToolToCanvas();
        });
    }
    // The rail stays contextual, but the menu is global: users can reach every
    // migrated tool without first knowing which workspace contains it.
    QSet<QString> menuToolIds;
    for (const auto w : kWorkspaces) {
        for (const auto* tool : registry_.forWorkspace(w)) {
            const QString id = fromUtf8(tool->id);
            if (menuToolIds.contains(id)) {
                continue;
            }
            menuToolIds.insert(id);
            const QString label = fromUtf8(tool->label);
            const bool implemented = isToolImplemented(tool->id);
            auto* menuAction = allToolsMenu_->addAction(
                toolIcon(tool->id, toolRail_->palette().color(QPalette::ButtonText)), label);
            menuAction->setObjectName(QStringLiteral("allTools_") + id);
            menuAction->setData(id);
            menuAction->setEnabled(implemented);
            QString tip = tool->shortcut.empty()
                              ? label
                              : label + QStringLiteral(" (%1)").arg(fromUtf8(tool->shortcut));
            if (!implemented) {
                tip += tr(" — pas encore migré (docs/desktop-v2/02-inventaire-et-parite.md)");
            }
            menuAction->setToolTip(tip);
            menuAction->setStatusTip(tip);
            const auto activate = [this, id] { (void)runCommand(QStringLiteral("tool.") + id); };
            connect(menuAction, &QAction::triggered, this, activate);
        }
    }
    if (!activeStillThere) {
        activeTool_ = QStringLiteral("select");
        for (auto* a : toolGroup_->actions()) {
            a->setChecked(a->data().toString() == activeTool_);
        }
    }
    // A workspace switch may fall back to Selection without triggering an action.
    // Keep the persistent status label in sync with the actual active tool.
    for (auto* a : toolGroup_->actions()) {
        if (a->data().toString() == activeTool_) {
            activeToolStatus_->setText(a->property("fullLabel").toString());
            break;
        }
    }
    if (const auto* tool = registry_.find(activeTool_.toStdString())) {
        activeToolStatus_->setText(fromUtf8(tool->label));
    }
    for (auto* action : allToolsMenu_->actions()) {
        action->setCheckable(true);
        action->setChecked(action->data().toString() == activeTool_);
    }
}

// --- Document, sélection, aperçu -------------------------------------------------------------

void ShellWindow::buildCentral() {
    auto* container = new QWidget(this);
    container->setObjectName(QStringLiteral("centralContainer"));
    auto* column = new QVBoxLayout(container);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    guideStrip_ = new QWidget(container);
    guideStrip_->setObjectName(QStringLiteral("guideStrip"));
    auto* stripColumn = new QVBoxLayout(guideStrip_);
    stripColumn->setContentsMargins(10, 6, 10, 6);
    stripColumn->setSpacing(2);
    auto* row = new QHBoxLayout;
    guideSteps_ = new QLabel(guideStrip_);
    guideSteps_->setObjectName(QStringLiteral("guideSteps"));
    guideSteps_->setTextFormat(Qt::RichText);
    guideHeadline_ = new QLabel(guideStrip_);
    guideHeadline_->setObjectName(QStringLiteral("guideHeadline"));
    guideHeadline_->setWordWrap(true);
    guideNext_ = new QPushButton(guideStrip_);
    guideNext_->setObjectName(QStringLiteral("guideNext"));
    guideNext_->setDefault(true);
    row->addWidget(guideSteps_);
    row->addStretch(1);
    row->addWidget(guideNext_);
    stripColumn->addLayout(row);
    stripColumn->addWidget(guideHeadline_);
    column->addWidget(guideStrip_);

    stack_ = new QStackedWidget(container);
    welcome_ = new WelcomePage(stack_);
    stack_->addWidget(welcome_);
    stack_->addWidget(canvas_);
    column->addWidget(stack_, 1);
    setCentralWidget(container);

    connect(welcome_, &WelcomePage::openRequested, this,
            [this] { runCommand(QStringLiteral("file.open")); });
    connect(welcome_, &WelcomePage::newRequested, this,
            [this] { runCommand(QStringLiteral("file.new")); });
    connect(welcome_, &WelcomePage::recentRequested, this, [this](const QString& path) {
        openProject(std::filesystem::path(path.toStdU16String()));
    });
    connect(guideNext_, &QPushButton::clicked, this, [this] { runGuideNext(); });
    guideSteps_->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                         Qt::LinksAccessibleByKeyboard);
    connect(guideSteps_, &QLabel::linkActivated, this, [this](const QString& link) {
        bool ok = false;
        const int index = link.mid(5).toInt(&ok);
        if (ok && index >= 0 && index < static_cast<int>(guide_.steps.size())) {
            (void)runGuideAction(guide_.steps[static_cast<std::size_t>(index)].fix);
        }
    });
    updateGuide();
}

std::vector<CommandEntry> ShellWindow::contextEntries() const {
    // Contextes actifs déduits de la sélection ; la liste des commandes vient du tableau du cœur.
    std::vector<CommandContext> contexts;
    const auto state = inspect(session_.project(), session_.selection().targets());
    if (!state.ids.empty()) {
        contexts.push_back(CommandContext::Object);
    }
    if (selectedTextOwner(session_.project(), state.ids).has_value()) {
        contexts.push_back(CommandContext::Text);
    }
    if (const auto target = pointTarget();
        target && satinRungCount(session_.project(), *target) >= 0) {
        contexts.push_back(CommandContext::Satin);
    }
    if (nodes_ != nullptr && nodes_->selectedCount() > 0) {
        contexts.push_back(CommandContext::Nodes);
    }
    const auto wanted = floatingBarCommands(contexts);
    const auto all = commandEntries();
    std::vector<CommandEntry> shown;
    for (const auto id : wanted) {
        for (const auto& e : all) {
            if (e.id == id && e.enabled) {
                shown.push_back(e);
            }
        }
    }
    return shown;
}

void ShellWindow::refreshFloatingBar() {
    if (floatingBar_ == nullptr) {
        return;
    }
    floatingBar_->setEntries(contextEntries());
}

void ShellWindow::showRadialMenu() {
    const auto entries = contextEntries();
    if (entries.empty()) {
        setNotice(tr("Menu radial : rien de sélectionné, donc aucune action à proposer."), false);
        return;
    }
    auto* radial = new RadialMenuWidget(this);
    radial->setAttribute(Qt::WA_DeleteOnClose);
    radial->setEntries(entries);
    connect(radial, &RadialMenuWidget::chosen, this, [this](const QString& id) { runCommand(id); });
    const QPoint cursor = QCursor::pos();
    const QPoint centre =
        canvas_->viewport()->rect().contains(canvas_->viewport()->mapFromGlobal(cursor))
            ? cursor
            : canvas_->viewport()->mapToGlobal(canvas_->viewport()->rect().center());
    radial->popupAt(centre);
}

bool ShellWindow::eventFilter(QObject* watched, QEvent* event) {
    if (floatingBar_ != nullptr && event->type() == QEvent::Resize &&
        watched == canvas_->viewport()) {
        floatingBar_->reposition();
    }
    return QMainWindow::eventFilter(watched, event);
}

void ShellWindow::updateGuide() {
    if (guideSteps_ == nullptr) {
        return;
    }
    GuideInput in;
    in.documentOpen = documentOpen_;
    in.hasObjects = !session_.project().embroidery_objects.empty();
    in.hasSegmentation = session_.project().segmentation.has_value();
    in.segmentationDirty = regions_ != nullptr && regions_->dirty();
    in.handEdits = countHandEdits(session_.project()).any();
    in.analysedEver = analysedRevision_.has_value();
    in.analysedCurrent = analysedRevision_ && *analysedRevision_ == session_.revision();
    in.analysisErrors = in.analysedCurrent ? analysisErrors_ : 0;
    in.analysisWarnings = in.analysedCurrent ? analysisWarnings_ : 0;
    in.checkSkipped = checkSkipped_;
    in.machineCheckedCurrent =
        machineCheckRevision_ && *machineCheckRevision_ == session_.revision();
    in.machineBlockers = in.machineCheckedCurrent ? machineBlockers_ : 0;
    in.machineWarnings = in.machineCheckedCurrent ? machineWarnings_ : 0;
    in.exportedEver = exportedRevision_.has_value();
    in.exportedCurrent = exportedRevision_ && *exportedRevision_ == session_.revision();
    guide_ = buildGuide(in);

    const QColor dim = palette().color(QPalette::Disabled, QPalette::WindowText);
    QStringList parts;
    int n = 1;
    for (const auto& s : guide_.steps) {
        const QString title = fromUtf8(s.title);
        const QString link =
            QStringLiteral("<a style='text-decoration:none' href='step:%1'>%2</a>");
        QString cell;
        switch (s.state) {
        case StepState::Done:
            cell = QStringLiteral("<span style='color:%1'>✓ %2</span>").arg(dim.name(), title);
            break;
        case StepState::Current:
            cell = QStringLiteral("<b>%1. %2</b>").arg(n).arg(title);
            break;
        case StepState::Todo:
            cell = QStringLiteral("<span style='color:%1'>%2. %3</span>")
                       .arg(dim.name())
                       .arg(n)
                       .arg(title);
            break;
        case StepState::Stale:
            cell = QStringLiteral("<b style='color:#c77700'>⟳ %1 (obsolète)</b>").arg(title);
            break;
        case StepState::Blocked:
            cell = QStringLiteral("<b style='color:#c0392b'>✖ %1 (erreur)</b>").arg(title);
            break;
        case StepState::Warning:
            cell = QStringLiteral("<span style='color:#c77700'>⚠ %1</span>").arg(title);
            break;
        case StepState::Skipped:
            cell = QStringLiteral("<span style='color:%1'>↷ %2 (ignoré)</span>")
                       .arg(dim.name(), title);
            break;
        }
        // Une phase qui a une action de retour (régénérer, relancer, voir les erreurs…) se clique.
        parts << (s.fix != NextAction::None ? link.arg(n - 1).arg(cell) : cell);
        ++n;
    }
    guideSteps_->setText(parts.join(QStringLiteral("  ›  ")));
    {
        QStringList tips;
        for (const auto& s : guide_.steps) {
            tips << QStringLiteral("%1 — %2. %3")
                        .arg(fromUtf8(s.title), fromUtf8(stepStateLabel(s.state)),
                             fromUtf8(s.detail));
        }
        for (const auto& issue : guide_.issues) {
            tips << QStringLiteral("%1 %2 (%3)")
                        .arg(issue.blocking ? QStringLiteral("⛔") : QStringLiteral("•"),
                             fromUtf8(issue.text), fromUtf8(issue.fixLabel));
        }
        guideSteps_->setToolTip(tips.join(QLatin1Char('\n')));
        guideSteps_->setAccessibleDescription(tips.join(QStringLiteral(" ")));
    }
    guideHeadline_->setText(fromUtf8(guide_.headline));
    guideNext_->setText(fromUtf8(guide_.nextLabel));
    guideNext_->setVisible(guide_.next != NextAction::None);
    guideStrip_->setVisible(documentOpen_);
    stack_->setCurrentWidget(documentOpen_ ? static_cast<QWidget*>(canvas_)
                                           : static_cast<QWidget*>(welcome_));
    QStringList recent;
    for (const auto& r : recent_) {
        recent << QString::fromUtf8(r.data(), static_cast<qsizetype>(r.size()));
    }
    welcome_->setRecent(recent);
}

bool ShellWindow::welcomeVisible() const {
    return stack_ != nullptr && stack_->currentWidget() == welcome_;
}

QString ShellWindow::guideHeadline() const {
    return guideHeadline_ != nullptr ? guideHeadline_->text() : QString();
}

bool ShellWindow::runGuideAction(NextAction action) {
    switch (action) {
    case NextAction::OpenImage:
        return runCommand(QStringLiteral("file.open"));
    case NextAction::Regenerate:
        if (regions_ == nullptr || !session_.project().segmentation) {
            return false;
        }
        regions_->regenerate(); // demande avant d'écraser des retouches
        return true;
    case NextAction::Analyse:
        setWorkspace(Workspace::Optimize);
        runDiagnostics();
        return true;
    case NextAction::ShowDiagnostics:
        setWorkspace(Workspace::Optimize);
        diagDock_->show();
        diagDock_->raise();
        return true;
    case NextAction::CheckMachine:
        setWorkspace(Workspace::Produce);
        return runCommand(QStringLiteral("export.check"));
    case NextAction::Export:
        return runCommand(QStringLiteral("file.export"));
    case NextAction::None:
        break;
    }
    return false;
}

bool ShellWindow::runGuideNext() {
    return runGuideAction(guide_.next);
}

void ShellWindow::setCheckSkipped(bool skipped) {
    if (checkSkipped_ == skipped) {
        return;
    }
    checkSkipped_ = skipped;
    updateGuide();
}

void ShellWindow::runOptimization(PathStrategy strategy, bool improve) {
    if (session_.project().embroidery_objects.size() < 2) {
        optimizePanel_->showMessage(tr("Il faut au moins deux objets brodés pour optimiser."));
        return;
    }
    if (optimizeTask_ != 0) {
        (void)tasks_.cancel(optimizeTask_); // une proposition périmée n'est jamais livrée
    }
    proposal_.reset();
    optimizePanel_->setBusy(true);
    const auto snapshot = session_.snapshot();
    const auto revision = session_.revision();
    optimizeTask_ = tasks_.submit<PathProposal>(
        tr("Optimisation de l'ordre").toStdString(),
        [snapshot, revision, strategy, improve](const TaskContext& ctx) {
            return proposeOrder(snapshot, revision, strategy, improve, ctx);
        },
        [this](Result<PathProposal>&& r) {
            optimizeTask_ = 0;
            if (!r) {
                optimizePanel_->showMessage(
                    tr("Optimisation impossible : %1").arg(fromUtf8(r.error().message)));
                return;
            }
            proposal_ = std::move(*r);
            optimizePanel_->showProposal(*proposal_);
            optimizePanel_->setStale(proposal_->revision != session_.revision());
            setNotice(tr("Proposition d'ordre calculée : vérifiez les mesures puis acceptez ou "
                         "refusez."),
                      false);
        });
}

bool ShellWindow::acceptOptimization() {
    if (!proposal_) {
        return false;
    }
    const auto r = applyProposal(session_, *proposal_);
    if (!r.ok) {
        optimizePanel_->setStale(true);
        setNotice(tr("Optimisation : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    proposal_.reset();
    optimizePanel_->showMessage(tr("Ordre appliqué. Annulable en une étape (Ctrl+Z)."));
    setNotice(tr("Ordre de couture optimisé."), false);
    return true;
}

void ShellWindow::refuseOptimization() {
    proposal_.reset();
    optimizePanel_->clearProposal();
    setNotice(tr("Proposition refusée : le document n'a pas changé."), false);
}

bool ShellWindow::setLiveBridgeEnabled(bool on, QString* error) {
    if (!on) {
        if (liveServer_) {
            // Invalide aussi les calculs d'agent déjà en cours : après la désactivation, plus
            // rien ne peut être appliqué, même une demande déjà approuvée à l'écran.
            session_.liveBridge().reset();
            liveServer_.reset();
        }
        companionPanel_->setActive(false, {});
        setNotice(tr("Compagnon IA : pont désactivé."), false);
        return true;
    }
    if (liveServer_) {
        return true;
    }
    ensureCompanion();
    auto server = std::make_unique<LiveServer>(companion_->service(), this);
    QString why;
    const auto dir = liveDir_.empty() ? mcp::live_directory() : liveDir_;
    if (!server->start(dir, &why)) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    }
    liveServer_ = std::move(server);
    companionPanel_->setActive(true, liveServer_->endpoint());
    companionPanel_->setCanUndo(session_.canUndo(), fromUtf8(session_.undoName()));
    setNotice(tr("Compagnon IA : pont activé. Chaque modification demandera votre accord."), false);
    return true;
}

LiveCompanion& ShellWindow::ensureCompanion() {
    if (!companion_) {
        companion_ = std::make_unique<LiveCompanion>(
            session_,
            [this](const mcp::ApprovalRequest& request) {
                return approvalOverride_ ? approvalOverride_(request)
                                         : askAgentApproval(this, request);
            },
            [this] {
                return currentPath_.empty() ? std::string("(sans titre)")
                                            : currentPath_.filename().string();
            });
        companion_->setOnChanged([this] {
            companionPanel_->setEvents(companion_->events());
            companionPanel_->setCanUndo(session_.canUndo(), fromUtf8(session_.undoName()));
        });
    }
    return *companion_;
}

void ShellWindow::setChatBackend(CompanionBackend* backend) {
    chatBackend_ = backend;
    chatPanel_->setBackend(backend);
}

void ShellWindow::openChat() {
    if (chatBackend_ == nullptr) {
        // Fournisseur réel : une demande = une exécution de l'agent hors fil d'interface ; ses
        // outils passent par le pont (accord et annulation) et jamais directement par le document.
        auto& live = ensureCompanion();
        if (!secretStore_) {
            secretStore_ = makeSystemSecretStore();
        }
        if (claudeBackend_ == nullptr) {
            claudeBackend_ = new ClaudeCompanionBackend(live.service(), *secretStore_, {}, this);
            claudeBackend_->setDiagnostics(&companionLog_);
            companionLog_.set_enabled(companion_settings::diagnosticsEnabled());
            companionLog_.set_content_consent(companion_settings::diagnosticsEnabled() &&
                                              companion_settings::diagnosticsContentConsent());
        }
        chatBackend_ = claudeBackend_;
        chatPanel_->setBackend(chatBackend_);
    }
    chatDock_->show();
    chatDock_->raise();
}

void ShellWindow::showCompanionSettings() {
    if (!secretStore_) {
        secretStore_ = makeSystemSecretStore();
    }
    CompanionSettingsDialog dialog(*secretStore_, companionLog_, this);
    if (dialog.exec() == QDialog::Accepted) {
        dialog.applyToSettings();
        if (claudeBackend_ != nullptr) {
            claudeBackend_->refresh();
        }
        chatPanel_->setBackend(chatBackend_); // relit l'état « prêt » du fournisseur
    }
}

bool ShellWindow::liveBridgeEnabled() const {
    return liveServer_ != nullptr && liveServer_->running();
}

QString ShellWindow::liveEndpoint() const {
    return liveServer_ ? liveServer_->endpoint() : QString();
}

std::string ShellWindow::liveToken() const {
    return liveServer_ ? liveServer_->token() : std::string();
}

void ShellWindow::onDocumentChanged(ChangeKind kind) {
    overlays_->onDocumentChanged();
    if (kind == ChangeKind::Replaced && chatPanel_ != nullptr) {
        // Nouveau document : l'ancienne conversation parle d'un autre motif.
        chatPanel_->clearConversation();
        if (claudeBackend_ != nullptr) {
            claudeBackend_->resetConversation();
        }
    }
    if (regions_) {
        if (kind == ChangeKind::Replaced) {
            regions_->onDocumentReplaced(); // nouveau document : ni région choisie ni correction en
                                            // attente
        } else {
            regions_->refresh();
        }
    }
    if (optimizePanel_ != nullptr) {
        optimizePanel_->setEnabledForDocument(session_.project().embroidery_objects.size() >= 2);
        if (proposal_ && proposal_->revision != session_.revision()) {
            optimizePanel_->setStale(true);
        }
    }
    if (kind != ChangeKind::Replaced && feedbackCue_ != nullptr) {
        feedbackCue_->pulse();
    }
    if (companionPanel_ != nullptr) {
        companionPanel_->setCanUndo(session_.canUndo(), fromUtf8(session_.undoName()));
    }
    refreshWindowTitle();
    rebuildObjectList();
    refreshActionStates();
    if (diagnostics_ != nullptr) {
        diagnostics_->setStale(true); // l'analyse affichée ne correspond plus au document
    }
    previewDebounce_->start(); // aperçu regroupé ; waitForIdle() le déclenche sans attendre
}

void ShellWindow::refreshActionStates() {
    const bool hasObjects = !session_.project().embroidery_objects.empty();
    undoAction_->setEnabled(session_.canUndo());
    redoAction_->setEnabled(session_.canRedo());
    undoAction_->setText(session_.canUndo()
                             ? tr("&Annuler « %1 »").arg(fromUtf8(session_.undoName()))
                             : tr("&Annuler"));
    redoAction_->setText(session_.canRedo()
                             ? tr("&Rétablir « %1 »").arg(fromUtf8(session_.redoName()))
                             : tr("&Rétablir"));
    saveAction_->setEnabled(documentOpen_);
    exportAction_->setEnabled(hasObjects);
    updateGuide();
    refreshFloatingBar();
}

void ShellWindow::rebuildObjectList() {
    const auto& objects = session_.project().embroidery_objects;
    // Mise à jour EN PLACE quand la liste d'objets est la même : cette méthode peut être appelée
    // depuis le signal `itemChanged` de l'item qu'elle rafraîchit, qu'il ne faut donc pas détruire.
    bool sameItems = static_cast<std::size_t>(objectTree_->topLevelItemCount()) == objects.size();
    for (std::size_t i = 0; sameItems && i < objects.size(); ++i) {
        sameItems =
            objectTree_->topLevelItem(static_cast<int>(i))->data(0, kIdRole).toULongLong() ==
            objects[i].id.value;
    }
    syncingTree_ = true;
    if (!sameItems) {
        objectTree_->clear();
    }
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& o = objects[i];
        QTreeWidgetItem* item = sameItems ? objectTree_->topLevelItem(static_cast<int>(i))
                                          : new QTreeWidgetItem(objectTree_);
        item->setData(0, kIdRole, static_cast<qulonglong>(o.id.value));
        item->setText(0, fromUtf8(o.name));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        item->setCheckState(0, o.visible ? Qt::Checked : Qt::Unchecked);
        item->setText(1, stitchTypeText(o));
        QPixmap swatch(14, 14);
        swatch.fill(QColor(o.rgb[0], o.rgb[1], o.rgb[2]));
        item->setIcon(1, QIcon(swatch));
    }
    syncingTree_ = false;
    syncTreeFromSelection();
}

void ShellWindow::syncTreeFromSelection() {
    syncingTree_ = true;
    objectTree_->clearSelection();
    for (int i = 0; i < objectTree_->topLevelItemCount(); ++i) {
        auto* item = objectTree_->topLevelItem(i);
        const Target t{TargetKind::EmbroideryObject, item->data(0, kIdRole).toULongLong()};
        if (session_.selection().contains(t)) {
            item->setSelected(true);
        }
    }
    syncingTree_ = false;
    const auto& objects = session_.project().embroidery_objects;
    if (objects.empty()) {
        summary_->setText(tr("Aucun objet de broderie."));
    } else {
        summary_->setText(tr("%1 objet(s), %2 sélectionné(s)")
                              .arg(objects.size())
                              .arg(session_.selection().targets().size()));
    }
}

void ShellWindow::onSelectionChangedInTree() {
    if (syncingTree_) {
        return;
    }
    std::vector<Target> targets;
    for (auto* item : objectTree_->selectedItems()) {
        targets.push_back({TargetKind::EmbroideryObject, item->data(0, kIdRole).toULongLong()});
    }
    syncingTree_ = true; // l'abonné de la sélection ne reconstruit pas l'arbre qui vient de changer
    session_.selection().replace(std::move(targets));
    syncingTree_ = false;
    syncTreeFromSelection();
}

void ShellWindow::requestPreview() {
    if (previewTask_ != 0) {
        tasks_.cancel(previewTask_); // un aperçu périmé n'est jamais livré
    }
    if (!documentOpen_) {
        canvas_->clearImage();
        previewShown_ = false;
        frame_ = PreviewImage{};
        return;
    }
    const auto snapshot = session_.snapshot();
    const auto revision = session_.revision();
    previewRevision_ = revision;
    const auto limit = simLimit_; // simulation : nombre de commandes à dessiner
    const RenderSettings quality = render_;
    previewTask_ = tasks_.submit<PreviewImage>(
        tr("Aperçu").toStdString(),
        [snapshot, revision, limit, quality](const TaskContext& ctx) {
            return render_preview(snapshot, revision, quality, ctx, limit);
        },
        [this](Result<PreviewImage>&& r) {
            if (!r) {
                setNotice(tr("Aperçu impossible : %1").arg(fromUtf8(r.error().message)), true);
                return;
            }
            if (r->revision != session_.revision()) {
                return; // le document a changé depuis : un autre aperçu est en route
            }
            QImage image;
            if (image.loadFromData(r->png.data(), static_cast<int>(r->png.size()), "PNG")) {
                canvas_->setImage(image);
                previewShown_ = true;
                frame_ = std::move(*r);
                frame_.png.clear(); // seul le repère et l'index sont conservés
                frame_.png.shrink_to_fit();
                canvas_->setHoopRect(QRectF(QPointF(frame_.mmToPxX(-frame_.hoop_half_w_mm),
                                                    frame_.mmToPxY(-frame_.hoop_half_h_mm)),
                                            QPointF(frame_.mmToPxX(frame_.hoop_half_w_mm),
                                                    frame_.mmToPxY(frame_.hoop_half_h_mm))));
                updateGrid();
                updateGuides();
                updateSourceOverlay();
                updatePointMarkers();
                refreshSimulationBar();
                updateSelectionBoxes();
                overlays_->onPreviewReady();
            }
        });
}

bool ShellWindow::previewShown() const {
    return previewShown_;
}

// --- Fichiers --------------------------------------------------------------------------------

void ShellWindow::refreshWindowTitle() {
    const QString name = currentPath_.empty()
                             ? tr("Sans titre")
                             : QString::fromStdU16String(currentPath_.filename().u16string());
    setWindowTitle(tr("OpenStitch V2 — %1 — %2[*]").arg(name, workspaceLabel(workspace_)));
    setWindowModified(documentOpen_ && session_.dirty());
}

bool ShellWindow::saveCurrentProject() {
    QString path;
    if (!currentPath_.empty() && currentPath_.extension() == ".osp") {
        path = QString::fromStdU16String(currentPath_.u16string());
    }
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"), QString(),
                                            tr("Projet OpenStitch (*.osp)"));
    }
    return !path.isEmpty() && saveProject(std::filesystem::path(path.toStdU16String()));
}

bool ShellWindow::saveProjectAs() {
    if (!documentOpen_) {
        return false;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Enregistrer le projet sous"),
        currentPath_.empty() ? QString() : QString::fromStdU16String(currentPath_.u16string()),
        tr("Projet OpenStitch (*.osp)"));
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QStringLiteral(".osp"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".osp");
    }
    return saveProject(std::filesystem::path(path.toStdU16String()));
}

bool ShellWindow::exportAgain() {
    if (!documentOpen_ || lastExportPath_.empty()) {
        setNotice(tr("Aucun export précédent : utilisez « Exporter pour la machine »."), true);
        return false;
    }
    exportDst(lastExportPath_);
    return true;
}

bool ShellWindow::confirmDiscardOrSave() {
    if (!documentOpen_ || !session_.dirty()) {
        return true;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Modifications non enregistrées"),
        tr("Ce projet contient des modifications non enregistrées. Voulez-vous les enregistrer "
           "avant de continuer ?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        return saveCurrentProject();
    }
    return answer == QMessageBox::Discard;
}

void ShellWindow::cancelPendingDocumentLoad() {
    ++loadGeneration_;
    if (documentLoadTask_ != 0) {
        (void)tasks_.cancel(documentLoadTask_);
        documentLoadTask_ = 0;
    }
}

void ShellWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && regions_ && regions_->active() &&
        !regions_->selection().empty()) {
        regions_->clearSelection();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && activeTool_ == QStringLiteral("stitch.points") &&
        pointSel_ >= 0) {
        clearPointSelection();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void ShellWindow::closeEvent(QCloseEvent* event) {
    if (!confirmDiscardOrSave()) {
        event->ignore();
        return;
    }
    cancelPendingDocumentLoad();
    QMainWindow::closeEvent(event);
}

void ShellWindow::chooseAndOpen() {
    QStringList machine;
    for (const auto& pattern : readableMachineFormats().front().patterns) {
        machine << fromUtf8(pattern);
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Ouvrir"), QString(),
        tr("Tout ce qui s'ouvre (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.osp %1 *.dxf);;"
           "Images et projets (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.osp);;"
           "Broderies machine (%1);;Dessin AutoCAD (*.dxf);;Tous les fichiers (*)")
            .arg(machine.join(QLatin1Char(' '))));
    if (path.isEmpty()) {
        return;
    }
    const std::filesystem::path p(path.toStdU16String());
    if (path.endsWith(QStringLiteral(".osp"), Qt::CaseInsensitive)) {
        openProject(p);
    } else if (isMachineDesignFile(p)) {
        importMachineDesign(p);
    } else if (path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive)) {
        importDxfFile(p);
    } else {
        importImage(p);
    }
}

void ShellWindow::resegmentImage() {
    const image::Image* working = documentOpen_ ? ensureProcessed() : nullptr;
    if (working == nullptr || working->empty() || !session_.project().segmentation) {
        setNotice(tr("Re-segmenter demande un projet numérisé depuis une image."), true);
        return;
    }
    if (regions_ != nullptr && regions_->dirty()) {
        setNotice(
            tr("Régénérez d'abord les objets (ou annulez) : des corrections de régions sont en "
               "attente."),
            true);
        return;
    }
    if (importDialog_ != nullptr) {
        importDialog_->close();
    }
    auto* dialog = new ImportDialog(QString(), 0.0, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    QImage preview(working->rgba.data(), working->width, working->height, working->width * 4,
                   QImage::Format_RGBA8888);
    dialog->setResegmentMode(preview.copy());
    dialog->setSettings(lastDigitizeSettings_);
    importDialog_ = dialog;
    QPointer<ImportDialog> guard(dialog);
    const image::Image image = *working; // copie : la tâche ne lit jamais l'état de l'interface
    connect(dialog, &ImportDialog::previewRequested, this,
            [this, guard, image](segmentation::SegmentationOptions options) {
                if (importPreviewTask_ != 0) {
                    (void)tasks_.cancel(importPreviewTask_);
                }
                importPreviewTask_ = tasks_.submit<SegmentationPreview>(
                    tr("Aperçu de la segmentation").toStdString(),
                    [image, options](const TaskContext& ctx) {
                        return previewSegmentation(image, options, ctx);
                    },
                    [this, guard](Result<SegmentationPreview>&& r) {
                        importPreviewTask_ = 0;
                        if (guard == nullptr) {
                            return;
                        }
                        if (!r) {
                            guard->setPreviewError(fromUtf8(r.error().message));
                            return;
                        }
                        guard->setPreview(*r);
                    });
            });
    connect(dialog, &QDialog::accepted, this, [this, guard] {
        if (guard == nullptr) {
            return;
        }
        lastDigitizeSettings_ = guard->settings();
        regions_->regenerate(true); // re-segmente avec ces réglages, puis régénère les objets
    });
    dialog->open();
}

void ShellWindow::chooseAndImportChart() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Importer un nuancier de fils"), QString(),
                                     tr("Nuanciers (*.csv *.json);;Tous les fichiers (*)"));
    if (path.isEmpty()) {
        return;
    }
    const auto dir = stateDir_.empty()
                         ? std::filesystem::temp_directory_path() / "openstitch_charts"
                         : stateDir_ / "thread_charts";
    const auto r = importThreadChart(std::filesystem::path(path.toStdU16String()), dir);
    if (!r) {
        setNotice(tr("Import du nuancier impossible : %1 %2")
                      .arg(fromUtf8(r.error().message), fromUtf8(r.error().detail)),
                  true);
        return;
    }
    setNotice(tr("Nuancier « %1 » importé : %2 fils (retrouvé au prochain démarrage).")
                  .arg(fromUtf8(r->name))
                  .arg(r->count),
              false);
}

void ShellWindow::removeImportedChart() {
    QStringList names;
    std::vector<std::string> ids;
    for (const auto& c : chartInfos()) {
        if (c.user) {
            names << fromUtf8(c.name);
            ids.push_back(c.id);
        }
    }
    if (names.isEmpty()) {
        setNotice(tr("Aucun nuancier importé à retirer."), false);
        return;
    }
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, tr("Retirer un nuancier importé"),
                                               tr("Nuancier à retirer :"), names, 0, false, &ok);
    if (ok) {
        const auto dir = stateDir_.empty()
                             ? std::filesystem::temp_directory_path() / "openstitch_charts"
                             : stateDir_ / "thread_charts";
        const bool removed =
            removeUserChart(ids[static_cast<std::size_t>(names.indexOf(pick))], dir);
        setNotice(removed ? tr("Nuancier retiré.") : tr("Nuancier introuvable."), !removed);
    }
}

void ShellWindow::chooseAndExportThreads() {
    if (!documentOpen_) {
        return;
    }
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Exporter la liste des fils"),
                                     QStringLiteral("fils.csv"), tr("Fichier CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    std::size_t rows = 0;
    const auto r = exportThreadListCsv(session_.project(),
                                       std::filesystem::path(path.toStdU16String()), &rows);
    setNotice(r.ok ? tr("Liste des fils exportée (%1 fils).").arg(rows) : fromUtf8(r.message),
              !r.ok);
}

void ShellWindow::chooseAndImportMachine() {
    QStringList filters;
    for (const auto& f : readableMachineFormats()) {
        QStringList patterns;
        for (const auto& p : f.patterns) {
            patterns << fromUtf8(p);
        }
        filters << QStringLiteral("%1 (%2)").arg(fromUtf8(f.label),
                                                 patterns.join(QLatin1Char(' ')));
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Importer une broderie machine"), QString(), filters.join(QStringLiteral(";;")));
    if (!path.isEmpty()) {
        importMachineDesign(std::filesystem::path(path.toStdU16String()));
    }
}

void ShellWindow::chooseAndImportDxf() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Importer un DXF"), QString(),
                                                      tr("Dessin AutoCAD (*.dxf)"));
    if (!path.isEmpty()) {
        importDxfFile(std::filesystem::path(path.toStdU16String()));
    }
}

void ShellWindow::chooseAndExportDxf() {
    QString path = QFileDialog::getSaveFileName(this, tr("Exporter les contours en DXF"), QString(),
                                                tr("Dessin AutoCAD (*.dxf)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".dxf");
    }
    exportDxfFile(std::filesystem::path(path.toStdU16String()));
}

void ShellWindow::importMachineDesign(const std::filesystem::path& file) {
    if (!confirmDiscardOrSave()) {
        return;
    }
    cancelPendingDocumentLoad();
    const auto generation = loadGeneration_;
    const auto revision = session_.revision();
    const QString name = QString::fromStdU16String(file.filename().u16string());
    setNotice(tr("Import de %1…").arg(name), false);
    documentLoadTask_ = tasks_.submit<document::Project>(
        tr("Import").toStdString(), [file](const TaskContext&) { return loadMachineProject(file); },
        [this, name, generation, revision](Result<document::Project>&& r) {
            if (generation != loadGeneration_) {
                return;
            }
            documentLoadTask_ = 0;
            if (revision != session_.revision()) {
                setNotice(tr("Document modifié pendant l'import : résultat ignoré."), true);
                return;
            }
            if (!r) {
                setNotice(tr("Import impossible : %1 (DST, PES, JEF, EXP… attendus)")
                              .arg(fromUtf8(r.error().message)),
                          true);
                return;
            }
            currentPath_.clear(); // un design importé n'a pas encore de projet enregistré
            session_.selection().clear();
            documentOpen_ = true;
            if (autosave_) {
                autosave_->clear();
            }
            session_.replaceProject(std::move(*r), true);
            setNotice(
                tr("%1 importé comme broderie machine : à numériser ou à exporter.").arg(name),
                false);
        });
}

void ShellWindow::importDxfFile(const std::filesystem::path& file) {
    if (!documentOpen_) {
        newProject();
        if (!documentOpen_) {
            return; // « nouveau » annulé par l'utilisateur
        }
    }
    const std::string path =
        file.u8string().empty()
            ? std::string()
            : std::string(reinterpret_cast<const char*>(file.u8string().c_str()));
    std::string escaped;
    for (const char c : path) {
        if (c == '"' || c == '\\') {
            escaped.push_back('\\');
        }
        escaped.push_back(c);
    }
    const auto result = session_.applyBatch(
        R"({"schema":"openstitch.batch/1","operations":[{"op":"import_dxf","file":")" + escaped +
            R"("}]})",
        tr("Importer un DXF").toStdString());
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        setNotice(tr("Import DXF impossible : %1")
                      .arg(outcomes.empty() ? tr("refusé") : fromUtf8(outcomes.back().message)),
                  true);
        return;
    }
    setNotice(tr("DXF importé : tracés ajoutés comme objets vectoriels (annulable)."), false);
}

void ShellWindow::exportDxfFile(const std::filesystem::path& file) {
    std::size_t count = 0;
    const auto r = exportVectorsToDxf(session_.project(), file, &count);
    if (!r.ok) {
        setNotice(tr("Export DXF impossible : %1").arg(fromUtf8(r.message)), true);
        return;
    }
    setNotice(tr("%1 tracé(s) exporté(s) en DXF.").arg(count), false);
}

void ShellWindow::importImage(const std::filesystem::path& image) {
    if (importDialog_ != nullptr) {
        importDialog_->close();
    }
    auto* dialog =
        new ImportDialog(QString::fromStdU16String(image.u16string()), heightSpin_->value(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    importDialog_ = dialog;
    QPointer<ImportDialog> guard(dialog);
    connect(dialog, &ImportDialog::previewRequested, this,
            [this, guard, image](segmentation::SegmentationOptions options) {
                if (importPreviewTask_ != 0) {
                    (void)tasks_.cancel(importPreviewTask_); // un aperçu périmé n'est jamais livré
                }
                importPreviewTask_ = tasks_.submit<SegmentationPreview>(
                    tr("Aperçu de la segmentation").toStdString(),
                    [image, options](const TaskContext& ctx) {
                        return previewSegmentation(image, options, ctx);
                    },
                    [this, guard](Result<SegmentationPreview>&& r) {
                        importPreviewTask_ = 0;
                        if (guard == nullptr) {
                            return; // la fenêtre a été fermée pendant le calcul
                        }
                        if (!r) {
                            guard->setPreviewError(fromUtf8(r.error().message));
                            return;
                        }
                        guard->setPreview(*r);
                    });
            });
    connect(dialog, &QDialog::accepted, this, [this, guard, image] {
        if (guard == nullptr) {
            return;
        }
        openImage(image, guard->heightMm(), guard->settings());
    });
    dialog->open();
}

void ShellWindow::openImage(const std::filesystem::path& image, double heightMm,
                            const project_ops::DigitizeSettings& settings) {
    if (!confirmDiscardOrSave()) {
        return;
    }
    cancelPendingDocumentLoad();
    const auto generation = loadGeneration_;
    const auto revision = session_.revision();
    DigitizeRequest request;
    request.image = image;
    request.target_height_mm = heightMm;
    request.settings = settings;
    lastDigitizeSettings_ = settings; // la régénération des objets réutilise ces réglages
    setNotice(
        tr("Numérisation de %1…").arg(QString::fromStdU16String(image.filename().u16string())),
        false);
    documentLoadTask_ = tasks_.submit<DigitizeOutput>(
        tr("Numérisation").toStdString(),
        [request](const TaskContext& ctx) { return run_digitize(request, ctx); },
        [this, image, generation, revision](Result<DigitizeOutput>&& r) {
            if (generation != loadGeneration_) {
                return; // un chargement plus récent a remplacé cette demande
            }
            documentLoadTask_ = 0;
            if (revision != session_.revision()) {
                setNotice(tr("Document modifié pendant l'import : résultat ignoré."), true);
                return;
            }
            if (!r) {
                setNotice(tr("Numérisation impossible : %1 %2")
                              .arg(fromUtf8(r.error().message), fromUtf8(r.error().hint)),
                          true);
                return;
            }
            currentPath_ = image;
            session_.selection().clear();
            const auto stats = r->stats;
            documentOpen_ = true;
            if (autosave_) {
                autosave_->clear();
            }
            session_.replaceProject(std::move(r->project), false);
            setNotice(tr("%1 objets, %2 points, %3 × %4 mm")
                          .arg(r->report.embroidery_objects)
                          .arg(stats.stitches)
                          .arg(r->width_mm, 0, 'f', 1)
                          .arg(r->height_mm, 0, 'f', 1),
                      false);
        });
}

void ShellWindow::openProject(const std::filesystem::path& osp) {
    if (!confirmDiscardOrSave()) {
        return;
    }
    cancelPendingDocumentLoad();
    const auto generation = loadGeneration_;
    const auto revision = session_.revision();
    setNotice(tr("Ouverture de %1…").arg(QString::fromStdU16String(osp.filename().u16string())),
              false);
    documentLoadTask_ = tasks_.submit<document::Project>(
        tr("Ouverture").toStdString(),
        [osp](const TaskContext&) { return project_io::load_project(osp); },
        [this, osp, generation, revision](Result<document::Project>&& r) {
            if (generation != loadGeneration_) {
                return;
            }
            documentLoadTask_ = 0;
            if (revision != session_.revision()) {
                setNotice(tr("Document modifié pendant l'ouverture : résultat ignoré."), true);
                return;
            }
            if (!r) {
                setNotice(tr("Ouverture impossible : %1").arg(fromUtf8(r.error().message)), true);
                return;
            }
            currentPath_ = osp;
            session_.selection().clear();
            documentOpen_ = true;
            if (autosave_) {
                autosave_->clear();
            }
            session_.replaceProject(std::move(*r), true);
            noteRecent(osp);
            setNotice(
                tr("Projet ouvert (%1 objets).").arg(session_.project().embroidery_objects.size()),
                false);
        });
}

bool ShellWindow::saveProject(const std::filesystem::path& osp) {
    const auto saved = project_io::save_project(osp, session_.project());
    if (!saved) {
        setNotice(tr("Enregistrement impossible : %1").arg(fromUtf8(saved.error().message)), true);
        return false;
    }
    currentPath_ = osp;
    session_.markSaved();
    refreshWindowTitle();
    noteRecent(osp);
    if (autosave_) {
        autosave_->clear(); // le document est enregistré : plus rien à récupérer
    }
    setNotice(tr("Projet enregistré."), false);
    feedbackCue_->pulse();
    return true;
}

void ShellWindow::setStateDirectory(const std::filesystem::path& dir) {
    stateDir_ = dir;
    QSettings settings(QString::fromStdU16String((dir / "motion.ini").u16string()),
                       QSettings::IniFormat);
    const bool reduced = settings.value(QStringLiteral("visual/reducedMotion"), false).toBool();
    reducedMotionAction_->setChecked(reduced);
    feedbackCue_->setReducedMotion(reduced);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    QSettings uiSettings(QString::fromStdU16String((dir / "ui.ini").u16string()),
                         QSettings::IniFormat);
    setCompactToolRail(uiSettings.value(QStringLiteral("tools/compact"), true).toBool());
    {
        // Préférences d'interface : thème et densité, relus sans jamais échouer. Appliquées avant
        // de redessiner quoi que ce soit ; stateDir_ est déjà fixé (elles se réenregistrent).
        const Preferences prefs = loadPreferences(dir / "preferences.txt");
        density_ = prefs.density;
        setThemeMode(QString::fromStdString(prefs.theme));
        setDensity(prefs.density);
        setRenderSettings(prefs.render);
    }
    (void)loadUserCharts(dir / "thread_charts");
    autosave_ = std::make_unique<AutosaveStore>(dir);
    recent_ = pruneMissingRecent(loadRecentList(dir / "recent.txt"));
    rebuildRecentMenu();
    if (autosaveTimer_ == nullptr) {
        autosaveTimer_ = new QTimer(this);
        autosaveTimer_->setInterval(60000);
        connect(autosaveTimer_, &QTimer::timeout, this, [this] { (void)autosaveNow(); });
    }
    autosaveTimer_->start();
}

bool ShellWindow::autosaveNow() {
    if (!autosave_ || !documentOpen_ || !session_.dirty()) {
        return false;
    }
    return autosave_->write(session_.project()).has_value();
}

bool ShellWindow::recoverAutosave() {
    if (!hasRecovery() || !confirmDiscardOrSave()) {
        return false;
    }
    auto loaded = autosave_->load();
    if (!loaded) {
        setNotice(tr("Récupération impossible : %1").arg(fromUtf8(loaded.error().message)), true);
        return false;
    }
    cancelPendingDocumentLoad();
    currentPath_.clear();
    documentOpen_ = true;
    session_.selection().clear();
    session_.replaceProject(std::move(*loaded), false); // non enregistré : reste « modifié »
    setNotice(tr("Document récupéré : pensez à l'enregistrer."), false);
    return true;
}

void ShellWindow::discardRecovery() {
    if (autosave_) {
        autosave_->clear();
    }
}

void ShellWindow::noteRecent(const std::filesystem::path& osp) {
    const auto u8 = osp.u8string();
    recent_ = pushRecent(std::move(recent_),
                         std::string(reinterpret_cast<const char*>(u8.data()), u8.size()));
    if (!stateDir_.empty()) {
        (void)saveRecentList(stateDir_ / "recent.txt", recent_);
    }
    rebuildRecentMenu();
}

void ShellWindow::rebuildRecentMenu() {
    if (recentMenu_ == nullptr) {
        return;
    }
    recentMenu_->clear();
    for (const auto& entry : recent_) {
        const QString text = QString::fromUtf8(entry.data(), static_cast<int>(entry.size()));
        auto* a = recentMenu_->addAction(text);
        connect(a, &QAction::triggered, this,
                [this, text] { openProject(std::filesystem::path(text.toStdU16String())); });
    }
    recentMenu_->setEnabled(!recent_.empty());
}

void ShellWindow::rebuildHistoryMenu() {
    historyMenu_->clear();
    const auto names = session_.undoNames();
    // Du plus récent au plus ancien ; choisir l'entrée k annule k + 1 étapes.
    for (std::size_t k = 0; k < names.size() && k < 30; ++k) {
        const auto& name = names[names.size() - 1 - k];
        auto* a = historyMenu_->addAction(fromUtf8(name));
        connect(a, &QAction::triggered, this, [this, k] {
            for (std::size_t i = 0; i <= k; ++i) {
                if (!session_.undo()) {
                    break;
                }
            }
        });
    }
    historyMenu_->setEnabled(!names.empty());
}

void ShellWindow::exportDst(const std::filesystem::path& dst) {
    lastExportPath_ = dst;
    const auto snapshot = session_.snapshot();
    const auto exportRevision = session_.revision();
    setNotice(tr("Export DST en cours…"), false);
    tasks_.submit<stitch::StitchStats>(
        tr("Export DST").toStdString(),
        [snapshot, dst](const TaskContext& ctx) { return export_project_file(snapshot, dst, ctx); },
        [this, dst, exportRevision](Result<stitch::StitchStats>&& r) {
            if (!r) {
                setNotice(tr("Export impossible : %1 %2")
                              .arg(fromUtf8(r.error().message), fromUtf8(r.error().detail)),
                          true);
                return;
            }
            exportedRevision_ = exportRevision;
            updateGuide();
            setNotice(tr("DST écrit : %1 (%2 points)")
                          .arg(QString::fromStdString(dst.filename().string()))
                          .arg(r->stitches),
                      false);
            feedbackCue_->pulse();
        });
}

// --- Retour d'état ---------------------------------------------------------------------------

void ShellWindow::setNotice(const QString& text, bool error) {
    notice_->setText(text);
    notice_->setProperty("error", error);
    notice_->setStyleSheet(error ? QStringLiteral("color: #c62828; font-weight: 600;") : QString());
    notice_->setAccessibleDescription(error ? tr("Erreur") : tr("Information"));
}

QString ShellWindow::lastNotice() const {
    return notice_->text();
}

void ShellWindow::refreshTaskStatus() {
    const auto running = tasks_.running();
    if (running.empty()) {
        progress_->hide();
        cancelButton_->hide();
        return;
    }
    const auto& t = running.front();
    progress_->setRange(0, t.total == 0 ? 0 : static_cast<int>(t.total)); // 0 = indéterminé
    progress_->setValue(static_cast<int>(t.done));
    progress_->setFormat(QString::fromStdString(t.label));
    progress_->show();
    cancelButton_->show();
}

void ShellWindow::runDiagnostics() {
    if (session_.project().embroidery_objects.empty()) {
        setNotice(tr("Rien à analyser : le document ne contient aucun objet brodé."), true);
        return;
    }
    diagnostics_->setBusy(true);
    const auto snapshot = session_.snapshot();
    const auto revision = session_.revision();
    tasks_.submit<DiagnosticsResult>(
        tr("Diagnostic").toStdString(),
        [snapshot, revision](const TaskContext& ctx) {
            return run_diagnostics(snapshot, revision, ctx);
        },
        [this](Result<DiagnosticsResult>&& r) {
            diagnostics_->setBusy(false);
            if (!r) {
                setNotice(tr("Analyse impossible : %1").arg(fromUtf8(r.error().message)), true);
                return;
            }
            diagnostics_->setResult(*r);
            analysedRevision_ = r->revision;
            analysisErrors_ = static_cast<int>(r->errors);
            analysisWarnings_ = static_cast<int>(r->warnings);
            updateGuide();
            diagnostics_->setStale(r->revision != session_.revision());
            setNotice(
                tr("Analyse : %1 erreur(s), %2 avertissement(s).").arg(r->errors).arg(r->warnings),
                r->errors > 0);
        });
}
void ShellWindow::onFindingActivated(const project_ops::Finding& finding) {
    std::vector<Target> targets;
    for (const auto id : finding.objects) {
        const Target t{TargetKind::EmbroideryObject, id};
        if (session_.exists(t)) { // un constat ancien peut citer un objet supprimé depuis
            targets.push_back(t);
        }
    }
    if (!targets.empty()) {
        session_.selection().replace(std::move(targets));
    }
    if (finding.at_mm && frame_.px_per_mm > 0.0) {
        // Le constat est en mm modèle (Y vers le haut) ; l'aperçu est en mm écran (Y vers le bas).
        canvas_->centreOnPx(
            QPointF(frame_.mmToPxX((*finding.at_mm)[0]), frame_.mmToPxY(-(*finding.at_mm)[1])));
    }
}

void ShellWindow::setGridSpacing(double mm) {
    gridSpacingMm_ = mm > 0.0 ? mm : 0.0;
    updateGrid();
    updateGuides();
}

void ShellWindow::updateGuides() {
    if (frame_.px_per_mm <= 0.0 || (guidesX_.empty() && guidesY_.empty())) {
        canvas_->setGuideLines({}, {}, {}, guideColor_);
        return;
    }
    QVector<double> xs;
    QVector<double> ys;
    for (const double mm : guidesX_) {
        xs.push_back(frame_.mmToPxX(mm));
    }
    for (const double mm : guidesY_) {
        ys.push_back(frame_.mmToPxY(-mm)); // mm modèle (Y vers le haut) -> pixels écran
    }
    canvas_->setGuideLines(xs, ys, QRectF(0, 0, frame_.width, frame_.height), guideColor_);
}

void ShellWindow::addGuide(bool vertical, double mm) {
    if (!std::isfinite(mm) || std::fabs(mm) > 1000.0) {
        return;
    }
    auto& list = vertical ? guidesX_ : guidesY_;
    if (std::find(list.begin(), list.end(), mm) == list.end() && list.size() < 200) {
        list.push_back(mm);
    }
    updateGuides();
}

void ShellWindow::askGuide(bool vertical) {
    bool ok = false;
    const double mm = QInputDialog::getDouble(
        this, vertical ? tr("Guide vertical") : tr("Guide horizontal"),
        vertical ? tr("Position X (mm, 0 = centre du motif) :")
                 : tr("Position Y (mm, 0 = centre du motif, vers le haut) :"),
        0.0, -1000.0, 1000.0, 2, &ok);
    if (ok) {
        addGuide(vertical, mm);
    }
}

void ShellWindow::clearGuides() {
    guidesX_.clear();
    guidesY_.clear();
    updateGuides();
}

void ShellWindow::setGuideColor(const QColor& color) {
    if (color.isValid()) {
        guideColor_ = color;
        emit guideColorChanged();
        updateGuides();
    }
}

void ShellWindow::setGridStepMm(double mm) {
    gridSpacingMm_ = std::fabs(mm);
    updateGrid();
    updateGuides();
}

void ShellWindow::zoomIn() {
    canvas_->zoomBy(1.25);
}

void ShellWindow::zoomOut() {
    canvas_->zoomBy(0.8);
}

void ShellWindow::zoomToSelection() {
    canvas_->fitSelection();
}

void ShellWindow::zoomActual() {
    canvas_->zoomActualSize(frame_.px_per_mm);
}

void ShellWindow::zoomBack() {
    if (!canvas_->zoomPrevious()) {
        setNotice(tr("Pas de zoom précédent."), false);
    }
}

void ShellWindow::updateGrid() {
    if (frame_.px_per_mm <= 0.0 || gridSpacingMm_ <= 0.0) {
        canvas_->setGrid({}, {}, {});
        return;
    }
    const auto g = gridLines(frame_, gridSpacingMm_);
    QVector<double> xs(g.verticalPx.begin(), g.verticalPx.end());
    QVector<double> ys(g.horizontalPx.begin(), g.horizontalPx.end());
    canvas_->setGrid(xs, ys, QRectF(0, 0, frame_.width, frame_.height));
}

// Icônes dessinées à la main pour la couleur du thème COURANT : le bouton « Tous les outils » et
// les boutons des panneaux (fermer, détacher) restaient à la couleur du thème de démarrage, donc
// presque invisibles après un changement clair / sombre.
void ShellWindow::refreshThemedIcons() {
    const QColor ink = palette().color(QPalette::WindowText);
    if (auto* all = findChild<QToolButton*>(QStringLiteral("allToolsButton"))) {
        all->setIcon(toolIcon("select", palette().color(QPalette::ButtonText)));
    }
    const auto draw = [&](bool closeIcon) {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(ink, 3.0, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        if (closeIcon) {
            p.drawLine(9, 9, 23, 23);
            p.drawLine(23, 9, 9, 23);
        } else {
            p.drawRect(8, 12, 12, 12);
            p.drawLine(12, 8, 24, 8);
            p.drawLine(24, 8, 24, 20);
        }
        return QIcon(pm);
    };
    const QIcon closeIcon = draw(true);
    const QIcon floatIcon = draw(false);
    for (auto* dock : findChildren<QDockWidget*>()) {
        if (auto* b =
                dock->findChild<QAbstractButton*>(QStringLiteral("qt_dockwidget_closebutton"))) {
            b->setIcon(closeIcon);
        }
        if (auto* b =
                dock->findChild<QAbstractButton*>(QStringLiteral("qt_dockwidget_floatbutton"))) {
            b->setIcon(floatIcon);
        }
    }
}

void ShellWindow::setThemeMode(const QString& mode) {
    themeMode_ = (mode == QLatin1String("light") || mode == QLatin1String("dark"))
                     ? mode
                     : QStringLiteral("system");
    // Qt 6.8 : le schéma de couleurs de l'application suit le choix (« unknown » = le système).
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;
    if (themeMode_ == QLatin1String("light")) {
        scheme = Qt::ColorScheme::Light;
    } else if (themeMode_ == QLatin1String("dark")) {
        scheme = Qt::ColorScheme::Dark;
    }
    QGuiApplication::styleHints()->setColorScheme(scheme);
    applyStudioTheme(*qApp, themeMode_, densityScale(density_));
    rebuildToolRail(); // régénère les icônes avec les couleurs du thème courant
    refreshThemedIcons();
    if (auto* a = findChild<QAction*>(QStringLiteral("theme_") + themeMode_)) {
        a->setChecked(true);
    }
    savePreferencesNow();
}

void ShellWindow::setRenderSettings(const RenderSettings& settings) {
    const RenderSettings next = clamped(settings);
    const bool changed = !(next == render_);
    render_ = next;
    const auto id = matchingRenderPreset(render_);
    for (const char* preset : {"draft", "balanced", "high", "photo"}) {
        if (auto* a = findChild<QAction*>(QStringLiteral("renderPreset_") +
                                          QString::fromLatin1(preset))) {
            a->setChecked(id == preset);
        }
    }
    if (renderDialog_ != nullptr) {
        renderDialog_->setSettings(render_);
    }
    if (changed) {
        savePreferencesNow();
        if (documentOpen_ && previewDebounce_ != nullptr) {
            previewDebounce_
                ->start(); // aperçu regroupé : un seul recalcul pour une rafale de curseurs
        }
    }
}

void ShellWindow::showRenderQuality() {
    if (renderDialog_ == nullptr) {
        renderDialog_ = new RenderQualityDialog(this);
        connect(renderDialog_, &RenderQualityDialog::settingsChanged, this,
                [this](const RenderSettings& s) { setRenderSettings(s); });
    }
    renderDialog_->setSettings(render_);
    renderDialog_->show();
    renderDialog_->raise();
    renderDialog_->activateWindow();
}

void ShellWindow::setDensity(UiDensity density) {
    density_ = density;
    applyStudioTheme(*qApp, themeMode_, densityScale(density_));
    if (auto* a = findChild<QAction*>(QStringLiteral("density_") + fromUtf8(densityId(density)))) {
        a->setChecked(true);
    }
    savePreferencesNow();
}

void ShellWindow::savePreferencesNow() {
    if (stateDir_.empty()) {
        return;
    }
    Preferences prefs;
    prefs.theme = themeMode_.toStdString();
    prefs.density = density_;
    prefs.render = render_;
    (void)savePreferences(stateDir_ / "preferences.txt", prefs);
}

void ShellWindow::onKnife(QPointF a, QPointF b) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    if (state.ids.empty()) {
        setNotice(tr("Couteau : sélectionnez d'abord la ou les formes à découper."), true);
        return;
    }
    const PointMm p0 = screenToModel(frame_.pxToMmX(a.x()), frame_.pxToMmY(a.y()));
    const PointMm p1 = screenToModel(frame_.pxToMmX(b.x()), frame_.pxToMmY(b.y()));
    const std::string batch = batchSplit(state.ids, p0, p1);
    if (batch.empty()) {
        return;
    }
    const auto result = session_.applyBatch(batch, tr("Découper").toStdString());
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        setNotice(tr("Découpe refusée : %1")
                      .arg(outcomes.empty() ? tr("la ligne ne traverse aucune forme sélectionnée")
                                            : fromUtf8(outcomes.back().message)),
                  true);
        return;
    }
    std::vector<Target> pieces;
    for (const auto id : createdObjectIds(result.report)) {
        pieces.push_back({TargetKind::EmbroideryObject, id});
    }
    if (!pieces.empty()) {
        session_.selection().replace(std::move(pieces));
    }
}

void ShellWindow::onMeasured(QPointF a, QPointF b) {
    const PointMm from = screenToModel(frame_.pxToMmX(a.x()), frame_.pxToMmY(a.y()));
    const PointMm to = screenToModel(frame_.pxToMmX(b.x()), frame_.pxToMmY(b.y()));
    const Measurement m = measure(from, to);
    const QString text = tr("%1 mm   Δx %2   Δy %3   %4°")
                             .arg(m.distanceMm, 0, 'f', 2)
                             .arg(m.dxMm, 0, 'f', 2)
                             .arg(m.dyMm, 0, 'f', 2)
                             .arg(m.angleDeg, 0, 'f', 1);
    canvas_->showMeasure(a, b, text);
    setNotice(tr("Mesure : ") + text, false);
}

int ShellWindow::simulationTotal() const {
    return static_cast<int>(frame_.command_count);
}

int ShellWindow::simulationPosition() const {
    return simLimit_ ? static_cast<int>(*simLimit_) : simulationTotal();
}

bool ShellWindow::simulationJump(const QString& id) {
    const int total = simulationTotal();
    if (!documentOpen_ || total <= 0) {
        return false;
    }
    if (!simMarks_ || simMarksRevision_ != session_.revision()) {
        auto marks = simulationMarks(session_.project());
        if (!marks) {
            setNotice(tr("Simulation : %1").arg(fromUtf8(marks.error().message)), true);
            return false;
        }
        simMarks_ = std::move(*marks);
        simMarksRevision_ = session_.revision();
    }
    const auto here = static_cast<std::size_t>(simulationPosition());
    const auto end = static_cast<std::size_t>(total);
    std::size_t target = here;
    if (id == QLatin1String("sim.prev-colour")) {
        target = previousMark(simMarks_->colours, here);
    } else if (id == QLatin1String("sim.prev-object")) {
        target = previousMark(simMarks_->objects, here);
    } else if (id == QLatin1String("sim.prev-point")) {
        target = here > 0 ? here - 1 : 0;
    } else if (id == QLatin1String("sim.next-point")) {
        target = std::min(here + 1, end);
    } else if (id == QLatin1String("sim.next-object")) {
        target = nextMark(simMarks_->objects, here, end);
    } else if (id == QLatin1String("sim.next-colour")) {
        target = nextMark(simMarks_->colours, here, end);
    } else if (id == QLatin1String("sim.end")) {
        target = end;
    } else {
        return false;
    }
    setSimulationPosition(static_cast<int>(target));
    return true;
}

void ShellWindow::setSimulationPosition(int position) {
    const int total = simulationTotal();
    if (total <= 0) {
        return;
    }
    const int p = std::clamp(position, 0, total);
    simLimit_ = p < total ? std::optional<std::size_t>(static_cast<std::size_t>(p)) : std::nullopt;
    refreshSimulationBar();
    previewDebounce_->start();
}

void ShellWindow::refreshSimulationBar() {
    const int total = simulationTotal();
    simSlider_->blockSignals(true);
    simSlider_->setRange(0, std::max(total, 1));
    simSlider_->setValue(simulationPosition());
    simSlider_->blockSignals(false);
    simSlider_->setEnabled(total > 0);
    simLabel_->setText(total > 0 ? tr("%1 / %2").arg(simulationPosition()).arg(total) : QString());
}

QMenu* ShellWindow::buildContextMenu() {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("canvasContextMenu"));
    std::string lastGroup;
    for (const auto& e : commandEntries()) {
        if (!e.enabled || !commandBelongsTo(CommandSurface::ContextMenu, e.id)) {
            continue;
        }
        if (!lastGroup.empty() && e.group != lastGroup) {
            menu->addSeparator();
        }
        lastGroup = e.group;
        const QString id = fromUtf8(e.id);
        auto* a = menu->addAction(fromUtf8(e.label));
        if (!e.shortcut.empty()) {
            a->setShortcut(QKeySequence(fromUtf8(e.shortcut)));
            a->setShortcutVisibleInContextMenu(true);
        }
        a->setData(id);
        connect(a, &QAction::triggered, this, [this, id] { runCommand(id); });
    }
    return menu;
}

void ShellWindow::showContextMenu(const QPoint& globalPos, QPointF scenePx) {
    // Clic droit sur un objet non sélectionné : il devient la sélection (comme la plupart des
    // éditeurs).
    if (const auto hit = pickAtPx(scenePx);
        hit && !session_.selection().contains({TargetKind::EmbroideryObject, *hit})) {
        session_.selection().replace({{TargetKind::EmbroideryObject, *hit}});
    }
    auto* menu = buildContextMenu();
    if (menu->isEmpty()) {
        delete menu;
        return;
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(globalPos);
}

void ShellWindow::exportImage(const std::filesystem::path& png, double pxPerMm) {
    const auto snapshot = session_.snapshot();
    setNotice(tr("Export de l'image en cours…"), false);
    tasks_.submit<int>(
        tr("Export image").toStdString(),
        [snapshot, png, pxPerMm](const TaskContext& ctx) -> Result<int> {
            if (auto r = export_preview_png(snapshot, png, pxPerMm, ctx); !r) {
                return std::unexpected(r.error());
            }
            return 0;
        },
        [this, png](Result<int>&& r) {
            if (!r) {
                setNotice(tr("Export impossible : %1 %2")
                              .arg(fromUtf8(r.error().message), fromUtf8(r.error().detail)),
                          true);
                return;
            }
            setNotice(tr("Image écrite : %1").arg(QString::fromStdString(png.filename().string())),
                      false);
            feedbackCue_->pulse();
        });
}

bool ShellWindow::savePresetNamed(const QString& name) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    const auto r =
        savePreset(session_, name.toStdString(), state.ids.empty() ? 0 : state.ids.back());
    if (!r.ok) {
        setNotice(fromUtf8(r.message), true);
        return false;
    }
    setNotice(tr("Préréglage « %1 » enregistré.").arg(name.trimmed()), false);
    return true;
}

bool ShellWindow::applyPresetIndex(int index) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    const auto r = applyPreset(session_, index, state.ids);
    if (!r.ok) {
        setNotice(fromUtf8(r.message), true);
    }
    return r.ok;
}

bool ShellWindow::removePresetIndex(int index) {
    const auto r = removePreset(session_, index);
    if (!r.ok) {
        setNotice(fromUtf8(r.message), true);
    }
    return r.ok;
}

void ShellWindow::rebuildPresetMenu() {
    presetMenu_->clear();
    bool any = false;
    for (const auto& e : commandEntries()) {
        if (e.id.rfind("preset.", 0) != 0) {
            continue;
        }
        any = true;
        auto* a = presetMenu_->addAction(fromUtf8(e.label));
        a->setEnabled(e.enabled);
        const QString id = fromUtf8(e.id);
        connect(a, &QAction::triggered, this, [this, id] { runCommand(id); });
    }
    if (!any) {
        presetMenu_->addAction(tr("Aucun préréglage"))->setEnabled(false);
    }
}

void ShellWindow::setSourceVisible(bool visible) {
    sourceShown_ = visible;
    canvas_->setSourceVisible(visible);
    updateSourceOverlay();
}

void ShellWindow::setClickToStitch(int tolerance, ClickKind kind) {
    clickTol_ = std::clamp(tolerance, 0, 100);
    clickKind_ = kind;
    if (tolSpin_ != nullptr && tolSpin_->value() != clickTol_) {
        QSignalBlocker block(tolSpin_);
        tolSpin_->setValue(clickTol_);
    }
    if (kindCombo_ != nullptr && kindCombo_->currentIndex() != static_cast<int>(kind)) {
        QSignalBlocker block(kindCombo_);
        kindCombo_->setCurrentIndex(static_cast<int>(kind));
    }
}

void ShellWindow::setBrushRadiusMm(double mm) {
    brushMm_ = std::clamp(mm, 0.2, 50.0);
}

const image::Image* ShellWindow::ensureProcessed() {
    if (!documentOpen_) {
        return nullptr;
    }
    if (!processedValid_ || processedRevision_ != session_.revision()) {
        auto image = processedImage(session_.project());
        processedValid_ = image.has_value();
        processedRevision_ = session_.revision();
        if (!image) {
            processed_ = {};
            setNotice(fromUtf8(image.error().message) + QStringLiteral(". ") +
                          fromUtf8(image.error().detail),
                      true);
            return nullptr;
        }
        processed_ = std::move(*image);
    }
    return &processed_;
}

std::optional<std::uint64_t> ShellWindow::pointTarget() const {
    const auto state = inspect(session_.project(), session_.selection().targets());
    if (state.ids.size() != 1) {
        return std::nullopt;
    }
    return state.ids.front();
}

bool ShellWindow::densityActive() const {
    return overlays_ && overlays_->densityActive();
}

bool ShellWindow::densityShown() const {
    return overlays_ && overlays_->densityShown();
}

const std::vector<DensityHotspotInfo>& ShellWindow::densityHotspots() const {
    static const std::vector<DensityHotspotInfo> none;
    return overlays_ ? overlays_->densityHotspots() : none;
}

bool ShellWindow::fabricRiskActive() const {
    return overlays_ && overlays_->fabricActive();
}

bool ShellWindow::fabricRiskShown() const {
    return overlays_ && overlays_->fabricShown();
}

const std::optional<FabricRisk>& ShellWindow::fabricRisk() const {
    static const std::optional<FabricRisk> none;
    return overlays_ ? overlays_->fabricRisk() : none;
}

int ShellWindow::nodeHandleCount() const {
    return nodes_ ? nodes_->handleCount() : 0;
}

void ShellWindow::polishDockTabs() {
    // Les onglets des panneaux de droite (Document, Diagnostic, Calques, Fils, Optimisation,
    // Compagnon) ne sont jamais tronqués en « C… » : ils défilent si la place manque.
    for (auto* bar : findChildren<QTabBar*>()) {
        bar->setElideMode(Qt::ElideNone);
        bar->setUsesScrollButtons(true);
    }
}

int ShellWindow::tangentHandleCount() const {
    return nodes_ ? nodes_->tangentCount() : 0;
}

QPointF ShellWindow::tangentHandlePx(int index) const {
    return nodes_ ? nodes_->tangentPx(index) : QPointF();
}

int ShellWindow::selectedNodeCount() const {
    return nodes_ ? nodes_->selectedCount() : 0;
}

QPointF ShellWindow::nodeHandlePx(int index) const {
    return nodes_ ? nodes_->handlePx(index) : QPointF();
}

void ShellWindow::updatePointMarkers() {
    nodes_->refresh();
    if (regions_) {
        regions_->refresh();
    }
    const auto target = activeTool_ == QStringLiteral("stitch.points") && documentOpen_
                            ? pointTarget()
                            : std::nullopt;
    pointHandles_.clear();
    if (!target || frame_.px_per_mm <= 0.0) {
        pointSel_ = -1;
        pointSels_.clear();
        pointObject_ = 0;
        pointView_ = {};
        canvas_->setPointMarkers({}, -1);
        return;
    }
    if (pointObject_ != *target) {
        pointSel_ = -1;
        pointSels_.clear();
    }
    pointObject_ = *target;
    auto view = stitchPoints(session_.project(), *target);
    if (!view) {
        pointView_ = {};
        pointSel_ = -1;
        canvas_->setPointMarkers({}, -1);
        setNotice(fromUtf8(view.error().message), true);
        return;
    }
    pointView_ = std::move(*view);
    QVector<QPointF> pts;
    for (std::size_t i = 0; i < pointView_.points.size(); ++i) {
        const auto& p = pointView_.points[i];
        if (!p.movable || p.deleted) {
            continue;
        }
        pointHandles_.push_back(i);
        pts.push_back({frame_.mmToPxX(p.pos.x), frame_.mmToPxY(-p.pos.y)});
    }
    if (pointSel_ >= static_cast<int>(pts.size())) {
        pointSel_ = -1;
    }
    std::erase_if(pointSels_, [&](int h) { return h < 0 || h >= static_cast<int>(pts.size()); });
    if (pointSel_ < 0) {
        pointSels_.clear();
    }
    QVector<int> chosen;
    for (const int h : pointSels_) {
        chosen.push_back(h);
    }
    canvas_->setPointMarkers(pts, chosen);
}

std::vector<std::size_t> ShellWindow::selectedPointIndices() const {
    std::vector<std::size_t> out;
    for (const int h : pointSels_) {
        if (h >= 0 && h < static_cast<int>(pointHandles_.size())) {
            out.push_back(pointView_.points[pointHandles_[static_cast<std::size_t>(h)]].index);
        }
    }
    return out;
}

void ShellWindow::selectAllPoints() {
    if (pointHandles_.empty()) {
        return;
    }
    pointSels_.clear();
    for (std::size_t h = 0; h < pointHandles_.size(); ++h) {
        pointSels_.push_back(static_cast<int>(h));
    }
    pointSel_ = pointSels_.back();
    updatePointMarkers();
    setNotice(tr("%1 points choisis.").arg(pointSels_.size()), false);
}

void ShellWindow::clearPointSelection() {
    pointSel_ = -1;
    pointSels_.clear();
    updatePointMarkers();
}

bool ShellWindow::deleteSelectedPoint() {
    const auto target = pointTarget();
    const auto indices = selectedPointIndices();
    if (!target || indices.empty()) {
        return false;
    }
    const auto r = deleteStitchPoints(session_, *target, indices);
    if (!r.ok) {
        setNotice(tr("Retouche de points : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    pointSel_ = -1;
    pointSels_.clear();
    setNotice(indices.size() > 1 ? tr("%1 points supprimés.").arg(indices.size())
                                 : tr("Point supprimé."),
              false);
    return true;
}

bool ShellWindow::restoreSelectedPoints() {
    const auto target = pointTarget();
    const auto indices = selectedPointIndices();
    if (!target || indices.empty()) {
        setNotice(tr("Choisissez d'abord des points."), false);
        return false;
    }
    const auto r = restoreStitchPoints(session_, *target, indices);
    setNotice(r.ok ? tr("Points rétablis.")
                   : tr("Retouche de points : %1").arg(fromUtf8(r.message)),
              !r.ok);
    return r.ok;
}

bool ShellWindow::setSelectedPointsJump(bool jump) {
    const auto target = pointTarget();
    const auto indices = selectedPointIndices();
    if (!target || indices.empty()) {
        setNotice(tr("Choisissez d'abord des points."), false);
        return false;
    }
    const auto r = setStitchPointsJump(session_, *target, indices, jump);
    setNotice(r.ok ? (jump ? tr("Points passés en saut (sans couture).") : tr("Points cousus."))
                   : tr("Retouche de points : %1").arg(fromUtf8(r.message)),
              !r.ok);
    return r.ok;
}

bool ShellWindow::setSelectedPointsTrim(bool trim) {
    const auto target = pointTarget();
    const auto indices = selectedPointIndices();
    if (!target || indices.empty()) {
        setNotice(tr("Choisissez d'abord des points."), false);
        return false;
    }
    const auto r = setStitchPointsTrim(session_, *target, indices, trim);
    setNotice(r.ok ? (trim ? tr("Fil coupé après ces points.") : tr("Coupe du fil retirée."))
                   : tr("Retouche de points : %1").arg(fromUtf8(r.message)),
              !r.ok);
    return r.ok;
}

bool ShellWindow::discardPointEdits() {
    const auto target = pointTarget();
    if (!target) {
        setNotice(tr("Sélectionnez un seul objet brodé."), false);
        return false;
    }
    const auto r = discardStitchEdits(session_, *target);
    if (!r.ok) {
        setNotice(fromUtf8(r.message), false);
        return false;
    }
    pointSel_ = -1;
    pointSels_.clear();
    setNotice(tr("Retouches effacées."), false);
    return true;
}

void ShellWindow::updateSourceOverlay() {
    if (!sourceShown_ || frame_.px_per_mm <= 0.0 || !documentOpen_) {
        canvas_->setSourceImage({}, {});
        canvas_->setDetailOverlay({}, {});
        return;
    }
    const image::Image* img = ensureProcessed();
    if (img == nullptr) {
        canvas_->setSourceImage({}, {});
        canvas_->setDetailOverlay({}, {});
        return;
    }
    const double mmpp = session_.project().mm_per_px.value;
    const double wMm = img->width * mmpp;
    const double hMm = img->height * mmpp;
    const QRectF rect(QPointF(frame_.mmToPxX(-wMm / 2.0), frame_.mmToPxY(-hMm / 2.0)),
                      QSizeF(wMm * frame_.px_per_mm, hMm * frame_.px_per_mm));
    const QImage view(img->rgba.data(), img->width, img->height, img->width * 4,
                      QImage::Format_RGBA8888);
    canvas_->setSourceImage(view.copy(), rect); // copie : `processed_` peut être recalculée
    const auto& mask = session_.project().detail_mask;
    if (mask.matches(img->width, img->height) && mask.any_detail()) {
        QImage overlay(img->width, img->height, QImage::Format_ARGB32);
        for (int y = 0; y < img->height; ++y) {
            for (int x = 0; x < img->width; ++x) {
                const int w = mask.weights[static_cast<std::size_t>(y) * img->width + x];
                overlay.setPixel(x, y, qRgba(230, 60, 60, w * 150 / 255));
            }
        }
        canvas_->setDetailOverlay(overlay, rect);
    } else {
        canvas_->setDetailOverlay({}, {});
    }
}

void ShellWindow::onStroke(QVector<QPointF> scenePx, Qt::KeyboardModifiers modifiers) {
    if (activeTool_ == QStringLiteral("region.brush") && regions_) {
        (void)regions_->paintStroke(scenePx, modifiers & Qt::ControlModifier);
        return;
    }
    if (activeTool_ == QStringLiteral("draw.freeform") && frame_.px_per_mm > 0.0) {
        std::vector<PointMm> stroke;
        stroke.reserve(static_cast<std::size_t>(scenePx.size()));
        for (const QPointF& p : scenePx) {
            stroke.push_back(screenToModel(frame_.pxToMmX(p.x()), frame_.pxToMmY(p.y())));
        }
        const auto r = createFreehandShape(session_, stroke);
        if (!r.ok) {
            setNotice(tr("Crayon : %1").arg(fromUtf8(r.message)), true);
            return;
        }
        session_.selection().replace({{TargetKind::EmbroideryObject, r.objectId}});
        setNotice(tr("Forme dessinée : ajustez-la avec l'outil Nœuds (B)."), false);
        return;
    }
    if (activeTool_ != QStringLiteral("detail-brush") || frame_.px_per_mm <= 0.0) {
        return;
    }
    const image::Image* img = ensureProcessed();
    if (img == nullptr) {
        return;
    }
    std::vector<std::pair<double, double>> centres;
    centres.reserve(static_cast<std::size_t>(scenePx.size()));
    for (const QPointF& p : scenePx) {
        centres.emplace_back(frame_.pxToMmX(p.x()), frame_.pxToMmY(p.y()));
    }
    const bool erase = modifiers & Qt::ControlModifier;
    const auto r = paintDetail(session_, *img, centres, brushMm_, erase);
    if (!r.ok) {
        setNotice(fromUtf8(r.message), true);
        return;
    }
    setNotice(erase ? tr("Zone de détail effacée.") : tr("Zone de détail peinte."), false);
    updateSourceOverlay();
}

void ShellWindow::applyToolToCanvas() {
    using Mode = PreviewCanvas::Mode;
    updatePointMarkers();
    if ((activeTool_ == QStringLiteral("region.edit") ||
         activeTool_ == QStringLiteral("region.brush")) &&
        regionPanel_ != nullptr) {
        // L'outil « Régions » a besoin de son panneau : on le montre au premier plan.
        if (auto* dock = qobject_cast<QDockWidget*>(regionPanel_->parentWidget())) {
            dock->show();
            dock->raise();
        }
    }
    const bool imageTool = activeTool_ == QStringLiteral("click-to-stitch") ||
                           activeTool_ == QStringLiteral("detail-brush");
    if (imageBar_ != nullptr) {
        imageBar_->setVisible(imageTool);
    }
    if (imageTool && !sourceShown_ && documentOpen_) {
        setSourceVisible(true); // on ne peut pas cliquer une image qu'on ne voit pas
    }
    if (activeTool_ == QStringLiteral("pan")) {
        canvas_->setMode(Mode::Pan);
    } else if (activeTool_ == QStringLiteral("draw.rectangle") ||
               activeTool_ == QStringLiteral("draw.ellipse")) {
        canvas_->setMode(Mode::DrawBox);
    } else if (activeTool_ == QStringLiteral("draw.polygon") ||
               activeTool_ == QStringLiteral("satin.column") ||
               activeTool_ == QStringLiteral("fill.direction-guide") ||
               activeTool_ == QStringLiteral("fill.break-line")) {
        canvas_->setMode(Mode::DrawPolygon);
    } else if (activeTool_ == QStringLiteral("measure")) {
        canvas_->setMode(Mode::Measure);
    } else if (activeTool_ == QStringLiteral("eyedropper")) {
        canvas_->setMode(Mode::Pick);
    } else if (activeTool_ == QStringLiteral("knife")) {
        canvas_->setMode(Mode::Cut);
    } else if (activeTool_ == QStringLiteral("click-to-stitch") ||
               activeTool_ == QStringLiteral("stitch.points") ||
               activeTool_ == QStringLiteral("region.edit")) {
        canvas_->setMode(Mode::Pick);
    } else if (activeTool_ == QStringLiteral("detail-brush") ||
               activeTool_ == QStringLiteral("region.brush") ||
               activeTool_ == QStringLiteral("draw.freeform")) {
        canvas_->setMode(Mode::Stroke);
    } else if (activeTool_ == QStringLiteral("node.edit") ||
               activeTool_ == QStringLiteral("fade.axis")) {
        canvas_->setMode(Mode::Nodes);
    } else if (activeTool_ == QStringLiteral("draw.bezier")) {
        canvas_->setMode(Mode::Pen);
    } else {
        canvas_->setMode(Mode::Select); // sélection : clic, cadre élastique
    }
}

void ShellWindow::newProject() {
    if (!confirmDiscardOrSave()) {
        return;
    }
    auto created = project_ops::create_empty_project({});
    if (!created) {
        setNotice(tr("Création du projet impossible : %1").arg(fromUtf8(created.error().message)),
                  true);
        return;
    }
    cancelPendingDocumentLoad();
    if (autosave_) {
        autosave_->clear();
    }
    currentPath_.clear();
    documentOpen_ = true;
    session_.selection().clear();
    session_.replaceProject(std::move(*created), false);
    setNotice(tr("Nouveau projet (cadre %1 × %2 mm). Choisissez un outil de dessin (R, E, Y).")
                  .arg(session_.project().canvas.width.value / 1000.0, 0, 'f', 0)
                  .arg(session_.project().canvas.height.value / 1000.0, 0, 'f', 0),
              false);
}

void ShellWindow::createShape(const std::string& batch, const QString& label) {
    if (batch.empty()) {
        setNotice(tr("Forme hors limites (0,1 à 2000 mm, à moins de 1000 mm du centre)."), true);
        return;
    }
    const auto result = session_.applyBatch(batch, label.toStdString());
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        setNotice(tr("%1 refusé : %2")
                      .arg(label, outcomes.empty() ? tr("opération invalide")
                                                   : fromUtf8(outcomes.back().message)),
                  true);
        return;
    }
    if (const auto id = createdObjectId(result.report)) {
        session_.selection().replace({{TargetKind::EmbroideryObject, *id}}); // l'objet créé
    }
}

void ShellWindow::onBoxDrawn(QRectF scenePx, Qt::KeyboardModifiers modifiers) {
    if (!documentOpen_ || frame_.px_per_mm <= 0.0) {
        return;
    }
    double w = scenePx.width() / frame_.px_per_mm;
    double h = scenePx.height() / frame_.px_per_mm;
    if (modifiers & Qt::ShiftModifier) {
        w = h = std::max(w, h); // contrainte : carré / cercle
    }
    const PointMm centre =
        screenToModel(frame_.pxToMmX(scenePx.center().x()), frame_.pxToMmY(scenePx.center().y()));
    if (activeTool_ == QStringLiteral("draw.ellipse")) {
        createShape(batchAddEllipse(centre, w, h), tr("Ellipse"));
    } else {
        createShape(batchAddRect(centre, w, h), tr("Rectangle"));
    }
}

void ShellWindow::onPolygonDrawn(QVector<QPointF> scenePx) {
    if (!documentOpen_ || frame_.px_per_mm <= 0.0) {
        return;
    }
    std::vector<PointMm> points;
    points.reserve(static_cast<std::size_t>(scenePx.size()));
    for (const QPointF& p : scenePx) {
        points.push_back(screenToModel(frame_.pxToMmX(p.x()), frame_.pxToMmY(p.y())));
    }
    if (activeTool_ == QStringLiteral("satin.column")) {
        // Points pris deux par deux : (rail A, rail B) pour chaque barreau ; un point seul est
        // ignoré.
        std::vector<std::pair<PointMm, PointMm>> rungs;
        for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
            rungs.emplace_back(points[i], points[i + 1]);
        }
        const auto r = createSatinColumn(session_, rungs);
        if (!r.ok) {
            setNotice(tr("Colonne satin : %1").arg(fromUtf8(r.message)), true);
            return;
        }
        session_.selection().replace({{TargetKind::EmbroideryObject, r.objectId}});
        setNotice(r.railsCross
                      ? tr("Colonne satin créée, mais les rails se croisent : corrigez-les.")
                      : tr("Colonne satin créée."),
                  r.railsCross);
        return;
    }
    if (activeTool_ == QStringLiteral("fill.direction-guide") ||
        activeTool_ == QStringLiteral("fill.break-line")) {
        // Guide de direction / ligne de rupture : tracés sur l'objet de broderie sélectionné. Le
        // remplissage devient directionnel au premier tracé (une seule étape d'annulation).
        const auto state = inspect(session_.project(), session_.selection().targets());
        const bool isBreak = activeTool_ == QStringLiteral("fill.break-line");
        if (state.ids.size() != 1) {
            setNotice(tr("Sélectionnez UN objet de broderie, puis tracez son %1.")
                          .arg(isBreak ? tr("ligne de rupture") : tr("guide de direction")),
                      true);
            return;
        }
        const auto r = addDirectionGuide(session_, state.ids.front(), points, isBreak);
        setNotice(r.ok ? (isBreak ? tr("Ligne de rupture tracée : la forme est coupée en secteurs.")
                                  : tr("Guide de direction tracé : le fil suit sa courbe."))
                       : fromUtf8(r.message),
                  !r.ok);
        return;
    }
    createShape(batchAddPolygon(points), tr("Polygone"));
}

PointMm ShellWindow::selectionCentreModel() const {
    // Centre de l'emprise commune des objets sélectionnés (mm modèle, Y vers le haut).
    bool any = false;
    BoxMm box;
    if (frame_.hits != nullptr) {
        for (const auto& t : session_.selection().targets()) {
            if (t.kind != TargetKind::EmbroideryObject) {
                continue;
            }
            if (const auto b = frame_.hits->boundsOf(t.id)) {
                box = !any ? *b
                           : BoxMm{std::min(box.x0, b->x0), std::min(box.y0, b->y0),
                                   std::max(box.x1, b->x1), std::max(box.y1, b->y1)};
                any = true;
            }
        }
    }
    return any ? screenToModel((box.x0 + box.x1) / 2.0, (box.y0 + box.y1) / 2.0) : PointMm{0, 0};
}

void ShellWindow::editSelection(const QString& command) {
    if (command == QLatin1String("edit.delete") && activeTool_ == QStringLiteral("stitch.points") &&
        pointSel_ >= 0) {
        deleteSelectedPoint();
        return;
    }
    if (command == QLatin1String("edit.delete") && regions_ && regions_->active() &&
        !regions_->selection().empty()) {
        (void)regions_->deleteSelected(); // Suppr retire les RÉGIONS choisies, pas des objets
        return;
    }
    if (command == QLatin1String("edit.delete") && activeTool_ == QStringLiteral("node.edit") &&
        nodes_->selectedCount() > 0) {
        (void)nodes_->deleteSelected(); // Suppr retire les nœuds choisis, pas l'objet entier
        return;
    }
    const auto state = inspect(session_.project(), session_.selection().targets());
    if (state.ids.empty()) {
        return;
    }
    const PointMm centre = selectionCentreModel();
    std::string batch;
    QString label;
    if (command == QLatin1String("edit.delete")) {
        batch = batchRemove(state.ids);
        label = tr("Supprimer");
    } else if (command == QLatin1String("edit.duplicate")) {
        batch = batchDuplicate(state.ids, {5.0, -5.0});
        label = tr("Dupliquer");
    } else if (command == QLatin1String("transform.rotate.cw")) {
        batch = batchRotate(state.ids, -90.0, centre); // horaire : angle négatif, Y vers le haut
        label = tr("Pivoter à droite");
    } else if (command == QLatin1String("transform.rotate.ccw")) {
        batch = batchRotate(state.ids, 90.0, centre);
        label = tr("Pivoter à gauche");
    } else if (command == QLatin1String("transform.mirror.h")) {
        batch = batchMirror(state.ids, MirrorAxis::Horizontal, centre);
        label = tr("Miroir horizontal");
    } else if (command == QLatin1String("transform.mirror.v")) {
        batch = batchMirror(state.ids, MirrorAxis::Vertical, centre);
        label = tr("Miroir vertical");
    } else if (command == QLatin1String("arrange.place")) {
        showPlaceDialog();
        return;
    } else if (command.startsWith(QLatin1String("arrange."))) {
        static const std::map<QString, AlignMode> aligns{
            {QStringLiteral("arrange.align.left"), AlignMode::Left},
            {QStringLiteral("arrange.align.centerh"), AlignMode::CenterH},
            {QStringLiteral("arrange.align.right"), AlignMode::Right},
            {QStringLiteral("arrange.align.top"), AlignMode::Top},
            {QStringLiteral("arrange.align.centerv"), AlignMode::CenterV},
            {QStringLiteral("arrange.align.bottom"), AlignMode::Bottom}};
        if (const auto it = aligns.find(command); it != aligns.end()) {
            batch = batchAlign(session_.project(), state.ids, it->second);
            label = tr("Aligner");
        } else if (command == QLatin1String("arrange.distribute.h")) {
            batch = batchDistribute(session_.project(), state.ids, DistributeAxis::Horizontal);
            label = tr("Répartir horizontalement");
        } else if (command == QLatin1String("arrange.distribute.v")) {
            batch = batchDistribute(session_.project(), state.ids, DistributeAxis::Vertical);
            label = tr("Répartir verticalement");
        } else if (command == QLatin1String("arrange.group")) {
            batch = batchGroup(state.ids, {});
            label = tr("Grouper");
        } else if (command == QLatin1String("arrange.ungroup")) {
            batch = batchUngroup(session_.project(), state.ids);
            label = tr("Dégrouper");
        }
        if (batch.empty()) {
            setNotice(tr("%1 : rien à faire pour cette sélection.").arg(label), false);
            return;
        }
    } else if (command.startsWith(QLatin1String("order."))) {
        const std::string move = command.mid(6).toStdString();
        batch = batchStack(state.ids, move);
        label = move == "front"     ? tr("Premier plan")
                : move == "back"    ? tr("Arrière-plan")
                : move == "forward" ? tr("Avancer")
                                    : tr("Reculer");
    }
    if (batch.empty()) {
        return;
    }
    const auto result = session_.applyBatch(batch, label.toStdString());
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        setNotice(tr("%1 refusé : %2")
                      .arg(label, outcomes.empty() ? tr("opération invalide")
                                                   : fromUtf8(outcomes.back().message)),
                  true);
        return;
    }
    if (command == QLatin1String("edit.duplicate")) {
        std::vector<Target> copies; // les copies deviennent la sélection
        for (const auto id : createdObjectIds(result.report)) {
            copies.push_back({TargetKind::EmbroideryObject, id});
        }
        session_.selection().replace(std::move(copies));
    }
}

void ShellWindow::nudgeSelection(int stepX, int stepUp, bool big) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    const double mm = big ? 10.0 : 1.0;
    applyTransform(batchTranslate(state.ids, stepX * mm, stepUp * mm), tr("Déplacer"));
}

bool ShellWindow::runActionWith(const QString& id, const Values& values) {
    const ActionDef* def = contextAction(id.toStdString()) != nullptr
                               ? contextAction(id.toStdString())
                               : findAction(id.toStdString());
    if (def == nullptr) {
        return false;
    }
    const auto state = inspect(session_.project(), session_.selection().targets());
    ActionContext ctx;
    ctx.ids = state.ids;
    ctx.centre = selectionCentreModel();
    ctx.project = &session_.project();
    std::string error;
    if (def->run) { // action adossée à une ICommand : validation puis exécution directe
        Values checked = values;
        if (!validateAction(*def, checked, ctx, error)) {
            setNotice(fromUtf8(def->label).remove(QStringLiteral("…")) + QStringLiteral(" : ") +
                          fromUtf8(error),
                      true);
            return false;
        }
        const auto r = def->run(session_, checked, ctx);
        setNotice(fromUtf8(def->label).remove(QStringLiteral("…")) +
                      (r.ok ? tr(" : fait.") : QStringLiteral(" : ") + fromUtf8(r.message)),
                  !r.ok);
        return r.ok;
    }
    const std::string batch = buildAction(*def, values, ctx, error);
    if (batch.empty()) {
        setNotice(fromUtf8(def->label).remove(QStringLiteral("…")) + QStringLiteral(" : ") +
                      fromUtf8(error),
                  true);
        return false;
    }
    const auto result = session_.applyBatch(batch, def->label);
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        setNotice(tr("%1 refusé : %2")
                      .arg(fromUtf8(def->label).remove(QStringLiteral("…")),
                           outcomes.empty() ? tr("opération invalide")
                                            : fromUtf8(outcomes.back().message)),
                  true);
        return false;
    }
    if (!documentOpen_) {
        documentOpen_ = true;
    }
    if (id == QLatin1String("text.remove")) {
        session_.selection().clear(); // les lettres supprimées ne sont plus sélectionnables
        setNotice(tr("Texte supprimé."), false);
        return true;
    }
    std::vector<Target> created;
    for (const auto newId : createdObjectIds(result.report)) {
        if (session_.project().findText(ObjectId{newId}) != nullptr) {
            // « Ajouter / modifier du texte » rend l'identifiant de l'objet TEXTE : on sélectionne
            // ses lettres (objets de broderie), ce que l'on peut déplacer et rééditer.
            for (const auto& emb : session_.project().embroidery_objects) {
                const auto* vec = session_.project().findObject(emb.source_vector);
                if (vec != nullptr && vec->text_owner && vec->text_owner->value == newId) {
                    created.push_back({TargetKind::EmbroideryObject, emb.id.value});
                }
            }
        } else {
            created.push_back({TargetKind::EmbroideryObject, newId});
        }
    }
    if (!created.empty()) {
        session_.selection().replace(std::move(created));
    }
    setNotice(fromUtf8(def->label).remove(QStringLiteral("…")) + tr(" : fait."), false);
    return true;
}

void ShellWindow::runAction(const QString& id) {
    const bool editText = id == QLatin1String("text.edit");
    const bool editGlyph = id == QLatin1String("text.glyph");
    const ActionDef* def = contextAction(id.toStdString()) != nullptr
                               ? contextAction(id.toStdString())
                               : findAction(id.toStdString());
    if (def == nullptr) {
        return;
    }
    if (def->fields.empty()) {
        runActionWith(id, {});
        return;
    }
    Values initial = defaultValues(*def);
    if (editText) { // le dialogue s'ouvre sur les réglages ACTUELS du texte sélectionné
        const auto state = inspect(session_.project(), session_.selection().targets());
        if (const auto owner = selectedTextOwner(session_.project(), state.ids)) {
            for (auto& [key, value] : textEditValues(session_.project(), *owner)) {
                initial[key] = std::move(value);
            }
        }
    }
    if (editGlyph) { // le dialogue s'ouvre sur les réglages ACTUELS de la lettre sélectionnée
        const auto state = inspect(session_.project(), session_.selection().targets());
        const auto target = glyphTargetOf(session_.project(), state.ids);
        if (!target.ok) {
            setNotice(fromUtf8(target.message), true);
            return;
        }
        for (auto& [key, value] : glyphEditValues(session_.project(), target)) {
            initial[key] = std::move(value);
        }
    }
    auto* dialog = new ActionDialog(*def, initial, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this,
            [this, id, dialog] { runActionWith(id, dialog->values()); });
    dialog->open();
}

void ShellWindow::selectAllObjects() {
    std::vector<Target> all;
    for (const auto& o : session_.project().embroidery_objects) {
        if (o.visible) { // un objet masqué n'est pas sélectionnable au canevas : cohérent ici
            all.push_back({TargetKind::EmbroideryObject, o.id.value});
        }
    }
    session_.selection().replace(std::move(all));
}

void ShellWindow::onCanvasRect(QRectF scenePx, Qt::KeyboardModifiers modifiers) {
    if (activeTool_ != QStringLiteral("select") || frame_.hits == nullptr) {
        return;
    }
    const BoxMm box{frame_.pxToMmX(scenePx.left()), frame_.pxToMmY(scenePx.top()),
                    frame_.pxToMmX(scenePx.right()), frame_.pxToMmY(scenePx.bottom())};
    std::vector<Target> targets;
    if (modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) {
        targets = session_.selection().targets(); // ajoute à la sélection existante
    }
    for (const auto id : frame_.hits->inside(box)) { // sélection « fenêtre » : objets entiers
        const Target t{TargetKind::EmbroideryObject, id};
        if (std::find(targets.begin(), targets.end(), t) == targets.end()) {
            targets.push_back(t);
        }
    }
    session_.selection().replace(std::move(targets));
}

std::vector<CommandEntry> ShellWindow::commandEntries() const {
    std::vector<CommandEntry> out;
    const bool hasObjects = !session_.project().embroidery_objects.empty();
    const auto q = [](const QString& s) { return s.toStdString(); };
    out.push_back({"file.new", q(tr("Nouveau projet")), q(tr("Fichier")), "Ctrl+N", true});
    out.push_back(
        {"file.open", q(tr("Ouvrir une image ou un projet…")), q(tr("Fichier")), "Ctrl+O", true});
    out.push_back({"file.save", q(tr("Enregistrer")), q(tr("Fichier")), "Ctrl+S", documentOpen_});
    out.push_back(
        {"file.export", q(tr("Exporter en DST…")), q(tr("Fichier")), "Ctrl+E", hasObjects});
    out.push_back({"edit.undo", q(tr("Annuler")), q(tr("Édition")), "Ctrl+Z", session_.canUndo()});
    out.push_back({"edit.redo", q(tr("Rétablir")), q(tr("Édition")), "Ctrl+Y", session_.canRedo()});
    out.push_back(
        {"selection.all", q(tr("Tout sélectionner")), q(tr("Sélection")), "Ctrl+A", hasObjects});
    out.push_back({"selection.clear", q(tr("Désélectionner")), q(tr("Sélection")), "Ctrl+Maj+A",
                   !session_.selection().empty()});
    out.push_back(
        {"view.fit", q(tr("Ajuster à la fenêtre")), q(tr("Affichage")), "F", canvas_->hasImage()});
    const bool hasSel = !inspect(session_.project(), session_.selection().targets()).ids.empty();
    const auto edit = [&](const char* id, const QString& label, const char* group,
                          const char* key) {
        out.push_back({id, q(label), q(tr(group)), key, hasSel});
    };
    out.push_back({"edit.delete", q(tr("Supprimer la sélection")), q(tr("Édition")), "Suppr",
                   hasSel || (regions_ && regions_->active() && !regions_->selection().empty())});
    edit("edit.duplicate", tr("Dupliquer la sélection"), "Édition", "Ctrl+D");
    edit("transform.rotate.cw", tr("Pivoter de 90° à droite"), "Transformer", "");
    edit("transform.rotate.ccw", tr("Pivoter de 90° à gauche"), "Transformer", "");
    edit("transform.mirror.h", tr("Miroir horizontal"), "Transformer", "");
    edit("transform.mirror.v", tr("Miroir vertical"), "Transformer", "");
    edit("arrange.align.left", tr("Aligner à gauche"), "Aligner", "");
    edit("arrange.align.centerh", tr("Centrer horizontalement"), "Aligner", "");
    edit("arrange.align.right", tr("Aligner à droite"), "Aligner", "");
    edit("arrange.align.top", tr("Aligner en haut"), "Aligner", "");
    edit("arrange.align.centerv", tr("Centrer verticalement"), "Aligner", "");
    edit("arrange.align.bottom", tr("Aligner en bas"), "Aligner", "");
    edit("arrange.distribute.h", tr("Répartir horizontalement"), "Aligner", "");
    edit("arrange.distribute.v", tr("Répartir verticalement"), "Aligner", "");
    edit("arrange.group", tr("Grouper"), "Édition", "Ctrl+G");
    edit("arrange.ungroup", tr("Dégrouper"), "Édition", "Ctrl+Maj+G");
    edit("arrange.place", tr("Position et taille…"), "Transformer", "");
    edit("order.front", tr("Premier plan"), "Ordre de couture", "");
    edit("order.forward", tr("Avancer d'un cran"), "Ordre de couture", "");
    edit("order.backward", tr("Reculer d'un cran"), "Ordre de couture", "");
    edit("order.back", tr("Arrière-plan"), "Ordre de couture", "");
    const bool hasImage = session_.project().hasImage();
    const auto prep = [&](const char* id, const QString& label) {
        out.push_back({id, q(label), q(tr("Image")), "", hasImage});
    };
    prep("image.prep.bc", tr("Luminosité et contraste…"));
    prep("image.prep.gray", tr("Image en niveaux de gris"));
    prep("image.prep.sharpen", tr("Netteté de l'image…"));
    prep("image.prep.clahe", tr("Contraste local de l'image (CLAHE)…"));
    prep("image.prep.rotate.r", tr("Pivoter l'image à droite"));
    prep("image.prep.rotate.l", tr("Pivoter l'image à gauche"));
    prep("image.prep.rotate.180", tr("Pivoter l'image de 180°"));
    prep("image.prep.flip.h", tr("Retourner l'image horizontalement"));
    prep("image.prep.flip.v", tr("Retourner l'image verticalement"));
    prep("image.prep.crop", tr("Recadrer l'image…"));
    for (const auto& def : actionCatalog()) {
        out.push_back(
            {"action." + def.id, def.label, def.group, "", !def.needsSelection || hasSel});
    }
    out.push_back({"preset.save", q(tr("Enregistrer les réglages de l'objet comme préréglage…")),
                   q(tr("Préréglages")), "", hasSel});
    for (std::size_t i = 0; i < session_.project().stitch_presets.size(); ++i) {
        const auto& name = session_.project().stitch_presets[i].name;
        out.push_back({"preset.apply." + std::to_string(i),
                       q(tr("Appliquer le préréglage « %1 »").arg(fromUtf8(name))),
                       q(tr("Préréglages")), "", hasSel});
        out.push_back({"preset.remove." + std::to_string(i),
                       q(tr("Supprimer le préréglage « %1 »").arg(fromUtf8(name))),
                       q(tr("Préréglages")), "", true});
    }
    out.push_back({"analysis.fabric",
                   q(overlays_->fabricActive() ? tr("Masquer le risque de plissement du tissu")
                                               : tr("Afficher le risque de plissement du tissu")),
                   q(tr("Analyse")), "",
                   overlays_->fabricActive() || !session_.project().embroidery_objects.empty()});
    {
        const auto state = inspect(session_.project(), session_.selection().targets());
        out.push_back({"text.edit", q(tr("Modifier le texte sélectionné…")), q(tr("Texte")), "",
                       selectedTextOwner(session_.project(), state.ids).has_value()});
    }
    out.push_back(
        {"thread.palette", q(tr("Nuanciers de fils et mélanges…")), q(tr("Fils")), "", hasObjects});
    out.push_back({"radial.open", q(tr("Menu radial des actions de la sélection")),
                   q(tr("Affichage")), "F2", hasSel});
    out.push_back(
        {"chat.open", q(tr("Discuter avec l'agent IA")), q(tr("Compagnon")), "", documentOpen_});
    out.push_back(
        {"companion.settings", q(tr("Réglages du compagnon IA…")), q(tr("Compagnon")), "", true});
    {
        const auto state = inspect(session_.project(), session_.selection().targets());
        const auto glyph = glyphTargetOf(session_.project(), state.ids);
        out.push_back({"text.glyph", q(tr("Modifier la lettre sélectionnée…")), q(tr("Texte")), "",
                       glyph.ok});
        out.push_back({"text.glyph.reset", q(tr("Rétablir la lettre sélectionnée")), q(tr("Texte")),
                       "", glyph.ok && glyph.hasEdit});
    }
    {
        // Édition des nœuds du contour : actifs quand des nœuds sont choisis (outil Nœuds, B).
        const bool picked = nodes_ != nullptr && nodes_->selectedCount() > 0;
        out.push_back(
            {"node.smooth", q(tr("Nœuds : rendre lisses")), q(tr("Contour")), "", picked});
        out.push_back(
            {"node.corner", q(tr("Nœuds : rendre anguleux")), q(tr("Contour")), "", picked});
        out.push_back({"node.hole.remove", q(tr("Supprimer le trou du nœud choisi")),
                       q(tr("Contour")), "", nodes_ != nullptr && nodes_->selectionIsInHole()});
    }
    {
        const auto state = inspect(session_.project(), session_.selection().targets());
        out.push_back({"text.remove", q(tr("Supprimer le texte sélectionné")), q(tr("Texte")), "",
                       selectedTextOwner(session_.project(), state.ids).has_value()});
    }
    out.push_back({"export.options", q(tr("Options machine, cadre et coupes…")), q(tr("Fichier")),
                   "", documentOpen_});
    out.push_back({"export.check", q(tr("Contrôler le motif avant l'export")), q(tr("Fichier")), "",
                   hasObjects});
    out.push_back({"analysis.pull", q(tr("Comparer le motif nominal et le tirage prédit")),
                   q(tr("Analyse")), "", hasObjects});
    out.push_back({"analysis.calibration", q(tr("Étalonner le tirage sur une pièce-test…")),
                   q(tr("Analyse")), "", true});
    {
        // Courbe de transition d'un fondu : disponible quand un seul objet porte un fondu.
        const auto target = pointTarget();
        const bool hasFade = target && fadeAxisView(session_.project(), *target).has_value();
        const std::pair<const char*, FadeCurve> curves[] = {
            {"fade.curve.linear", FadeCurve::Linear},
            {"fade.curve.smooth", FadeCurve::Smooth},
            {"fade.curve.steep", FadeCurve::Steep},
            {"fade.curve.easein", FadeCurve::EaseIn},
            {"fade.curve.easeout", FadeCurve::EaseOut}};
        for (const auto& [id, curve] : curves) {
            out.push_back({id, q(tr("Courbe du fondu : %1").arg(fromUtf8(fadeCurveLabel(curve)))),
                           q(tr("Fondu")), "", hasFade});
        }
    }
    {
        // Barreaux d'une colonne satin à rails : un seul objet sélectionné, de ce type.
        const auto target = pointTarget();
        const bool isSatin = target && satinRungCount(session_.project(), *target) >= 0;
        out.push_back({"satin.rung.insert",
                       q(tr("Satin : ajouter un barreau (au plus grand écart)")), q(tr("Satin")),
                       "", isSatin});
        out.push_back({"satin.rung.remove", q(tr("Satin : retirer le barreau le plus redondant")),
                       q(tr("Satin")), "", isSatin});
        out.push_back({"satin.diagnose", q(tr("Satin : diagnostiquer rails et barreaux")),
                       q(tr("Satin")), "", isSatin});
    }
    out.push_back({"vector.curves", q(tr("Convertir les contours en courbes")), q(tr("Contour")),
                   "", hasObjects});
    out.push_back(
        {"vector.smooth", q(tr("Lisser les contours")), q(tr("Contour")), "", hasObjects});
    out.push_back({"optimize.order", q(tr("Optimiser l'ordre de couture…")), q(tr("Analyse")), "",
                   session_.project().embroidery_objects.size() >= 2});
    out.push_back({"companion.toggle",
                   q(liveBridgeEnabled() ? tr("Désactiver le pont du compagnon IA")
                                         : tr("Activer le pont du compagnon IA")),
                   q(tr("Compagnon IA")), "", true});
    out.push_back({"analysis.run", q(tr("Analyser le document (diagnostic)")), q(tr("Analyse")), "",
                   !session_.project().embroidery_objects.empty()});
    {
        // Corrections de régions : atteignables au clavier et par la palette, actives avec l'outil
        // Régions (X) / Pinceau (U) ; la sélection de régions est celle du modèle partagé.
        const bool regionTool = regions_ && regions_->active();
        const auto chosen = regions_ ? regions_->selection().size() : std::size_t{0};
        const auto region = [&](const char* id, const QString& label, bool ok) {
            out.push_back({id, q(label), q(tr("Régions")), "", regionTool && ok});
        };
        region("region.select-color", tr("Régions : sélectionner la même couleur"), chosen >= 1);
        region("region.select-neighbours", tr("Régions : sélectionner les voisines"), chosen >= 1);
        region("region.invert", tr("Régions : inverser la sélection"), regionTool);
        region("region.merge", tr("Régions : fusionner la sélection"), chosen >= 2);
        region("region.recolor", tr("Régions : recolorer la sélection…"), chosen >= 1);
        region("region.regenerate", tr("Régions : régénérer les objets"),
               regions_ && regions_->dirty());
        region("region.objects", tr("Régions : sélectionner les objets de ces régions"),
               chosen >= 1);
        out.push_back(
            {"region.from-objects", q(tr("Régions : régions des objets sélectionnés")),
             q(tr("Régions")), "",
             regionTool && inspect(session_.project(), session_.selection().targets()).count > 0});
    }
    {
        const auto state = inspect(session_.project(), session_.selection().targets());
        const bool one = state.ids.size() == 1;
        const int guides = one ? directionGuideCount(session_.project(), state.ids.front()) : -1;
        const int breaks = one ? breakLineCount(session_.project(), state.ids.front()) : -1;
        out.push_back({"fill.guides.clear", q(tr("Effacer les guides de direction")),
                       q(tr("Remplissage")), "", guides > 0});
        out.push_back({"fill.breaks.clear", q(tr("Effacer les lignes de rupture")),
                       q(tr("Remplissage")), "", breaks > 0});
    }
    {
        const bool pointTool = activeTool_ == QStringLiteral("stitch.points");
        const bool chosen = pointTool && pointSel_ >= 0;
        const auto point = [&](const char* id, const QString& label, bool ok) {
            out.push_back({id, q(label), q(tr("Points")), "", ok});
        };
        point("point.restore", tr("Points : rétablir les points choisis"), chosen);
        point("point.jump", tr("Points : passer en saut (sans couture)"), chosen);
        point("point.stitch", tr("Points : passer en point cousu"), chosen);
        point("point.trim", tr("Points : couper le fil après"), chosen);
        point("point.untrim", tr("Points : ne pas couper le fil après"), chosen);
        point("point.select-all", tr("Points : tout choisir"), pointTool);
    }
    {
        const bool imageShown = documentOpen_ && frame_.px_per_mm > 0.0;
        const auto view = [&](const char* id, const QString& label, bool ok) {
            out.push_back({id, q(label), q(tr("Affichage")), "", ok});
        };
        view("view.zoom-in", tr("Zoom avant"), imageShown);
        view("view.zoom-out", tr("Zoom arrière"), imageShown);
        view("view.zoom-selection", tr("Zoom sur la sélection"),
             imageShown && !session_.selection().empty());
        view("view.zoom-actual", tr("Taille réelle (100 %)"), imageShown);
        view("view.zoom-previous", tr("Zoom précédent"), canvas_->zoomHistoryDepth() > 0);
        view("guide.add-v", tr("Guide vertical…"), imageShown);
        view("guide.add-h", tr("Guide horizontal…"), imageShown);
        view("guide.clear", tr("Effacer les guides"), guideCount() > 0);
    }
    out.push_back({"guide.skip-check",
                   q(checkSkipped_ ? tr("Parcours : reprendre le contrôle du motif")
                                   : tr("Parcours : ignorer le contrôle du motif (facultatif)")),
                   q(tr("Parcours")), "", documentOpen_});
    out.push_back({"image.resegment", q(tr("Re-segmenter l'image…")), q(tr("Image")), "",
                   documentOpen_ && session_.project().segmentation.has_value()});
    {
        // Filtres d'affichage : visibilité des objets (jamais de suppression), une étape chacun.
        const auto sel = inspect(session_.project(), session_.selection().targets());
        const bool one = !sel.ids.empty();
        out.push_back({"view.only-colour", q(tr("Afficher uniquement la couleur de la sélection")),
                       q(tr("Affichage")), "", one && !sel.rgb.mixed});
        out.push_back({"view.only-type",
                       q(tr("Afficher uniquement le type de points de la sélection")),
                       q(tr("Affichage")), "", one && !sel.typeId.mixed});
        out.push_back({"view.show-all", q(tr("Tout afficher (retirer les filtres)")),
                       q(tr("Affichage")), "Ctrl+Shift+H",
                       hiddenObjectCount(session_.project()) > 0});
    }
    for (const auto& [simId, simLabel] :
         {std::pair{"sim.prev-colour", tr("Simulation : couleur précédente")},
          std::pair{"sim.prev-object", tr("Simulation : objet précédent")},
          std::pair{"sim.prev-point", tr("Simulation : un point en arrière")},
          std::pair{"sim.next-point", tr("Simulation : un point en avant")},
          std::pair{"sim.next-object", tr("Simulation : objet suivant")},
          std::pair{"sim.next-colour", tr("Simulation : couleur suivante")},
          std::pair{"sim.end", tr("Simulation : aller à la fin")}}) {
        out.push_back(
            {simId, q(simLabel), q(tr("Simulation")), "", documentOpen_ && simulationTotal() > 0});
    }
    out.push_back({"file.export-threads", q(tr("Exporter la liste des fils (CSV)…")),
                   q(tr("Fichier")), "",
                   documentOpen_ && !session_.project().embroidery_objects.empty()});
    out.push_back({"file.import-chart", q(tr("Importer un nuancier de fils (CSV, JSON)…")),
                   q(tr("Fils")), "", true});
    out.push_back({"file.remove-chart", q(tr("Retirer un nuancier importé…")), q(tr("Fils")), "",
                   std::ranges::any_of(chartInfos(), [](const auto& c) { return c.user; })});
    out.push_back({"file.save-as", q(tr("Enregistrer le projet sous…")), q(tr("Fichier")),
                   "Ctrl+Shift+S", documentOpen_});
    out.push_back({"file.export-again", q(tr("Exporter de nouveau (derniers réglages)")),
                   q(tr("Fichier")), "F10", documentOpen_ && !lastExportPath_.empty()});
    out.push_back({"file.import-machine", q(tr("Importer une broderie machine…")), q(tr("Fichier")),
                   "", true});
    out.push_back({"file.import-dxf", q(tr("Importer un DXF…")), q(tr("Fichier")), "", true});
    out.push_back({"file.export-dxf", q(tr("Exporter les contours en DXF…")), q(tr("Fichier")), "",
                   !session_.project().vector_objects.empty()});
    out.push_back(
        {"view.render", q(tr("Qualité du rendu réaliste…")), q(tr("Affichage")), "", true});
    for (const char* preset : {"draft", "balanced", "high", "photo"}) {
        out.push_back({std::string("render.preset.") + preset,
                       q(tr("Rendu réaliste : %1").arg(fromUtf8(renderPresetLabel(preset)))),
                       q(tr("Affichage")), "", true});
    }
    out.push_back({"analysis.density",
                   q(overlays_->densityActive() ? tr("Masquer la carte de densité")
                                                : tr("Afficher la carte de densité")),
                   q(tr("Analyse")), "",
                   overlays_->densityActive() || !session_.project().embroidery_objects.empty()});
    int n = 1;
    for (const auto w : kWorkspaces) {
        out.push_back({"workspace." + std::string(workspaceId(w)),
                       q(tr("Espace : %1").arg(workspaceLabel(w))), q(tr("Espace")),
                       "Ctrl+" + std::to_string(n++), true});
    }
    // Tout outil est atteignable depuis n'importe quel espace (principe B) ; ceux qui ne sont pas
    // encore migrés sont listés désactivés, avec la raison.
    for (const auto& t : registry_.tools()) {
        const bool ok = isToolImplemented(t.id);
        out.push_back({"tool." + t.id, t.label + (ok ? "" : q(tr(" — pas encore migré"))),
                       q(tr("Outil")), t.shortcut, ok});
    }
    for (auto& e : out) {
        if (!e.shortcut.empty()) {
            continue;
        }
        for (const auto& [id, key] : kCommandShortcuts) {
            if (e.id == id) {
                e.shortcut = key;
            }
        }
    }
    return out;
}

bool ShellWindow::runCommand(const QString& id) {
    for (const auto& e : commandEntries()) {
        if (QString::fromStdString(e.id) != id) {
            continue;
        }
        if (!e.enabled) {
            return false;
        }
        if (id == QLatin1String("file.new")) {
            newProject();
        } else if (id == QLatin1String("file.open")) {
            chooseAndOpen();
        } else if (id == QLatin1String("file.save")) {
            saveAction_->trigger();
        } else if (id == QLatin1String("file.export")) {
            exportAction_->trigger();
        } else if (id == QLatin1String("edit.undo")) {
            session_.undo();
        } else if (id == QLatin1String("edit.redo")) {
            session_.redo();
        } else if (id == QLatin1String("edit.delete") || id == QLatin1String("edit.duplicate") ||
                   id.startsWith(QLatin1String("transform.")) ||
                   id.startsWith(QLatin1String("arrange.")) ||
                   id.startsWith(QLatin1String("order."))) {
            editSelection(id);
        } else if (id.startsWith(QLatin1String("image.prep."))) {
            runImagePrep(id);
        } else if (id.startsWith(QLatin1String("action."))) {
            runAction(id.mid(7));
        } else if (id == QLatin1String("preset.save")) {
            bool ok = false;
            const QString name = QInputDialog::getText(this, tr("Nouveau préréglage"), tr("Nom :"),
                                                       QLineEdit::Normal, QString(), &ok);
            if (ok) {
                savePresetNamed(name);
            }
        } else if (id.startsWith(QLatin1String("preset.apply."))) {
            applyPresetIndex(id.mid(13).toInt());
        } else if (id.startsWith(QLatin1String("preset.remove."))) {
            removePresetIndex(id.mid(14).toInt());
        } else if (id == QLatin1String("analysis.fabric")) {
            overlays_->toggleFabricRisk();
        } else if (id == QLatin1String("node.smooth") || id == QLatin1String("node.corner")) {
            return nodes_->setSelectedKind(id == QLatin1String("node.smooth") ? NodeKind::Smooth
                                                                              : NodeKind::Corner);
        } else if (id == QLatin1String("node.hole.remove")) {
            return nodes_->removeSelectedHole();
        } else if (id == QLatin1String("radial.open")) {
            showRadialMenu();
        } else if (id == QLatin1String("chat.open")) {
            openChat();
        } else if (id == QLatin1String("companion.settings")) {
            showCompanionSettings();
        } else if (id == QLatin1String("thread.palette")) {
            auto* dialog = new ThreadPaletteDialog(session_, this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        } else if (id == QLatin1String("text.edit") || id == QLatin1String("text.glyph")) {
            runAction(id);
        } else if (id == QLatin1String("text.glyph.reset") || id == QLatin1String("text.remove")) {
            return runActionWith(id, {});
        } else if (id == QLatin1String("export.options")) {
            auto* dialog = new ExportOptionsDialog(
                session_, stateDir_.empty() ? std::filesystem::path{} : stateDir_ / "hoops.txt",
                this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        } else if (id == QLatin1String("export.check")) {
            const auto check = checkExport(session_.project());
            setNotice(fromUtf8(check.summary()),
                      !check.ok || !check.fitsHoop || !check.fitsMachineField);
            if (!check.ok) {
                return false;
            }
            machineCheckRevision_ = session_.revision();
            machineBlockers_ = (check.fitsHoop ? 0 : 1) + (check.fitsMachineField ? 0 : 1) +
                               (check.formatAccepted ? 0 : 1);
            machineWarnings_ = 0;
            updateGuide();
        } else if (id == QLatin1String("analysis.pull")) {
            const auto cmp = compareProjectPull(session_.project());
            setNotice(fromUtf8(cmp.summary()), !cmp.ok);
            if (!cmp.ok) {
                return false;
            }
        } else if (id == QLatin1String("analysis.calibration")) {
            auto* dialog = new CalibrationDialog(&session_.project(), this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        } else if (id.startsWith(QLatin1String("fade.curve."))) {
            const auto target = pointTarget();
            if (!target) {
                setNotice(tr("Sélectionnez un seul objet portant un fondu."), true);
                return false;
            }
            static const std::pair<const char*, FadeCurve> kCurves[] = {
                {"linear", FadeCurve::Linear},
                {"smooth", FadeCurve::Smooth},
                {"steep", FadeCurve::Steep},
                {"easein", FadeCurve::EaseIn},
                {"easeout", FadeCurve::EaseOut}};
            for (const auto& [name, curve] : kCurves) {
                if (id.mid(11) == QLatin1String(name)) {
                    const auto r = setFadeCurve(session_, *target, curve);
                    if (!r.ok) {
                        setNotice(fromUtf8(r.message), true);
                        return false;
                    }
                    setNotice(tr("Courbe du fondu : %1.").arg(fromUtf8(fadeCurveLabel(curve))),
                              false);
                    break;
                }
            }
        } else if (id.startsWith(QLatin1String("satin.rung.")) ||
                   id == QLatin1String("satin.diagnose")) {
            const auto target = pointTarget();
            if (!target) {
                setNotice(tr("Sélectionnez une seule colonne satin."), true);
                return false;
            }
            if (id == QLatin1String("satin.diagnose")) {
                const auto d = diagnoseSatin(session_.project(), *target);
                setNotice(fromUtf8(satinDiagnosisSummary(d)), !d.clean());
            } else {
                const bool insert = id == QLatin1String("satin.rung.insert");
                const auto index = insert ? widestRungGap(session_.project(), *target)
                                          : redundantRung(session_.project(), *target);
                if (!index) {
                    setNotice(insert ? tr("Il faut au moins deux barreaux.")
                                     : tr("Une colonne satin garde au moins deux barreaux."),
                              true);
                    return false;
                }
                const auto r = insert ? insertSatinRungBetween(session_, *target, *index)
                                      : removeSatinRung(session_, *target, *index);
                if (!r.ok) {
                    setNotice(fromUtf8(r.message), true);
                    return false;
                }
                setNotice(insert ? tr("Barreau ajouté.") : tr("Barreau retiré."), false);
            }
        } else if (id == QLatin1String("vector.curves") || id == QLatin1String("vector.smooth")) {
            const auto selected = inspect(session_.project(), session_.selection().targets());
            const auto r = id == QLatin1String("vector.curves")
                               ? convertToCurves(session_, selected.ids)
                               : smoothContours(session_, selected.ids);
            if (!r.ok) {
                setNotice(fromUtf8(r.message), true);
                return false;
            }
            setNotice(id == QLatin1String("vector.curves") ? tr("Contours convertis en courbes.")
                                                           : tr("Contours lissés."),
                      false);
        } else if (id == QLatin1String("optimize.order")) {
            runOptimization(optimizePanel_->strategy(), optimizePanel_->improve());
        } else if (id == QLatin1String("companion.toggle")) {
            QString error;
            if (!setLiveBridgeEnabled(!liveBridgeEnabled(), &error)) {
                companionPanel_->setError(error);
                setNotice(tr("Compagnon IA : %1").arg(error), true);
                return false;
            }
        } else if (id == QLatin1String("analysis.run")) {
            setWorkspace(Workspace::Optimize);
            runDiagnostics();
        } else if (id == QLatin1String("fill.guides.clear") ||
                   id == QLatin1String("fill.breaks.clear")) {
            const auto state = inspect(session_.project(), session_.selection().targets());
            if (state.ids.size() == 1) {
                const auto r = clearDirectionGuides(session_, state.ids.front(),
                                                    id == QLatin1String("fill.breaks.clear"));
                setNotice(r.ok ? tr("Effacé (annulable).") : fromUtf8(r.message), !r.ok);
            }
        } else if (id == QLatin1String("point.restore")) {
            (void)restoreSelectedPoints();
        } else if (id == QLatin1String("point.jump")) {
            (void)setSelectedPointsJump(true);
        } else if (id == QLatin1String("point.stitch")) {
            (void)setSelectedPointsJump(false);
        } else if (id == QLatin1String("point.trim")) {
            (void)setSelectedPointsTrim(true);
        } else if (id == QLatin1String("point.untrim")) {
            (void)setSelectedPointsTrim(false);
        } else if (id == QLatin1String("point.select-all")) {
            selectAllPoints();
        } else if (id == QLatin1String("view.zoom-in")) {
            zoomIn();
        } else if (id == QLatin1String("view.zoom-out")) {
            zoomOut();
        } else if (id == QLatin1String("view.zoom-selection")) {
            zoomToSelection();
        } else if (id == QLatin1String("view.zoom-actual")) {
            zoomActual();
        } else if (id == QLatin1String("view.zoom-previous")) {
            zoomBack();
        } else if (id == QLatin1String("guide.add-v")) {
            askGuide(true);
        } else if (id == QLatin1String("guide.add-h")) {
            askGuide(false);
        } else if (id == QLatin1String("guide.clear")) {
            clearGuides();
        } else if (id == QLatin1String("guide.skip-check")) {
            setCheckSkipped(!checkSkipped_);
        } else if (id == QLatin1String("image.resegment")) {
            resegmentImage();
        } else if (id == QLatin1String("view.only-colour") ||
                   id == QLatin1String("view.only-type") || id == QLatin1String("view.show-all")) {
            const auto sel = inspect(session_.project(), session_.selection().targets());
            ServiceResult r{false, {}};
            if (id == QLatin1String("view.show-all")) {
                r = showAllObjects(session_);
            } else if (sel.ids.empty()) {
                r = {false, "Sélectionnez d'abord un objet."};
            } else if (id == QLatin1String("view.only-colour")) {
                r = sel.rgb.mixed
                        ? ServiceResult{false, "Les objets choisis n'ont pas la même couleur."}
                        : showOnlyColour(session_, sel.rgb.value);
            } else {
                r = sel.typeId.mixed
                        ? ServiceResult{false, "Les objets choisis n'ont pas le même type."}
                        : showOnlyType(session_, sel.typeId.value);
            }
            setNotice(r.ok ? tr("Affichage modifié (annulable). « Tout afficher » le rétablit.")
                           : fromUtf8(r.message),
                      !r.ok);
            if (!r.ok) {
                return false;
            }
        } else if (id.startsWith(QLatin1String("sim."))) {
            return simulationJump(id);
        } else if (id == QLatin1String("file.export-threads")) {
            chooseAndExportThreads();
        } else if (id == QLatin1String("file.import-chart")) {
            chooseAndImportChart();
        } else if (id == QLatin1String("file.remove-chart")) {
            removeImportedChart();
        } else if (id == QLatin1String("file.save-as")) {
            (void)saveProjectAs();
        } else if (id == QLatin1String("file.export-again")) {
            (void)exportAgain();
        } else if (id == QLatin1String("file.import-machine")) {
            chooseAndImportMachine();
        } else if (id == QLatin1String("file.import-dxf")) {
            chooseAndImportDxf();
        } else if (id == QLatin1String("file.export-dxf")) {
            chooseAndExportDxf();
        } else if (id == QLatin1String("view.render")) {
            showRenderQuality();
        } else if (id.startsWith(QLatin1String("render.preset."))) {
            if (const auto p = renderPreset(id.mid(14).toStdString())) {
                setRenderSettings(*p);
            }
        } else if (id == QLatin1String("analysis.density")) {
            overlays_->toggleDensity();
        } else if (id == QLatin1String("selection.all")) {
            if (regions_ && regions_->active()) {
                regions_->selectAll(); // avec l'outil Régions, Ctrl+A choisit les régions
            } else if (activeTool_ == QStringLiteral("stitch.points")) {
                selectAllPoints(); // avec la retouche de points : les points de l'objet
            } else {
                selectAllObjects();
            }
        } else if (id == QLatin1String("selection.clear")) {
            if (regions_ && regions_->active()) {
                regions_->clearSelection();
            } else if (activeTool_ == QStringLiteral("stitch.points")) {
                clearPointSelection();
            } else {
                session_.selection().clear();
            }
        } else if (id == QLatin1String("region.select-color")) {
            regions_->selectSameColor();
        } else if (id == QLatin1String("region.select-neighbours")) {
            regions_->selectNeighbours();
        } else if (id == QLatin1String("region.invert")) {
            regions_->invertSelection();
        } else if (id == QLatin1String("region.merge")) {
            (void)regions_->mergeSelection();
        } else if (id == QLatin1String("region.recolor")) {
            regions_->requestRecolor();
        } else if (id == QLatin1String("region.regenerate")) {
            regions_->regenerate();
        } else if (id == QLatin1String("region.objects")) {
            (void)regions_->selectObjectsOfSelection();
        } else if (id == QLatin1String("region.from-objects")) {
            (void)regions_->selectRegionsOfSelectedObjects();
        } else if (id == QLatin1String("view.fit")) {
            canvas_->fitAll();
        } else if (id.startsWith(QLatin1String("workspace."))) {
            for (const auto w : kWorkspaces) {
                if (id == QString::fromStdString("workspace." + std::string(workspaceId(w)))) {
                    setWorkspace(w);
                }
            }
        } else if (id.startsWith(QLatin1String("tool."))) {
            const std::string toolId = id.mid(5).toStdString();
            const auto* tool = registry_.find(toolId);
            if (tool == nullptr) {
                return false;
            }
            // Outil absent de l'espace courant : on bascule vers le premier espace qui le montre.
            if ((tool->workspaces & workspaceBit(workspace_)) == 0) {
                for (const auto w : kWorkspaces) {
                    if ((tool->workspaces & workspaceBit(w)) != 0) {
                        setWorkspace(w);
                        break;
                    }
                }
            }
            for (auto* a : toolGroup_->actions()) {
                if (a->data().toString() == id.mid(5) && a->isEnabled()) {
                    a->setChecked(true);
                    a->trigger();
                }
            }
        }
        return true;
    }
    return false;
}

void ShellWindow::showCommandPalette() {
    auto* palette = new CommandPalette(this);
    palette->setAttribute(Qt::WA_DeleteOnClose);
    palette->setEntries(commandEntries());
    connect(palette, &CommandPalette::chosen, this, [this](const QString& id) { runCommand(id); });
    palette->open();
}

void ShellWindow::updateSelectionBoxes() {
    QVector<QRectF> boxes;
    if (frame_.hits != nullptr) {
        for (const auto& t : session_.selection().targets()) {
            if (t.kind != TargetKind::EmbroideryObject) {
                continue;
            }
            if (const auto b = frame_.hits->boundsOf(t.id)) {
                const double x0 = frame_.mmToPxX(b->x0);
                const double y0 = frame_.mmToPxY(b->y0);
                boxes.push_back(
                    QRectF(QPointF(x0, y0), QPointF(frame_.mmToPxX(b->x1), frame_.mmToPxY(b->y1)))
                        .adjusted(-3, -3, 3, 3));
            }
        }
    }
    canvas_->setSelectionBoxes(boxes);
}

std::optional<QPointF> ShellWindow::objectCentrePx(std::uint64_t id) const {
    if (frame_.hits == nullptr) {
        return std::nullopt;
    }
    const auto p = frame_.hits->samplePoint(id);
    if (!p) {
        return std::nullopt;
    }
    return QPointF(frame_.mmToPxX(p->first), frame_.mmToPxY(p->second));
}

std::optional<std::uint64_t> ShellWindow::pickAtPx(QPointF scenePx) const {
    if (frame_.hits == nullptr) {
        return std::nullopt;
    }
    // Tolérance : 5 pixels d'écran, au moins 0,5 mm (petits fils fins faciles à viser).
    const double zoom = std::max(canvas_->zoom(), 1e-6);
    const double tolMm = std::max(0.5, 5.0 / zoom / frame_.px_per_mm);
    return frame_.hits->pick(frame_.pxToMmX(scenePx.x()), frame_.pxToMmY(scenePx.y()), tolMm);
}

void ShellWindow::onMoveStart(QPointF scenePx) {
    const auto hit = pickAtPx(scenePx);
    if (hit && !session_.selection().contains({TargetKind::EmbroideryObject, *hit})) {
        session_.selection().replace({{TargetKind::EmbroideryObject, *hit}});
    }
}

QString ShellWindow::helpText() const {
    QString out;
    std::string group;
    for (const auto& e : commandEntries()) {
        if (e.group != group) {
            group = e.group;
            out += QStringLiteral("\n== %1 ==\n").arg(fromUtf8(group));
        }
        out += QStringLiteral("  %1").arg(fromUtf8(e.label));
        if (!e.shortcut.empty()) {
            out += QStringLiteral("    [%1]").arg(fromUtf8(e.shortcut));
        }
        out += QLatin1Char('\n');
    }
    out += tr("\n== Souris ==\n  Molette : zoom · Clic milieu / outil Main : déplacer la vue\n"
              "  Clic : sélectionner · Maj / Ctrl : ajouter / basculer · Glisser : cadre de "
              "sélection\n");
    return out;
}

namespace {
struct SheetJob {
    ProductionSheet sheet;
    std::filesystem::path png;
};

void layoutProductionPdf(const QString& pdfPath, const ProductionSheet& s, const QImage& preview,
                         const QString& title) {
    QPdfWriter writer(pdfPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(120);
    writer.setTitle(title);
    QPainter p(&writer);
    const int margin = 60;
    int y = margin;
    QFont f = p.font();
    f.setPointSize(16);
    f.setBold(true);
    p.setFont(f);
    p.drawText(margin, y + 24, title);
    y += 60;
    f.setPointSize(10);
    f.setBold(false);
    p.setFont(f);
    const auto line = [&](const QString& text) {
        p.drawText(margin, y, text);
        y += 22;
    };
    line(QObject::tr("Motif : %1 × %2 mm — cadre : %3 × %4 mm%5")
             .arg(s.designWidthMm, 0, 'f', 1)
             .arg(s.designHeightMm, 0, 'f', 1)
             .arg(s.hoopWidthMm, 0, 'f', 0)
             .arg(s.hoopHeightMm, 0, 'f', 0)
             .arg(s.fitsHoop ? QString() : QObject::tr("  ⚠ dépasse la zone brodable")));
    line(QObject::tr("%1 objets · %2 points · %3 sauts · %4 coupes · %5 changements de fil")
             .arg(s.objects)
             .arg(s.stitches)
             .arg(s.jumps)
             .arg(s.trims)
             .arg(s.colorChanges));
    line(QObject::tr("Fil : %1 m · durée estimée : %2 min (800 points/min)")
             .arg(s.threadMetres, 0, 'f', 1)
             .arg(s.minutesAt800spm, 0, 'f', 0));
    y += 10;
    if (!preview.isNull()) {
        const int maxW = writer.width() - 2 * margin;
        const QImage scaled =
            preview.scaled(maxW, 520, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage(margin, y, scaled);
        y += scaled.height() + 20;
    }
    f.setBold(true);
    p.setFont(f);
    line(QObject::tr("Plan de fils (ordre de couture)"));
    f.setBold(false);
    p.setFont(f);
    int n = 1;
    for (const auto& b : s.blocks) {
        if (y > writer.height() - margin) {
            writer.newPage();
            y = margin;
        }
        p.fillRect(margin, y - 14, 18, 14, QColor(b.rgb[0], b.rgb[1], b.rgb[2]));
        p.drawRect(margin, y - 14, 18, 14);
        p.drawText(margin + 28, y,
                   QObject::tr("%1. #%2%3%4 — %5 points, %6 mm — %7")
                       .arg(n++)
                       .arg(b.rgb[0], 2, 16, QLatin1Char('0'))
                       .arg(b.rgb[1], 2, 16, QLatin1Char('0'))
                       .arg(b.rgb[2], 2, 16, QLatin1Char('0'))
                       .arg(b.stitches)
                       .arg(b.lengthMm, 0, 'f', 0)
                       .arg(QString::fromStdString(b.objects)));
        y += 22;
    }
    p.end();
}
} // namespace

void ShellWindow::exportProductionSheet(const std::filesystem::path& pdf) {
    const auto snapshot = session_.snapshot();
    const auto png = std::filesystem::temp_directory_path() / "openstitch_v2_sheet_preview.png";
    setNotice(tr("Fiche de production en cours…"), false);
    tasks_.submit<SheetJob>(
        tr("Fiche de production").toStdString(),
        [snapshot, png](const TaskContext& ctx) -> Result<SheetJob> {
            auto sheet = buildProductionSheet(*snapshot, ctx);
            if (!sheet) {
                return std::unexpected(sheet.error());
            }
            SheetJob job{std::move(*sheet), {}};
            if (job.sheet.stitches > 0) {
                if (export_preview_png(snapshot, png, 8.0, ctx)) {
                    job.png = png;
                }
            }
            return job;
        },
        [this, pdf](Result<SheetJob>&& r) {
            if (!r) {
                setNotice(tr("Fiche impossible : %1").arg(fromUtf8(r.error().message)), true);
                return;
            }
            QImage preview;
            if (!r->png.empty()) {
                preview.load(QString::fromStdU16String(r->png.u16string()));
                std::error_code ec;
                std::filesystem::remove(r->png, ec);
            }
            layoutProductionPdf(QString::fromStdU16String(pdf.u16string()), r->sheet, preview,
                                tr("Fiche de production — OpenStitch"));
            setNotice(tr("Fiche écrite : %1").arg(QString::fromStdString(pdf.filename().string())),
                      false);
        });
}

bool ShellWindow::applyPrep(const PrepRequest& request) {
    const auto result = applyImagePrep(session_, request);
    if (!result.ok) {
        setNotice(tr("Prétraitement refusé : %1").arg(fromUtf8(result.message)), true);
        return false;
    }
    setNotice(tr("Étape d'image ajoutée. Relancez « Segmenter l'image… » pour la prendre en "
                 "compte."),
              false);
    return true;
}

void ShellWindow::runImagePrep(const QString& id) {
    if (!session_.project().hasImage()) {
        setNotice(tr("Le projet n'a pas d'image source."), true);
        return;
    }
    PrepRequest r;
    const QString kind = id.mid(QStringLiteral("image.prep.").size());
    if (kind == QLatin1String("gray")) {
        r.kind = PrepKind::Grayscale;
    } else if (kind == QLatin1String("rotate.r")) {
        r.kind = PrepKind::RotateRight;
    } else if (kind == QLatin1String("rotate.l")) {
        r.kind = PrepKind::RotateLeft;
    } else if (kind == QLatin1String("rotate.180")) {
        r.kind = PrepKind::Rotate180;
    } else if (kind == QLatin1String("flip.h")) {
        r.kind = PrepKind::FlipHorizontal;
    } else if (kind == QLatin1String("flip.v")) {
        r.kind = PrepKind::FlipVertical;
    } else if (kind == QLatin1String("sharpen") || kind == QLatin1String("clahe")) {
        const bool sharpen = kind == QLatin1String("sharpen");
        bool ok = false;
        const double v = QInputDialog::getDouble(
            this, sharpen ? tr("Netteté") : tr("Contraste local"),
            sharpen ? tr("Force (0,05 à 3) :") : tr("Limite (1 à 8) :"), sharpen ? 1.0 : 2.0,
            sharpen ? 0.05 : 1.0, sharpen ? 3.0 : 8.0, 2, &ok);
        if (!ok) {
            return;
        }
        r.kind = sharpen ? PrepKind::Sharpen : PrepKind::Clahe;
        r.a = v;
    } else if (kind == QLatin1String("bc")) {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Luminosité et contraste"));
        auto* form = new QFormLayout(&dialog);
        auto* brightness = new QSpinBox(&dialog);
        auto* contrast = new QSpinBox(&dialog);
        for (auto* s : {brightness, contrast}) {
            s->setRange(-100, 100);
        }
        form->addRow(tr("Luminosité"), brightness);
        form->addRow(tr("Contraste"), contrast);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        r.kind = PrepKind::BrightnessContrast;
        r.a = brightness->value();
        r.b = contrast->value();
    } else if (kind == QLatin1String("crop")) {
        const auto size = workingImageSize(session_.project());
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Recadrer l'image (%1 × %2 px)").arg(size.width).arg(size.height));
        auto* form = new QFormLayout(&dialog);
        auto* x = new QSpinBox(&dialog);
        auto* y = new QSpinBox(&dialog);
        auto* w = new QSpinBox(&dialog);
        auto* h = new QSpinBox(&dialog);
        x->setRange(0, size.width);
        y->setRange(0, size.height);
        w->setRange(8, size.width);
        h->setRange(8, size.height);
        w->setValue(size.width);
        h->setValue(size.height);
        form->addRow(tr("Gauche (px)"), x);
        form->addRow(tr("Haut (px)"), y);
        form->addRow(tr("Largeur (px)"), w);
        form->addRow(tr("Hauteur (px)"), h);
        auto* buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        r.kind = PrepKind::Crop;
        r.x = x->value();
        r.y = y->value();
        r.w = w->value();
        r.h = h->value();
    } else {
        return;
    }
    (void)applyPrep(r);
}

void ShellWindow::showPlaceDialog() {
    const auto state = inspect(session_.project(), session_.selection().targets());
    if (state.ids.empty()) {
        return;
    }
    std::vector<ObjectId> shapes;
    for (const auto id : state.ids) {
        if (const auto* emb = session_.project().findEmbroidery(ObjectId{id})) {
            shapes.push_back(emb->source_vector);
        }
    }
    const auto geometry = commands::selection_geometry(session_.project(), shapes);
    if (!geometry) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Position et taille"));
    auto* form = new QFormLayout(&dialog);
    const auto spin = [&](double value, double lo, double hi, const QString& suffix) {
        auto* s = new QDoubleSpinBox(&dialog);
        s->setRange(lo, hi);
        s->setDecimals(2);
        s->setSuffix(suffix);
        s->setValue(value);
        return s;
    };
    auto* x = spin(to_millimeters(geometry->center.x).value, -1000, 1000, QStringLiteral(" mm"));
    auto* y = spin(to_millimeters(geometry->center.y).value, -1000, 1000, QStringLiteral(" mm"));
    auto* w = spin(to_millimeters(geometry->width).value, 0.1, 2000, QStringLiteral(" mm"));
    auto* h = spin(to_millimeters(geometry->height).value, 0.1, 2000, QStringLiteral(" mm"));
    auto* angle = spin(0.0, -360, 360, QStringLiteral(" °"));
    form->addRow(tr("Centre X"), x);
    form->addRow(tr("Centre Y (vers le haut)"), y);
    form->addRow(tr("Largeur"), w);
    form->addRow(tr("Hauteur"), h);
    form->addRow(tr("Rotation (anti-horaire)"), angle);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    placeSelection(x->value(), y->value(), w->value(), h->value(), angle->value());
}

bool ShellWindow::placeSelection(double xMm, double yMm, double widthMm, double heightMm,
                                 double rotateDeg) {
    const auto state = inspect(session_.project(), session_.selection().targets());
    PlaceRequest request;
    request.xMm = xMm;
    request.yMm = yMm;
    request.widthMm = widthMm;
    request.heightMm = heightMm;
    request.rotateDeg = rotateDeg;
    const auto batch = batchPlace(state.ids, request);
    if (batch.empty()) {
        setNotice(tr("Position et taille : valeurs hors limites."), true);
        return false;
    }
    const auto result = session_.applyBatch(batch, tr("Position et taille").toStdString());
    if (!result.committed) {
        setNotice(tr("Position et taille refusées."), true);
    }
    return result.committed;
}

void ShellWindow::applyTransform(const std::string& batch, const QString& label) {
    if (batch.empty()) {
        updateSelectionBoxes(); // geste hors limites ou nul : on rétablit l'affichage
        return;
    }
    const auto result = session_.applyBatch(batch, label.toStdString());
    if (!result.committed) {
        const auto& outcomes = result.report.outcomes;
        setNotice(tr("%1 refusé : %2")
                      .arg(label, outcomes.empty() ? tr("opération invalide")
                                                   : fromUtf8(outcomes.back().message)),
                  true);
        updateSelectionBoxes();
    }
}

QPointF ShellWindow::snapDelta(QPointF deltaPx) {
    canvas_->setSnapGuides({});
    if (frame_.px_per_mm <= 0.0 || frame_.hits == nullptr) {
        return deltaPx;
    }
    const auto selection = inspect(session_.project(), session_.selection().targets());
    if (selection.ids.empty()) {
        return deltaPx;
    }
    // Boîte d'un objet cousu en mm « modèle » (Y vers le haut), depuis l'index de sélection.
    const auto boxOf = [&](std::uint64_t id) -> std::optional<SnapBox> {
        const auto b = frame_.hits->boundsOf(id);
        if (!b) {
            return std::nullopt;
        }
        // Emprises de l'index en mm « écran » (Y vers le bas) -> mm « modèle » (Y vers le haut).
        return SnapBox{b->x0, -b->y1, b->x1, -b->y0};
    };
    std::optional<SnapBox> moving;
    for (const auto id : selection.ids) {
        if (const auto b = boxOf(id)) {
            moving = moving ? SnapBox{std::min(moving->x0, b->x0), std::min(moving->y0, b->y0),
                                      std::max(moving->x1, b->x1), std::max(moving->y1, b->y1)}
                            : *b;
        }
    }
    if (!moving) {
        return deltaPx;
    }
    std::vector<SnapBox> others;
    for (const auto& object : session_.project().embroidery_objects) {
        if (std::find(selection.ids.begin(), selection.ids.end(), object.id.value) !=
            selection.ids.end()) {
            continue;
        }
        if (const auto b = boxOf(object.id.value)) {
            others.push_back(*b);
        }
    }
    // Les guides sont des cibles d'accrochage : un trait vertical n'a qu'un x, un horizontal qu'un
    // y (l'autre coordonnée est reportée très loin pour ne jamais s'accrocher).
    constexpr double kFar = 1.0e6;
    for (const double x : guidesX_) {
        others.push_back({x, kFar, x, kFar});
    }
    for (const double y : guidesY_) {
        others.push_back({kFar, y, kFar, y});
    }
    const bool altHeld = QGuiApplication::keyboardModifiers() & Qt::AltModifier;
    // Seuil de 6 pixels de vue, converti en mm (zoom du canevas compris).
    const double zoom = std::max(0.01, canvas_->transform().m11());
    const double thresholdMm = (snapEnabled_ && !altHeld) ? 6.0 / (zoom * frame_.px_per_mm) : 0.0;
    const PointMm requested{deltaPx.x() / frame_.px_per_mm, -deltaPx.y() / frame_.px_per_mm};
    const auto snapped = snapMove(*moving, requested, others, thresholdMm, true);
    QVector<QLineF> lines;
    for (const auto& g : snapped.guides) {
        if (g.vertical) {
            lines.push_back(QLineF(frame_.mmToPxX(g.positionMm), frame_.mmToPxY(-g.fromMm),
                                   frame_.mmToPxX(g.positionMm), frame_.mmToPxY(-g.toMm)));
        } else {
            lines.push_back(QLineF(frame_.mmToPxX(g.fromMm), frame_.mmToPxY(-g.positionMm),
                                   frame_.mmToPxX(g.toMm), frame_.mmToPxY(-g.positionMm)));
        }
    }
    canvas_->setSnapGuides(lines);
    setNotice(tr("Déplacement : %1 mm, %2 mm%3")
                  .arg(snapped.delta.x, 0, 'f', 1)
                  .arg(snapped.delta.y, 0, 'f', 1)
                  .arg(snapped.snappedX || snapped.snappedY
                           ? tr("  (aimanté — Alt pour désactiver)")
                           : QString()),
              false);
    return QPointF(snapped.delta.x * frame_.px_per_mm, -snapped.delta.y * frame_.px_per_mm);
}

void ShellWindow::onMoveDragged(QPointF deltaPx) {
    if (frame_.px_per_mm <= 0.0) {
        return;
    }
    const auto state = inspect(session_.project(), session_.selection().targets());
    // Axe Y : l'image descend, le modèle monte.
    applyTransform(
        batchTranslate(state.ids, deltaPx.x() / frame_.px_per_mm, -deltaPx.y() / frame_.px_per_mm),
        tr("Déplacer"));
}

void ShellWindow::onScaleDragged(QPointF pivotPx, double factor) {
    if (frame_.px_per_mm <= 0.0) {
        return;
    }
    const auto state = inspect(session_.project(), session_.selection().targets());
    const PointMm pivot = screenToModel(frame_.pxToMmX(pivotPx.x()), frame_.pxToMmY(pivotPx.y()));
    applyTransform(batchScale(state.ids, factor, pivot), tr("Mettre à l'échelle"));
}

void ShellWindow::onCanvasClick(QPointF scenePx, Qt::KeyboardModifiers modifiers) {
    if (regions_ && regions_->onCanvasClick(scenePx, modifiers)) {
        return; // l'outil « Régions » a traité le clic
    }
    if ((activeTool_ == QStringLiteral("node.edit") ||
         activeTool_ == QStringLiteral("fade.axis")) &&
        frame_.px_per_mm > 0.0) {
        // Clic hors poignée : choisit l'objet sous le curseur (ses nœuds apparaissent) ou, sur le
        // fond, retire seulement la sélection de nœuds.
        nodes_->clearSelection();
        if (const auto hit = pickAtPx(scenePx)) {
            session_.selection().replace({{TargetKind::EmbroideryObject, *hit}});
        }
        return;
    }
    if (activeTool_ == QStringLiteral("stitch.points") && frame_.px_per_mm > 0.0) {
        const auto target = pointTarget();
        if (!target) {
            setNotice(tr("Retouche de points : sélectionnez un seul objet brodé."), false);
            return;
        }
        if (pointHandles_.empty() || pointObject_ != *target) {
            updatePointMarkers();
        }
        // Poignée sous le clic (rayon de capture de 8 px de vue).
        const double captureScene = 8.0 / std::max(0.01, canvas_->transform().m11());
        int hit = -1;
        double best = captureScene * captureScene;
        for (std::size_t h = 0; h < pointHandles_.size(); ++h) {
            const auto& p = pointView_.points[pointHandles_[h]];
            const double dx = frame_.mmToPxX(p.pos.x) - scenePx.x();
            const double dy = frame_.mmToPxY(-p.pos.y) - scenePx.y();
            const double d = dx * dx + dy * dy;
            if (d <= best) {
                best = d;
                hit = static_cast<int>(h);
            }
        }
        if (hit >= 0) {
            if (modifiers & Qt::ControlModifier) {
                if (const auto it = std::find(pointSels_.begin(), pointSels_.end(), hit);
                    it != pointSels_.end()) {
                    pointSels_.erase(it);
                    pointSel_ = pointSels_.empty() ? -1 : pointSels_.back();
                } else {
                    pointSels_.push_back(hit);
                    pointSel_ = hit;
                }
            } else if ((modifiers & Qt::AltModifier) && pointSel_ >= 0) {
                // Alt+clic : plage de points entre la poignée principale et celle-ci.
                const int a = std::min(pointSel_, hit);
                const int b = std::max(pointSel_, hit);
                pointSels_.clear();
                for (int h = a; h <= b; ++h) {
                    pointSels_.push_back(h);
                }
                pointSel_ = hit;
            } else {
                pointSels_ = {hit};
                pointSel_ = hit;
            }
            updatePointMarkers();
            return;
        }
        if (pointSel_ < 0 || pointSel_ >= static_cast<int>(pointHandles_.size())) {
            setNotice(tr("Cliquez d'abord sur un point à retoucher (Ctrl+clic : en ajouter)."),
                      false);
            return;
        }
        const auto& sel = pointView_.points[pointHandles_[static_cast<std::size_t>(pointSel_)]];
        const PointMm dest =
            screenToModel(frame_.pxToMmX(scenePx.x()), frame_.pxToMmY(scenePx.y()));
        const bool insert = modifiers & Qt::ShiftModifier;
        const auto r = insert ? insertStitchPoint(session_, *target, sel.index, dest)
                              : moveStitchPoints(session_, *target, {{sel.index, dest}});
        if (!r.ok) {
            setNotice(tr("Retouche de points : %1").arg(fromUtf8(r.message)), true);
        } else {
            setNotice(insert ? tr("Point inséré.") : tr("Point déplacé."), false);
        }
        return;
    }
    if (activeTool_ == QStringLiteral("click-to-stitch") && frame_.px_per_mm > 0.0) {
        const image::Image* img = ensureProcessed();
        if (img == nullptr) {
            return;
        }
        const auto r = clickToStitch(session_, *img, frame_.pxToMmX(scenePx.x()),
                                     frame_.pxToMmY(scenePx.y()), clickTol_, clickKind_);
        if (!r.ok) {
            setNotice(tr("Clic-pour-broder : %1").arg(fromUtf8(r.message)), true);
            return;
        }
        session_.selection().replace({{TargetKind::EmbroideryObject, r.objectId}});
        setNotice(tr("Clic-pour-broder : zone brodée."), false);
        return;
    }
    if (activeTool_ == QStringLiteral("eyedropper") && frame_.hits != nullptr) {
        // Pipette : prélève le fil de l'objet cousu sous le curseur et l'applique à la sélection
        // courante (une annulation) ; sans sélection, la couleur est seulement annoncée.
        const auto hit = pickAtPx(scenePx);
        if (!hit) {
            setNotice(tr("Pipette : aucun fil sous le curseur."), false);
            return;
        }
        for (const auto& o : session_.project().embroidery_objects) {
            if (o.id.value != *hit) {
                continue;
            }
            const QColor c(o.rgb[0], o.rgb[1], o.rgb[2]);
            const auto state = inspect(session_.project(), session_.selection().targets());
            if (!state.ids.empty() && !(state.ids.size() == 1 && state.ids.front() == *hit)) {
                inspector_->applyColor(c);
                setNotice(tr("Pipette : %1 appliqué à la sélection.").arg(c.name().toUpper()),
                          false);
            } else {
                setNotice(tr("Pipette : %1 (« %2 »).").arg(c.name().toUpper(), fromUtf8(o.name)),
                          false);
            }
            return;
        }
        return;
    }
    if (activeTool_ != QStringLiteral("select") || frame_.hits == nullptr) {
        return;
    }
    const auto hit = pickAtPx(scenePx);
    if (!hit) {
        if (!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier))) {
            session_.selection().clear();
        }
        return;
    }
    const Target target{TargetKind::EmbroideryObject, *hit};
    if (modifiers & Qt::ControlModifier) {
        session_.selection().toggle(target);
    } else if (modifiers & Qt::ShiftModifier) {
        session_.selection().add(target);
    } else {
        session_.selection().replace({target});
    }
}

void ShellWindow::waitForIdle() {
    // Une tâche terminée peut relancer le minuteur d'aperçu : on boucle jusqu'à stabilité.
    do {
        if (previewDebounce_->isActive()) {
            previewDebounce_->stop();
            requestPreview();
        }
        tasks_.waitIdle();
    } while (previewDebounce_->isActive());
    QApplication::processEvents();
    refreshTaskStatus();
}

} // namespace openstitch::desktop_v2
