// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"
#include "main_window_internal.hpp"
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

namespace openstitch::desktop {

// Repère scène (mm, Y vers le bas, Qt) <-> repère modèle (µm, Y vers le haut,
// ADR-003). Conversion utilisée partout où une géométrie est construite
// depuis un geste souris (rectangle/ellipse/polygone dessinés à la main).
Vec2um sceneMmToModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}
QPointF modelToSceneMm(Vec2um pos) {
    return QPointF(to_millimeters(pos.x).value, -to_millimeters(pos.y).value);
}

Micrometers vectorize_tolerance_from_detail(int detail) {
    const double t = std::clamp(static_cast<double>(detail), 0.0, 100.0) / 100.0;
    constexpr double kLooseUm = 1000.0; // detail 0 : lissage fort
    constexpr double kFineUm = 40.0;    // detail 100 : contour tres fidele
    return Micrometers{
        static_cast<std::int32_t>(std::lround(kLooseUm * std::pow(kFineUm / kLooseUm, t)))};
}

namespace {
// Étiquette à texte élidé (ligne d'indications) : le texte complet reste dans
// l'infobulle ; l'élision suit la largeur courante, jamais l'inverse.
class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;
    void setFullText(const QString& text) {
        full_ = text;
        setToolTip(text);
        updateElision();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        updateElision();
    }

private:
    void updateElision() { setText(fontMetrics().elidedText(full_, Qt::ElideRight, width())); }
    QString full_;
};
} // namespace

