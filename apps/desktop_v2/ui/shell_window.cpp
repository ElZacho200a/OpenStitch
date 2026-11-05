// SPDX-License-Identifier: Apache-2.0
#include "shell_window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
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
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <map>

#include "openstitch/commands/placement_ops.hpp"

#include "action_dialog.hpp"
#include "calque_panel.hpp"
#include "command_palette.hpp"
#include "diagnostics_panel.hpp"
#include "feedback_cue.hpp"
#include "inspector_model.hpp"
#include "inspector_panel.hpp"
#include "object_labels.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_canvas.hpp"
#include "preview_service.hpp"
#include "thread_panel.hpp"
#include "transform_batch.hpp"
#include <QComboBox>
#include <QGuiApplication>
#include <QSignalBlocker>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStyleHints>

namespace openstitch::desktop_v2 {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;
constexpr double kPreviewPxPerMm = 3.0;

QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

// Outils réellement migrés : les autres sont déclarés dans le registre mais désactivés.
bool isToolImplemented(const std::string& id) {
    return id == "select" || id == "pan" || id == "draw.rectangle" || id == "draw.ellipse" ||
           id == "draw.polygon" || id == "measure" || id == "eyedropper" || id == "knife" ||
           id == "click-to-stitch" || id == "detail-brush" || id == "stitch.points" ||
           id == "satin.column";
}

constexpr Workspace kWorkspaces[] = {Workspace::Design, Workspace::Digitize, Workspace::Optimize,
                                     Workspace::Produce};

QString workspaceLabel(Workspace w) {
    switch (w) {
    case Workspace::Design:
        return QObject::tr("Conception");
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
    setCentralWidget(canvas_);

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
    });
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
    connect(canvas_, &PreviewCanvas::polygonDrawnPx, this, &ShellWindow::onPolygonDrawn);
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
    refreshActionStates();
}

ShellWindow::~ShellWindow() {
    // Les abonnements pointent vers `this` : on les retire avant que les widgets disparaissent, et
    // on annule les tâches avant que la session (membre) soit détruite.
    pump_->stop();
    previewDebounce_->stop();
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
    recentMenu_ = file->addMenu(tr("Fichiers &récents"));
    recentMenu_->setObjectName(QStringLiteral("menu_recent"));
    saveAction_ = file->addAction(tr("&Enregistrer"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] {
        QString path = QString::fromStdString(currentPath_.string());
        if (path.isEmpty() || !path.endsWith(QStringLiteral(".osp"))) {
            path = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"), QString(),
                                                tr("Projet OpenStitch (*.osp)"));
        }
        if (!path.isEmpty()) {
            saveProject(std::filesystem::path(path.toStdU16String()));
        }
    });
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
    connect(all, &QAction::triggered, this, &ShellWindow::selectAllObjects);
    auto* none = edit->addAction(tr("&Désélectionner"));
    none->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    connect(none, &QAction::triggered, this, [this] { session_.selection().clear(); });
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
    auto* themeMenu = view->addMenu(tr("&Thème"));
    auto* themeGroup = new QActionGroup(this);
    for (const auto& [mode, label] :
         {std::pair{"system", tr("Système")}, std::pair{"light", tr("Clair")},
          std::pair{"dark", tr("Sombre")}}) {
        auto* t = themeMenu->addAction(label);
        t->setCheckable(true);
        t->setChecked(QLatin1String(mode) == QLatin1String("system"));
        themeGroup->addAction(t);
        connect(t, &QAction::triggered, this,
                [this, mode] { setThemeMode(QString::fromLatin1(mode)); });
    }
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
}

void ShellWindow::buildWorkspaceBar() {
    workspaceBar_ = addToolBar(tr("Espaces de travail"));
    workspaceBar_->setObjectName(QStringLiteral("workspaceBar"));
    workspaceBar_->setMovable(false);
    workspaceGroup_ = new QActionGroup(this);
    workspaceGroup_->setExclusive(true);
    int n = 1;
    for (const auto w :
         {Workspace::Design, Workspace::Digitize, Workspace::Optimize, Workspace::Produce}) {
        auto* a = workspaceBar_->addAction(workspaceLabel(w));
        a->setCheckable(true);
        a->setData(static_cast<int>(w));
        a->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(n++)));
        workspaceGroup_->addAction(a);
        workspaceActions_.push_back(a);
        connect(a, &QAction::triggered, this, [this, w] { setWorkspace(w); });
    }
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
    toolRail_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolRail_->setMaximumWidth(56);
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
    docDock_->raise();
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

