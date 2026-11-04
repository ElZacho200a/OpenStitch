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
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QPixmap>
#include <QProgressBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "inspector_panel.hpp"
#include "object_labels.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "preview_canvas.hpp"
#include "preview_service.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;
constexpr double kPreviewPxPerMm = 3.0;

QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

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
    session_.unsubscribe(sessionToken_);
    session_.selection().unsubscribe(selectionToken_);
}

// --- Construction ----------------------------------------------------------------------------

void ShellWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&Fichier"));
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
    exportAction_ = file->addAction(tr("E&xporter en DST…"));
    exportAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(exportAction_, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exporter en DST"), QString(),
                                                          tr("Tajima DST (*.dst)"));
        if (!path.isEmpty()) {
            exportDst(std::filesystem::path(path.toStdU16String()));
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

    auto* view = menuBar()->addMenu(tr("&Affichage"));
    auto* fit = view->addAction(tr("&Ajuster à la fenêtre"));
    connect(fit, &QAction::triggered, canvas_, &PreviewCanvas::fitAll);
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
    layout->addWidget(inspector_);
    summary_ = new QLabel(body);
    summary_->setObjectName(QStringLiteral("summary"));
    summary_->setWordWrap(true);
    layout->addWidget(summary_);
    dock->setWidget(body);
    addDockWidget(Qt::RightDockWidgetArea, dock);

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
        const bool implemented = tool->id == "select" || tool->id == "pan";
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
            canvas_->setDragMode(activeTool_ == QStringLiteral("pan") ||
                                         activeTool_ == QStringLiteral("select")
                                     ? QGraphicsView::ScrollHandDrag
                                     : QGraphicsView::NoDrag);
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
    saveAction_->setEnabled(hasObjects);
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
    if (session_.project().embroidery_objects.empty()) {
        canvas_->clearImage();
        previewShown_ = false;
        frame_ = PreviewImage{};
        return;
    }
    const auto snapshot = session_.snapshot();
    const auto revision = session_.revision();
    previewRevision_ = revision;
    previewTask_ = tasks_.submit<PreviewImage>(
        tr("Aperçu").toStdString(),
        [snapshot, revision](const TaskContext& ctx) {
            return render_preview(snapshot, revision, kPreviewPxPerMm, ctx);
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
        [snapshot, dst](const TaskContext& ctx) { return export_project_dst(snapshot, dst, ctx); },
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

void ShellWindow::onCanvasClick(QPointF scenePx, Qt::KeyboardModifiers modifiers) {
    if (activeTool_ != QStringLiteral("select") || frame_.hits == nullptr) {
        return;
    }
    const double xMm = frame_.pxToMmX(scenePx.x());
    const double yMm = frame_.pxToMmY(scenePx.y());
    // Tolérance : 5 pixels d'écran, au moins 0,5 mm (petits fils fins faciles à viser).
    const double zoom = std::max(canvas_->zoom(), 1e-6);
    const double tolMm = std::max(0.5, 5.0 / zoom / frame_.px_per_mm);
    const auto hit = frame_.hits->pick(xMm, yMm, tolMm);
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