MainWindow::MainWindow() {
    updateWindowTitle();
    resize(1100, 800);
    fitWindowToScreen(0.92); // jamais plus grand que l'écran (portables à 125-150 %)
    setAcceptDrops(true);

    scene_ = new QGraphicsScene(this);
    view_ = new CanvasView(scene_, this);

    applyCanvasToView();

    auto* central = new QWidget(this);
    auto* grid = new QGridLayout(central);
    grid->setSpacing(0);
    grid->setContentsMargins(0, 0, 0, 0);
    auto* corner = new QLabel(tr("mm"), central);
    corner->setAlignment(Qt::AlignCenter);
    grid->addWidget(corner, 0, 0);
    grid->addWidget(new Ruler(Qt::Horizontal, view_, central), 0, 1);
    grid->addWidget(new Ruler(Qt::Vertical, view_, central), 1, 0);
    grid->addWidget(view_, 1, 1);
    setCentralWidget(central);

    buildMenus();
    buildPropertiesPanel();
    buildAnalysisPanel();
    buildOrderPanel();
    buildDocumentPanel();
    buildWorkflowPanel();
    buildFilterPanel();
    buildHistoryPanel();
    buildThreadPanel();
    // Le dock Couleurs existe dès le départ, caché : sa place dans la disposition enregistrée
    // n'est plus perdue au redémarrage (HP-UX-024).
    showColorsPanel();
    colorsDock_->hide();
    // [cablage-A:debut]
    // [cablage-A:fin]
    // [cablage-B:debut]
    buildVectorToolsMenu();
    // [cablage-B:fin]
    // [cablage-C:debut]
    buildMeasureTool();
    buildEyedropperTool();
    buildBrushTool();
    buildBucketTool();
    buildClickToStitchTool();
    buildDetailZonesTool();
    // [cablage-C:fin]
    // [cablage-D:debut]
    buildOrderJumpsFeature();
    buildFabricRiskFeature();
    // [cablage-D:fin]
    // [cablage-E:debut]
    buildDensityFeature();
    buildPullPreviewFeature();
    // [cablage-E:fin]
    buildCompanionDock();
    panelsMenu_->addAction(historyDock_->toggleViewAction());
    for (auto* d :
         {documentDock_, propertiesDock_, workflowDock_, orderDock_, filterDock_, analysisDock_}) {
        panelsMenu_->addAction(d->toggleViewAction());
    }
    // Filtres d'affichage en onglet derrière les propriétés : l'inspecteur garde toute la
    // hauteur de la colonne de droite au lieu de la partager à parts égales.
    tabifyDockWidget(propertiesDock_, filterDock_);
    propertiesDock_->raise();
    // Analyse, Historique, Fils et Couleurs en onglets (HP-UX-024) : un seul de ces panneaux
    // occupe la colonne de droite à la fois, au lieu de s'empiler sur toute la hauteur.
    panelsMenu_->addAction(colorsDock_->toggleViewAction());
    for (QDockWidget* secondary : {historyDock_, threadDock_, colorsDock_}) {
        tabifyDockWidget(analysisDock_, secondary);
    }
    panelsMenu_->addSeparator();
    // Mode portable (HP-UX-027) : Document, Ordre, Workflow et Propriétés partagent une seule
    // colonne en onglets, le canevas récupère toute la largeur de l'autre côté.
    auto* portableAct = panelsMenu_->addAction(tr("Mode &portable (une seule colonne)"));
    portableAct->setObjectName(QStringLiteral("action_portableMode"));
    portableAct->setCheckable(true);
    portableAct->setToolTip(tr("Regroupe les panneaux en onglets dans une colonne unique : "
                               "utile sur un petit écran."));
    connect(portableAct, &QAction::toggled, this, [this](bool on) {
        const std::array<QDockWidget*, 3> leftDocks{documentDock_, orderDock_, workflowDock_};
        std::array<bool, 3> hidden{};
        for (std::size_t i = 0; i < leftDocks.size(); ++i) {
            hidden[i] = leftDocks[i]->isHidden();
        }
        if (on) {
            for (QDockWidget* dock : leftDocks) {
                addDockWidget(Qt::RightDockWidgetArea, dock);
                tabifyDockWidget(propertiesDock_, dock);
            }
            propertiesDock_->raise();
        } else {
            addDockWidget(Qt::LeftDockWidgetArea, documentDock_);
            splitDockWidget(documentDock_, workflowDock_, Qt::Vertical);
            tabifyDockWidget(documentDock_, orderDock_);
            documentDock_->raise();
        }
        for (std::size_t i = 0; i < leftDocks.size(); ++i) {
            leftDocks[i]->setVisible(!hidden[i]); // déplacer un panneau ne le rouvre pas
        }
    });
    auto* resetLayoutAct = panelsMenu_->addAction(tr("&Réinitialiser la disposition"));
    resetLayoutAct->setObjectName(QStringLiteral("action_resetLayout"));
    resetLayoutAct->setToolTip(tr("Remet les panneaux et les barres à leur place d'origine."));
    connect(resetLayoutAct, &QAction::triggered, this, [this, portableAct] {
        if (!defaultWindowState_.isEmpty()) {
            restoreState(defaultWindowState_);
            const QSignalBlocker blocker(
                portableAct); // la disposition d'origine n'est pas portable
            portableAct->setChecked(false);
        }
    });
    buildMainToolbar();
    addToolBarBreak(); // la barre contextuelle sur sa propre rangée
    buildContextToolbar();
    buildToolPalette();
    // Simulation et bande des fils partagent la rangée du bas (HP-UX-026), visibles seulement
    // quand une séquence existe (cf. refreshThreadStrip). La bande est posée en premier : elle
    // garde la largeur de ses pastilles, la simulation prend le reste (débordement « » sinon).
    buildThreadStrip();
    buildSimulationToolbar();
    // Une barre masquée doit pouvoir se rouvrir depuis un menu, comme les panneaux.
    panelsMenu_->insertSeparator(resetLayoutAct);
    for (auto* bar :
         {mainToolbar_, contextToolbar_, toolPalette_, simToolbar_,
          static_cast<QToolBar*>(simNavBar_), threadStripBar_, clickToStitchBar_, detailBar_}) {
        panelsMenu_->insertAction(resetLayoutAct, bar->toggleViewAction());
    }

    buildHelpMenu();
    applyCompanionSettings();
    buildWorkspacesMenu();

    // Noms accessibles (lecteurs d'écran) sur les grandes zones.
    view_->setAccessibleName(tr("Canevas"));
    view_->setAccessibleDescription(tr("Zone de travail du motif, en millimètres."));
    for (auto* d :
         {documentDock_, propertiesDock_, workflowDock_, orderDock_, filterDock_, analysisDock_}) {
        if (d != nullptr) {
            d->setAccessibleName(d->windowTitle());
        }
    }

    applyAccessibility();

    // Tailles par défaut : le canevas reste prioritaire (les panneaux ne prennent pas la
    // largeur que leur contenu réclamerait), puis capture pour « Réinitialiser la disposition ».
    resizeDocks({documentDock_, propertiesDock_}, {260, 320}, Qt::Horizontal);
    defaultWindowState_ = saveState();

    // Restaure la disposition de l'interface (préférences UI, pas de données
    // métier — celles-ci restent dans le .osp). Les panneaux vides seront
    // masqués ensuite par les refresh.
    restoreUiLayout();

    // Ligne d'indications des gestes (L5) : widget PERMANENT (un widget ajouté par
    // addWidget serait masqué pendant tout showMessage) ; largeur ignorée pour ne
    // jamais imposer sa taille à la fenêtre ; texte élidé.
    hintsLabel_ = new ElidedLabel(this);
    hintsLabel_->setObjectName(QStringLiteral("hintsLabel"));
    hintsLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    hintsLabel_->setMinimumWidth(0);
    hintsLabel_->setAccessibleName(tr("Indications de geste"));
    statusBar()->addPermanentWidget(hintsLabel_, 1);
    toolLabel_ = new ElidedTextLabel(this);
    statusBar()->addPermanentWidget(toolLabel_);
    savedLabel_ = new ElidedTextLabel(this);
    savedLabel_->setObjectName(QStringLiteral("savedLabel"));
    savedLabel_->setAccessibleName(tr("État d'enregistrement"));
    statusBar()->addPermanentWidget(savedLabel_);
    updateSavedIndicator();
    // Un message d'état ne reste jamais affiché indéfiniment : celui d'hier (« Projet
    // enregistré ») ne doit pas survivre aux indications de geste. Les messages qui
    // passent un délai explicite gardent leur délai ; les autres sont effacés après 10 s.
    statusClearTimer_ = new QTimer(this);
    statusClearTimer_->setSingleShot(true);
    statusClearTimer_->setInterval(10000);
    connect(statusClearTimer_, &QTimer::timeout, this, [this] { statusBar()->clearMessage(); });
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString& text) {
        if (text.isEmpty()) {
            statusClearTimer_->stop();
        } else {
            statusClearTimer_->start();
        }
    });
    qApp->installEventFilter(this); // ShortcutOverride, cf. main_window_ux.cpp
    // Étiquette élidable, sans largeur minimale propre (HP-UX-026) : la barre d'état ne fixe plus
    // la largeur minimale de la fenêtre.
    cursorLabel_ = new ElidedTextLabel(this);
    statusBar()->addPermanentWidget(cursorLabel_);
    // Taille du cadre en permanence, cliquable : ouvre « Taille du cadre… » (HP-UX-033).
    hoopStatusButton_ = new QToolButton(this);
    hoopStatusButton_->setObjectName(QStringLiteral("button_hoopStatus"));
    hoopStatusButton_->setAutoRaise(true);
    // Minimum explicite : le texte complet garde sa place quand la fenêtre est large, mais ce
    // bouton ne doit jamais forcer la largeur minimale de la barre d'état (HP-QA-008).
    hoopStatusButton_->setMinimumWidth(60);
    hoopStatusButton_->setToolTip(tr("Taille du cadre de broderie : cliquer pour la modifier."));
    hoopStatusButton_->setAccessibleName(tr("Taille du cadre"));
    connect(hoopStatusButton_, &QToolButton::clicked, this, &MainWindow::setHoopSize);
    statusBar()->addPermanentWidget(hoopStatusButton_);
    applyCanvasToView(); // libellé initial
    // Bascule d'accroche des nœuds visible en permanence (même état que l'entrée du menu
    // Affichage) : sans elle, savoir si le glisser va s'accrocher exige d'ouvrir le menu.
    if (snapNodesAct_ != nullptr) {
        auto* snapButton = new QToolButton(this);
        snapButton->setObjectName(QStringLiteral("button_snapStatus"));
        snapButton->setText(tr("Accroche"));
        snapButton->setCheckable(true);
        snapButton->setAutoRaise(true);
        snapButton->setToolTip(tr("Accroche des nœuds aux sommets des autres objets pendant le "
                                  "glisser (Ctrl : suspendre)."));
        snapButton->setAccessibleName(tr("Accroche des nœuds"));
        snapButton->setChecked(snapNodesAct_->isChecked());
        connect(snapButton, &QToolButton::toggled, snapNodesAct_, &QAction::setChecked);
        connect(snapNodesAct_, &QAction::toggled, snapButton, &QToolButton::setChecked);
        statusBar()->addPermanentWidget(snapButton);
    }
    // Les bascules des modes d'édition changent le contexte d'interaction du canevas.
    for (QAction* act : {stitchEditModeAct_, satinEditModeAct_, satinGuideModeAct_,
                         railEditModeAct_, directionGuideModeAct_, gradientEditModeAct_}) {
        if (act != nullptr) {
            connect(act, &QAction::toggled, this, [this] {
                updateInteractionContext();
                updateShortcutsState();
            });
        }
    }
    if (mergeAct_ != nullptr) {
        connect(mergeAct_, &QAction::toggled, this, [this] { updateShortcutsState(); });
    }
    setTool(Tool::Select);
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF mm) {
        // HP-UX-008 : coordonnées dans l'unité d'affichage choisie (mm ou pouces).
        cursorLabel_->setText(length_units::formatPoint(mm.x(), mm.y(), length_units::current()));
    });
    connect(view_, &CanvasView::cropSelectedMm, this, &MainWindow::onCropSelected);
    connect(view_, &CanvasView::boxDrawnMm, this, &MainWindow::onBoxDrawn);
    connect(view_, &CanvasView::canvasDoubleClickedMm, this, &MainWindow::onCanvasDoubleClicked);
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF physicalMm) {
        // physicalMm est déjà Y-haut (cf. CanvasView::mouseMoveEvent) ; on
        // revient au repère scène (Y-bas) pour les aperçus QGraphicsItem.
        const QPointF sceneMm(physicalMm.x(), -physicalMm.y());
        if (view_->gestureActive() || QGuiApplication::mouseButtons() != Qt::NoButton) {
            hideHoverHighlight(); // pas de surbrillance pendant un glisser / panoramique
        } else {
            scheduleHoverHighlight(sceneMm);
        }
        // Accroche (façon Fusion 360) : seulement pour les outils où le clic
        // pose réellement le point prévisualisé ici -- cf. commentaire de
        // findSnapPointMm (Bézier/rectangle/ellipse exclus).
        const bool snapEligible =
            currentTool_ == Tool::DrawPolygon || currentTool_ == Tool::DrawSatinColumn;
        const std::optional<QPointF> snap =
            snapEligible && !snapSuspended() ? findSnapPointMm(sceneMm) : std::nullopt;
        updateSnapIndicator(snap);
        const QPointF effectiveMm = snap.value_or(sceneMm);
        if (currentTool_ == Tool::DrawPolygon && !pendingPolygonVertices_.empty()) {
            updatePolygonPreview(effectiveMm);
        }
        if (currentTool_ == Tool::DrawSatinColumn && !pendingSatinPoints_.empty()) {
            updateSatinColumnPreview(effectiveMm);
        }
        if (currentTool_ == Tool::DrawBezier && !pendingBezierNodes_.empty()) {
            updateBezierPreview(sceneMm);
        }
    });

    connect(view_, &CanvasView::freeformPointMm, this, &MainWindow::onFreeformPointAdded);
    connect(view_, &CanvasView::freeformStrokeFinished, this, &MainWindow::finishFreeform);
    connect(view_, &CanvasView::bezierPointDraggingMm, this, &MainWindow::onBezierPointDragging);
    connect(view_, &CanvasView::bezierPointCommittedMm, this, &MainWindow::onBezierPointCommitted);

    connect(view_, &CanvasView::canvasClickedMm, this, &MainWindow::onCanvasClicked);
    connect(view_, &CanvasView::selectionClickedMm, this, &MainWindow::onSelectionClicked);
    connect(view_, &CanvasView::nodeRectangleMm, this, &MainWindow::onNodeRectangle);
    connect(view_, &CanvasView::stitchRectangleMm, this, &MainWindow::selectStitchPointsInRect);
    connect(view_, &CanvasView::selectionRectangleMm, this, &MainWindow::onSelectionRectangle);
    connect(view_, &CanvasView::selectBelowRequested, this, &MainWindow::onSelectBelow);
    // Préréglage de navigation GLOBAL : toute fenêtre reflète un changement venu d'ailleurs.
    connect(&InteractionMap::notifier(), &PresetNotifier::presetChanged, this,
            [this](Preset preset) {
                QAction* act = preset == Preset::Touchpad ? navTouchpadAct_ : navOpenStitchAct_;
                if (act != nullptr && !act->isChecked()) {
                    act->setChecked(true);
                }
                refreshHints();
            });
    connect(view_, &CanvasView::cursorLeftViewport, this, [this] { hideHoverHighlight(); });
    connect(view_, &CanvasView::modifiersChanged, this, [this](Qt::KeyboardModifiers mods) {
        heldModifiers_ = mods;
        refreshHints();
    });
    connect(view_, &CanvasView::canvasContextMenu, this, &MainWindow::onCanvasContextMenu);
    // Flèches clavier : déplace l'objet vectoriel sélectionné (mode Sélection
    // uniquement) — même commande que le glisser du corps de forme.
    connect(view_, &CanvasView::nudgeRequestedMm, this, [this](QPointF deltaSceneMm) {
        if (!selectedObject_ || currentTool_ != Tool::Select) {
            return;
        }
        // Multi-sélection : tout l'ensemble bouge, en UN pas d'annulation.
        // Rafale de flèches = un seul pas d'annulation (fenêtre de fusion de la pile).
        translateObjects(selectedObjectIds(), sceneMmToModel(deltaSceneMm), /*coalesce=*/true);
    });
    // Un changement de thème redessine les couches (couleurs des points, repères).
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { displayImage(processed_); });

    // État d'accueil, flottant au centre du canevas tant qu'aucun document.
    emptyState_ = new EmptyStateWidget(view_);
    connect(emptyState_, &EmptyStateWidget::openImageRequested, this, &MainWindow::openImage);
    connect(emptyState_, &EmptyStateWidget::openProjectRequested, this, &MainWindow::loadProject);
    connect(emptyState_, &EmptyStateWidget::importDstRequested, this, &MainWindow::importDst);
    // Même garde HP-FILE-001 que les autres points d'entrée destructeurs de
    // document (newProject) : un clic sur un récent de l'écran d'accueil ne
    // doit pas perdre un travail en cours sans confirmation.
    connect(emptyState_, &EmptyStateWidget::openRecentRequested, this,
            [this](const QString& path) { openRecentFile(path); });
    refreshRecentFilesUi();
    connect(view_, &CanvasView::viewChanged, this, &MainWindow::positionEmptyState);

    statusBar()->showMessage(tr("Ouvrez une image (PNG, JPEG, BMP, TIFF) pour commencer."));
    view_->fitCanvas();

    // HP-FILE-004 : autosave périodique, démarré dès l'ouverture (protège
    // dès avant la première modification, pas seulement après).
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setInterval(loadGeneralPreferences().autosaveMinutes * 60'000); // HP-UX-007
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::onAutosaveTick);
    autosaveTimer_->start();
    // Différé après le premier passage de la boucle d'évènements (donc après
    // window.show() dans main.cpp) pour que le dialogue de récupération
    // s'affiche au-dessus d'une fenêtre déjà visible.
    QTimer::singleShot(0, this, &MainWindow::checkAutosaveRecovery);

    updateActions();
}