void ShellWindow::setWorkspace(Workspace workspace) {
    workspace_ = workspace;
    for (auto* a : workspaceActions_) {
        a->setChecked(a->data().toInt() == static_cast<int>(workspace));
    }
    rebuildToolRail();
    (workspace == Workspace::Optimize ? diagDock_ : docDock_)->raise();
    simBar_->setVisible(workspace == Workspace::Produce);
    if (workspace != Workspace::Produce && simLimit_) {
        setSimulationPosition(simulationTotal()); // quitter Production termine la simulation
    }
    setWindowTitle(tr("OpenStitch V2 — %1").arg(workspaceLabel(workspace)));
}

void ShellWindow::rebuildToolRail() {
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
        // Rail compact : le bouton porte la touche du raccourci (ou les initiales), le libellé
        // complet est dans l'infobulle et le nom accessible.
        QString shortText = fromUtf8(tool->shortcut);
        shortText.replace(QStringLiteral("Maj+"), QStringLiteral("⇧"));
        if (shortText.isEmpty()) {
            shortText = label.left(2);
        }
        auto* a = new QAction(shortText, toolGroup_);
        a->setCheckable(true);
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
        }
        if (fromUtf8(tool->id) == activeTool_ && implemented) {
            a->setChecked(true);
            activeStillThere = true;
        }
        connect(a, &QAction::triggered, this, [this, a] {
            activeTool_ = a->data().toString();
            applyToolToCanvas();
        });
    }
    if (!activeStillThere) {
        activeTool_ = QStringLiteral("select");
        for (auto* a : toolGroup_->actions()) {
            a->setChecked(a->data().toString() == activeTool_);
        }
    }
}

// --- Document, sélection, aperçu -------------------------------------------------------------

void ShellWindow::onDocumentChanged(ChangeKind kind) {
    if (kind != ChangeKind::Replaced && feedbackCue_ != nullptr) {
        feedbackCue_->pulse();
    }
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
    previewTask_ = tasks_.submit<PreviewImage>(
        tr("Aperçu").toStdString(),
        [snapshot, revision, limit](const TaskContext& ctx) {
            return render_preview(snapshot, revision, kPreviewPxPerMm, ctx, limit);
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
                updateSourceOverlay();
                updatePointMarkers();
                refreshSimulationBar();
                updateSelectionBoxes();
            }
        });
}

bool ShellWindow::previewShown() const {
    return previewShown_;
}

// --- Fichiers --------------------------------------------------------------------------------

void ShellWindow::chooseAndOpen() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Ouvrir"), QString(),
                                     tr("Images et projets (*.png *.jpg *.jpeg *.bmp *.tif *.tiff "
                                        "*.osp);;Tous les fichiers (*)"));
    if (path.isEmpty()) {
        return;
    }
    const std::filesystem::path p(path.toStdU16String());
    if (path.endsWith(QStringLiteral(".osp"), Qt::CaseInsensitive)) {
        openProject(p);
    } else {
        openImage(p, heightSpin_->value());
    }
}

