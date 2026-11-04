// SPDX-License-Identifier: Apache-2.0
#include "shell_window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QProgressBar>
#include <QScrollArea>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <map>

#include "action_dialog.hpp"
#include "calque_panel.hpp"
#include "command_palette.hpp"
#include "diagnostics_panel.hpp"
#include "inspector_model.hpp"
#include "inspector_panel.hpp"
#include "object_labels.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "preview_canvas.hpp"
#include "preview_service.hpp"
#include "transform_batch.hpp"
#include <QGuiApplication>
#include <QSlider>
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
           id == "draw.polygon" || id == "measure" || id == "eyedropper";
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
    });
    connect(canvas_, &PreviewCanvas::clickedPx, this, &ShellWindow::onCanvasClick);
    connect(canvas_, &PreviewCanvas::rectPx, this, &ShellWindow::onCanvasRect);
    connect(canvas_, &PreviewCanvas::boxDrawnPx, this, &ShellWindow::onBoxDrawn);
    connect(canvas_, &PreviewCanvas::measuredPx, this, &ShellWindow::onMeasured);
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
    tasks_.cancelAll();
    // Les widgets enfants sont détruits APRÈS les membres de cette classe : ceux qui référencent la
    // session (l'inspecteur) doivent disparaître tant qu'elle existe.
    delete inspector_;
    inspector_ = nullptr;
    delete calques_;
    calques_ = nullptr;
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
    auto* transform = edit->addMenu(tr("&Transformer"));
    addEdit(transform, tr("Pivoter de 90° à &droite"), "transform.rotate.cw");
    addEdit(transform, tr("Pivoter de 90° à &gauche"), "transform.rotate.ccw");
    addEdit(transform, tr("Miroir &horizontal"), "transform.mirror.h");
    addEdit(transform, tr("Miroir &vertical"), "transform.mirror.v");
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
    presetMenu_ = menuBar()->addMenu(tr("&Préréglages"));
    connect(presetMenu_, &QMenu::aboutToShow, this, &ShellWindow::rebuildPresetMenu);
    auto* fit = view->addAction(tr("&Ajuster à la fenêtre"));
    connect(fit, &QAction::triggered, canvas_, &PreviewCanvas::fitAll);
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

void ShellWindow::onDocumentChanged(ChangeKind) {
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
    setNotice(tr("Projet enregistré."), false);
    return true;
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

void ShellWindow::applyToolToCanvas() {
    using Mode = PreviewCanvas::Mode;
    if (activeTool_ == QStringLiteral("pan")) {
        canvas_->setMode(Mode::Pan);
    } else if (activeTool_ == QStringLiteral("draw.rectangle") ||
               activeTool_ == QStringLiteral("draw.ellipse")) {
        canvas_->setMode(Mode::DrawBox);
    } else if (activeTool_ == QStringLiteral("draw.polygon")) {
        canvas_->setMode(Mode::DrawPolygon);
    } else if (activeTool_ == QStringLiteral("measure")) {
        canvas_->setMode(Mode::Measure);
    } else if (activeTool_ == QStringLiteral("eyedropper")) {
        canvas_->setMode(Mode::Pick);
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
    edit("order.front", tr("Premier plan"), "Ordre de couture", "");
    edit("order.forward", tr("Avancer d'un cran"), "Ordre de couture", "");
    edit("order.backward", tr("Reculer d'un cran"), "Ordre de couture", "");
    edit("order.back", tr("Arrière-plan"), "Ordre de couture", "");
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
                   id.startsWith(QLatin1String("order."))) {
            editSelection(id);
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