MainWindow::~MainWindow() {
    // Un calcul d'aperçu de segmentation en cours travaille sur une copie de l'image, mais son
    // résultat est livré à cette fenêtre : on le laisse finir avant de la détruire.
    if (segmentThread_ != nullptr) {
        segmentThread_->wait();
    }
    // Les actions et la scène sont des enfants QObject détruits après la
    // partie MainWindow. Aucune de leurs notifications tardives ne doit
    // rappeler un slot du type dérivé dont le destructeur a déjà commencé.
    for (QObject* child : findChildren<QObject*>()) {
        QObject::disconnect(child, nullptr, this, nullptr);
    }
    // Rend le créneau à la récupération (sans le supprimer) : une fenêtre qui
    // disparaît sans passer par closeEvent ressemble à un arrêt anormal.
    flushAutosave();
    releaseAutosaveSlot(slotFor(currentProjectPath_));
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&Fichier"));
    newProjectAct_ = fileMenu->addAction(tr("&Nouveau projet"));
    auto* newAct = newProjectAct_;
    newAct->setObjectName(QStringLiteral("action_newProject"));
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &MainWindow::newProject);
    fileMenu->addSeparator();
    // Ctrl+O ouvre tout (projet, fichier machine, SVG, image) en routant sur l'extension, comme
    // le glisser-déposer (HP-UX-017) ; l'import d'image garde son entrée et un autre raccourci.
    openAnyAct_ = fileMenu->addAction(tr("&Ouvrir…"));
    openAnyAct_->setObjectName(QStringLiteral("action_openAny"));
    openAnyAct_->setShortcut(QKeySequence::Open);
    connect(openAnyAct_, &QAction::triggered, this, &MainWindow::openAny);
    openImageAct_ = fileMenu->addAction(tr("Im&porter une image ou un SVG…"));
    auto* openAct = openImageAct_;
    openAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    connect(openAct, &QAction::triggered, this, &MainWindow::openImage);
    fileMenu->addSeparator();
    // Pas de « … » sur Enregistrer : il n'ouvre un dialogue que pour un
    // document encore sans fichier (HP-FILE-002).
    saveProjectAct_ = fileMenu->addAction(tr("Enre&gistrer le projet"));
    auto* saveProjectAct = saveProjectAct_;
    saveProjectAct->setObjectName(QStringLiteral("action_saveProject"));
    saveProjectAct->setShortcut(QKeySequence::Save);
    connect(saveProjectAct, &QAction::triggered, this, &MainWindow::saveProject);
    saveProjectAsAct_ = fileMenu->addAction(tr("Enregistrer le projet &sous…"));
    auto* saveProjectAsAct = saveProjectAsAct_;
    saveProjectAsAct->setObjectName(QStringLiteral("action_saveProjectAs"));
    saveProjectAsAct->setShortcut(QKeySequence::SaveAs);
    connect(saveProjectAsAct, &QAction::triggered, this, &MainWindow::saveProjectAs);
    loadProjectAct_ = fileMenu->addAction(tr("Ou&vrir un projet…"));
    auto* loadProjectAct = loadProjectAct_;
    connect(loadProjectAct, &QAction::triggered, this, &MainWindow::loadProject);
    fileMenu->addSeparator();
    // Rempli par refreshRecentFilesUi() (appelée une première fois depuis le
    // constructeur, après la construction de emptyState_) -- HP-FILE-003.
    recentMenu_ = fileMenu->addMenu(tr("&Récents"));
    // Purge des fichiers disparus à l'affichage du menu seulement (pas à chaque
    // enregistrement : QFileInfo::exists bloque sur un lecteur réseau).
    connect(recentMenu_, &QMenu::aboutToShow, this, [this] {
        recentFiles_ = pruneMissingRecentFiles(loadRecentFiles());
        saveRecentFiles(recentFiles_);
        rebuildRecentUi();
    });
    // « Vider la liste » vit dans le menu Fichier et non dans le sous-menu : le sous-menu
    // ne contient que les fichiers (un test et refreshRecentFilesUi() comptent ses actions).
    clearRecentAct_ = fileMenu->addAction(tr("Vider &la liste des récents"));
    clearRecentAct_->setObjectName(QStringLiteral("action_clearRecent"));
    connect(clearRecentAct_, &QAction::triggered, this, [this] {
        saveRecentFiles(QStringList());
        refreshRecentFilesUi();
    });
    exportDstAct_ = fileMenu->addAction(tr("&Exporter une broderie machine…"));
    exportDstAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(exportDstAct_, &QAction::triggered, this, &MainWindow::exportDst);
    exportDstQuickAct_ = fileMenu->addAction(tr("Exporter &avec les derniers réglages"));
    exportDstQuickAct_->setObjectName(QStringLiteral("action_exportQuick"));
    exportDstQuickAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    exportDstQuickAct_->setToolTip(
        tr("Exporte tout de suite avec le format et les options du dernier export, sans "
           "résumé : seul le nom du fichier est demandé."));
    connect(exportDstQuickAct_, &QAction::triggered, this, &MainWindow::exportDstQuick);
    buildProductionMenu(fileMenu);
    auto* importDstAct = fileMenu->addAction(tr("&Importer une broderie machine…"));
    connect(importDstAct, &QAction::triggered, this, &MainWindow::importDst);
    fileMenu->addSeparator();
    insertDesignAct_ = fileMenu->addAction(tr("Insérer un design…"));
    insertDesignAct_->setObjectName(QStringLiteral("action_insertDesign"));
    insertDesignAct_->setToolTip(tr("Ajoute le contenu d'un projet .osp ou d'une broderie machine "
                                    "au projet ouvert (annulable en un pas)."));
    connect(insertDesignAct_, &QAction::triggered, this, &MainWindow::insertDesign);
    auto* importDxfAct = fileMenu->addAction(tr("Importer un &DXF…"));
    connect(importDxfAct, &QAction::triggered, this, &MainWindow::importDxf);
    exportDxfAct_ = fileMenu->addAction(tr("Exporter en D&XF…"));
    auto* exportDxfAct = exportDxfAct_;
    connect(exportDxfAct, &QAction::triggered, this, &MainWindow::exportDxf);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Quitter"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Édition"));
    auto* searchAction = editMenu->addAction(tr("Rec&hercher une commande…"));
    searchAction->setObjectName(QStringLiteral("action_commandPalette"));
    searchAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    searchAction->setStatusTip(tr("Rechercher une commande des menus."));
    connect(searchAction, &QAction::triggered, this, [this] {
        if (auto* existing = findChild<QDialog*>(QStringLiteral("commandPaletteDialog"))) {
            existing->raise();
            existing->activateWindow();
            return;
        }
        auto* palette = new CommandPaletteDialog(menuBar(), this);
        palette->setAttribute(Qt::WA_DeleteOnClose);
        palette->show();
        palette->raise();
        palette->activateWindow();
    });
    editMenu->addSeparator();
    undoAct_ = editMenu->addAction(tr("&Annuler"));
    undoAct_->setObjectName(QStringLiteral("action_undo"));
    undoAct_->setShortcut(QKeySequence::Undo);
    connect(undoAct_, &QAction::triggered, this, &MainWindow::undo);
    redoAct_ = editMenu->addAction(tr("&Rétablir"));
    redoAct_->setObjectName(QStringLiteral("action_redo"));
    // Pluriel : Ctrl+Y ET Ctrl+Maj+Z (le geste des logiciels graphiques).
    redoAct_->setShortcuts(QKeySequence::Redo);
    connect(redoAct_, &QAction::triggered, this, &MainWindow::redo);
    editMenu->addSeparator();
    duplicateSelectionAct_ = editMenu->addAction(tr("&Dupliquer la forme"));
    duplicateSelectionAct_->setObjectName(QStringLiteral("action_duplicateSelection"));
    connect(duplicateSelectionAct_, &QAction::triggered, this, [this] {
        if (selectedObject_ && !hasMultiSelection()) {
            duplicateVectorObject(*selectedObject_);
        }
    });
    offsetSelectionAct_ = editMenu->addAction(tr("Dé&caler la forme…"));
    offsetSelectionAct_->setObjectName(QStringLiteral("action_offsetSelection"));
    connect(offsetSelectionAct_, &QAction::triggered, this, [this] {
        if (selectedObject_ && !hasMultiSelection()) {
            offsetVectorObject(*selectedObject_);
        }
    });
    buildAlignMenu(editMenu);
    buildArrangeMenu(editMenu);
    buildObjectEditActions(editMenu);
    editMenu->addSeparator();
    auto* aiPrefsAct = editMenu->addAction(tr("Préférences — &Intelligence artificielle…"));
    connect(aiPrefsAct, &QAction::triggered, this, &MainWindow::openAiPreferences);
    auto* generalPrefsAct = editMenu->addAction(tr("Préférences générales…"));
    generalPrefsAct->setObjectName(QStringLiteral("action_generalPreferences"));
    connect(generalPrefsAct, &QAction::triggered, this, &MainWindow::openGeneralPreferences);
    auto* settingsAct = editMenu->addAction(tr("Paramètres…"));
    settingsAct->setObjectName(QStringLiteral("action_settings"));
    settingsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);

    auto* imageMenu = menuBar()->addMenu(tr("&Image"));
    const auto addOpAction = [this, imageMenu](const QString& text, image::ImageOp op) {
        auto* act = imageMenu->addAction(text);
        connect(act, &QAction::triggered, this, [this, op] { executeOp(op); });
        imageActions_.append(act);
        return act;
    };

    addOpAction(tr("Niveaux de &gris"), image::GrayscaleOp{});

    auto* bcAct = imageMenu->addAction(tr("&Luminosité/contraste…"));
    connect(bcAct, &QAction::triggered, this, &MainWindow::adjustBrightnessContrast);
    imageActions_.append(bcAct);

    addOpAction(tr("Dé&bruitage léger"), image::MedianDenoiseOp{1});
    addOpAction(tr("Débruitage &moyen"), image::MedianDenoiseOp{2});
    addOpAction(tr("Lissage bilatéral (conserve les &contours)"), image::BilateralDenoiseOp{2});
    addOpAction(tr("Aplat des &zones (illustration)"), image::FlattenOp{2});
    addOpAction(tr("Contra&ste local (CLAHE)"), image::ClaheOp{2.0});
    addOpAction(tr("&Netteté"), image::SharpenOp{1.0});
    addOpAction(tr("Lignes cohérentes (photo → &dessin au trait)"), image::CoherentLinesOp{});

    auto* quantAct = imageMenu->addAction(tr("&Quantifier les couleurs…"));
    connect(quantAct, &QAction::triggered, this, &MainWindow::quantizeColors);
    imageActions_.append(quantAct);

    imageMenu->addSeparator();
    addOpAction(tr("Symétrie &horizontale"), image::FlipOp{true});
    addOpAction(tr("Symétrie &verticale"), image::FlipOp{false});
    addOpAction(tr("Rotation 90° h&oraire"), image::Rotate90Op{1});
    addOpAction(tr("Rotation 90° &antihoraire"), image::Rotate90Op{3});

    imageMenu->addSeparator();
    cropAct_ = imageMenu->addAction(tr("&Recadrer l'image (glisser un cadre)"));
    cropAct_->setCheckable(true);
    connect(cropAct_, &QAction::toggled, this,
            [this](bool on) { setTool(on ? Tool::Rect : Tool::Select); });
    imageActions_.append(cropAct_);

    auto* segMenu = menuBar()->addMenu(tr("&Segmentation"));
    segmentAct_ = segMenu->addAction(tr("&Segmenter l'image…"));
    auto* segAct = segmentAct_;
    segmentAct_->setShortcut(QKeySequence(Qt::Key_F6));
    connect(segAct, &QAction::triggered, this, &MainWindow::segmentImage);
    imageActions_.append(segAct);

    showSegAct_ = segMenu->addAction(tr("&Afficher la carte des régions"));
    showSegAct_->setCheckable(true);
    showSegAct_->setShortcut(QKeySequence(Qt::Key_W));
    showSegAct_->setToolTip(tr("Affiche ou masque la carte des régions par-dessus l'image (W)."));
    connect(showSegAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    buildRegionViewControls(segMenu);
    segmentMenu_ = segMenu;

    mergeColorsAct_ = segMenu->addAction(tr("Fusionner les co&uleurs…"));
    mergeColorsAct_->setObjectName(QStringLiteral("action_mergeColors"));
    mergeColorsAct_->setToolTip(
        tr("Panneau des couleurs de la segmentation : fusionne des teintes entre elles, ou réduit "
           "la palette à N couleurs avec aperçu."));
    connect(mergeColorsAct_, &QAction::triggered, this, &MainWindow::showColorsPanel);
    segMenu->addSeparator();
    mergeAct_ = segMenu->addAction(tr("&Fusionner avec… (cliquer la région cible)"));
    mergeAct_->setCheckable(true);
    mergeAct_->setToolTip(
        tr("Fusionne la région (ou toutes les régions sélectionnées) dans la région que vous "
           "cliquez ensuite ; elle garde sa couleur."));
    connect(mergeAct_, &QAction::toggled, this, [this](bool on) { setMergeMode(on); });
    regionActions_.append(mergeAct_);
    mergeSelectionAct_ = segMenu->addAction(tr("Fusionner la sélecti&on"));
    mergeSelectionAct_->setObjectName(QStringLiteral("action_mergeSelection"));
    // J (« joindre ») : la touche seule, une main restant sur la souris.
    mergeSelectionAct_->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_M), QKeySequence(Qt::Key_J)});
    mergeSelectionAct_->setToolTip(tr("Fusionne toutes les régions sélectionnées dans la dernière "
                                      "cliquée (Ctrl+M ou J)."));
    connect(mergeSelectionAct_, &QAction::triggered, this, &MainWindow::mergeSelectedRegions);
    absorbAct_ = segMenu->addAction(tr("Fusionner dans la voisine &principale"));
    absorbAct_->setObjectName(QStringLiteral("action_absorbIntoNeighbour"));
    // A (« absorber »).
    absorbAct_->setShortcuts(
        {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), QKeySequence(Qt::Key_A)});
    absorbAct_->setToolTip(
        tr("Fusionne la région dans la voisine avec laquelle elle partage la plus longue "
           "frontière (Ctrl+Maj+M ou A)."));
    connect(absorbAct_, &QAction::triggered, this, &MainWindow::absorbSelectedRegionIntoNeighbour);
    cleanIslandsAct_ = segMenu->addAction(tr("Nettoyer les petits &îlots…"));
    cleanIslandsAct_->setObjectName(QStringLiteral("action_cleanIslands"));
    cleanIslandsAct_->setToolTip(
        tr("Sur toute l'image : fusionne chaque petite région dans la voisine dont la couleur est "
           "la plus proche (comme « boucher les trous »)."));
    connect(cleanIslandsAct_, &QAction::triggered, this, &MainWindow::cleanIslands);
    segMenu->addSeparator();
    selectSameColorAct_ = segMenu->addAction(tr("Sélectionner la &même couleur"));
    selectSameColorAct_->setObjectName(QStringLiteral("action_selectSameColor"));
    selectSameColorAct_->setShortcut(QKeySequence(Qt::Key_C));
    selectSameColorAct_->setToolTip(
        tr("Sélectionne toutes les régions qui ont la couleur de la région active (C)."));
    connect(selectSameColorAct_, &QAction::triggered, this,
            &MainWindow::selectRegionsWithSameColor);
    regionActions_.append(selectSameColorAct_);
    selectNeighboursAct_ = segMenu->addAction(tr("Sélectionner les voisi&nes"));
    selectNeighboursAct_->setObjectName(QStringLiteral("action_selectNeighbours"));
    selectNeighboursAct_->setShortcut(QKeySequence(Qt::Key_N));
    selectNeighboursAct_->setToolTip(
        tr("Ajoute à la sélection les régions voisines de la sélection (N)."));
    connect(selectNeighboursAct_, &QAction::triggered, this, &MainWindow::selectNeighbourRegions);
    regionActions_.append(selectNeighboursAct_);
    selectAllRegionsAct_ = segMenu->addAction(tr("&Tout sélectionner"));
    selectAllRegionsAct_->setObjectName(QStringLiteral("action_selectAllRegions"));
    selectAllRegionsAct_->setShortcut(QKeySequence::SelectAll);
    connect(selectAllRegionsAct_, &QAction::triggered, this, &MainWindow::selectAllInActiveLayer);
    // Parcours des régions au clavier (ordre des identifiants, avec retour au début). Ni Tab (il
    // sert à la navigation entre les panneaux) ni Entrée (réservée à la validation d'un champ et
    // à la fin d'un tracé) : Alt + flèches gauche / droite.
    nextRegionAct_ = segMenu->addAction(tr("Région suivante"));
    nextRegionAct_->setObjectName(QStringLiteral("action_nextRegion"));
    nextRegionAct_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    nextRegionAct_->setToolTip(tr("Sélectionne la région suivante (Alt+→)."));
    connect(nextRegionAct_, &QAction::triggered, this, [this] { selectAdjacentRegion(+1); });
    previousRegionAct_ = segMenu->addAction(tr("Région précédente"));
    previousRegionAct_->setObjectName(QStringLiteral("action_previousRegion"));
    previousRegionAct_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    previousRegionAct_->setToolTip(tr("Sélectionne la région précédente (Alt+←)."));
    connect(previousRegionAct_, &QAction::triggered, this, [this] { selectAdjacentRegion(-1); });

    // Suppr universel (L5-T4a) : l'objectName historique « action_deleteRegion »
    // est conservé (tests, snapshot d'actions) ; l'action est activée par
    // updateActions() selon la sélection (pas via regionActions_).
    deleteSelectionAct_ = editMenu->addAction(tr("Su&pprimer la sélection"));
    // Dans Édition, juste avant Dupliquer/Décaler (et non en fin de menu).
    editMenu->insertAction(duplicateSelectionAct_, deleteSelectionAct_);
    deleteSelectionAct_->setObjectName(QStringLiteral("action_deleteRegion"));
    deleteSelectionAct_->setShortcut(QKeySequence::Delete);
    connect(deleteSelectionAct_, &QAction::triggered, this, &MainWindow::deleteSelection);

    auto* recolorAct = segMenu->addAction(tr("&Recolorer la sélection…"));
    recolorAct->setObjectName(QStringLiteral("action_recolorRegions"));
    connect(recolorAct, &QAction::triggered, this, &MainWindow::recolorSelectedRegion);
    regionActions_.append(recolorAct);
    restoreColorAct_ = segMenu->addAction(tr("Rétablir la &couleur d'origine"));
    restoreColorAct_->setObjectName(QStringLiteral("action_restoreRegionColors"));
    restoreColorAct_->setToolTip(
        tr("Rend à chaque région sélectionnée la couleur moyenne de l'image segmentée."));
    connect(restoreColorAct_, &QAction::triggered, this, &MainWindow::restoreSelectedRegionColors);
    regionActions_.append(restoreColorAct_);

    segMenu->addSeparator();
    vectorizeRegionAct_ = segMenu->addAction(tr("&Vectoriser la sélection"));
    vectorizeRegionAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F7));
    vectorizeRegionAct_->setToolTip(
        tr("Convertit la ou les régions sélectionnées en objets vectoriels éditables "
           "(un seul pas d'annulation). Maj+F7 règle le détail ; F7 réutilise le dernier."));
    auto* vectorizeAct = vectorizeRegionAct_;
    connect(vectorizeAct, &QAction::triggered, this, [this] { vectorizeSelectedRegion(true); });
    // F7 : même action sans dialogue, avec le dernier détail (HP-UX-021).
    auto* quickVectorize = new QShortcut(QKeySequence(Qt::Key_F7), this);
    connect(quickVectorize, &QShortcut::activated, this, [this] {
        if (vectorizeRegionAct_->isEnabled()) {
            vectorizeSelectedRegion(false);
        }
    });
    regionActions_.append(vectorizeAct);

    buildShapeMenu();
    buildTextMenu();

    auto* embMenu = menuBar()->addMenu(tr("&Broderie"));
    autoDigitizeAct_ = embMenu->addAction(tr("Numérisation &automatique"));
    autoDigitizeAct_->setObjectName(QStringLiteral("action_autoDigitize"));
    autoDigitizeAct_->setShortcut(QKeySequence(Qt::Key_F8));
    auto* autoAct = autoDigitizeAct_;
    connect(autoAct, &QAction::triggered, this, &MainWindow::autoDigitize);
    convertVectorsAct_ = embMenu->addAction(tr("&Convertir les vecteurs en broderie"));
    convertVectorsAct_->setObjectName(QStringLiteral("action_convertVectors"));
    convertVectorsAct_->setToolTip(
        tr("Choisit automatiquement tatami ou contour pour chaque forme vectorielle sans broderie "
           "(SVG importé), sans passer par l'image."));
    connect(convertVectorsAct_, &QAction::triggered, this, &MainWindow::convertVectorsToEmbroidery);
    aiSegmentAct_ = segMenu->addAction(icons::aiSegment(), tr("Segmenter avec l'&IA…"));
    aiSegmentAct_->setObjectName(QStringLiteral("action_segmentWithAi"));
    aiSegmentAct_->setToolTip(
        tr("Détecte des formes avec SAM 2 : crée des régions éditables (à fusionner, recolorer "
           "puis vectoriser) ou directement les objets de broderie, au choix."));
    connect(aiSegmentAct_, &QAction::triggered, this, &MainWindow::segmentWithAi);
    embMenu->addSeparator();
    // HP-UX-002 : création immédiate au dernier réglage (pas de « … » : aucun dialogue) ; le
    // sous-menu « Créer avec réglages » garde la saisie des valeurs avant création.
    createStitchAct_ = embMenu->addAction(tr("Créer un objet de &point de contour"));
    createStitchAct_->setObjectName(QStringLiteral("action_createStitch"));
    connect(createStitchAct_, &QAction::triggered, this, &MainWindow::createRunningStitchObject);
    createTatamiAct_ = embMenu->addAction(tr("Créer un remplissage &tatami"));
    createTatamiAct_->setObjectName(QStringLiteral("action_createTatami"));
    connect(createTatamiAct_, &QAction::triggered, this, &MainWindow::createTatamiObject);
    createSatinAct_ = embMenu->addAction(tr("Créer un &satin automatique"));
    createSatinAct_->setObjectName(QStringLiteral("action_createSatin"));
    connect(createSatinAct_, &QAction::triggered, this, &MainWindow::createSatinObject);
    auto* createWithSettingsMenu = embMenu->addMenu(tr("Créer avec &réglages"));
    createStitchWithSettingsAct_ = createWithSettingsMenu->addAction(tr("Point de contour…"));
    createStitchWithSettingsAct_->setObjectName(QStringLiteral("action_createStitchWithSettings"));
    connect(createStitchWithSettingsAct_, &QAction::triggered, this,
            &MainWindow::createRunningStitchObjectWithSettings);
    createTatamiWithSettingsAct_ = createWithSettingsMenu->addAction(tr("Remplissage tatami…"));
    createTatamiWithSettingsAct_->setObjectName(QStringLiteral("action_createTatamiWithSettings"));
    connect(createTatamiWithSettingsAct_, &QAction::triggered, this,
            &MainWindow::createTatamiObjectWithSettings);
    createSatinWithSettingsAct_ = createWithSettingsMenu->addAction(tr("Satin automatique…"));
    createSatinWithSettingsAct_->setObjectName(QStringLiteral("action_createSatinWithSettings"));
    connect(createSatinWithSettingsAct_, &QAction::triggered, this,
            &MainWindow::createSatinObjectWithSettings);
    buildStitchPresetMenu(embMenu); // HP-ENG-013
    autoSatinAct_ = embMenu->addAction(tr("Convertir en satin automatiquement…"));
    autoSatinAct_->setToolTip(
        tr("Le logiciel dessine le satin tout seul, sans que vous posiez les deux bords. "
           "Vérifiez le résultat avant de broder."));
    connect(autoSatinAct_, &QAction::triggered, this, &MainWindow::autoConvertToSatin);
    embMenu->addSeparator();
    fillAngleAct_ = embMenu->addAction(tr("&Orientation du remplissage…"));
    fillAngleAct_->setToolTip(
        tr("Change l'angle des fils du remplissage tatami sélectionné (clic sur la forme "
           "ou dans l'ordre de couture)."));
    connect(fillAngleAct_, &QAction::triggered, this, &MainWindow::changeFillAngle);
    convertSatinAct_ = embMenu->addAction(tr("Con&vertir les satins auto en tatami"));
    convertSatinAct_->setToolTip(
        tr("Remplace les colonnes satin automatiques (qui débordent sur les formes "
           "concaves) par des remplissages tatami découpés sur la région."));
    connect(convertSatinAct_, &QAction::triggered, this, &MainWindow::convertSatinsToTatami);
    // Finitions de la séquence du projet (Lots E/F) : coupes automatiques,
    // points d'arrêt, points courts -- réglage de projet, annulable.
    auto* finishingAct = embMenu->addAction(tr("Options de &génération…"));
    finishingAct->setObjectName(QStringLiteral("action_generationOptions"));
    connect(finishingAct, &QAction::triggered, this, [this] {
        const auto edited = editSequenceFinishing(this, project_.finishing);
        if (edited && *edited != project_.finishing) {
            undoStack_.execute(std::make_unique<commands::SetFinishingCommand>(*edited), project_);
            refreshImage();
            updateActions();
        }
    });
    embMenu->addSeparator();
    // Mode d'édition des points générés (Lot 8.2). Mode exclusif : activable
    // seulement dans un contexte valide (objet de broderie sélectionné, non
    // Dirty -- cf. updateActions), désactivé/décoché sinon.
    stitchEditModeAct_ = embMenu->addAction(icons::editPoints(), tr("&Éditer les points…"));
    stitchEditModeAct_->setObjectName(QStringLiteral("action_stitchEditMode"));
    stitchEditModeAct_->setCheckable(true);
    stitchEditModeAct_->setShortcut(QKeySequence(Qt::Key_E));
    stitchEditModeAct_->setToolTip(
        tr("Déplacer un à un les points de couture générés de l'objet sélectionné (E). "
           "Distinct de l'édition des nœuds vectoriels : ici, seul le point cousu bouge, "
           "pas la forme source."));
    connect(stitchEditModeAct_, &QAction::toggled, this, &MainWindow::onStitchEditModeToggled);
    buildStitchPointEditActions(embMenu); // sélection, suppression, saut, coupe (HP-EDIT-001..003)
    // Mode unique de remodelage satin : active À LA FOIS les nœuds de rail et
    // les guides transversaux, pour ne plus avoir à choisir entre deux modes
    // séparés dont la différence n'était pas claire à l'usage (défaut
    // remonté en revue). Les deux mécanismes internes restent séparés
    // (satinGuideModeAct_/railEditModeAct_ ci-dessous, conservés comme
    // actions « avancées » pour qui veut n'afficher que l'un des deux) —
    // cette action se contente de les activer ensemble.
    satinEditModeAct_ = embMenu->addAction(tr("&Modifier la colonne satin (rails + guides)…"));
    satinEditModeAct_->setObjectName(QStringLiteral("action_satinEditMode"));
    satinEditModeAct_->setCheckable(true);
    satinEditModeAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_E));
    satinEditModeAct_->setToolTip(
        tr("Affiche ensemble les nœuds des deux rails (glisser pour déplacer, double-clic "
           "pour ajouter un nœud) et les guides transversaux (glisser une extrémité) de la "
           "colonne satin sélectionnée (Maj+E)."));
    connect(satinEditModeAct_, &QAction::toggled, this, [this](bool on) {
        satinGuideModeAct_->setChecked(on);
        railEditModeAct_->setChecked(on);
    });
    auto* satinAdvancedMenu = embMenu->addMenu(tr("Réglages fins du satin"));
    satinAdvancedMenu->setToolTip(
        tr("Ne montre que les bords ou que les repères d'orientation — utile pour une "
           "colonne dense où les deux ensemble surchargeraient l'affichage."));
    satinGuideModeAct_ =
        satinAdvancedMenu->addAction(tr("Éditer seulement les repères d'orientation du satin…"));
    satinGuideModeAct_->setObjectName(QStringLiteral("action_satinGuideMode"));
    satinGuideModeAct_->setCheckable(true);
    satinGuideModeAct_->setToolTip(
        tr("Afficher les guides transversaux du satin et glisser leurs extrémités le long "
           "des rails. Plusieurs guides peuvent orienter une même colonne."));
    connect(satinGuideModeAct_, &QAction::toggled, this, &MainWindow::onSatinGuideModeToggled);
    addSatinGuideAct_ = satinAdvancedMenu->addAction(tr("Ajouter un guide satin"));
    addSatinGuideAct_->setObjectName(QStringLiteral("action_addSatinGuide"));
    addSatinGuideAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_G));
    connect(addSatinGuideAct_, &QAction::triggered, this, &MainWindow::addSatinGuide);
    removeSatinGuideAct_ = satinAdvancedMenu->addAction(tr("Supprimer le guide satin sélectionné"));
    removeSatinGuideAct_->setObjectName(QStringLiteral("action_removeSatinGuide"));
    connect(removeSatinGuideAct_, &QAction::triggered, this, &MainWindow::removeSelectedSatinGuide);
    railEditModeAct_ = satinAdvancedMenu->addAction(tr("Éditer seulement les bords du satin…"));
    railEditModeAct_->setObjectName(QStringLiteral("action_satinRailEditMode"));
    railEditModeAct_->setCheckable(true);
    railEditModeAct_->setToolTip(
        tr("Afficher les nœuds des deux rails de la colonne satin sélectionnée et les "
           "glisser pour remodeler la colonne. Double-clic sur un rail : ajoute un nœud "
           "(forme exactement conservée)."));
    connect(railEditModeAct_, &QAction::toggled, this, &MainWindow::onSatinRailEditModeToggled);
    embMenu->addSeparator();
    buildDirectionalActions(embMenu);
    embMenu->addSeparator();
    statsAct_ = new QAction(tr("Statisti&ques…"), this);
    statsAct_->setShortcut(QKeySequence(Qt::Key_F9));
    connect(statsAct_, &QAction::triggered, this, &MainWindow::showStatistics);

    auto* viewMenu = menuBar()->addMenu(tr("&Affichage"));
    // « Plans d'affichage » (et non « Calques ») : le mot « couches » est réservé aux couches de
    // couture d'une forme (HP-OBJ-023) ; ce menu choisit seulement ce que le canevas montre.
    auto* layersMenu = viewMenu->addMenu(tr("Plans d'affic&hage"));
    showImageAct_ = layersMenu->addAction(tr("&Image"));
    showImageAct_->setCheckable(true);
    showImageAct_->setChecked(true);
    showImageAct_->setShortcut(QKeySequence(Qt::Key_1));
    connect(showImageAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    // La carte des régions (segmentation) : action partagée avec le menu Segmentation.
    showSegAct_->setShortcuts({QKeySequence(Qt::Key_W), QKeySequence(Qt::Key_2)});
    layersMenu->addAction(showSegAct_);
    showVectorsAct_ = layersMenu->addAction(tr("&Vecteurs"));
    showVectorsAct_->setCheckable(true);
    showVectorsAct_->setChecked(true);
    showVectorsAct_->setShortcut(QKeySequence(Qt::Key_3));
    connect(showVectorsAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    showStitchesAct_ = layersMenu->addAction(tr("&Broderie (points)"));
    showStitchesAct_->setCheckable(true);
    showStitchesAct_->setChecked(true);
    showStitchesAct_->setShortcut(QKeySequence(Qt::Key_4));
    connect(showStitchesAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    buildRealisticMenu(viewMenu);
    buildColorwayMenu(viewMenu); // HP-THR-006/008/011
    buildImageOpacityControls(viewMenu);
    viewMenu->addSeparator();
    zoomInAct_ = viewMenu->addAction(tr("Zoom &avant"));
    auto* zoomInAct = zoomInAct_;
    // Ctrl++ demande Maj sur un clavier AZERTY : Ctrl+= (sans Maj) en plus.
    zoomInAct->setShortcuts(
        {QKeySequence(QKeySequence::ZoomIn), QKeySequence(Qt::CTRL | Qt::Key_Equal)});
    connect(zoomInAct, &QAction::triggered, view_, &CanvasView::zoomIn);
    zoomOutAct_ = viewMenu->addAction(tr("Zoom a&rrière"));
    auto* zoomOutAct = zoomOutAct_;
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, view_, &CanvasView::zoomOut);
    fitCanvasAct_ = viewMenu->addAction(tr("A&juster au canevas"));
    auto* fitAct = fitCanvasAct_;
    // Ctrl+0 et F sur la MÊME action : les deux sont alors affichés dans le menu.
    fitAct->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_0), QKeySequence(Qt::Key_F)});
    connect(fitAct, &QAction::triggered, view_, &CanvasView::fitCanvas);
    zoomSelectionAct_ = viewMenu->addAction(tr("Zoom sur la &sélection"));
    zoomSelectionAct_->setObjectName(QStringLiteral("action_zoomSelection"));
    zoomSelectionAct_->setShortcut(QKeySequence(Qt::Key_Z));
    connect(zoomSelectionAct_, &QAction::triggered, this, &MainWindow::zoomToSelection);
    zoomPreviousAct_ = viewMenu->addAction(tr("Zoom précéd&ent"));
    zoomPreviousAct_->setObjectName(QStringLiteral("action_zoomPrevious"));
    zoomPreviousAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Z));
    connect(zoomPreviousAct_, &QAction::triggered, view_, &CanvasView::zoomToPreviousView);
    zoomActualAct_ = viewMenu->addAction(tr("&100 % (taille réelle)"));
    zoomActualAct_->setObjectName(QStringLiteral("action_zoomActual"));
    zoomActualAct_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(zoomActualAct_, &QAction::triggered, view_, &CanvasView::zoomActualSize);
    buildViewDisplayMenu(viewMenu); // HP-VIEW-003/007/010/012

    viewMenu->addSeparator();
    auto* hoopAct = viewMenu->addAction(tr("Tai&lle du cadre…"));
    hoopAct->setToolTip(tr("Définit la zone physique de broderie (cadre)."));
    connect(hoopAct, &QAction::triggered, this, &MainWindow::setHoopSize);
    machineAct_ = viewMenu->addAction(tr("Machine…"));
    machineAct_->setObjectName(QStringLiteral("action_machine"));
    machineAct_->setToolTip(tr("Choisit le profil de la machine à broder : champ, cadres, "
                               "aiguilles, formats (vérifications avant export)."));
    connect(machineAct_, &QAction::triggered, this, &MainWindow::chooseMachine);

    viewMenu->addSeparator();
    auto* themeMenu = viewMenu->addMenu(tr("&Thème"));
    auto* themeGroup = new QActionGroup(this);
    const auto addThemeAct = [&](const QString& label, ThemeMode m) {
        auto* act = themeMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(AppTheme::instance().mode() == m);
        themeGroup->addAction(act);
        connect(act, &QAction::triggered, this, [m] { AppTheme::instance().setMode(m); });
    };
    addThemeAct(tr("Clair"), ThemeMode::Light);
    addThemeAct(tr("Sombre"), ThemeMode::Dark);

    auto* densityMenu = viewMenu->addMenu(tr("&Densité"));
    auto* densityGroup = new QActionGroup(this);
    const auto addDensityAct = [&](const QString& label, Density d) {
        auto* act = densityMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(AppTheme::instance().density() == d);
        densityGroup->addAction(act);
        connect(act, &QAction::triggered, this, [d] { AppTheme::instance().setDensity(d); });
    };
    addDensityAct(tr("Confortable"), Density::Comfortable);
    addDensityAct(tr("Compact"), Density::Compact);
    buildNavigationMenu(viewMenu);
    snapNodesAct_ = viewMenu->addAction(tr("Accrochage des nœuds au glisser"));
    snapNodesAct_->setObjectName(QStringLiteral("action_snapNodesOnDrag"));
    snapNodesAct_->setCheckable(true);
    snapNodesAct_->setChecked(
        QSettings().value(QStringLiteral("edit/snapNodesOnDrag"), false).toBool());
    snapNodesAct_->setToolTip(tr("Accroche un nœud glissé aux sommets des autres objets "
                                 "(Ctrl pendant le glisser : sans accroche ; Maj verrouille l'axe "
                                 "et garde l'accroche). Désactivé par défaut."));
    snapNodesAct_->setStatusTip(snapNodesAct_->toolTip());
    connect(snapNodesAct_, &QAction::toggled, this,
            [](bool on) { QSettings().setValue(QStringLiteral("edit/snapNodesOnDrag"), on); });

    viewMenu->addSeparator();
    // Menu des panneaux : rempli dans le constructeur une fois les docks construits
    // (buildMenus s'exécute avant eux). Permet de rouvrir un panneau fermé par sa croix.
    panelsMenu_ = viewMenu->addMenu(tr("&Panneaux"));
    panelsMenu_->setObjectName(QStringLiteral("menu_panels"));
    auto* hidePanelsAct = viewMenu->addAction(tr("&Masquer les panneaux"));
    hidePanelsAct_ = hidePanelsAct;
    hidePanelsAct->setCheckable(true);
    hidePanelsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    hidePanelsAct->setToolTip(tr("Mode canevas : masque tous les panneaux."));
    connect(hidePanelsAct, &QAction::toggled, this, [this](bool hide) {
        hidePanelsMode_ = hide;
        std::vector<QDockWidget*> docks{documentDock_, propertiesDock_, workflowDock_,
                                        orderDock_,    filterDock_,     analysisDock_};
        docks.push_back(historyDock_);
        docks.push_back(companionDock_);
        if (hide) {
            panelsToRestore_.clear();
            for (auto* d : docks) {
                if (d != nullptr && d->isVisible()) {
                    panelsToRestore_.push_back(d);
                    d->hide();
                }
            }
        } else {
            for (auto* d : panelsToRestore_) {
                if (d != nullptr) {
                    d->show();
                }
            }
            panelsToRestore_.clear();
        }
    });
}