void ShellWindow::openImage(const std::filesystem::path& image, double heightMm) {
    DigitizeRequest request;
    request.image = image;
    request.target_height_mm = heightMm;
    setNotice(tr("Numérisation de %1…").arg(QString::fromStdString(image.filename().string())),
              false);
    tasks_.submit<DigitizeOutput>(
        tr("Numérisation").toStdString(),
        [request](const TaskContext& ctx) { return run_digitize(request, ctx); },
        [this, image](Result<DigitizeOutput>&& r) {
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
    setNotice(tr("Ouverture de %1…").arg(QString::fromStdString(osp.filename().string())), false);
    tasks_.submit<document::Project>(
        tr("Ouverture").toStdString(),
        [osp](const TaskContext&) { return project_io::load_project(osp); },
        [this, osp](Result<document::Project>&& r) {
            if (!r) {
                setNotice(tr("Ouverture impossible : %1").arg(fromUtf8(r.error().message)), true);
                return;
            }
            currentPath_ = osp;
            session_.selection().clear();
            documentOpen_ = true;
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
    if (!hasRecovery()) {
        return false;
    }
    auto loaded = autosave_->load();
    if (!loaded) {
        setNotice(tr("Récupération impossible : %1").arg(fromUtf8(loaded.error().message)), true);
        return false;
    }
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
    const auto snapshot = session_.snapshot();
    setNotice(tr("Export DST en cours…"), false);
    tasks_.submit<stitch::StitchStats>(
        tr("Export DST").toStdString(),
        [snapshot, dst](const TaskContext& ctx) { return export_project_file(snapshot, dst, ctx); },
        [this, dst](Result<stitch::StitchStats>&& r) {
            if (!r) {
                setNotice(tr("Export impossible : %1 %2")
                              .arg(fromUtf8(r.error().message), fromUtf8(r.error().detail)),
                          true);
                return;
            }
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
    static const char* const kPrefixes[] = {"edit.",         "transform.",    "order.",
                                            "selection.",    "action.satin.", "action.fill.",
                                            "preset.apply.", "preset.save"};
    std::string lastGroup;
    for (const auto& e : commandEntries()) {
        bool wanted = false;
        for (const char* p : kPrefixes) {
            wanted = wanted || e.id.rfind(p, 0) == 0;
        }
        if (!wanted || !e.enabled || e.id == "edit.undo" || e.id == "edit.redo") {
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

void ShellWindow::updatePointMarkers() {
    const auto target = activeTool_ == QStringLiteral("stitch.points") && documentOpen_
                            ? pointTarget()
                            : std::nullopt;
    pointHandles_.clear();
    if (!target || frame_.px_per_mm <= 0.0) {
        pointSel_ = -1;
        pointObject_ = 0;
        pointView_ = {};
        canvas_->setPointMarkers({}, -1);
        return;
    }
    if (pointObject_ != *target) {
        pointSel_ = -1;
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
    canvas_->setPointMarkers(pts, pointSel_);
}

bool ShellWindow::deleteSelectedPoint() {
    if (pointSel_ < 0 || pointSel_ >= static_cast<int>(pointHandles_.size()) || !pointTarget()) {
        return false;
    }
    const auto index = pointView_.points[pointHandles_[static_cast<std::size_t>(pointSel_)]].index;
    const auto r = deleteStitchPoints(session_, *pointTarget(), {index});
    if (!r.ok) {
        setNotice(tr("Retouche de points : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    pointSel_ = -1;
    setNotice(tr("Point supprimé."), false);
    return true;
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
               activeTool_ == QStringLiteral("satin.column")) {
        canvas_->setMode(Mode::DrawPolygon);
    } else if (activeTool_ == QStringLiteral("measure")) {
        canvas_->setMode(Mode::Measure);
    } else if (activeTool_ == QStringLiteral("eyedropper")) {
        canvas_->setMode(Mode::Pick);
    } else if (activeTool_ == QStringLiteral("knife")) {
        canvas_->setMode(Mode::Cut);
    } else if (activeTool_ == QStringLiteral("click-to-stitch") ||
               activeTool_ == QStringLiteral("stitch.points")) {
        canvas_->setMode(Mode::Pick);
    } else if (activeTool_ == QStringLiteral("detail-brush")) {
        canvas_->setMode(Mode::Stroke);
    } else {
        canvas_->setMode(Mode::Select); // sélection : clic, cadre élastique
    }
}

void ShellWindow::newProject() {
    auto created = project_ops::create_empty_project({});
    if (!created) {
        setNotice(tr("Création du projet impossible : %1").arg(fromUtf8(created.error().message)),
                  true);
        return;
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
    const ActionDef* def = findAction(id.toStdString());
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
    std::vector<Target> created;
    for (const auto newId : createdObjectIds(result.report)) {
        created.push_back({TargetKind::EmbroideryObject, newId});
    }
    if (!created.empty()) {
        session_.selection().replace(std::move(created));
    }
    setNotice(fromUtf8(def->label).remove(QStringLiteral("…")) + tr(" : fait."), false);
    return true;
}

void ShellWindow::runAction(const QString& id) {
    const ActionDef* def = findAction(id.toStdString());
    if (def == nullptr) {
        return;
    }
    if (def->fields.empty()) {
        runActionWith(id, {});
        return;
    }
    auto* dialog = new ActionDialog(*def, defaultValues(*def), this);
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
    edit("edit.delete", tr("Supprimer la sélection"), "Édition", "Suppr");
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
    out.push_back({"analysis.run", q(tr("Analyser le document (diagnostic)")), q(tr("Analyse")), "",
                   !session_.project().embroidery_objects.empty()});
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
        } else if (id == QLatin1String("analysis.run")) {
            setWorkspace(Workspace::Optimize);
            runDiagnostics();
        } else if (id == QLatin1String("selection.all")) {
            selectAllObjects();
        } else if (id == QLatin1String("selection.clear")) {
            session_.selection().clear();
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
            pointSel_ = hit;
            updatePointMarkers();
            return;
        }
        if (pointSel_ < 0 || pointSel_ >= static_cast<int>(pointHandles_.size())) {
            setNotice(tr("Cliquez d'abord sur un point à retoucher."), false);
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