void MainWindow::resetDocumentState() {
    // Invalide les commandes différées en vol (QTimer::singleShot, cf.
    // renderBase/MoveStitchPointCommand) : elles ne doivent jamais s'exécuter
    // sur un document qui n'est plus celui pour lequel elles ont été construites.
    ++documentGeneration_;
    // Le nouveau document n'est rattaché à aucun fichier tant qu'il n'a pas
    // été enregistré (ou qu'une ouverture n'a pas réinstallé la cible juste
    // après) : sans ça, un Ctrl+S après « Nouveau » écraserait le projet
    // précédent sans rien demander (HP-FILE-002).
    setCurrentProjectPath(QString());
    savedAt_ = QTime{}; // l’heure de l’ancien projet ne concerne jamais le suivant
    updateSavedIndicator();
    migratedFromVersion_ = 0;
    migratedFromPath_.clear();
    undoStack_.clear();
    if (companionDock_ != nullptr) {
        companionDock_->documentReplaced();
    }
    if (claudeBackend_ != nullptr) {
        claudeBackend_->resetConversation();
        if (codeBackend_ != nullptr) {
            codeBackend_->resetConversation();
        }
    }
    liveBridge_.reset(); // nouveau document : l'ancienne session live est close
    sequence_.reset();
    editStates_.clear();
    setSelection({});
    isolatedIds_
        .clear(); // l'isolation est un état de session : jamais reportée sur un autre document
    if (isolateAct_ != nullptr && isolateAct_->isChecked()) {
        QSignalBlocker block(isolateAct_);
        isolateAct_->setChecked(false);
    }
    if (mergeAct_ != nullptr) {
        mergeAct_->setChecked(false); // remet aussi mergeMode_ à false (cf. buildMenus)
    }

    // Modes d'édition exclusifs : décochés SANS passer par leurs slots
    // (`project_` vient d'être remplacé mais l'affichage ne l'est pas encore —
    // displayImage() y serait prématuré ; le refreshImage() de l'appelant s'en
    // charge), puis cibles relâchées.
    for (QAction* mode : {stitchEditModeAct_, satinEditModeAct_, satinGuideModeAct_,
                          railEditModeAct_, directionGuideModeAct_, gradientEditModeAct_}) {
        if (mode != nullptr && mode->isChecked()) {
            QSignalBlocker block(mode);
            mode->setChecked(false);
        }
    }
    stitchEditTarget_.reset();
    stitchEditView_.reset();
    satinGuideTarget_.reset();
    selectedSatinGuide_.reset();
    railEditTarget_.reset();
    directionGuideTarget_.reset();

    // Tracés en cours : leurs aperçus appartiennent à la scène, qui survit au
    // changement de document — sans ça, un polygone à moitié posé resterait
    // dessiné par-dessus le nouveau document et son prochain clic créerait un
    // objet mêlant les deux.
    cancelPolygonDraw();
    cancelFreeformDraw();
    cancelSatinColumnDraw();
    cancelBezierDraw();
    cancelDirectionGuideDraw();
    updateSnapIndicator(std::nullopt);

    // Simulation : jamais laissée en lecture sur une séquence qui disparaît.
    // (updateSimulationRange() remettra l'étiquette et les bornes en phase.)
    if (simTimer_ != nullptr) {
        simTimer_->stop();
    }
    if (simPlayAct_ != nullptr && simPlayAct_->isChecked()) {
        QSignalBlocker block(simPlayAct_);
        simPlayAct_->setChecked(false);
        simPlayAct_->setText(tr("▶ Lecture"));
    }
    simStep_ = -1;
}

bool MainWindow::confirmDiscardChanges(const QString& question) {
    if (!isWindowModified()) {
        return true;
    }
    // Libellés explicites plutôt que les libellés standard de Qt : le reste de
    // l'interface est en français sans dépendre des traductions Qt installées
    // (HP-I18N-001 n'existe pas encore).
    QMessageBox box(QMessageBox::Warning, tr("Modifications non enregistrées"), question,
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    // Nomme le projet concerné : « le projet » seul ne dit pas lequel on perd.
    box.setInformativeText(tr("Projet concerné : « %1 ».")
                               .arg(currentProjectPath_.isEmpty()
                                        ? tr("Sans titre")
                                        : QFileInfo(currentProjectPath_).fileName()));
    box.button(QMessageBox::Save)->setText(tr("Enregistrer"));
    box.button(QMessageBox::Discard)->setText(tr("Ne pas enregistrer"));
    box.button(QMessageBox::Cancel)->setText(tr("Annuler"));
    box.setDefaultButton(QMessageBox::Save);
    box.exec();
    const auto answer = box.standardButton(box.clickedButton());
    if (answer == QMessageBox::Save) {
        saveProject();
        // Enregistrement annulé (dialogue de fichier fermé) ou en échec : le
        // document reste modifié, donc on n'enchaîne pas sur sa destruction.
        return !isWindowModified();
    }
    return answer == QMessageBox::Discard;
}

void MainWindow::undo() {
    if (undoStack_.undo(project_)) {
        refreshImage();
        // Revenir à l'état enregistré rend le document propre.
        setWindowModified(!undoStack_.isClean());
        updateActions();
    }
}

project_ops::LiveBridge::Result
MainWindow::commitLiveBatch(const std::string& sessionId, std::uint64_t revision,
                            const std::string& batchJson, const std::string& label, bool approved) {
    auto result = liveBridge_.commit(sessionId, revision, batchJson, label, approved);
    if (result.applied) {
        afterLiveCommit();
    }
    return result;
}

void MainWindow::afterLiveCommit() {
    // Même rafraîchissement qu'un Undo/Redo : le document a changé sous l'interface.
    refreshImage();
    setWindowModified(!undoStack_.isClean());
    updateActions();
}

void MainWindow::redo() {
    if (undoStack_.redo(project_)) {
        refreshImage();
        setWindowModified(!undoStack_.isClean());
        updateActions();
    }
}

void MainWindow::applyCanvasToView() {
    const QSizeF want(to_millimeters(project_.canvas.width).value,
                      to_millimeters(project_.canvas.height).value);
    if (view_->canvasSizeMm() != want) {
        view_->setCanvasSizeMm(want);
    }
    view_->setUsableMarginMm(to_millimeters(project_.canvas.margin).value);
    view_->setHoopShape(project_.canvas.shape, to_millimeters(project_.canvas.corner_radius).value);
    if (hoopStatusButton_ != nullptr) {
        hoopStatusButton_->setText(
            tr("Cadre %1 × %2 mm").arg(want.width(), 0, 'f', 0).arg(want.height(), 0, 'f', 0));
    }
}

void MainWindow::positionEmptyState() {
    if (emptyState_ == nullptr || !emptyState_->isVisible()) {
        return;
    }
    emptyState_->adjustSize();
    const QSize s = emptyState_->size();
    const QRect vr = view_->rect();
    emptyState_->move(vr.center().x() - s.width() / 2, vr.center().y() - s.height() / 2);
}

void MainWindow::updateEmptyState() {
    if (emptyState_ == nullptr) {
        return;
    }
    // N'impose PAS d'ouvrir une image/un projet pour travailler : un motif
    // purement vectoriel (formes dessinées à la main, sans jamais importer
    // d'image) est un flux valide (canevas par défaut 100x100 mm, cf.
    // document::Canvas) -- défaut trouvé en usage réel, la pastille restait
    // affichée EN PERMANENCE par-dessus le canevas dès lors qu'aucune image
    // n'était chargée, même après avoir dessiné plusieurs formes.
    emptyState_->setVisible(!hasDocumentContent());
    positionEmptyState();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Sauve les préférences d'interface (géométrie + disposition des panneaux).
    saveUiLayout();
    // Même garde que « Nouveau projet » (HP-FILE-001) : enregistrement annulé
    // ou refusé -> la fenêtre reste ouverte.
    event->setAccepted(
        confirmDiscardChanges(tr("Le projet a été modifié. Enregistrer avant de quitter ?")));

    // HP-FILE-004 : une fermeture réellement acceptée (enregistrée ou
    // abandon explicite) ne laisse rien à récupérer au prochain lancement.
    // Si l'évènement n'est pas accepté (fermeture annulée), l'autosave
    // continue de tourner normalement.
    if (event->isAccepted()) {
        liveBridge_.close();
        if (liveServer_) {
            liveServer_->stop();
        }
        autosaveTimer_->stop();
        flushAutosave();
        discardAutosave(slotFor(currentProjectPath_));
        discardPendingRecoverySlot();
    }
}

void MainWindow::adjustBrightnessContrast() {
    if (!project_.hasImage()) {
        return;
    }
    BrightnessDialog dialog(this);
    connect(&dialog, &BrightnessDialog::previewRequested, this, [this](double b, double c) {
        if (const auto img = image::apply_op(processed_, image::BrightnessContrastOp{b, c})) {
            displayImage(*img);
        }
    });
    if (dialog.exec() == QDialog::Accepted &&
        (dialog.brightness() != 0.0 || dialog.contrast() != 0.0)) {
        executeOp(image::BrightnessContrastOp{dialog.brightness(), dialog.contrast()});
    } else {
        displayImage(processed_); // retire l'aperçu
    }
}

std::optional<QPoint> MainWindow::mmToImagePixel(QPointF mm) const {
    if (processed_.empty()) {
        return std::nullopt;
    }
    const double mmPerPx = project_.mm_per_px.value;
    const double left = -processed_.width * mmPerPx / 2.0;
    const double top = -processed_.height * mmPerPx / 2.0;
    const int x = static_cast<int>(std::floor((mm.x() - left) / mmPerPx));
    const int y = static_cast<int>(std::floor((mm.y() - top) / mmPerPx));
    if (x < 0 || y < 0 || x >= processed_.width || y >= processed_.height) {
        return std::nullopt;
    }
    return QPoint(x, y);
}

void MainWindow::refreshHints() {
    if (hintsLabel_ == nullptr) {
        return;
    }
    const QList<Hint> hints =
        InteractionMap::hintsFor(interactionContext(), heldModifiers_, selectedObject_.has_value());
    QStringList parts;
    for (const Hint& hint : hints) {
        parts << tr("%1 : %2").arg(hint.gesture, hint.label);
    }
    hintsFullText_ = parts.join(QStringLiteral("  ·  "));
    static_cast<ElidedLabel*>(hintsLabel_)->setFullText(hintsFullText_);
}

// SELECTION-MUTATOR:BEGIN
// Seul bloc autorisé à écrire selectedObject_/selectedRegion_/selectedEmbroidery_/
// multiSelection_ (garde tests/check_selection_single_mutator.cmake). Tout autre
// site passe par setSelection()/editSelection().
void MainWindow::setSelection(Selection selection) {
    selection = desktop::normalize_selection(std::move(selection), [this](RegionId r) {
        return project_.segmentation && project_.segmentation->find(r) != nullptr;
    });
    std::vector<ObjectId> objects = std::move(selection.objects);
    selectedRegion_ = selection.region;
    extraRegions_ = std::move(selection.extraRegions);
    selectedEmbroidery_ = selection.embroidery;
    if (objects.empty()) {
        selectedObject_.reset();
    } else {
        selectedObject_ = objects.back();
    }
    if (objects.size() > 1) {
        multiSelection_ = std::move(objects);
    } else {
        multiSelection_.clear(); // cas legacy : 0 ou 1 objet porté par selectedObject_
    }
    Q_ASSERT(multiSelection_.empty() ||
             (selectedObject_ && multiSelection_.back() == *selectedObject_));
    Q_ASSERT(multiSelection_.empty() || (!selectedRegion_ && !selectedEmbroidery_));
}

// SELECTION-MUTATOR:END

namespace {

// Active/désactive une action ET explique, dans l'info-bulle et la barre d'état, ce qu'il faut
// faire pour l'activer. L'info-bulle d'origine est mémorisée à la première utilisation.
void setEnabledWithReason(QAction* act, bool enabled, const QString& whyDisabled) {
    if (act == nullptr) {
        return;
    }
    if (!act->property("baseToolTip").isValid()) {
        act->setProperty("baseToolTip", act->toolTip());
        act->setProperty("baseStatusTip", act->statusTip());
    }
    act->setEnabled(enabled);
    const QString baseTip = act->property("baseToolTip").toString();
    if (enabled) {
        act->setToolTip(baseTip);
        act->setStatusTip(act->property("baseStatusTip").toString());
    } else {
        act->setToolTip(baseTip + QStringLiteral("\n") + whyDisabled);
        act->setStatusTip(whyDisabled);
    }
}

} // namespace

bool MainWindow::hasDocumentContent() const {
    return project_.hasImage() || !project_.vector_objects.empty() ||
           !project_.embroidery_objects.empty() || !project_.text_objects.empty() ||
           project_.imported_design.has_value();
}

void MainWindow::updateActions() {
    updateCompanionContext();
    syncColorwayUi(); // HP-THR-006/008 (main_window_colorways.cpp)
    updateShapeActions();
    updateObjectEditActions();
    updateTextActions();
    const bool hasImage = project_.hasImage();
    for (QAction* act : imageActions_) {
        setEnabledWithReason(act, hasImage, tr("Ouvrez d'abord une image."));
    }
    // Outil Recadrer de la palette : sans image il ne peut rien faire (le glisser serait ignoré).
    setEnabledWithReason(toolRectAct_, hasImage, tr("Ouvrez d'abord une image."));
    setEnabledWithReason(showSegAct_, project_.segmentation.has_value(),
                         tr("Segmentez d'abord l'image (menu Segmentation)."));
    setEnabledWithReason(mergeColorsAct_, project_.segmentation.has_value(),
                         tr("Segmentez d'abord l'image (menu Segmentation)."));
    setEnabledWithReason(nextRegionAct_, project_.segmentation.has_value(),
                         tr("Segmentez d'abord l'image (menu Segmentation)."));
    setEnabledWithReason(previousRegionAct_, project_.segmentation.has_value(),
                         tr("Segmentez d'abord l'image (menu Segmentation)."));
    const QString noVectors = tr("Aucun objet vectoriel : vectorisez une région ou dessinez.");
    setEnabledWithReason(showVectorsAct_, !project_.vector_objects.empty(), noVectors);
    setEnabledWithReason(showStitchesAct_, sequence_.has_value(),
                         tr("Aucune broderie : créez un objet de broderie."));
    // Actions mono-objet : désactivées dès que la multi-sélection compte > 1 objet.
    const bool singleObject = selectedObject_.has_value() && !hasMultiSelection();
    QString needOneShape = tr("Sélectionnez d'abord une forme (clic sur le motif).");
    if (hasMultiSelection()) {
        needOneShape = tr("Une seule forme à la fois : plusieurs sont sélectionnées.");
    }
    // Les types de points s'appliquent à une ou plusieurs formes sélectionnées.
    const bool anyObject = !selectedObjectIds().empty();
    const QString needShapes =
        tr("Sélectionnez d'abord une ou plusieurs formes (clic sur le motif).");
    setEnabledWithReason(createStitchAct_, anyObject, needShapes);
    setEnabledWithReason(createTatamiAct_, anyObject, needShapes);
    setEnabledWithReason(createSatinAct_, anyObject, needShapes);
    setEnabledWithReason(createStitchWithSettingsAct_, anyObject, needShapes);
    setEnabledWithReason(createTatamiWithSettingsAct_, anyObject, needShapes);
    setEnabledWithReason(createSatinWithSettingsAct_, anyObject, needShapes);
    setEnabledWithReason(autoSatinAct_, anyObject, needShapes);
    setEnabledWithReason(duplicateSelectionAct_, singleObject, needOneShape);
    setEnabledWithReason(offsetSelectionAct_, singleObject, needOneShape);
    updateAlignActions();
    updateArrangeActions();
    refreshHistoryPanel();
    refreshThreadSelectionInfo();
    setEnabledWithReason(fillAngleAct_, currentFillObject() != nullptr,
                         tr("Sélectionnez un objet à remplissage (tatami ou directionnel)."));
    setEnabledWithReason(convertSatinAct_,
                         std::any_of(project_.embroidery_objects.begin(),
                                     project_.embroidery_objects.end(),
                                     [](const auto& e) { return e.is_satin(); }),
                         tr("Aucun satin dans le projet."));
    const QString needStitches =
        tr("Aucun point généré : créez d'abord un objet de broderie (tatami, satin…).");
    setEnabledWithReason(statsAct_, sequence_.has_value(), needStitches);
    setEnabledWithReason(exportDstAct_, sequence_.has_value(), needStitches);
    setEnabledWithReason(exportDstQuickAct_, sequence_.has_value(), needStitches);
    setEnabledWithReason(productionAct_, sequence_.has_value(), needStitches);
    // Actions « document requis » : mêmes gardes que leurs slots (qui restent en place).
    // Même critère que l'accueil, l'autosave et les slots, import machine inclus.
    // Enregistrer reste actif dès que le document a un fichier.
    const bool hasDocument = hasDocumentContent();
    setEnabledWithReason(saveProjectAct_, hasDocument || !currentProjectPath_.isEmpty(),
                         tr("Rien à enregistrer : ouvrez une image ou dessinez une forme."));
    setEnabledWithReason(saveProjectAsAct_, hasDocument,
                         tr("Rien à enregistrer : ouvrez une image ou dessinez une forme."));
    setEnabledWithReason(exportDxfAct_, !project_.vector_objects.empty(),
                         tr("Aucun objet vectoriel à exporter."));
    setEnabledWithReason(analyzeAct_, sequence_.has_value(), needStitches);
    const bool hasSelection = selectedRegion_.has_value() && project_.segmentation.has_value();
    for (QAction* act : regionActions_) {
        setEnabledWithReason(act, hasSelection,
                             tr("Sélectionnez d'abord une région dans l'image segmentée."));
    }
    const std::size_t pickedRegions = selectedRegionIds().size();
    setEnabledWithReason(mergeSelectionAct_, pickedRegions >= 2,
                         tr("Sélectionnez au moins deux régions (Ctrl+clic ou Maj+clic)."));
    setEnabledWithReason(
        absorbAct_, pickedRegions == 1,
        tr("Sélectionnez une seule région : elle sera fusionnée dans sa voisine."));
    setEnabledWithReason(cleanIslandsAct_,
                         project_.segmentation.has_value() &&
                             project_.segmentation->region_count() > 1,
                         tr("Segmentez d'abord l'image (menu Segmentation)."));
    setEnabledWithReason(
        selectAllRegionsAct_,
        (project_.segmentation.has_value() && project_.segmentation->region_count() > 0) ||
            !project_.vector_objects.empty(),
        tr("Rien à sélectionner : segmentez l'image ou créez des formes."));
    // Suppr universel : région, objet(s) vectoriel(s) ou objet de broderie.
    setEnabledWithReason(deleteSelectionAct_,
                         hasSelection || selectedObject_.has_value() ||
                             selectedEmbroidery_.has_value(),
                         tr("Sélectionnez d'abord une région ou un objet."));
    if (!mergeAct_->isEnabled()) {
        mergeAct_->setChecked(false);
    }

    // Mode d'édition des points (Lot 8.2) : sortie propre dès que le contexte
    // qui l'a activé n'est plus valide -- sélection perdue, objet différent
    // sélectionné, ou objet devenu Dirty entre-temps (satin routé voisin,
    // suppression d'un objet adjacent…). `editStates_` est déjà à jour (calculé
    // par le refreshImage() qui précède systématiquement cet appel), donc
    // aucun appel au cœur ici : uniquement des lectures.
    document::EmbroideryObject* stitchEditEmb = resolveSelectedEmbroidery();
    const stitch_generation::ObjectEditState stitchEditState =
        stitchEditEmb != nullptr ? editStateOf(stitchEditEmb->id)
                                 : stitch_generation::ObjectEditState::Clean;
    const bool stitchEditSameTarget = stitchEditTarget_.has_value() && stitchEditEmb != nullptr &&
                                      *stitchEditTarget_ == stitchEditEmb->id;
    // Trop de points déplaçables (cf. kMaxEditableStitchHandles, renderBase) :
    // le mode ne doit jamais rester actif sans aucune poignée visible --
    // sortie propre + explication, plutôt qu'un mode "actif" muet.
    const bool stitchEditTooManyPoints =
        stitchEditView_.has_value() &&
        static_cast<std::size_t>(
            std::count_if(stitchEditView_->raw.begin(), stitchEditView_->raw.end(),
                          stitch_generation::is_movable_point)) > kMaxEditableStitchHandles;
    bool stitchEditNeedsRerender = false;
    if (stitchEditModeAct_->isChecked() &&
        (stitchEditEmb == nullptr || !stitchEditSameTarget ||
         stitchEditState == stitch_generation::ObjectEditState::Dirty || stitchEditTooManyPoints)) {
        if (stitchEditTooManyPoints) {
            statusBar()->showMessage(
                tr("Édition des points désactivée : cet objet compte plus de %1 points de "
                   "couture déplaçables. Réduisez la densité ou la longueur de point pour "
                   "l'éditer point par point.")
                    .arg(kMaxEditableStitchHandles));
        }
        QSignalBlocker block(stitchEditModeAct_);
        stitchEditModeAct_->setChecked(false);
        stitchEditTarget_.reset();
        stitchEditView_.reset();
        stitchEditNeedsRerender = true;
    }
    stitchEditModeAct_->setEnabled(stitchEditEmb != nullptr &&
                                   stitchEditState != stitch_generation::ObjectEditState::Dirty);
    updateStitchPointActions();
    if (stitchEditNeedsRerender) {
        displayImage(processed_); // retire les poignées de l'objet quitté
    }

    document::EmbroideryObject* satinGuideEmb = resolveSelectedEmbroidery();
    const auto* selectedSatin = satinGuideEmb != nullptr
                                    ? std::get_if<document::SatinParams>(&satinGuideEmb->params)
                                    : nullptr;
    const bool satinGuideContext = selectedSatin != nullptr && selectedSatin->rungs.size() >= 2;
    const bool satinGuideSameTarget = satinGuideTarget_.has_value() && satinGuideEmb != nullptr &&
                                      *satinGuideTarget_ == satinGuideEmb->id;
    if (satinGuideModeAct_->isChecked() && (!satinGuideContext || !satinGuideSameTarget)) {
        QSignalBlocker block(satinGuideModeAct_);
        satinGuideModeAct_->setChecked(false);
        satinGuideTarget_.reset();
        selectedSatinGuide_.reset();
        displayImage(processed_);
    }
    satinGuideModeAct_->setEnabled(satinGuideContext);
    if (selectedSatin != nullptr && selectedSatinGuide_ &&
        *selectedSatinGuide_ >= selectedSatin->rungs.size()) {
        selectedSatinGuide_.reset();
    }
    const bool satinGuideEditing = satinGuideModeAct_->isChecked() && satinGuideContext;
    addSatinGuideAct_->setEnabled(satinGuideEditing);
    const bool structuralJunctionGuide =
        satinGuideEditing && selectedSatinGuide_ &&
        stitch_generation::satin_guide_junction(*selectedSatin, *selectedSatinGuide_).has_value();
    removeSatinGuideAct_->setEnabled(satinGuideEditing && selectedSatinGuide_.has_value() &&
                                     selectedSatin->rungs.size() > 2 && !structuralJunctionGuide);

    updateDirectionGuideActions();
    updateGradientActions();

    document::EmbroideryObject* railEditEmb = resolveSelectedEmbroidery();
    const bool railEditContext = railEditEmb != nullptr && railEditEmb->is_satin();
    const bool railEditSameTarget = railEditTarget_.has_value() && railEditEmb != nullptr &&
                                    *railEditTarget_ == railEditEmb->id;
    if (railEditModeAct_->isChecked() && (!railEditContext || !railEditSameTarget)) {
        QSignalBlocker block(railEditModeAct_);
        railEditModeAct_->setChecked(false);
        railEditTarget_.reset();
        displayImage(processed_);
    }
    railEditModeAct_->setEnabled(railEditContext);

    // Synchronise le bouton unifié avec les deux modes qu'il agrège : si l'un
    // des deux s'est désactivé tout seul (perte de contexte ci-dessus), le
    // bouton « Modifier la colonne satin » ne doit pas rester coché alors
    // qu'une partie du remodelage a silencieusement cessé de s'afficher.
    if (satinEditModeAct_ != nullptr) {
        QSignalBlocker block(satinEditModeAct_);
        satinEditModeAct_->setChecked(satinGuideModeAct_->isChecked() &&
                                      railEditModeAct_->isChecked());
        satinEditModeAct_->setEnabled(satinGuideContext || railEditContext);
    }

    undoAct_->setEnabled(undoStack_.canUndo());
    redoAct_->setEnabled(undoStack_.canRedo());
    undoAct_->setText(undoStack_.canUndo()
                          ? tr("&Annuler %1").arg(QString::fromStdString(undoStack_.undoName()))
                          : tr("&Annuler"));
    redoAct_->setText(undoStack_.canRedo()
                          ? tr("&Rétablir %1").arg(QString::fromStdString(undoStack_.redoName()))
                          : tr("&Rétablir"));

    updateInspector();
    syncDocumentSelection();
    updateContextToolbar();
    updateSegmentationWorkflowActions();
    refreshWorkflow();
    updateEmptyState();
    // Les sorties automatiques bloquent QAction::toggled : synchroniser le contexte réel,
    // pas seulement les indications, sinon le prochain rectangle devient un panoramique.
    updateInteractionContext();
}

} // namespace openstitch::desktop
