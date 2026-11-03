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

QString fmtUm(Micrometers v) {
    return QStringLiteral("%1 µm (%2 mm)").arg(v.value).arg(to_millimeters(v).value, 0, 'f', 3);
}
QString fmtVec(Vec2um v) {
    return QStringLiteral("(%1, %2) µm").arg(v.x.value).arg(v.y.value);
}
QString fmtOptVec(const std::optional<Vec2um>& v) {
    return v ? fmtVec(*v) : QStringLiteral("(absent)");
}
QString fmtBool(bool b) {
    return b ? QStringLiteral("oui") : QStringLiteral("non");
}
QString fmtNodeType(geometry::NodeType t) {
    return t == geometry::NodeType::Smooth ? QStringLiteral("Lisse") : QStringLiteral("Coin");
}
QString fmtShortStitch(document::SatinShortStitch v) {
    switch (v) {
    case document::SatinShortStitch::Disabled:
        return QStringLiteral("Désactivés");
    case document::SatinShortStitch::RemoveAndRedistribute:
        return QStringLiteral("Retirer/redistribuer");
    case document::SatinShortStitch::SingleInset:
        return QStringLiteral("Inset simple");
    case document::SatinShortStitch::MultiLevelInset:
        return QStringLiteral("Inset multi-niveaux");
    }
    return QStringLiteral("?");
}
QString fmtSplit(document::SatinSplit v) {
    switch (v) {
    case document::SatinSplit::Disabled:
        return QStringLiteral("Désactivé");
    case document::SatinSplit::Simple:
        return QStringLiteral("Simple");
    case document::SatinSplit::Staggered:
        return QStringLiteral("Décalé");
    case document::SatinSplit::DeterministicJitter:
        return QStringLiteral("Jitter déterministe");
    }
    return QStringLiteral("?");
}
QString fmtCap(document::SatinCap v) {
    switch (v) {
    case document::SatinCap::Flat:
        return QStringLiteral("Plat");
    case document::SatinCap::Rounded:
        return QStringLiteral("Arrondi");
    case document::SatinCap::Tapered:
        return QStringLiteral("Effilé");
    case document::SatinCap::Automatic:
        return QStringLiteral("Auto");
    }
    return QStringLiteral("?");
}
QString fmtLock(document::SatinLock v) {
    switch (v) {
    case document::SatinLock::None:
        return QStringLiteral("Aucun");
    case document::SatinLock::BackAndForth:
        return QStringLiteral("Aller-retour");
    case document::SatinLock::Triangle:
        return QStringLiteral("Triangle");
    case document::SatinLock::MicroZigzag:
        return QStringLiteral("Micro-zigzag");
    }
    return QStringLiteral("?");
}
QString fmtStitchPointType(document::StitchPointType v) {
    return v == document::StitchPointType::Stitch ? QStringLiteral("Stitch")
                                                  : QStringLiteral("Jump");
}
QString fmtCommandType(stitch::CommandType v) {
    switch (v) {
    case stitch::CommandType::Stitch:
        return QStringLiteral("Stitch");
    case stitch::CommandType::Jump:
        return QStringLiteral("Jump");
    case stitch::CommandType::Trim:
        return QStringLiteral("Trim");
    case stitch::CommandType::ColorChange:
        return QStringLiteral("ColorChange");
    case stitch::CommandType::Stop:
        return QStringLiteral("Stop");
    case stitch::CommandType::End:
        return QStringLiteral("End");
    }
    return QStringLiteral("?");
}
QString fmtPass(stitch::StitchPass v) {
    switch (v) {
    case stitch::StitchPass::Underlay:
        return QStringLiteral("Underlay");
    case stitch::StitchPass::TopStitch:
        return QStringLiteral("TopStitch");
    case stitch::StitchPass::Travel:
        return QStringLiteral("Travel");
    case stitch::StitchPass::Lock:
        return QStringLiteral("Lock");
    case stitch::StitchPass::Manual:
        return QStringLiteral("Manual");
    }
    return QStringLiteral("?");
}
QString fmtEditState(stitch_generation::ObjectEditState v) {
    switch (v) {
    case stitch_generation::ObjectEditState::Clean:
        return QStringLiteral("Clean");
    case stitch_generation::ObjectEditState::ManuallyEdited:
        return QStringLiteral("ManuallyEdited");
    case stitch_generation::ObjectEditState::Dirty:
        return QStringLiteral("Dirty");
    }
    return QStringLiteral("?");
}
QString fmtSeverity(stitch_analysis::Severity v) {
    switch (v) {
    case stitch_analysis::Severity::Info:
        return QStringLiteral("Info");
    case stitch_analysis::Severity::Warning:
        return QStringLiteral("Warning");
    case stitch_analysis::Severity::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("?");
}

void dumpPath(QStringList& out, const geometry::Path& path, const QString& label) {
    out << QStringLiteral("  %1 : %2, %3 noeud(s)")
               .arg(label, path.closed ? QStringLiteral("fermé") : QStringLiteral("ouvert"))
               .arg(path.nodes.size());
    for (std::size_t i = 0; i < path.nodes.size(); ++i) {
        const auto& n = path.nodes[i];
        out << QStringLiteral("    [%1] pos=%2  type=%3  tan_in=%4  tan_out=%5")
                   .arg(i)
                   .arg(fmtVec(n.pos), fmtNodeType(n.type), fmtOptVec(n.tan_in),
                        fmtOptVec(n.tan_out));
    }
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

bool MainWindow::stitchParamsForType(const document::EmbroideryObject& emb, int type,
                                     document::StitchParams& params, std::string& label,
                                     std::optional<std::vector<geometry::PathSet>>& restoredContour,
                                     QString& error) {
    switch (type) {
    case 0: { // contour cousu
        document::RunningStitchParams rp;
        rp.repeats = 3;
        params = rp;
        label = "Type : contour";
        return true;
    }
    case 1: // tatami
        params = document::TatamiParams{};
        label = "Type : tatami";
        return true;
    case 5: // motif répété le long du contour (HP-STI-002)
        params = document::MotifRunParams{};
        label = "Type : motif le long du contour";
        return true;
    case 6: // remplissage à motif (HP-STI-006)
        params = document::MotifFillParams{};
        label = "Type : remplissage à motif";
        return true;
    case 4: // blanket / zigzag le long du contour (HP-STI-003)
        params = document::BlanketStitchParams{};
        label = "Type : blanket";
        return true;
    case 7: // remplissage concentrique (HP-STI-007)
        params = document::ContourFillParams{};
        label = "Type : remplissage concentrique";
        return true;
    case 8: // remplissage radial (HP-STI-008)
        params = document::RadialFillParams{};
        label = "Type : remplissage radial";
        return true;
    case 9: // remplissage en spirale (HP-STI-009)
        params = document::SpiralFillParams{};
        label = "Type : remplissage en spirale";
        return true;
    case 10: // remplissage croisé (HP-STI-014)
        params = document::CrosshatchFillParams{};
        label = "Type : remplissage croisé";
        return true;
    case 11: // point de croix (HP-STI-012)
        params = document::CrossStitchParams{};
        label = "Type : point de croix";
        return true;
    case 3: { // remplissage directionnel (réglages du tatami repris s'il y a lieu)
        auto directional = directionalParamsFor(emb);
        if (!directional) {
            error = tr("Aucun contour source pour le remplissage directionnel.");
            return false;
        }
        params = std::move(*directional);
        label = "Type : remplissage directionnel";
        return true;
    }
    case 2: { // satin par squelette et traversées orientées
        const auto* currentSource = project_.findObject(emb.source_vector);
        if (currentSource == nullptr || currentSource->paths.empty()) {
            error = tr("Aucun contour source pour construire le satin.");
            return false;
        }
        // Le satin suit la région telle que segmentée, pas le contour agrandi par le
        // recouvrement des tatamis voisins (sinon il déborde de sa zone).
        restoredContour = pristineSatinContour(*currentSource);
        document::VectorObject restoredCopy;
        const document::VectorObject* source = currentSource;
        if (restoredContour) {
            restoredCopy = *currentSource;
            restoredCopy.paths = *restoredContour;
            source = &restoredCopy;
        }
        document::AutoSatinParams sp;
        sp.strategy = document::kDefaultAutoSatinStrategy;
        AutoSatinPreview preview = previewAutoSatin(*source, sp);
        QString methodNote;
        if (preview.columns == 0) {
            // Le squelette refuse la forme : on essaie les autres méthodes. La méthode retenue
            // est écrite sur l'objet (visible dans l'inspecteur) et nommée dans l'historique.
            if (const auto alt = firstWorkingAutoSatinStrategy(*source, sp);
                alt && *alt != sp.strategy) {
                sp.strategy = *alt;
                preview = previewAutoSatin(*source, sp);
                methodNote = autoSatinStrategyLabel(*alt);
            }
        }
        if (preview.columns == 0) {
            // Refus nommé, jamais contourné par une géométrie de moindre qualité.
            error = tr("Aucune colonne satin n'a pu être construite pour cette région :\n%1\n\n"
                       "Essayez un tatami.")
                        .arg(preview.messages.isEmpty() ? tr("forme non exploitable")
                                                        : preview.messages.join(QLatin1Char('\n')));
            return false;
        }
        params = sp;
        label = methodNote.isEmpty() ? std::string("Type : satin")
                                     : ("Type : satin (" + methodNote.toStdString() + ")");
        return true;
    }
    default:
        error = tr("Type de points inconnu.");
        return false;
    }
}

void MainWindow::setStitchType(ObjectId embroideryId, int type) {
    auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr || stitchTypeIndex(*emb) == type) {
        return;
    }
    document::StitchParams params;
    std::string label;
    std::optional<std::vector<geometry::PathSet>> restoredContour; // satin : contour brut
    QString error;
    if (!stitchParamsForType(*emb, type, params, label, restoredContour, error)) {
        QMessageBox::warning(this, type == 2 ? tr("Satin impossible") : tr("Conversion impossible"),
                             error);
        return;
    }
    // `ConvertFillGroupCommand` (pas `SetStitchTypeCommand` seule) : un
    // réseau satin auto-généré en plusieurs sections partage un seul
    // `source_vector` entre plusieurs `EmbroideryObject` -- `embroideryId`
    // ici n'en désigne qu'UN (le premier trouvé par `embroideryForVector`).
    // Sans ce garde-fou, changer de type ne convertirait que cette
    // première section et laisserait les autres en satin réel, inchangées
    // -- exactement le défaut réel signalé (« résidu de satin qui reste
    // même en revenant en tatami », 2026-09-04, cf. le commentaire de la
    // commande pour le détail complet). Elle ne regroupe que les sections satin à rails,
    // pas les couches indépendantes d'un fondu partageant elles aussi un source_vector.
    using Convert = commands::ConvertFillGroupCommand;
    auto convert = std::make_unique<Convert>(embroideryId, std::move(params), std::move(label));
    if (restoredContour) {
        auto group = std::make_unique<commands::CompositeCommand>("Type : satin");
        group->add(std::make_unique<commands::SetVectorPathsCommand>(
            emb->source_vector, *restoredContour, "Contour brut de la région"));
        group->add(std::move(convert));
        undoStack_.execute(std::move(group), project_);
    } else {
        undoStack_.execute(std::move(convert), project_);
    }
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
}

void MainWindow::onCanvasContextMenu(QPointF posMm, QPoint globalPos) {
    const auto hit = objectAt(posMm);
    // Carte des régions affichée, aucun objet sous le curseur : menu de la région pointée.
    if (!hit && showSegAct_ != nullptr && showSegAct_->isChecked() && project_.segmentation &&
        satinEmbroideryAt(posMm) == nullptr) {
        const auto px = mmToImagePixel(posMm);
        if (px) {
            if (const auto region =
                    segmentation::region_at(*project_.segmentation, px->x(), px->y())) {
                showRegionContextMenu(*region, globalPos);
                return;
            }
        }
    }
    QMenu menu(this);
    document::EmbroideryObject* emb = nullptr;
    std::optional<ObjectId> vecId; // objet vectoriel à supprimer/dupliquer, si pertinent

    if (hit && hasMultiSelection() && isObjectSelected(*hit)) {
        // Clic droit sur un membre de la multi-sélection : on la garde ; seules les
        // entrées qui valent pour tout l'ensemble sont proposées (les entrées
        // mono-objet -- type de points, dupliquer, décaler -- sont absentes).
        auto* title = menu.addAction(tr("%1 objets").arg(multiSelection_.size()));
        title->setEnabled(false);
        menu.addSeparator();
        auto* multiTypeMenu = menu.addMenu(tr("Type de points (tous)"));
        for (int t = 0; t < kStitchTypeCount; ++t) {
            auto* act = multiTypeMenu->addAction(stitchTypeLabel(t));
            connect(act, &QAction::triggered, this, [this, t] { setStitchTypeForSelection(t); });
        }
        menu.addSeparator();
        auto* deleteAllAct = menu.addAction(tr("&Supprimer %1 objets").arg(multiSelection_.size()));
        deleteAllAct->setObjectName(QStringLiteral("contextDeleteSelection"));
        connect(deleteAllAct, &QAction::triggered, this, &MainWindow::deleteSelection);
    } else if (hit) {
        setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {*hit}});
        const auto* vec = project_.findObject(*hit);
        emb = embroideryForVector(*hit);
        if (vec != nullptr) {
            vecId = vec->id;
            auto* title = menu.addAction(QString::fromStdString(vec->name));
            title->setEnabled(false);
            menu.addSeparator();
        }
    } else if (auto* satinHit = satinEmbroideryAt(posMm)) {
        // Repli : colonne satin manuelle (objet vectoriel source invisible,
        // cf. satinEmbroideryAt) — sans quoi ni le menu « Type de points » ni
        // le débogage ne seraient jamais accessibles pour ces objets.
        emb = satinHit;
        vecId = emb->source_vector; // proxy caché : sa suppression entraîne le satin
        setSelection({.region = std::nullopt, .embroidery = emb->id, .objects = {}});
        auto* title = menu.addAction(QString::fromStdString(emb->name));
        title->setEnabled(false);
        menu.addSeparator();
    }

    if (emb != nullptr) {
        const ObjectId embId = emb->id;
        auto* typeMenu = menu.addMenu(tr("Type de points"));
        const int current = stitchTypeIndex(*emb);
        for (int t = 0; t < kStitchTypeCount; ++t) {
            auto* act = typeMenu->addAction(stitchTypeLabel(t));
            act->setCheckable(true);
            act->setChecked(t == current);
            connect(act, &QAction::triggered, this, [this, embId, t] { setStitchType(embId, t); });
        }
        if (emb->is_tatami()) {
            auto* rot = menu.addAction(tr("Orientation du remplissage…"));
            connect(rot, &QAction::triggered, this, &MainWindow::changeFillAngle);
        }
        if (emb->is_tatami() || emb->is_directional()) {
            auto* contour = menu.addAction(tr("Convertir en remplissage concentrique"));
            connect(contour, &QAction::triggered, this,
                    [this, embId] { convertToContourFill(embId); });
        }
        if (emb->is_tatami() || emb->is_directional()) {
            auto* autoGuide = menu.addAction(tr("Generer un guide depuis la forme"));
            connect(autoGuide, &QAction::triggered, this,
                    &MainWindow::generateDirectionGuideFromShape);
        }
        if (emb->is_directional()) {
            auto* guides = menu.addAction(tr("Guides de direction…"));
            connect(guides, &QAction::triggered, this, [this, embId] {
                editSelection([embId](Selection& sel) { sel.embroidery = embId; });
                directionGuideModeAct_->setChecked(true);
            });
        }
        if (hit) {
            auto* addLayer = menu.addAction(tr("Ajouter une couche…"));
            addLayer->setObjectName(QStringLiteral("contextAddLayer"));
            connect(addLayer, &QAction::triggered, this,
                    [this, embId] { addLayerInteractive(embId); });
        }
        // Nombre de couches de la forme : précise l'effet des entrées de suppression ci-dessous.
        const auto layerCount =
            std::count_if(project_.embroidery_objects.begin(), project_.embroidery_objects.end(),
                          [&](const auto& e) { return e.source_vector == emb->source_vector; });
        menu.addSeparator();
        const std::uint32_t embColorKey = colorKeyOf(emb->rgb);
        const int embFilterType = stitchFilterFamily(stitchTypeIndex(*emb));
        connect(menu.addAction(tr("Afficher uniquement cette couleur")), &QAction::triggered, this,
                [this, embColorKey] { isolateFilterColor(embColorKey); });
        connect(menu.addAction(tr("Afficher uniquement ce type de points")), &QAction::triggered,
                this, [this, embFilterType] { isolateFilterType(embFilterType); });
        menu.addSeparator();
        // Supprimer la broderie seule n'a de sens que si la forme source
        // reste utilisable après (cas d'un vrai objet vectoriel visible,
        // convertible en un autre type) — pas pour le proxy caché du satin
        // manuel, où « Supprimer » plus bas fait déjà tout disparaître.
        if (hit) {
            auto* removeStitchAct =
                menu.addAction(layerCount > 1 ? tr("Supprimer la couche « %1 » (garder la forme)")
                                                    .arg(QString::fromStdString(emb->name))
                                              : tr("Supprimer la broderie (garder la forme)"));
            connect(removeStitchAct, &QAction::triggered, this,
                    [this, embId] { deleteEmbroideryObjectOnly(embId); });
        }
        // Entrée de diagnostic réservée aux développeurs : masquée sauf si
        // OPENSTITCH_DEBUG=1 (variable d'environnement) ou clé QSettings « debug/menu » à vrai.
        if (debugMenuEnabled()) {
            auto* debugAct = menu.addAction(tr("&Déboguer : afficher toutes les données…"));
            connect(debugAct, &QAction::triggered, this, [this, embId] { showDebugDump(embId); });
        }
    } else if (hit && vecId) {
        // HP-UX-028 : une forme sans couture propose les mêmes quatre types de points (création
        // immédiate, un pas d'annulation, zéro dialogue).
        auto* typeMenu = menu.addMenu(tr("Type de points"));
        typeMenu->setObjectName(QStringLiteral("contextStitchTypeMenu"));
        for (int t = 0; t < kStitchTypeCount; ++t) {
            auto* act = typeMenu->addAction(stitchTypeLabel(t));
            connect(act, &QAction::triggered, this, [this, t] { setStitchTypeForSelection(t); });
        }
    }

    if (vecId) {
        menu.addSeparator();
        const ObjectId targetVecId = *vecId;
        if (hit) {
            auto* dupAct = menu.addAction(tr("Dupliquer"));
            connect(dupAct, &QAction::triggered, this,
                    [this, targetVecId] { duplicateVectorObject(targetVecId); });
            auto* offsetAct = menu.addAction(tr("Décaler…"));
            connect(offsetAct, &QAction::triggered, this,
                    [this, targetVecId] { offsetVectorObject(targetVecId); });
        }
        // Pas de raccourci Suppr ici : « Supprimer la sélection »
        // (action_deleteRegion, universelle) le porte déjà au niveau fenêtre — un
        // second raccourci identique créerait une ambiguïté Qt au lieu d'agir.
        const auto shapeLayers =
            std::count_if(project_.embroidery_objects.begin(), project_.embroidery_objects.end(),
                          [&](const auto& e) { return e.source_vector == targetVecId; });
        auto* deleteAct = menu.addAction(
            shapeLayers > 1 ? tr("&Supprimer la forme et ses %1 couches").arg(shapeLayers)
                            : tr("&Supprimer"));
        connect(deleteAct, &QAction::triggered, this,
                [this, targetVecId] { deleteVectorObject(targetVecId); });
    }

    // Outils de sélection (Édition ▸ Sélection) : mêmes actions, accessibles au clic droit.
    if (const auto* selectionMenu = findChild<QMenu*>(QStringLiteral("menu_selection"))) {
        if (!menu.isEmpty()) {
            menu.addSeparator();
        }
        QMenu* shared = menu.addMenu(tr("Sélection"));
        for (QAction* act : selectionMenu->actions()) {
            if (act->menu() == nullptr) {
                shared->addAction(act);
            }
        }
    }

    // Filtres d'affichage actifs : toujours pouvoir tout réafficher, même sur le fond vide.
    const bool filtersActive =
        !hiddenColors_.empty() || minAreaMm2_ > 0.0 ||
        std::find(showType_.begin(), showType_.end(), false) != showType_.end();
    if (filtersActive) {
        if (!menu.isEmpty()) {
            menu.addSeparator();
        }
        connect(menu.addAction(tr("Tout afficher (filtres)")), &QAction::triggered, this,
                &MainWindow::showAllFilters);
    }

    if (!menu.isEmpty()) {
        menu.addSeparator();
    }
    // Calques accessibles partout (même sans objet sous le curseur).
    auto* layers = menu.addMenu(tr("Plans d'affichage"));
    for (QAction* act : {showImageAct_, showSegAct_, showVectorsAct_, showStitchesAct_}) {
        if (act != nullptr) {
            layers->addAction(act);
        }
    }

    displayImage(processed_); // reflète la sélection éventuelle
    updateActions();
    menu.exec(globalPos);
}

void MainWindow::deleteVectorObject(ObjectId id) {
    const auto* obj = project_.findObject(id);
    const QString name = obj != nullptr ? QString::fromStdString(obj->name) : tr("objet");
    undoStack_.execute(std::make_unique<commands::RemoveVectorObjectCommand>(id), project_);
    // L'objet supprimé quitte la sélection (principal compris : le précédent
    // est promu) ; la broderie rattachée disparaît avec lui : si elle était
    // ciblée, la sélection ne doit pas pointer dans le vide.
    pruneSelection();
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("« %1 » supprimé.").arg(name));
}

void MainWindow::deleteEmbroideryObjectOnly(ObjectId id) {
    const auto* obj = project_.findEmbroidery(id);
    const QString name = obj != nullptr ? QString::fromStdString(obj->name) : tr("objet");
    undoStack_.execute(removeEmbroideryCommand(id), project_);
    editSelection([id](Selection& sel) {
        if (sel.embroidery == id) {
            sel.embroidery.reset();
        }
    });
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Broderie « %1 » supprimée (forme conservée).").arg(name));
}

void MainWindow::duplicateVectorObject(ObjectId id) {
    const auto* source = project_.findObject(id);
    if (source == nullptr) {
        return;
    }
    document::VectorObject copy = *source;
    copy.id = project_.object_ids.next();
    copy.name = tr("%1 (copie)").arg(QString::fromStdString(source->name)).toStdString();
    const ObjectId copyId = copy.id;
    // Léger décalage (2 mm) pour que la copie reste visible distinctement au
    // lieu de se superposer exactement à l'original.
    constexpr Micrometers kOffset{2'000};
    const QString sourceName = QString::fromStdString(source->name);
    // La copie emporte TOUTES les couches de couture de la forme (mêmes réglages, même ordre) ;
    // la translation déplace aussi leur géométrie propre (guides, rails). Un seul pas d'annulation.
    auto composite = std::make_unique<commands::CompositeCommand>(tr("Dupliquer").toStdString());
    composite->add(std::make_unique<commands::AddVectorObjectCommand>(std::move(copy)));
    const int layers = addEmbroideryCopies(*composite, id, copyId);
    composite->add(std::make_unique<commands::TranslateVectorObjectCommand>(
        copyId, Vec2um{kOffset, -kOffset}));
    undoStack_.execute(std::move(composite), project_);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {copyId}});
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        layers > 1 ? tr("« %1 » dupliqué avec ses %2 couches.").arg(sourceName).arg(layers)
                   : tr("« %1 » dupliqué.").arg(sourceName));
}

int MainWindow::addEmbroideryCopies(commands::CompositeCommand& composite, ObjectId sourceId,
                                    ObjectId copyId) {
    int count = 0;
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.source_vector != sourceId) {
            continue;
        }
        document::EmbroideryObject copy = emb;
        copy.id = project_.object_ids.next();
        copy.source_vector = copyId;
        composite.add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(copy)));
        ++count;
    }
    return count;
}

void MainWindow::duplicateAndTranslate(const std::vector<ObjectId>& ids, Vec2um delta) {
    // Copie(s) + translation des COPIES = UN pas d'annulation ; les originaux ne bougent pas.
    auto composite =
        std::make_unique<commands::CompositeCommand>(tr("Dupliquer en déplaçant").toStdString());
    std::vector<ObjectId> copies;
    for (const ObjectId id : ids) {
        const auto* source = project_.findObject(id);
        if (source == nullptr) {
            continue;
        }
        document::VectorObject copy = *source;
        copy.id = project_.object_ids.next();
        copy.name = tr("%1 (copie)").arg(QString::fromStdString(source->name)).toStdString();
        copies.push_back(copy.id);
        composite->add(std::make_unique<commands::AddVectorObjectCommand>(std::move(copy)));
        addEmbroideryCopies(*composite, id, copies.back());
        composite->add(
            std::make_unique<commands::TranslateVectorObjectCommand>(copies.back(), delta));
    }
    if (copies.empty()) {
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = copies});
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 objet(s) dupliqué(s) et déplacé(s).").arg(copies.size()));
}

void MainWindow::offsetVectorObject(ObjectId id) {
    bool ok = false;
    const double offsetMm = QInputDialog::getDouble(
        this, tr("Décaler la forme"),
        tr("Décalage (mm) — positif : agrandit vers l'extérieur ; négatif : rétrécit vers "
           "l'intérieur"),
        2.0, -50.0, 50.0, 2, &ok);
    if (!ok || offsetMm == 0.0) {
        return;
    }
    // inset_path_set : delta > 0 = retrait intérieur -- convention inverse de
    // « positif agrandit » côté utilisateur, d'où le signe opposé ici.
    offsetVectorObjectCore(id, to_micrometers(Millimeters{-offsetMm}));
}

void MainWindow::offsetVectorObjectCore(ObjectId id, Micrometers delta) {
    const auto* source = project_.findObject(id);
    if (source == nullptr) {
        return;
    }
    // Aplatit d'abord les courbes (nœuds Lisses) : Clipper2 (sous
    // inset_path_set) travaille sur des polygones bruts, sans notion de
    // tangente -- même raison qu'à l'export DXF (geometry::flatten est la
    // même fonction, même tolérance).
    static constexpr Micrometers kFlattenTolerance{50};
    const auto flattenToPath = [](const geometry::Path& path) {
        const geometry::Polyline flat = geometry::flatten(path, kFlattenTolerance);
        geometry::Path out;
        out.closed = flat.closed;
        out.nodes.reserve(flat.points.size());
        for (const Vec2um& p : flat.points) {
            out.nodes.push_back(
                geometry::PathNode{p, geometry::NodeType::Corner, std::nullopt, std::nullopt});
        }
        return out;
    };

    document::VectorObject result;
    result.id = project_.object_ids.next();
    result.name = tr("%1 (décalé)").arg(QString::fromStdString(source->name)).toStdString();
    result.rgb = source->rgb;
    for (const auto& set : source->paths) {
        geometry::PathSet flatSet;
        flatSet.outer = flattenToPath(set.outer);
        for (const auto& hole : set.holes) {
            flatSet.holes.push_back(flattenToPath(hole));
        }
        const auto offsetResult = geometry::inset_path_set(flatSet, delta);
        if (!offsetResult) {
            QMessageBox::warning(this, tr("Décalage impossible"),
                                 QString::fromStdString(offsetResult.error().message));
            return;
        }
        for (auto& offsetSet : *offsetResult) {
            result.paths.push_back(std::move(offsetSet));
        }
    }
    if (result.paths.empty()) {
        statusBar()->showMessage(tr("Décalage trop important — la forme disparaît complètement."));
        return;
    }

    const ObjectId newId = result.id;
    std::vector<document::VectorObject> batch;
    batch.push_back(std::move(result));
    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::move(batch), std::vector<document::EmbroideryObject>{}, "Décalage"),
                       project_);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {newId}});
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Forme décalée créée."));
}

QString MainWindow::buildDebugDump(ObjectId embroideryId) const {
    QStringList out;
    const auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr) {
        return tr("Objet de broderie introuvable (id %1).").arg(embroideryId.value);
    }

    out << QStringLiteral("=== OpenStitch — export debug ===")
        << QStringLiteral("Généré : %1")
               .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        << QStringLiteral("Application : %1 %2")
               .arg(QString::fromUtf8(kAppName), QString::fromUtf8(kAppVersion))
        << QString();

    out << QStringLiteral("--- Identité (objet de broderie) ---")
        << QStringLiteral("ObjectId : %1").arg(emb->id.value)
        << QStringLiteral("Nom : \"%1\"").arg(QString::fromStdString(emb->name))
        << QStringLiteral("Type de points : %1")
               .arg(emb->is_auto_satin()    ? QStringLiteral("Satin (squelette)")
                    : emb->is_satin()       ? QStringLiteral("Satin (rails)")
                    : emb->is_tatami()      ? QStringLiteral("Tatami")
                    : emb->is_directional() ? QStringLiteral("Remplissage directionnel")
                                            : QStringLiteral("Contour (running stitch)"))
        << QStringLiteral("Visible : %1").arg(fmtBool(emb->visible))
        << QStringLiteral("Ordre figé : %1").arg(fmtBool(emb->locked))
        << QStringLiteral("Couleur RGB : (%1, %2, %3)  #%4")
               .arg(emb->rgb[0])
               .arg(emb->rgb[1])
               .arg(emb->rgb[2])
               .arg(QStringLiteral("%1%2%3")
                        .arg(emb->rgb[0], 2, 16, QChar('0'))
                        .arg(emb->rgb[1], 2, 16, QChar('0'))
                        .arg(emb->rgb[2], 2, 16, QChar('0'))
                        .toUpper())
        << QStringLiteral("source_vector (ObjectId) : %1").arg(emb->source_vector.value)
        << QString();

    out << QStringLiteral("--- État d'édition (retouches manuelles, Lot 8) ---")
        << QStringLiteral("edited_fingerprint : %1").arg(emb->edited_fingerprint)
        << QStringLiteral("edited_point_count : %1").arg(emb->edited_point_count)
        << QStringLiteral("État dérivé : %1").arg(fmtEditState(editStateOf(emb->id)))
        << QStringLiteral("Retouches (overrides) : %1").arg(emb->overrides.size());
    if (emb->overrides.empty()) {
        out << QStringLiteral("  (aucune)");
    } else {
        for (const auto& ov : emb->overrides) {
            out << QStringLiteral("  base_index=%1  moved_to=%2  forced_type=%3  trim_after=%4")
                       .arg(ov.base_index)
                       .arg(fmtOptVec(ov.moved_to))
                       .arg(ov.forced_type ? fmtStitchPointType(*ov.forced_type)
                                           : QStringLiteral("(absent)"))
                       .arg(fmtBool(ov.trim_after));
        }
    }
    out << QString();

    out << QStringLiteral("--- Paramètres de couture ---");
    std::visit(
        [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, document::RunningStitchParams>) {
                out << QStringLiteral("Variant : RunningStitchParams")
                    << QStringLiteral("stitch_length : %1").arg(fmtUm(p.stitch_length))
                    << QStringLiteral("min_length : %1").arg(fmtUm(p.min_length))
                    << QStringLiteral("repeats : %1").arg(p.repeats);
            } else if constexpr (std::is_same_v<T, document::TatamiParams>) {
                out << QStringLiteral("Variant : TatamiParams")
                    << QStringLiteral("angle : %1 rad (%2°)")
                           .arg(p.angle.radians, 0, 'f', 4)
                           .arg(p.angle.radians * 180.0 / std::numbers::pi, 0, 'f', 2)
                    << QStringLiteral("row_spacing : %1").arg(fmtUm(p.row_spacing))
                    << QStringLiteral("stitch_length : %1").arg(fmtUm(p.stitch_length))
                    << QStringLiteral("inset : %1").arg(fmtUm(p.inset))
                    << QStringLiteral("stagger : %1").arg(p.stagger)
                    << QStringLiteral("underlay_edge : %1").arg(fmtBool(p.underlay_edge))
                    << QStringLiteral("underlay_parallel : %1").arg(fmtBool(p.underlay_parallel))
                    << QStringLiteral("underlay_inset : %1").arg(fmtUm(p.underlay_inset))
                    << QStringLiteral("underlay_spacing : %1").arg(fmtUm(p.underlay_spacing))
                    << QStringLiteral("hidden_underpath : %1").arg(fmtBool(p.hidden_underpath))
                    << QStringLiteral("entry_point : %1").arg(fmtOptVec(p.entry_point));
            } else if constexpr (std::is_same_v<T, document::DirectionalFillParams>) {
                out << QStringLiteral("Variant : DirectionalFillParams")
                    << QStringLiteral("row_spacing : %1").arg(fmtUm(p.row_spacing))
                    << QStringLiteral("stitch_length : %1").arg(fmtUm(p.stitch_length))
                    << QStringLiteral("edge_weight : %1").arg(p.edge_weight, 0, 'f', 2)
                    << QStringLiteral("inset : %1").arg(fmtUm(p.inset))
                    << QStringLiteral("stagger : %1").arg(p.stagger)
                    << QStringLiteral("underlay_edge : %1").arg(fmtBool(p.underlay_edge))
                    << QStringLiteral("underlay_parallel : %1").arg(fmtBool(p.underlay_parallel))
                    << QStringLiteral("underlay_inset : %1").arg(fmtUm(p.underlay_inset))
                    << QStringLiteral("underlay_spacing : %1").arg(fmtUm(p.underlay_spacing))
                    << QStringLiteral("hidden_underpath : %1").arg(fmtBool(p.hidden_underpath))
                    << QStringLiteral("sector_overlap : %1").arg(fmtUm(p.sector_overlap))
                    << QStringLiteral("handmade : %1 (%2 %)")
                           .arg(fmtBool(p.handmade))
                           .arg(p.handmade_intensity)
                    << QStringLiteral("seed : %1").arg(p.seed) << QString();
                for (std::size_t i = 0; i < p.guides.size(); ++i) {
                    dumpPath(out, p.guides[i], QStringLiteral("guide[%1]").arg(i));
                }
                for (std::size_t i = 0; i < p.break_lines.size(); ++i) {
                    dumpPath(out, p.break_lines[i], QStringLiteral("break_line[%1]").arg(i));
                }
            } else if constexpr (std::is_same_v<T, document::AutoSatinParams>) {
                out << QStringLiteral("Variant : AutoSatinParams")
                    << QStringLiteral("spacing : %1").arg(fmtUm(p.spacing))
                    << QStringLiteral("split_stitch : %1").arg(fmtSplit(p.split_stitch))
                    << QStringLiteral("split_threshold (Lmax) : %1").arg(fmtUm(p.split_threshold))
                    << QStringLiteral("split_length (y) : %1").arg(fmtUm(p.split_length))
                    << QStringLiteral("short_stitch : %1").arg(fmtShortStitch(p.short_stitch))
                    << QStringLiteral("pull_compensation : %1").arg(fmtUm(p.pull_compensation))
                    << QStringLiteral("center_underlay : %1").arg(fmtBool(p.center_underlay))
                    << QStringLiteral("underlay_edge : %1").arg(fmtBool(p.underlay_edge))
                    << QStringLiteral("underlay_zigzag : %1").arg(fmtBool(p.underlay_zigzag))
                    << QStringLiteral("pull_left : %1").arg(fmtUm(p.pull_left))
                    << QStringLiteral("pull_right : %1").arg(fmtUm(p.pull_right))
                    << QStringLiteral("push_start : %1").arg(fmtUm(p.push_start))
                    << QStringLiteral("push_end : %1").arg(fmtUm(p.push_end))
                    << QStringLiteral("cap_start : %1").arg(fmtCap(p.cap_start))
                    << QStringLiteral("cap_end : %1").arg(fmtCap(p.cap_end))
                    << QStringLiteral("lock_start : %1").arg(fmtLock(p.lock_start))
                    << QStringLiteral("lock_end : %1").arg(fmtLock(p.lock_end))
                    << QStringLiteral("guides : %1").arg(p.guides.size());
                for (std::size_t i = 0; i < p.guides.size(); ++i) {
                    const auto& g = p.guides[i];
                    out << QStringLiteral("guide[%1] : ancre (%2, %3) µm, angle %4 rad (%5)")
                               .arg(i)
                               .arg(g.anchor.x.value)
                               .arg(g.anchor.y.value)
                               .arg(g.angle.radians)
                               .arg(g.absolute ? QStringLiteral("absolu")
                                               : QStringLiteral("relatif"));
                }
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                out << QStringLiteral("Variant : SatinParams")
                    << QStringLiteral("density : %1").arg(fmtUm(p.density))
                    << QStringLiteral("pull_compensation : %1").arg(fmtUm(p.pull_compensation))
                    << QStringLiteral("center_underlay : %1").arg(fmtBool(p.center_underlay))
                    << QStringLiteral("max_width : %1").arg(fmtUm(p.max_width))
                    << QStringLiteral("max_width_hard : %1").arg(fmtUm(p.max_width_hard))
                    << QStringLiteral("short_stitch : %1").arg(fmtShortStitch(p.short_stitch))
                    << QStringLiteral("split_stitch : %1").arg(fmtSplit(p.split_stitch))
                    << QStringLiteral("cap_start : %1").arg(fmtCap(p.cap_start))
                    << QStringLiteral("cap_end : %1").arg(fmtCap(p.cap_end))
                    << QStringLiteral("max_stitch_length : %1").arg(fmtUm(p.max_stitch_length))
                    << QStringLiteral("underlay_edge : %1").arg(fmtBool(p.underlay_edge))
                    << QStringLiteral("underlay_zigzag : %1").arg(fmtBool(p.underlay_zigzag))
                    << QStringLiteral("pull_left : %1").arg(fmtUm(p.pull_left))
                    << QStringLiteral("pull_right : %1").arg(fmtUm(p.pull_right))
                    << QStringLiteral("push_start : %1").arg(fmtUm(p.push_start))
                    << QStringLiteral("push_end : %1").arg(fmtUm(p.push_end))
                    << QStringLiteral("lock_start : %1").arg(fmtLock(p.lock_start))
                    << QStringLiteral("lock_end : %1").arg(fmtLock(p.lock_end))
                    << QStringLiteral("lock_length : %1").arg(fmtUm(p.lock_length))
                    << QStringLiteral("lock_passes : %1").arg(p.lock_passes)
                    << QStringLiteral("entry_point : %1").arg(fmtOptVec(p.entry_point))
                    << QStringLiteral("exit_point : %1").arg(fmtOptVec(p.exit_point)) << QString();

                out << QStringLiteral("--- Rails et barreaux satin ---");
                dumpPath(out, p.rail_a, QStringLiteral("rail_a"));
                dumpPath(out, p.rail_b, QStringLiteral("rail_b"));
                out << QStringLiteral("rungs (%1) :").arg(p.rungs.size());
                if (p.rungs.empty()) {
                    out << QStringLiteral("  (aucun — fallback fill_satin sans barreaux)");
                } else {
                    for (std::size_t i = 0; i < p.rungs.size(); ++i) {
                        const auto& r = p.rungs[i];
                        out << QStringLiteral("  [%1] a=%2  b=%3  link_id=%4")
                                   .arg(i)
                                   .arg(fmtVec(r.a), fmtVec(r.b),
                                        r.link_id ? QString::number(*r.link_id)
                                                  : QStringLiteral("(absent)"));
                    }
                }
                if (p.topology) {
                    out << QStringLiteral("topology : section_index=%1  section_count=%2  "
                                          "start_junction=%3  end_junction=%4")
                               .arg(p.topology->section_index)
                               .arg(p.topology->section_count)
                               .arg(p.topology->start_junction
                                        ? QString::number(*p.topology->start_junction)
                                        : QStringLiteral("(absent)"))
                               .arg(p.topology->end_junction
                                        ? QString::number(*p.topology->end_junction)
                                        : QStringLiteral("(absent)"));
                } else {
                    out << QStringLiteral("topology : (absent — colonne indépendante/legacy)");
                }
            }
        },
        emb->params);
    out << QString();

    out << QStringLiteral("--- Objet vectoriel source ---");
    if (const auto* vec = project_.findObject(emb->source_vector)) {
        out << QStringLiteral("ObjectId : %1").arg(vec->id.value)
            << QStringLiteral("Nom : \"%1\"").arg(QString::fromStdString(vec->name))
            << QStringLiteral("source_region : %1")
                   .arg(vec->source_region ? QString::number(vec->source_region->value)
                                           : QStringLiteral("(absent)"))
            << QStringLiteral("Couleur RGB : (%1, %2, %3)")
                   .arg(vec->rgb[0])
                   .arg(vec->rgb[1])
                   .arg(vec->rgb[2])
            << QStringLiteral("Visible : %1").arg(fmtBool(vec->visible))
            << QStringLiteral("Nombre de morceaux (paths) : %1").arg(vec->paths.size());
        for (std::size_t s = 0; s < vec->paths.size(); ++s) {
            const auto& set = vec->paths[s];
            out << QStringLiteral(" Morceau %1 :").arg(s);
            dumpPath(out, set.outer, QStringLiteral("contour extérieur"));
            for (std::size_t h = 0; h < set.holes.size(); ++h) {
                dumpPath(out, set.holes[h], QStringLiteral("trou %1").arg(h));
            }
        }
    } else {
        out << QStringLiteral("(introuvable — ObjectId %1)").arg(emb->source_vector.value);
    }
    out << QString();

    out << QStringLiteral("--- Séquence générée (portion de cet objet) ---");
    if (!sequence_) {
        out << QStringLiteral("Aucune séquence générée pour l'instant (document sans image/objets, "
                              "ou génération en échec).");
    } else {
        const auto raw = stitch_generation::raw_slice(*sequence_, emb->id);
        out << QStringLiteral("Total commandes (raw_slice) : %1").arg(raw.size());
        if (raw.empty()) {
            out << QStringLiteral(
                "  (cet objet ne produit aucune commande — vérifier params/rails)");
        } else {
            const stitch::StitchSequence subSeq{raw};
            const auto stats = stitch::compute_stats(subSeq);
            out << QStringLiteral("  Stitch=%1  Jump=%2  Trim=%3  ColorChange=%4")
                       .arg(stats.stitches)
                       .arg(stats.jumps)
                       .arg(stats.trims)
                       .arg(stats.color_changes)
                << QStringLiteral("  Longueur de fil cousu : %1 mm")
                       .arg(stats.thread_length_um / 1000.0, 0, 'f', 2)
                << QStringLiteral("  Boîte englobante : min=%1  max=%2")
                       .arg(fmtVec(stats.bounds.min), fmtVec(stats.bounds.max));

            std::array<std::size_t, 5> perPass{}; // Underlay,TopStitch,Travel,Lock,Manual
            for (const auto& cmd : raw) {
                ++perPass[static_cast<std::size_t>(cmd.pass)];
            }
            out << QStringLiteral("  Par passe : Underlay=%1  TopStitch=%2  Travel=%3  Lock=%4  "
                                  "Manual=%5")
                       .arg(perPass[0])
                       .arg(perPass[1])
                       .arg(perPass[2])
                       .arg(perPass[3])
                       .arg(perPass[4]);

            out << QStringLiteral("Détail des commandes :");
            for (std::size_t i = 0; i < raw.size(); ++i) {
                const auto& cmd = raw[i];
                out << QStringLiteral("  [%1] pass=%2  type=%3  pos=%4")
                           .arg(i)
                           .arg(fmtPass(cmd.pass), fmtCommandType(cmd.type), fmtVec(cmd.pos));
            }
        }
    }
    out << QString();

    out << QStringLiteral("--- Analyse (stitch_analysis) ---");
    if (!sequence_) {
        out << QStringLiteral("(aucune séquence à analyser)");
    } else {
        const auto findings = stitch_analysis::analyze_project(project_, *sequence_).findings;
        std::size_t count = 0;
        for (const auto& f : findings) {
            if (f.object != emb->id) {
                continue;
            }
            ++count;
            out << QStringLiteral("  [%1] %2 : %3  (%4)")
                       .arg(fmtSeverity(f.severity), QString::fromStdString(f.category),
                            QString::fromStdString(f.message), fmtVec(f.location));
        }
        if (count == 0) {
            out << QStringLiteral("  Aucun problème détecté pour cet objet.");
        }
    }

    return out.join(QChar('\n'));
}

void MainWindow::showDebugDump(ObjectId embroideryId) {
    const QString text = buildDebugDump(embroideryId);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Données de débogage — objet %1").arg(embroideryId.value));
    dialog.resize(760, 640);
    auto* layout = new QVBoxLayout(&dialog);

    auto* editor = new QPlainTextEdit(&dialog);
    editor->setReadOnly(true);
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    editor->setFont(mono);
    editor->setPlainText(text);
    layout->addWidget(editor);

    auto* buttons = new QDialogButtonBox(&dialog);
    auto* copyBtn = buttons->addButton(tr("Copier"), QDialogButtonBox::ActionRole);
    auto* saveBtn = buttons->addButton(tr("Enregistrer sous…"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(copyBtn, &QPushButton::clicked, &dialog,
            [text] { QGuiApplication::clipboard()->setText(text); });
    connect(saveBtn, &QPushButton::clicked, &dialog, [this, &dialog, text] {
        const QString path =
            QFileDialog::getSaveFileName(&dialog, tr("Enregistrer les données de débogage"),
                                         QStringLiteral("debug.txt"), tr("Texte (*.txt)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(text.toUtf8());
        } else {
            QMessageBox::warning(this, tr("Écriture impossible"),
                                 tr("Le fichier n'a pas pu être écrit. Vérifiez le dossier "
                                    "choisi et vos droits d'écriture."));
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

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

void MainWindow::buildMainToolbar() {
    mainToolbar_ = addToolBar(tr("Barre principale"));
    mainToolbar_->setObjectName(QStringLiteral("mainToolbar"));
    mainToolbar_->setMovable(false);
    mainToolbar_->setIconSize(QSize(18, 18));

    // Les mêmes QAction que dans les menus : grisées, raccourcies et décrites de la même façon.
    const auto addShared = [this](QAction* act, const QIcon& icon) {
        act->setIcon(icon);
        QString tip = act->text();
        tip.remove(QLatin1Char('&'));
        tip.remove(QStringLiteral("…"));
        if (!act->shortcut().isEmpty()) {
            tip += QStringLiteral(" (%1)").arg(act->shortcut().toString(QKeySequence::NativeText));
        }
        act->setToolTip(tip);
        mainToolbar_->addAction(act);
    };
    addShared(newProjectAct_, icons::newProject());
    addShared(openImageAct_, icons::openImage());
    addShared(loadProjectAct_, icons::openProject());
    addShared(saveProjectAct_, icons::save());
    mainToolbar_->addSeparator();
    undoAct_->setIcon(icons::undo());
    redoAct_->setIcon(icons::redo());
    mainToolbar_->addAction(undoAct_);
    mainToolbar_->addAction(redoAct_);
    mainToolbar_->addSeparator();
    addShared(zoomOutAct_, icons::zoomOut());
    addShared(fitCanvasAct_, icons::fit());
    addShared(zoomInAct_, icons::zoomIn());
    mainToolbar_->addSeparator();
    addShared(analyzeAct_, icons::analyze());
    showStitchesAct_->setIcon(icons::stitches());
    mainToolbar_->addAction(showStitchesAct_);
    addShared(exportDstAct_, icons::exportDst());
    // Action principale de la fenêtre : bouton plein, en texte (l'icône grise n'aurait pas le
    // contraste voulu sur le fond d'accent).
    if (auto* exportButton =
            qobject_cast<QToolButton*>(mainToolbar_->widgetForAction(exportDstAct_))) {
        exportButton->setProperty("primary", true);
        exportButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
}

void MainWindow::buildContextToolbar() {
    contextToolbar_ = addToolBar(tr("Barre contextuelle"));
    contextToolbar_->setObjectName(QStringLiteral("contextToolbar"));
    contextToolbar_->setMovable(false);
    updateContextToolbar();
}

document::EmbroideryObject* MainWindow::resolveSelectedEmbroidery() {
    document::EmbroideryObject* emb = nullptr;
    if (selectedEmbroidery_) {
        emb = project_.findEmbroidery(*selectedEmbroidery_);
    }
    if (emb == nullptr && selectedObject_ && !hasMultiSelection()) {
        emb = embroideryForVector(*selectedObject_);
    }
    return emb;
}

stitch_generation::ObjectEditState MainWindow::editStateOf(ObjectId id) const {
    for (const auto& [candidate, state] : editStates_) {
        if (candidate == id) {
            return state;
        }
    }
    return stitch_generation::ObjectEditState::Clean; // absent du cache = Clean (jamais retouché)
}

void MainWindow::updateContextToolbar() {
    if (contextToolbar_ == nullptr) {
        return;
    }
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();
    const bool hasMulti = hasMultiSelection();
    const bool hasVec = selectedObject_.has_value() && !hasMulti;
    const bool hasReg = selectedRegion_ && project_.segmentation;
    const int pts = sequence_ ? static_cast<int>(sequence_->commands.size()) : 0;

    // Signature de l'état : évite de reconstruire (et de faire clignoter) la barre
    // quand rien de pertinent n'a changé.
    QString sig;
    if (emb != nullptr) {
        // Etat retouche (Lot 8.2) et mode d'edition inclus dans la signature :
        // sans eux, la barre ne se reconstruirait pas quand seul l'un des deux
        // change (meme objet/type toujours selectionne).
        sig =
            QStringLiteral("E%1t%2s%3m%4g%5i%6r%7")
                .arg(emb->id.value)
                .arg(stitchTypeIndex(*emb))
                .arg(static_cast<int>(editStateOf(emb->id)))
                .arg(stitchEditModeAct_->isChecked() ? 1 : 0)
                .arg(satinGuideModeAct_->isChecked() ? 1 : 0)
                .arg(selectedSatinGuide_ ? static_cast<qlonglong>(*selectedSatinGuide_) : -1)
                .arg(railEditModeAct_->isChecked() ? 1 : 0) +
            QStringLiteral("d%1g%2")
                .arg(directionGuideModeAct_->isChecked() ? 1 : 0)
                .arg(gradientEditModeAct_ != nullptr && gradientEditModeAct_->isChecked() ? 1 : 0);
    } else if (hasMulti) {
        sig = QStringLiteral("M%1").arg(multiSelection_.size());
    } else if (hasVec) {
        sig = QStringLiteral("V%1").arg(selectedObject_->value);
    } else if (hasReg) {
        sig = QStringLiteral("R%1").arg(selectedRegion_->value);
    } else {
        sig = QStringLiteral("N%1_%2x%3")
                  .arg(pts)
                  .arg(project_.canvas.width.value)
                  .arg(project_.canvas.height.value);
    }
    if (sig == contextSig_) {
        return;
    }
    contextSig_ = sig;
    // QToolBar::clear() ne détruit ni les actions ni les widgets créés par addAction(texte)
    // / addWidget() : ils s'accumulaient comme enfants de la barre à chaque reconstruction.
    // On détruit ceux que la barre possède ; les actions persistantes (membres *_Act_,
    // parentées à la fenêtre) sont seulement retirées. Détachés tout de suite (le compte
    // d'enfants reste stable) mais détruits en différé : cette méthode peut être appelée
    // depuis le slot d'un des boutons qu'on retire.
    const QList<QAction*> previous = contextToolbar_->actions();
    contextToolbar_->clear();
    for (QAction* old : previous) {
        if (old->parent() != contextToolbar_) {
            continue;
        }
        if (auto* widgetAct = qobject_cast<QWidgetAction*>(old)) {
            if (QWidget* w = widgetAct->defaultWidget()) {
                w->hide();
                w->setParent(nullptr);
                w->deleteLater();
            }
        }
        old->setParent(nullptr);
        old->deleteLater();
    }
    // Le layout de la barre crée un QToolButton par action simple et ne le détruit qu'en
    // différé (deleteLater) : tant que la boucle d'évènements n'a pas tourné, ils restent
    // enfants de la barre. On les détache tout de suite (leur destruction différée suit son
    // cours) ; le bouton d'extension (« >> ») est permanent et reste en place. Dépend d'un
    // détail interne de Qt : l'objectName "qt_toolbar_ext_button" du QToolBarExtension
    // (stable de Qt 5 à 6.8) ; s'il changeait, le bouton serait détaché puis détruit, et la
    // barre n'afficherait plus le menu de débordement (sans plantage).
    for (auto* stale : contextToolbar_->findChildren<QToolButton*>(Qt::FindDirectChildrenOnly)) {
        if (stale->objectName() != QLatin1String("qt_toolbar_ext_button")) {
            stale->hide();
            stale->setParent(nullptr);
            stale->deleteLater();
        }
    }

    if (emb != nullptr) {
        const ObjectId id = emb->id;
        contextToolbar_->addWidget(new ElidedTextLabel(tr("Type de points :"), contextToolbar_));
        const int current = stitchTypeIndex(*emb);
        const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin"), tr("Directionnel")};
        for (int t = 0; t < 4; ++t) {
            auto* btn = new QToolButton(contextToolbar_);
            btn->setText(names[t]);
            btn->setAccessibleName(tr("Type de points : %1").arg(names[t]));
            btn->setCheckable(true);
            btn->setAutoExclusive(true); // un seul type actif : annoncé comme un groupe
            btn->setChecked(t == current);
            connect(btn, &QToolButton::clicked, this, [this, id, t] { setStitchType(id, t); });
            contextToolbar_->addWidget(btn);
        }
        // Types sans bouton direct : un menu « Plus » (concentrique, radial, spirale, croisé,
        // motifs).
        {
            auto* more = new QToolButton(contextToolbar_);
            more->setText(tr("Plus ▾"));
            more->setObjectName(QStringLiteral("button_contextStitchTypeMore"));
            more->setAccessibleName(tr("Autres types de points"));
            more->setPopupMode(QToolButton::InstantPopup);
            auto* moreMenu = new QMenu(more);
            for (int t = kStitchTypeDirectButtons; t < kStitchTypeCount; ++t) {
                auto* act = moreMenu->addAction(stitchTypeLabel(t));
                act->setCheckable(true);
                act->setChecked(t == current);
                connect(act, &QAction::triggered, this, [this, id, t] { setStitchType(id, t); });
            }
            more->setMenu(moreMenu);
            contextToolbar_->addWidget(more);
        }
        if (emb->is_tatami()) {
            contextToolbar_->addSeparator();
            auto* rot = contextToolbar_->addAction(tr("Orientation…"));
            connect(rot, &QAction::triggered, this, &MainWindow::changeFillAngle);
            contextToolbar_->addAction(autoDirectionGuideAct_);
            contextToolbar_->addAction(twoColorBlendAct_);
            contextToolbar_->addAction(gradientEditModeAct_);
        }
        if (emb->is_directional()) {
            contextToolbar_->addSeparator();
            contextToolbar_->addAction(autoDirectionGuideAct_);
            contextToolbar_->addAction(twoColorBlendAct_);
            contextToolbar_->addAction(gradientEditModeAct_);
            contextToolbar_->addAction(directionGuideModeAct_);
            if (directionGuideModeAct_->isChecked()) {
                contextToolbar_->addAction(drawDirectionGuideAct_);
                contextToolbar_->addAction(drawBreakLineAct_);
            }
        }
        contextToolbar_->addSeparator();
        contextToolbar_->addAction(stitchEditModeAct_);
        if (emb->is_satin()) {
            // Un seul bouton visible par défaut (rails + guides ensemble) ;
            // le sous-menu Broderie ▸ Réglages fins du satin reste
            // disponible pour n'afficher qu'un seul des deux aspects.
            contextToolbar_->addAction(satinEditModeAct_);
            if (satinGuideModeAct_->isChecked()) {
                contextToolbar_->addAction(addSatinGuideAct_);
                contextToolbar_->addAction(removeSatinGuideAct_);
            }
        }
        const auto editState = editStateOf(id);
        if (editState == stitch_generation::ObjectEditState::ManuallyEdited) {
            contextToolbar_->addWidget(new ElidedTextLabel(tr("✎ retouché"), contextToolbar_));
        } else if (editState == stitch_generation::ObjectEditState::Dirty) {
            auto* warn = new ElidedTextLabel(tr("⚠ retouches obsolètes"), contextToolbar_);
            warn->setToolTip(tr("La géométrie ou l'ordre de couture a changé depuis les dernières "
                                "retouches : elles ne sont plus appliquées."));
            contextToolbar_->addWidget(warn);
        }
        if (editState != stitch_generation::ObjectEditState::Clean) {
            auto* discard = contextToolbar_->addAction(tr("Abandonner les retouches"));
            connect(discard, &QAction::triggered, this, [this, id] { discardOverrides(id); });
        }
    } else if (hasMulti || hasVec) {
        // HP-UX-028 : les mêmes quatre boutons « Type de points » pour une ou N formes ; un clic
        // crée (ou convertit) la couture au dernier réglage, en un pas d'annulation, sans dialogue.
        if (hasMulti) {
            contextToolbar_->addWidget(new ElidedTextLabel(
                tr("%1 objets sélectionnés · type de points :").arg(multiSelection_.size()),
                contextToolbar_));
        } else {
            contextToolbar_->addWidget(
                new ElidedTextLabel(tr("Objet vectoriel · type de points :"), contextToolbar_));
        }
        const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin"), tr("Directionnel")};
        for (int t = 0; t < 4; ++t) {
            auto* btn = new QToolButton(contextToolbar_);
            btn->setText(names[t]);
            btn->setObjectName(QStringLiteral("button_contextStitchType%1").arg(t));
            btn->setAccessibleName(tr("Type de points : %1").arg(names[t]));
            connect(btn, &QToolButton::clicked, this, [this, t] { setStitchTypeForSelection(t); });
            contextToolbar_->addWidget(btn);
        }
        // Types sans bouton direct : un menu « Plus » (concentrique, radial, spirale, croisé,
        // motifs).
        {
            auto* more = new QToolButton(contextToolbar_);
            more->setText(tr("Plus ▾"));
            more->setObjectName(QStringLiteral("button_contextStitchTypeMore"));
            more->setAccessibleName(tr("Autres types de points"));
            more->setPopupMode(QToolButton::InstantPopup);
            auto* moreMenu = new QMenu(more);
            for (int t = kStitchTypeDirectButtons; t < kStitchTypeCount; ++t) {
                auto* act = moreMenu->addAction(stitchTypeLabel(t));
                act->setCheckable(true);
                act->setChecked(false);
                connect(act, &QAction::triggered, this,
                        [this, t] { setStitchTypeForSelection(t); });
            }
            more->setMenu(moreMenu);
            contextToolbar_->addWidget(more);
        }
    } else if (hasReg) {
        const auto* region = project_.segmentation->find(*selectedRegion_);
        if (region != nullptr) {
            const double mmPerPx = project_.mm_per_px.value;
            const double areaMm2 = region->pixel_count * mmPerPx * mmPerPx;
            const std::vector<RegionId> picked = selectedRegionIds();
            QString label = tr("Région %1 · %2 mm²").arg(region->id.value).arg(areaMm2, 0, 'f', 1);
            if (picked.size() > 1) {
                std::size_t pixels = 0;
                for (const RegionId r : picked) {
                    if (const auto* rr = project_.segmentation->find(r)) {
                        pixels += rr->pixel_count;
                    }
                }
                label = tr("%1 régions · %2 mm²")
                            .arg(picked.size())
                            .arg(static_cast<double>(pixels) * mmPerPx * mmPerPx, 0, 'f', 1);
            }
            contextToolbar_->addWidget(new ElidedTextLabel(label, contextToolbar_));
        }
        contextToolbar_->addAction(mergeSelectionAct_); // Fusionner la sélection (>= 2 régions)
        contextToolbar_->addAction(mergeAct_);          // Fusionner avec… (mode)
        auto* recolorButton = contextToolbar_->addAction(tr("Recolorer…"));
        connect(recolorButton, &QAction::triggered, this, &MainWindow::recolorSelectedRegion);
        // Les actions du menu elles-mêmes (libellé, raccourci et état identiques partout).
        contextToolbar_->addAction(deleteSelectionAct_);
        contextToolbar_->addAction(vectorizeRegionAct_);
    } else {
        if (sequence_) {
            const auto st = stitch::compute_stats(*sequence_);
            const double w = to_millimeters(st.bounds.max.x - st.bounds.min.x).value;
            const double h = to_millimeters(st.bounds.max.y - st.bounds.min.y).value;
            contextToolbar_->addWidget(new ElidedTextLabel(
                tr("Motif : %1 × %2 mm · %3 points · %4 changement(s) de couleur")
                    .arg(w, 0, 'f', 1)
                    .arg(h, 0, 'f', 1)
                    .arg(st.stitches)
                    .arg(st.color_changes),
                contextToolbar_));
        } else {
            contextToolbar_->addWidget(new ElidedTextLabel(
                tr("Ouvrez une image, ou sélectionnez une région ou un objet."), contextToolbar_));
        }
        auto* hoop = contextToolbar_->addAction(
            tr("Cadre : %1 × %2 mm…")
                .arg(to_millimeters(project_.canvas.width).value, 0, 'f', 0)
                .arg(to_millimeters(project_.canvas.height).value, 0, 'f', 0));
        connect(hoop, &QAction::triggered, this, &MainWindow::setHoopSize);
    }
}

void MainWindow::buildToolPalette() {
    toolPalette_ = new QToolBar(tr("Outils"), this);
    toolPalette_->setObjectName(QStringLiteral("toolPalette"));
    toolPalette_->setMovable(false);
    toolPalette_->setIconSize(QSize(20, 20));
    addToolBar(Qt::LeftToolBarArea, toolPalette_);

    auto* group = new QActionGroup(this);
    const auto addTool = [&](const QIcon& icon, const QString& text, Tool tool,
                             const QKeySequence& key) {
        auto* act = toolPalette_->addAction(icon, text);
        act->setCheckable(true);
        // « Nom (aide) » devient « Nom (touche) » puis l'aide sur la ligne suivante.
        const int paren = text.indexOf(QStringLiteral(" ("));
        if (paren > 0 && text.endsWith(QLatin1Char(')'))) {
            const QString shortName = text.left(paren);
            const QString help = text.mid(paren + 2, text.size() - paren - 3);
            act->setToolTip(tr("%1 (%2)\n%3").arg(shortName, key.toString(), help));
        } else {
            act->setToolTip(tr("%1 (%2)").arg(text, key.toString()));
        }
        act->setShortcut(key);
        group->addAction(act);
        connect(act, &QAction::triggered, this, [this, tool] { setTool(tool); });
        return act;
    };
    toolSelectAct_ =
        addTool(icons::select(), tr("Sélection"), Tool::Select, QKeySequence(Qt::Key_V));
    toolPanAct_ = addTool(icons::pan(), tr("Déplacer la vue"), Tool::Pan, QKeySequence(Qt::Key_H));
    toolRectAct_ = addTool(icons::rect(), tr("Recadrer l'image (glisser un cadre)"), Tool::Rect,
                           QKeySequence(Qt::Key_M));
    toolDrawRectAct_ = addTool(icons::drawRect(), tr("Dessiner un rectangle"), Tool::DrawRectangle,
                               QKeySequence(Qt::Key_R));
    toolDrawEllipseAct_ = addTool(icons::ellipse(), tr("Dessiner une ellipse (Maj = cercle)"),
                                  Tool::DrawEllipse, QKeySequence(Qt::Key_O));
    toolDrawPolygonAct_ = addTool(icons::polygon(), tr("Dessiner un polygone (segments droits)"),
                                  Tool::DrawPolygon, QKeySequence(Qt::Key_P));
    toolDrawPolygonRegularAct_ =
        addTool(icons::regularPolygon(), tr("Dessiner un polygone régulier (glisser un cadre)"),
                Tool::DrawPolygonRegular, QKeySequence(Qt::Key_G));
    polygonSidesSpin_ = new QSpinBox(toolPalette_);
    polygonSidesSpin_->setRange(3, 12);
    polygonSidesSpin_->setValue(6);
    polygonSidesSpin_->setToolTip(
        tr("Nombre de côtés du polygone régulier (molette ou flèches ↑ ↓)"));
    polygonSidesSpin_->setMaximumWidth(48);
    polygonSidesSpin_->setAlignment(Qt::AlignCenter);
    polygonSidesSpin_->setButtonSymbols(
        QAbstractSpinBox::NoButtons); // trop étroit pour les flèches
    toolPalette_->addWidget(polygonSidesSpin_);
    toolDrawBezierAct_ =
        addTool(icons::bezierCurve(),
                tr("Dessiner une courbe de Bézier (clic = coin, clic-glisser = nœud lisse)"),
                Tool::DrawBezier, QKeySequence(Qt::Key_B));
    openPathAct_ = toolPalette_->addAction(icons::openPath(), tr("Tracé ouvert"));
    openPathAct_->setObjectName(QStringLiteral("action_openPath"));
    openPathAct_->setCheckable(true);
    openPathAct_->setShortcut(QKeySequence(Qt::Key_U));
    openPathAct_->setToolTip(
        tr("Tracé ouvert (U)\nCoché : le polygone (P) et la courbe de Bézier (B) se terminent sans "
           "être fermés — une ligne ou une courbe ouverte, pour un point de contour ou un satin de "
           "bordure. 2 points suffisent."));
    connect(openPathAct_, &QAction::toggled, this, [this](bool open) {
        statusBar()->showMessage(open ? tr("Tracé ouvert : les prochains polygones et courbes "
                                           "ne seront pas fermés.")
                                      : tr("Tracé fermé : les prochains polygones et courbes "
                                           "seront fermés."),
                                 6000);
    });
    // Outil « Ligne » (HP-VEC-002) : raccourci vers le polygone en mode tracé ouvert (clics,
    // Entrée ou double-clic pour terminer sans fermer).
    auto* lineToolAct = toolPalette_->addAction(icons::openPath(), tr("Ligne"));
    lineToolAct->setObjectName(QStringLiteral("action_lineTool"));
    lineToolAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_L));
    lineToolAct->setToolTip(tr("Ligne (Maj+L)\nClics successifs, Entrée pour terminer SANS fermer "
                               "(ligne ou polyligne ouverte). Active le tracé ouvert."));
    connect(lineToolAct, &QAction::triggered, this, [this] {
        openPathAct_->setChecked(true);
        setTool(Tool::DrawPolygon);
    });
    toolDrawFreeformAct_ = addTool(icons::freeform(), tr("Dessiner à main levée (lasso)"),
                                   Tool::DrawFreeform, QKeySequence(Qt::Key_L));
    toolDrawSatinColumnAct_ =
        addTool(icons::satinColumn(), tr("Colonne satin (clics alternés côté A / côté B)"),
                Tool::DrawSatinColumn, QKeySequence(Qt::Key_S));
    toolCutAct_ = addTool(
        icons::knife(), tr("Couteau (tracez une ligne pour découper les formes qu'elle traverse)"),
        Tool::Cut, QKeySequence(Qt::Key_K));
    if (shapeMenu_ != nullptr) {
        shapeMenu_->addAction(toolCutAct_);
    }
    attachMeasureEyedropperTools();
    attachBrushTool();
    attachBucketTool();
    attachClickToStitchTool();
    attachDetailZonesTool();
    toolSelectAct_->setChecked(true);

    toolPalette_->addSeparator();
    // Boutons génériques Terminer/Annuler : le double-clic reste disponible
    // pour polygone/bézier (Entrée aussi, cf. plus bas), mais un moyen visible
    // et cliquable de valider une forme manquait — défaut remonté en usage
    // réel (le double-clic seul n'est pas découvrable).
    finishDrawAct_ = toolPalette_->addAction(icons::checkmark(), tr("Terminer la forme (Entrée)"));
    finishDrawAct_->setToolTip(tr("Terminer la forme en cours (Entrée)"));
    finishDrawAct_->setEnabled(false);
    connect(finishDrawAct_, &QAction::triggered, this, [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            finishPolygon();
        } else if (currentTool_ == Tool::DrawBezier) {
            finishBezier();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            finishSatinColumn();
        } else if (drawingDirectionGuide()) {
            finishDirectionGuide();
        }
    });
    cancelDrawAct_ = toolPalette_->addAction(icons::cancelDraw(), tr("Annuler le tracé (Échap)"));
    cancelDrawAct_->setToolTip(tr("Annuler le tracé en cours (Échap)"));
    cancelDrawAct_->setEnabled(false);
    connect(cancelDrawAct_, &QAction::triggered, this, [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            cancelPolygonDraw();
        } else if (currentTool_ == Tool::DrawBezier) {
            cancelBezierDraw();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            cancelSatinColumnDraw();
        } else if (drawingDirectionGuide()) {
            cancelDirectionGuideDraw();
        }
    });

    // Échap : revient à la Sélection, annule le mode fusion en cours et
    // quitte le mode d'édition des points (Lot 8.2) s'il est actif -- pas
    // d'annulation du glisser en cours (l'architecture ne le permet pas
    // proprement : les poignées sont détruites/reconstruites à chaque
    // rafraîchissement, cf. renderBase), seulement une sortie propre du mode.
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut_ = escape;
    connect(escape, &QShortcut::activated, this, [this] {
        // Rien à annuler (aucun mode, outil de sélection) : Échap désélectionne les régions.
        const bool nothingToCancel =
            !mergeAct_->isChecked() && !stitchEditModeAct_->isChecked() &&
            !satinEditModeAct_->isChecked() && !satinGuideModeAct_->isChecked() &&
            !railEditModeAct_->isChecked() && !directionGuideModeAct_->isChecked() &&
            currentTool_ == Tool::Select;
        if (nothingToCancel && !selectedRegionIds().empty()) {
            selectRegions({}, SelectMode::Replace);
            return;
        }
        // Rien d'autre à relâcher : Échap désélectionne aussi les objets (HP-UX-018), sans
        // avoir à cliquer dans un « vide » souvent rempli de formes.
        if (nothingToCancel) {
            const Selection current = currentSelection();
            if (current.embroidery.has_value() || !current.objects.empty()) {
                setSelection({});
                displayImage(processed_);
                updateActions();
                return;
            }
        }
        if (mergeAct_->isChecked()) {
            mergeAct_->setChecked(false);
        }
        if (stitchEditModeAct_->isChecked()) {
            stitchEditModeAct_->setChecked(false);
        }
        if (satinEditModeAct_->isChecked()) {
            satinEditModeAct_->setChecked(false);
        }
        if (satinGuideModeAct_->isChecked()) {
            satinGuideModeAct_->setChecked(false);
        }
        if (railEditModeAct_->isChecked()) {
            railEditModeAct_->setChecked(false);
        }
        if (directionGuideModeAct_->isChecked()) {
            directionGuideModeAct_->setChecked(false);
        }
        setTool(Tool::Select);
    });

    // Terminaison/retrait de point, communs à tout outil de tracé multi-clics
    // (polygone/bézier/satin) : Entrée/Retour termine (comme un double-clic
    // ou le bouton Terminer ci-dessus), Retour arrière retire le dernier
    // point posé. Sans effet hors de ces outils.
    const auto finishDrawShortcut = [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            finishPolygon();
        } else if (currentTool_ == Tool::DrawBezier) {
            finishBezier();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            finishSatinColumn();
        } else if (drawingDirectionGuide()) {
            finishDirectionGuide();
        }
    };
    auto* drawFinishReturn = new QShortcut(QKeySequence(Qt::Key_Return), this);
    drawReturnShortcut_ = drawFinishReturn;
    connect(drawFinishReturn, &QShortcut::activated, this, finishDrawShortcut);
    auto* drawFinishEnter = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    drawEnterShortcut_ = drawFinishEnter;
    connect(drawFinishEnter, &QShortcut::activated, this, finishDrawShortcut);
    auto* drawBackspace = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    drawBackspaceShortcut_ = drawBackspace;
    connect(drawBackspace, &QShortcut::activated, this, [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            removeLastPolygonVertex();
        } else if (currentTool_ == Tool::DrawBezier) {
            removeLastBezierPoint();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            removeLastSatinColumnPoint();
        } else if (drawingDirectionGuide()) {
            removeLastDirectionGuidePoint();
        }
    });
    // Ces trois touches ne servent qu'au tracé / aux modes : désactivées le reste du temps,
    // sans quoi elles étaient volées à toute la fenêtre (Entrée dans un champ de
    // l'inspecteur ou une liste, Retour arrière...).
    updateShortcutsState();
}

void MainWindow::setTool(Tool tool) {
    // Quitter l'outil Polygone en cours de tracé annule proprement le tracé
    // (aperçu détruit, sommets oubliés) — jamais de polygone à moitié posé
    // qui survivrait à un changement d'outil.
    if (currentTool_ == Tool::DrawPolygon && tool != Tool::DrawPolygon) {
        cancelPolygonDraw();
    }
    const bool leavingStroke =
        (currentTool_ == Tool::DrawFreeform || currentTool_ == Tool::Cut) && tool != currentTool_;
    if (leavingStroke) {
        cancelFreeformDraw();
    }
    if (tool != Tool::DrawFreeform) {
        lassoSelectArmed_ = false; // le lasso de sélection ne survit pas à un changement d'outil
    }
    if (currentTool_ == Tool::DrawSatinColumn && tool != Tool::DrawSatinColumn) {
        cancelSatinColumnDraw();
    }
    if (currentTool_ == Tool::DrawBezier && tool != Tool::DrawBezier) {
        cancelBezierDraw();
    }
    if (drawingDirectionGuide() && tool != currentTool_) {
        cancelDirectionGuideDraw();
    }
    // Un changement d'outil peut laisser le repère d'accroche affiché à une
    // position qui ne correspond plus à rien (ex. sorti du mode Polygone
    // sans bouger la souris) : masqué jusqu'au prochain mouvement pertinent.
    updateSnapIndicator(std::nullopt);
    currentTool_ = tool;

    // Synchronise les cases d'outils (sans réémettre).
    const auto sync = [](QAction* act, bool on) {
        if (act == nullptr)
            return;
        QSignalBlocker block(act);
        act->setChecked(on);
    };
    sync(toolSelectAct_, tool == Tool::Select);
    sync(toolPanAct_, tool == Tool::Pan);
    sync(toolRectAct_, tool == Tool::Rect);
    sync(toolDrawRectAct_, tool == Tool::DrawRectangle);
    sync(toolDrawEllipseAct_, tool == Tool::DrawEllipse);
    sync(toolDrawPolygonAct_, tool == Tool::DrawPolygon);
    sync(toolDrawPolygonRegularAct_, tool == Tool::DrawPolygonRegular);
    sync(toolDrawBezierAct_, tool == Tool::DrawBezier);
    sync(toolDrawFreeformAct_, tool == Tool::DrawFreeform);
    sync(toolDrawSatinColumnAct_, tool == Tool::DrawSatinColumn);
    sync(toolCutAct_, tool == Tool::Cut);
    sync(toolTextAct_, tool == Tool::Text);
    if (cropAct_ != nullptr) {
        QSignalBlocker block(cropAct_);
        cropAct_->setChecked(tool == Tool::Rect);
    }

    // Applique au canevas : Rectangle = recadrage ; Rectangle/Ellipse dessinés
    // = cadre élastique générique ; Polygone = clics successifs ; Main levée =
    // glisser continu ; sinon vue libre. dragMode() est recalculé par
    // CanvasView lui-même à partir de TOUS ses booléens à chaque appel (cf.
    // CanvasView::updateDragMode) : ordre d'appel indifférent, plus de
    // clobbering entre ces six appels (défaut trouvé par revue -- voir le
    // commentaire détaillé dans canvas_view.cpp).
    view_->setCropMode(tool == Tool::Rect);
    view_->setBoxDrawMode(tool == Tool::DrawRectangle || tool == Tool::DrawEllipse ||
                          tool == Tool::DrawPolygonRegular);
    view_->setPolygonDrawMode(tool == Tool::DrawPolygon || tool == Tool::DrawDirectionGuide ||
                              tool == Tool::DrawBreakLine);
    view_->setBezierDrawMode(tool == Tool::DrawBezier);
    view_->setFreeformDrawMode(tool == Tool::DrawFreeform || tool == Tool::Cut);
    view_->setSatinPairDrawMode(tool == Tool::DrawSatinColumn);
    if (tool == Tool::Pan) {
        view_->setCursor(Qt::OpenHandCursor);
        view_->viewport()->setCursor(Qt::OpenHandCursor);
    } else if (tool == Tool::Select) {
        view_->setCursor(Qt::ArrowCursor);
        view_->viewport()->setCursor(Qt::ArrowCursor);
    }

    if (toolLabel_ != nullptr) {
        const QString name = tool == Tool::Select          ? tr("Sélection")
                             : tool == Tool::Pan           ? tr("Déplacer la vue")
                             : tool == Tool::Rect          ? tr("Recadrer l'image")
                             : tool == Tool::DrawRectangle ? tr("Dessiner un rectangle")
                             : tool == Tool::DrawEllipse   ? tr("Dessiner une ellipse")
                             : tool == Tool::DrawPolygon   ? tr("Dessiner un polygone")
                             : tool == Tool::DrawPolygonRegular
                                 ? tr("Dessiner un polygone régulier")
                             : tool == Tool::DrawBezier   ? tr("Dessiner une courbe de Bézier")
                             : tool == Tool::DrawFreeform ? tr("Dessiner à main levée")
                             : tool == Tool::DrawDirectionGuide ? tr("Guide de direction")
                             : tool == Tool::DrawBreakLine      ? tr("Ligne de rupture")
                             : tool == Tool::Cut                ? tr("Couteau")
                             : tool == Tool::Text               ? tr("Texte")
                                                                : tr("Colonne satin");
        toolLabel_->setText(tr("Outil : %1").arg(name));
    }
    // Rappel explicite du geste attendu à l'activation de l'outil : défaut
    // trouvé en usage réel (rectangle/ellipse ne réagissaient « à rien » en
    // apparence pour qui cliquait sans glisser — le mécanisme fonctionnait,
    // mais rien n'indiquait qu'un GLISSER était nécessaire).
    if (tool == Tool::Rect) {
        statusBar()->showMessage(
            tr("Glissez le cadre à conserver — l'image sera recadrée au relâchement (Échap : "
               "annuler). Pour dessiner un rectangle, utilisez l'outil R."));
    } else if (tool == Tool::DrawRectangle || tool == Tool::DrawEllipse) {
        statusBar()->showMessage(
            tr("Cliquez-glissez sur le canevas pour dessiner le cadre, puis relâchez."));
    } else if (tool == Tool::DrawPolygonRegular) {
        statusBar()->showMessage(
            tr("Cliquez-glissez pour dessiner le polygone (inscrit dans le cadre) — réglez le "
               "nombre de côtés dans la palette d'outils."));
    } else if (tool == Tool::DrawPolygon) {
        statusBar()->showMessage(
            tr("Cliquez pour poser chaque sommet — Entrée/double-clic/bouton ✓ pour "
               "terminer (3 sommets min.), Échap pour annuler."));
    } else if (tool == Tool::DrawBezier) {
        statusBar()->showMessage(
            tr("Cliquez (coin) ou cliquez-glissez (nœud lisse) pour poser chaque point — "
               "Entrée/double-clic/bouton ✓ pour terminer (2 nœuds min.), Échap pour annuler."));
    } else if (tool == Tool::DrawFreeform) {
        statusBar()->showMessage(tr("Cliquez-glissez pour tracer la forme à main levée."));
    } else if (tool == Tool::Text) {
        statusBar()->showMessage(
            tr("Texte : cliquez sur le canevas pour poser le texte (bord gauche de la ligne de "
               "base), puis réglez police, hauteur et type de point."));
    } else if (tool == Tool::Cut) {
        statusBar()->showMessage(
            tr("Couteau : cliquez-glissez une ligne à travers la ou les formes à découper "
               "(sans sélection, toutes les formes traversées sont coupées)."));
    } else if (tool == Tool::DrawSatinColumn) {
        statusBar()->showMessage(
            tr("Cliquez alternativement côté A puis côté B de chaque paire — Entrée/"
               "double-clic/bouton ✓ pour terminer (2 paires min.), Échap pour annuler."));
    } else if (tool == Tool::DrawDirectionGuide) {
        statusBar()->showMessage(
            tr("Cliquez les points de la courbe que le fil doit suivre — Entrée/double-clic "
               "pour terminer (2 points min.), Échap pour annuler."));
    } else if (tool == Tool::DrawBreakLine) {
        statusBar()->showMessage(
            tr("Cliquez les points de la ligne de rupture, d'un bord de la forme à l'autre — "
               "Entrée/double-clic pour terminer, Échap pour annuler."));
    }
    syncMeasureEyedropperTool(tool);
    syncBrushTool(tool);
    syncBucketTool(tool);
    syncClickToStitchTool(tool);
    syncDetailZonesTool(tool); // après syncBrushTool : réutilise le geste du pinceau
    updateDrawActionsState();
    updateInteractionContext();
    hideHoverHighlight();
}

void MainWindow::buildWorkflowPanel() {
    workflowDock_ = new QDockWidget(tr("Workflow"), this);
    workflowDock_->setObjectName(QStringLiteral("workflowDock"));
    workflowDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    workflowPanel_ = new WorkflowPanel(workflowDock_);
    workflowDock_->setWidget(scrollableDockContent(workflowPanel_, workflowDock_));
    if (documentDock_ != nullptr) {
        splitDockWidget(documentDock_, workflowDock_, Qt::Vertical); // sous Document
    } else {
        addDockWidget(Qt::LeftDockWidgetArea, workflowDock_);
    }
    connect(&AppTheme::instance(), &AppTheme::changed, workflowPanel_, &WorkflowPanel::applyTheme);
    connect(workflowPanel_, &WorkflowPanel::stepClicked, this, &MainWindow::onWorkflowStepClicked);
}

void MainWindow::refreshWorkflow() {
    if (workflowPanel_ == nullptr) {
        return;
    }
    using S = WorkflowPanel::State;
    std::array<S, WorkflowPanel::kStepCount> st{};
    const bool img = project_.hasImage();
    const bool seg = project_.segmentation.has_value();
    const bool vec = !project_.vector_objects.empty();
    const bool emb = !project_.embroidery_objects.empty();
    const bool imported = project_.imported_design.has_value();
    const bool seq = sequence_.has_value();
    // Source ≠ raster : un SVG peut fournir directement des vecteurs, et un DST/PES
    // directement des points. Les étapes inapplicables sont indiquées « non requis »
    // plutôt que « à faire » : elles ne doivent pas bloquer le parcours utilisateur.
    const bool hasSource = img || seg || vec || emb || imported;
    const bool machineOnly = imported && !img && !seg && !vec;

    st[0] = hasSource ? S::Done : S::NotStarted;
    if (!hasSource) {
        st[1] = S::NotStarted;
        st[2] = S::NotStarted;
    } else if (machineOnly) {
        st[1] = S::Skipped;
        st[2] = S::Skipped;
    } else {
        st[1] = !img ? S::Skipped : ((seg || vec) ? S::Done : S::Available);
        st[2] = vec ? S::Done : (seg ? S::Available : S::NotStarted);
    }
    st[3] = (emb || imported) ? S::Done : ((vec || seg) ? S::Available : S::NotStarted);
    st[4] = seq ? S::Available : S::NotStarted;
    st[5] = seq ? S::Available : S::NotStarted;
    workflowPanel_->setStates(st);
}

void MainWindow::buildDocumentPanel() {
    documentDock_ = new QDockWidget(tr("Document"), this);
    documentDock_->setObjectName(QStringLiteral("documentDock"));
    documentDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    documentPanel_ = new DocumentPanel(documentDock_);
    documentDock_->setWidget(documentPanel_);
    addDockWidget(Qt::LeftDockWidgetArea, documentDock_);
    if (orderDock_ != nullptr) {
        tabifyDockWidget(documentDock_, orderDock_); // Document et Ordre en onglets
        documentDock_->raise();
    }

    connect(documentPanel_, &DocumentPanel::embroiderySelected, this, [this](ObjectId id) {
        Selection sel{.region = std::nullopt, .embroidery = id, .objects = {}};
        if (const auto* e = project_.findEmbroidery(id);
            e != nullptr && project_.findObject(e->source_vector) != nullptr) {
            sel.objects = {e->source_vector}; // met en évidence la forme au canevas
        }
        setSelection(std::move(sel));
        displayImage(processed_);
        updateActions();
    });
    connect(
        documentPanel_, &DocumentPanel::regionsSelected, this,
        [this](const std::vector<std::uint64_t>& ids, std::uint64_t active) {
            if (ids.size() < 2) {
                return; // une seule ligne : traitée par regionSelected (comportement historique)
            }
            std::vector<RegionId> others;
            for (const std::uint64_t id : ids) {
                if (id != active) {
                    others.push_back(RegionId{static_cast<std::uint32_t>(id)});
                }
            }
            setSelection({.region = RegionId{static_cast<std::uint32_t>(active)},
                          .embroidery = std::nullopt,
                          .objects = {},
                          .extraRegions = others});
            announceRegionSelection();
            displayImage(processed_);
            updateActions();
        });
    // Clic sur un groupe de sections : la forme source (donc toutes les sections) est visée.
    connect(documentPanel_, &DocumentPanel::groupSelected, this,
            [this](ObjectId sourceVector, int sections) {
                if (project_.findObject(sourceVector) == nullptr) {
                    return;
                }
                setSelection({.region = std::nullopt,
                              .embroidery = std::nullopt,
                              .objects = {sourceVector}});
                displayImage(processed_);
                updateActions();
                statusBar()->showMessage(
                    tr("Forme sélectionnée : %1 section(s) de couture.").arg(sections));
            });
    // Cases de la liste et renommage : commandes annulables (jamais de mutation directe).
    connect(documentPanel_, &DocumentPanel::visibilityToggled, this,
            [this](ObjectId id, bool visible) {
                undoStack_.execute(
                    std::make_unique<commands::SetEmbroideryVisibleCommand>(id, visible), project_);
                refreshImage();
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::orderLockToggled, this,
            [this](ObjectId id, bool locked) {
                undoStack_.execute(std::make_unique<commands::SetEmbroideryLockCommand>(id, locked),
                                   project_);
                refreshOrderPanel();
                refreshImage();
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::renameRequested, this,
            [this](ObjectId id, const QString& name) {
                undoStack_.execute(
                    std::make_unique<commands::RenameEmbroideryCommand>(id, name.toStdString()),
                    project_);
                refreshImage();
                updateActions();
            });
    // Calques du document (HP-OBJ-024) : pile au-dessus des onglets de la liste Document.
    {
        CalquePanel* calques = documentPanel_->calquePanel();
        connect(calques, &CalquePanel::addCalqueRequested, this, &MainWindow::addCalque);
        connect(calques, &CalquePanel::removeCalqueRequested, this, &MainWindow::removeCalque);
        connect(calques, &CalquePanel::renameCalqueRequested, this,
                &MainWindow::renameCalqueInteractive);
        connect(calques, &CalquePanel::moveSelectionToCalqueRequested, this,
                &MainWindow::moveSelectionToCalque);
        connect(calques, &CalquePanel::calqueActivated, this, [this](ObjectId id) {
            if (project_.findCalque(id) != nullptr) {
                project_.active_calque = id; // état d'interface : ni annulable ni enregistré
                refreshDocumentPanel();
                statusBar()->showMessage(
                    tr("Calque actif : « %1 ».")
                        .arg(QString::fromStdString(project_.findCalque(id)->name)));
            }
        });
        connect(calques, &CalquePanel::moveCalqueRequested, this, [this](ObjectId id, int dir) {
            undoStack_.execute(std::make_unique<commands::MoveCalqueCommand>(id, dir), project_);
            refreshImage();
            updateActions();
        });
        connect(calques, &CalquePanel::calqueVisibilityToggled, this,
                [this](ObjectId id, bool visible) {
                    undoStack_.execute(
                        std::make_unique<commands::SetCalqueVisibleCommand>(id, visible), project_);
                    refreshImage();
                    updateActions();
                });
        connect(calques, &CalquePanel::calqueLockToggled, this, [this](ObjectId id, bool locked) {
            undoStack_.execute(std::make_unique<commands::SetCalqueLockedCommand>(id, locked),
                               project_);
            pruneSelection();
            refreshImage();
            updateActions();
        });
    }
    // Couches de couture (HP-OBJ-023) : bouton « + Couche » et menu de ligne.
    connect(documentPanel_, &DocumentPanel::addLayerRequested, this,
            [this](ObjectId id, bool interactive) {
                if (interactive) {
                    addLayerInteractive(id);
                } else {
                    addLayerQuick(id);
                }
            });
    connect(documentPanel_, &DocumentPanel::duplicateLayerRequested, this,
            [this](ObjectId id) { duplicateLayer(id); });
    connect(documentPanel_, &DocumentPanel::regionSelected, this, [this](RegionId id) {
        setSelection({.region = id, .embroidery = std::nullopt, .objects = {}});
        displayImage(processed_);
        updateActions();
    });
    // Forme vectorielle non cousue (ligne indépendante de la liste).
    connect(documentPanel_, &DocumentPanel::vectorSelected, this, [this](ObjectId id) {
        if (project_.findObject(id) == nullptr) {
            return;
        }
        setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {id}});
        displayImage(processed_);
        updateActions();
    });
    // Ctrl/Maj + clic : plusieurs lignes = plusieurs formes dans la sélection (l'inspecteur
    // affiche « N objets »). Une broderie sans forme source valide ne peut pas rejoindre un lot.
    connect(documentPanel_, &DocumentPanel::objectsSelected, this,
            [this](const std::vector<ObjectId>& embroidery, const std::vector<ObjectId>& vectors) {
                std::vector<ObjectId> objects;
                for (const ObjectId id : embroidery) {
                    const auto* e = project_.findEmbroidery(id);
                    if (e != nullptr && project_.findObject(e->source_vector) != nullptr) {
                        objects.push_back(e->source_vector);
                    }
                }
                for (const ObjectId id : vectors) {
                    if (project_.findObject(id) != nullptr) {
                        objects.push_back(id);
                    }
                }
                if (objects.size() < 2 && !embroidery.empty()) {
                    setSelection({.region = std::nullopt,
                                  .embroidery = embroidery.back(),
                                  .objects = objects});
                } else {
                    setSelection({.region = std::nullopt,
                                  .embroidery = std::nullopt,
                                  .objects = std::move(objects)});
                }
                displayImage(processed_);
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::selectionCleared, this, [this] {
        setSelection({});
        displayImage(processed_);
        updateActions();
    });
    // Menu de ligne : mêmes commandes annulables que les cases et les boutons de l'Ordre.
    connect(documentPanel_, &DocumentPanel::visibilityChangeRequested, this,
            [this](const std::vector<ObjectId>& ids, bool visible) {
                auto composite = std::make_unique<commands::CompositeCommand>(
                    (visible ? tr("Afficher %1 objets") : tr("Masquer %1 objets"))
                        .arg(ids.size())
                        .toStdString());
                for (const ObjectId id : ids) {
                    composite->add(
                        std::make_unique<commands::SetEmbroideryVisibleCommand>(id, visible));
                }
                undoStack_.execute(std::move(composite), project_);
                refreshImage();
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::orderLockChangeRequested, this,
            [this](const std::vector<ObjectId>& ids, bool locked) {
                auto composite = std::make_unique<commands::CompositeCommand>(
                    (locked ? tr("Figer l'ordre de %1 objets") : tr("Libérer l'ordre de %1 objets"))
                        .arg(ids.size())
                        .toStdString());
                for (const ObjectId id : ids) {
                    composite->add(
                        std::make_unique<commands::SetEmbroideryLockCommand>(id, locked));
                }
                undoStack_.execute(std::move(composite), project_);
                refreshOrderPanel();
                refreshImage();
                updateActions();
            });
    connect(documentPanel_, &DocumentPanel::deleteRequested, this,
            [this](const std::vector<ObjectId>& embroidery, const std::vector<ObjectId>& vectors) {
                // Même règle que la suppression d'une broderie choisie : la broderie seule, sauf
                // proxy invisible (colonne satin manuelle) dont la suppression emporte la source.
                std::vector<ObjectId> removeVectors = vectors;
                std::vector<ObjectId> removeEmbroideries;
                for (const ObjectId id : embroidery) {
                    const auto* e = project_.findEmbroidery(id);
                    if (e == nullptr) {
                        continue;
                    }
                    const auto* source = project_.findObject(e->source_vector);
                    if (source != nullptr && !source->visible) {
                        removeVectors.push_back(source->id);
                    } else {
                        removeEmbroideries.push_back(id);
                    }
                }
                const std::size_t total = removeVectors.size() + removeEmbroideries.size();
                if (total == 0) {
                    return;
                }
                auto composite = std::make_unique<commands::CompositeCommand>(
                    tr("Supprimer %1 objets").arg(total).toStdString());
                for (const ObjectId id : removeEmbroideries) {
                    composite->add(removeEmbroideryCommand(id)); // un dégradé part en entier
                }
                for (std::size_t i = 0; i < removeVectors.size(); ++i) {
                    if (std::find(removeVectors.begin(),
                                  removeVectors.begin() + static_cast<std::ptrdiff_t>(i),
                                  removeVectors[i]) ==
                        removeVectors.begin() + static_cast<std::ptrdiff_t>(i)) {
                        composite->add(std::make_unique<commands::RemoveVectorObjectCommand>(
                            removeVectors[i]));
                    }
                }
                undoStack_.execute(std::move(composite), project_);
                setSelection({});
                refreshImage();
                updateActions();
                statusBar()->showMessage(tr("%1 objet(s) supprimé(s).").arg(total));
            });
    // Glisser-déposer : nouvel ordre complet via la commande des boutons Monter/Descendre.
    connect(documentPanel_, &DocumentPanel::orderChangeRequested, this,
            [this](const std::vector<ObjectId>& order) {
                if (order.size() != project_.embroidery_objects.size()) {
                    return; // liste périmée : le document fait foi
                }
                undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order),
                                   project_);
                refreshImage();
                refreshOrderPanel();
                updateActions();
            });
}

bool MainWindow::debugMenuEnabled() {
    return qEnvironmentVariable("OPENSTITCH_DEBUG") == QLatin1String("1") ||
           QSettings().value(QStringLiteral("debug/menu"), false).toBool();
}

void MainWindow::setDockAutoVisible(QDockWidget* dock, bool visible, bool force) {
    if (dock == nullptr) {
        return;
    }
    // Un rafraîchissement n'affiche un dock que lors de la transition vide -> non vide
    // (premier contenu) : ensuite, la visibilité appartient à l'utilisateur (croix du dock,
    // menu Affichage > Panneaux). `force` : demande explicite de l'utilisateur (Analyse).
    const bool hadContent = dockHadContent_.value(dock, false);
    dockHadContent_[dock] = visible;
    const bool show = visible && (force || !hadContent);
    if (!hidePanelsMode_) {
        if (show) {
            dock->show();
        } else if (!visible) {
            dock->hide();
        }
        return;
    }
    // Mode canevas seul : on ne touche pas à l'affichage, mais on tient la liste de
    // restauration à jour pour que la sortie du mode reflète l'état voulu.
    const auto it = std::find(panelsToRestore_.begin(), panelsToRestore_.end(), dock);
    if (show && it == panelsToRestore_.end()) {
        panelsToRestore_.push_back(dock);
    } else if (!visible && it != panelsToRestore_.end()) {
        panelsToRestore_.erase(it);
    }
}

void MainWindow::refreshDocumentPanel() {
    if (documentPanel_ == nullptr) {
        return;
    }
    documentPanel_->refresh(project_, editStates_);
    setDockAutoVisible(documentDock_, project_.hasImage() || !project_.embroidery_objects.empty());
    syncDocumentSelection();
}

void MainWindow::syncDocumentSelection() {
    if (documentPanel_ == nullptr) {
        return;
    }
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();
    if (emb != nullptr) {
        documentPanel_->syncSelection(DocumentPanel::Kind::Embroidery, emb->id.value);
    } else if (selectedRegion_) {
        std::vector<std::uint64_t> ids;
        for (const RegionId r : selectedRegionIds()) {
            ids.push_back(r.value);
        }
        documentPanel_->syncRegions(ids, selectedRegion_->value);
    } else if (hasMultiSelection()) {
        std::vector<std::uint64_t> ids;
        for (const ObjectId id : selectedObjectIds()) {
            ids.push_back(id.value);
        }
        documentPanel_->syncObjects(ids);
    } else if (selectedObject_) {
        // Forme sans broderie : ligne indépendante de la liste (vide si l'objet est masqué
        // ailleurs).
        documentPanel_->syncSelection(DocumentPanel::Kind::Vector, selectedObject_->value);
    } else {
        documentPanel_->syncSelection(DocumentPanel::Kind::None, 0);
    }
}

void MainWindow::buildPropertiesPanel() {
    propertiesDock_ = new QDockWidget(tr("Propriétés"), this);
    propertiesDock_->setObjectName(QStringLiteral("propertiesDock"));
    propertiesDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesPanel_ = new PropertiesPanel(propertiesDock_);
    // Zone défilante : un inspecteur de 20 champs ne doit pas imposer sa hauteur à la
    // fenêtre (hauteur minimale mesurée : 934 px avec un satin sélectionné).
    auto* propertiesScroll = new QScrollArea(propertiesDock_);
    propertiesScroll->setWidgetResizable(true);
    propertiesScroll->setFrameShape(QFrame::NoFrame);
    propertiesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    propertiesScroll->setWidget(propertiesPanel_);
    propertiesDock_->setWidget(propertiesScroll);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);

    // Édition d'un paramètre -> commande annulable -> régénération.
    // Boutons de l'inspecteur des régions : déclenchent l'action de menu du même nom (même état,
    // même grisage, même annulation).
    connect(propertiesPanel_, &PropertiesPanel::regionActionRequested, this,
            [this](const QString& actionName) {
                if (auto* act = findChild<QAction*>(actionName);
                    act != nullptr && act->isEnabled()) {
                    act->trigger();
                }
            });
    connect(propertiesPanel_, &PropertiesPanel::paramsEdited, this,
            [this](ObjectId id, document::StitchParams params, QString field) {
                // Auto-satin : l'inspecteur n'édite que les réglages scalaires ; les
                // guides (aussi posés depuis le canevas), l'entrée/sortie et les champs
                // sans widget viennent du document ACTUEL, jamais d'une copie périmée.
                if (auto* edited = std::get_if<document::AutoSatinParams>(&params)) {
                    if (const auto* current = project_.findEmbroidery(id)) {
                        if (const auto* cur =
                                std::get_if<document::AutoSatinParams>(&current->params)) {
                            edited->guides = cur->guides;
                            edited->entry_point = cur->entry_point;
                            edited->exit_point = cur->exit_point;
                            edited->push_start = cur->push_start;
                            edited->push_end = cur->push_end;
                            edited->lock_length = cur->lock_length;
                            edited->lock_passes = cur->lock_passes;
                        }
                    }
                }
                // HP-UX-029 : une modification qui rend des retouches manuelles obsolètes est
                // signalée AVANT d'être appliquée (bandeau non modal, annulable).
                if (const auto* current = project_.findEmbroidery(id);
                    current != nullptr && !(current->params == params)) {
                    noteOverridesInvalidated(id, current->params);
                }
                // Le champ nomme le pas d'historique et sert de clé de fusion : une
                // rafale (molette, flèches du spinbox) reste un seul pas d'annulation.
                // La commande est appliquée TOUT DE SUITE ; seule la régénération des points
                // est différée (HP-PERF-009).
                undoStack_.execute(std::make_unique<commands::SetStitchParamsCommand>(
                                       id, std::move(params), field.toStdString()),
                                   project_);
                // Le formulaire correspond désormais au document (guides repris du
                // document pour l'auto-satin) : pas de reconstruction ni de perte de focus.
                if (const auto* now = project_.findEmbroidery(id)) {
                    propertiesPanel_->adoptParams(id, now->params);
                    pendingCreationMemory_ = now->params; // dernier réglage, mémorisé au débounce
                }
                // scheduleParamsRefresh() appelle updateActions() (pas updateInspector() seul) :
                // un changement de paramètres peut faire passer un objet retouché à Dirty
                // (Lot 8.2) -- undo/redo, mode d'édition des points et indicateurs doivent
                // suivre. Ne reconstruit PAS le formulaire de paramètres (qui perdrait le
                // focus en cours de frappe) : le garde-fou kind==inspectedKind_/id==
                // inspectedId_ dans updateInspector() s'applique toujours. La régénération
                // des points (et donc l'état Clean/ManuallyEdited/Dirty) suit 150 ms après la
                // dernière édition, hors du fil d'interface.
                scheduleParamsRefresh();
                refreshOverrideWarning();
            });
    connect(propertiesPanel_, &PropertiesPanel::discardOverridesRequested, this,
            &MainWindow::discardOverrides);
    connect(propertiesPanel_, &PropertiesPanel::convertToDirectionalRequested, this,
            &MainWindow::convertToDirectional);
    connect(propertiesPanel_, &PropertiesPanel::convertToContourFillRequested, this,
            &MainWindow::convertToContourFill);
    connect(propertiesPanel_, &PropertiesPanel::convertToFillKindRequested, this,
            &MainWindow::convertToFillKind);
    connect(propertiesPanel_, &PropertiesPanel::createBorderSatinRequested, this,
            &MainWindow::createBorderSatin);
    connect(propertiesPanel_, &PropertiesPanel::borderSatinEdited, this,
            &MainWindow::editBorderSatin);
    connect(propertiesPanel_, &PropertiesPanel::joinModeEdited, this, &MainWindow::setJoinMode);
    connect(propertiesPanel_, &PropertiesPanel::connectorModeEdited, this,
            &MainWindow::setConnectorMode);
    connect(propertiesPanel_, &PropertiesPanel::knockoutEdited, this, &MainWindow::setKnockout);
    connect(propertiesPanel_, &PropertiesPanel::densityGradientRequested, this,
            &MainWindow::setDensityGradient);
    connect(
        propertiesPanel_, &PropertiesPanel::editSatinGuidesRequested, this, [this](ObjectId id) {
            if (auto* emb = project_.findEmbroidery(id); emb != nullptr && emb->is_auto_satin()) {
                editSelection([id](Selection& sel) { sel.embroidery = id; });
                if (directionGuideModeAct_->isChecked()) {
                    directionGuideModeAct_->setChecked(false);
                }
                directionGuideModeAct_->setChecked(true);
                if (directionGuideTarget_) {
                    setTool(Tool::DrawDirectionGuide);
                }
            }
        });
    connect(propertiesPanel_, &PropertiesPanel::satinGuideChangeRequested, this,
            &MainWindow::changeAutoSatinGuide);
    connect(propertiesPanel_, &PropertiesPanel::satinGuideSelected, this,
            &MainWindow::highlightAutoSatinGuide);
    connectInspectorEditing();
    connect(propertiesPanel_, &PropertiesPanel::satinGuideRemoveRequested, this,
            &MainWindow::removeAutoSatinGuide);
    connect(propertiesPanel_, &PropertiesPanel::compareSatinMethodsRequested, this,
            &MainWindow::compareAutoSatinMethods);
    // Barre du sélecteur de couches : mêmes commandes annulables que la liste Document.
    connect(propertiesPanel_, &PropertiesPanel::layerMoveRequested, this,
            [this](ObjectId id, int direction) { moveLayer(id, direction); });
    connect(propertiesPanel_, &PropertiesPanel::layerVisibilityRequested, this,
            [this](ObjectId id, bool visible) {
                undoStack_.execute(
                    std::make_unique<commands::SetEmbroideryVisibleCommand>(id, visible), project_);
                refreshImage();
                updateActions();
            });
    connect(propertiesPanel_, &PropertiesPanel::layerRenameRequested, this,
            &MainWindow::renameLayerInteractive);
    connect(propertiesPanel_, &PropertiesPanel::editDirectionGuidesRequested, this,
            [this](ObjectId id) {
                if (auto* emb = project_.findEmbroidery(id);
                    emb != nullptr && emb->is_directional()) {
                    editSelection([id](Selection& sel) { sel.embroidery = id; });
                    if (directionGuideModeAct_->isChecked()) {
                        directionGuideModeAct_->setChecked(false);
                    }
                    directionGuideModeAct_->setChecked(true);
                }
            });
}

void MainWindow::updateInspector() {
    if (propertiesPanel_ == nullptr) {
        return;
    }
    // Cible : objet de broderie (dock Ordre) > remplissage rattaché à l'objet
    // vectoriel sélectionné > objet vectoriel > région > rien.
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();

    QList<PropertiesPanel::FillLayer> layers;
    if (emb != nullptr) {
        for (const auto& layer : project_.embroidery_objects) {
            if (layer.source_vector == emb->source_vector) {
                layers.push_back(
                    {layer.id,
                     tr("%1 · %2").arg(layers.size() + 1).arg(QString::fromStdString(layer.name)),
                     QColor(layer.rgb[0], layer.rgb[1], layer.rgb[2]), layer.visible});
            }
        }
    }
    propertiesPanel_->setFillLayers(layers, emb ? std::optional<ObjectId>{emb->id} : std::nullopt);
    propertiesPanel_->setBlendActions(twoColorBlendAct_, blendZonesAct_, !layers.empty(),
                                      hasMultiSelection());
    updateGradientPanel();

    int kind = -1;
    std::uint64_t id = 0;
    if (emb != nullptr) {
        kind = 0;
        // Clé : l'objet ET sa couleur (la pastille de l'inspecteur doit suivre un changement de
        // couleur ou de fil, annulation comprise).
        id = (emb->id.value << 24) ^ (static_cast<std::uint64_t>(emb->rgb[0]) << 16) ^
             (static_cast<std::uint64_t>(emb->rgb[1]) << 8) ^
             static_cast<std::uint64_t>(emb->rgb[2]);
    } else if (hasMultiSelection()) {
        kind = 3; // « N objets » : type de points commun et réglages de groupe
        // Clé : la sélection ET le type de chaque couture (un changement de type regroupe ou
        // dégroupe le formulaire).
        std::uint64_t key = 1469598103934665603ULL;
        const auto mix = [&key](std::uint64_t v) { key = (key ^ v) * 1099511628211ULL; };
        for (const ObjectId vectorId : multiSelection_) {
            mix(vectorId.value);
            for (const auto& e : project_.embroidery_objects) {
                if (e.source_vector == vectorId) {
                    mix(e.id.value);
                    mix(e.params.index());
                }
            }
        }
        id = key;
    } else if (selectedObject_) {
        kind = 1;
        id = selectedObject_->value;
    } else if (selectedRegion_ && project_.segmentation) {
        kind = 2;
        // Clé : toutes les régions sélectionnées ET la couleur de l'active (un recolorage ou une
        // fusion doit reconstruire la pastille et les boutons, sans toucher aux autres
        // inspecteurs).
        std::uint64_t key = 1469598103934665603ULL;
        const auto mix = [&key](std::uint64_t v) { key = (key ^ v) * 1099511628211ULL; };
        for (const RegionId r : selectedRegionIds()) {
            mix(r.value);
        }
        if (const auto* active = project_.segmentation->find(*selectedRegion_)) {
            mix(active->rgb[0]);
            mix(active->rgb[1]);
            mix(active->rgb[2]);
        }
        id = key;
    }

    // Indicateur Clean/ManuallyEdited/Dirty (Lot 8.2) : mis à jour à CHAQUE
    // appel, y compris quand kind/id n'ont pas changé (une retouche/undo/redo
    // peut faire évoluer l'état sans changer la sélection) -- volontairement
    // AVANT le "ne reconstruit que si la sélection a changé" ci-dessous, pour
    // ne jamais dépendre de lui.
    propertiesPanel_->setEditState(emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
                                   emb != nullptr ? editStateOf(emb->id)
                                                  : stitch_generation::ObjectEditState::Clean,
                                   emb != nullptr ? static_cast<int>(emb->overrides.size()) : 0);
    refreshOverrideWarning(); // HP-UX-029 : suit annulation, rétablissement et abandon
    propertiesPanel_->setJoinMode(emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
                                  emb != nullptr ? emb->join : document::JoinMode::Inherit);
    propertiesPanel_->setConnectorMode(
        emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
        emb != nullptr ? emb->connector : document::ConnectorMode::Inherit);
    propertiesPanel_->setKnockout(emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
                                  emb != nullptr && emb->knockout,
                                  emb != nullptr ? emb->knockout_margin.value / 1000.0 : 0.3);

    // Auto-satin : liste des guides et diagnostic mis à jour à chaque appel (les
    // guides se posent aussi depuis le canevas), sans reconstruire le formulaire.
    if (emb != nullptr && emb->is_auto_satin()) {
        propertiesPanel_->setAutoSatinState(
            emb->id, &std::get<document::AutoSatinParams>(emb->params), autoSatinSummary(*emb));
    }

    // Ne reconstruit que si la sélection a changé (n'interrompt pas une édition) OU si
    // les paramètres du document ne sont plus ceux que montre le formulaire (annulation,
    // changement de type de points qui garde le même id, rotation au canevas...) : sinon
    // le premier champ touché écraserait le document avec des valeurs périmées.
    bool formStale = kind == 0 && !propertiesPanel_->showsParams(emb->params);
    if (kind == 3) {
        // Groupe de même type : le formulaire montre le premier ; annulation => reconstruction.
        const EmbroideryGroup group = selectedEmbroideryGroup();
        const auto* rep = group.sameType ? project_.findEmbroidery(group.ids.front()) : nullptr;
        formStale = rep != nullptr && !propertiesPanel_->showsGroupParams(rep->params);
        // HP-UX-038 : une valeur d'un AUTRE objet du groupe (annulation, glisser…) change
        // l'affichage « — » : le formulaire est reconstruit quand les valeurs mixtes ont changé.
        if (!formStale && rep != nullptr) {
            std::vector<document::StitchParams> members;
            for (const ObjectId memberId : group.ids) {
                if (const auto* m = project_.findEmbroidery(memberId)) {
                    members.push_back(m->params);
                }
            }
            formStale = !propertiesPanel_->showsGroupMembers(members);
        }
    }
    if (kind == 1) {
        // Boîte X/Y/L/H : reconstruite si la forme a changé (annulation, glisser, flèches...).
        const auto* current = project_.findObject(*selectedObject_);
        const auto box = current != nullptr ? vectorBoxMm(*current) : std::nullopt;
        formStale = box && !propertiesPanel_->showsVectorBox(*selectedObject_, *box);
    }
    if (kind == inspectedKind_ && id == inspectedId_ && !formStale) {
        return;
    }
    inspectedKind_ = kind;
    inspectedId_ = id;

    if (kind == 0) {
        propertiesPanel_->showEmbroidery(*emb);
        if (emb->is_auto_satin()) {
            propertiesPanel_->setAutoSatinState(
                emb->id, &std::get<document::AutoSatinParams>(emb->params), autoSatinSummary(*emb));
        }
    } else if (kind == 3) {
        int withStitches = 0;
        for (const ObjectId vectorId : multiSelection_) {
            if (embroideryForVector(vectorId) != nullptr) {
                ++withStitches;
            }
        }
        const EmbroideryGroup group = selectedEmbroideryGroup();
        const auto* rep = group.sameType ? project_.findEmbroidery(group.ids.front()) : nullptr;
        std::vector<document::StitchParams> members;
        if (rep != nullptr) {
            for (const ObjectId memberId : group.ids) {
                if (const auto* m = project_.findEmbroidery(memberId)) {
                    members.push_back(m->params);
                }
            }
        }
        propertiesPanel_->showMultiSelection(static_cast<int>(multiSelection_.size()), withStitches,
                                             rep, static_cast<int>(group.ids.size()), members);
    } else if (kind == 1) {
        const auto* vec = project_.findObject(*selectedObject_);
        int nodes = 0;
        if (vec != nullptr) {
            for (const auto& set : vec->paths) {
                nodes += static_cast<int>(set.outer.nodes.size());
                for (const auto& h : set.holes)
                    nodes += static_cast<int>(h.nodes.size());
            }
        }
        const QString details =
            tr("Objet vectoriel — %1 morceau(x), %2 nœud(s).\n"
               "Créez un objet de broderie (menu Broderie) pour régler la couture.")
                .arg(vec != nullptr ? vec->paths.size() : 0)
                .arg(nodes);
        const auto box = vec != nullptr ? vectorBoxMm(*vec) : std::nullopt;
        if (vec != nullptr && box) {
            propertiesPanel_->showVectorObject(vec->id, QString::fromStdString(vec->name), details,
                                               *box);
        } else {
            propertiesPanel_->showInfo(vec != nullptr ? QString::fromStdString(vec->name)
                                                      : tr("Objet vectoriel"),
                                       details);
        }
    } else if (kind == 2) {
        const auto* region = project_.segmentation->find(*selectedRegion_);
        if (region != nullptr) {
            const std::vector<RegionId> picked = selectedRegionIds();
            std::size_t pixels = 0;
            for (const RegionId r : picked) {
                if (const auto* rr = project_.segmentation->find(r)) {
                    pixels += rr->pixel_count;
                }
            }
            const double mmPerPx = project_.mm_per_px.value;
            const double areaMm2 = static_cast<double>(pixels) * mmPerPx * mmPerPx;
            PropertiesPanel::RegionSelectionInfo info;
            info.activeColor = QColor(region->rgb[0], region->rgb[1], region->rgb[2]);
            info.canMerge = picked.size() >= 2;
            info.canAbsorb = picked.size() == 1;
            if (picked.size() == 1) {
                const auto neighbours =
                    segmentation::neighbors_of(*project_.segmentation, region->id);
                info.title = tr("Région %1").arg(region->id.value);
                info.summary = tr("Aire : %1 mm²   ·   %2 pixels\nVoisines : %3")
                                   .arg(areaMm2, 0, 'f', 1)
                                   .arg(pixels)
                                   .arg(neighbours.size());
            } else {
                info.title = tr("%1 régions").arg(picked.size());
                info.summary = tr("Aire totale : %1 mm²   ·   %2 pixels\nRégion active : %3 "
                                  "(la dernière cliquée)")
                                   .arg(areaMm2, 0, 'f', 1)
                                   .arg(pixels)
                                   .arg(region->id.value);
            }
            propertiesPanel_->showRegions(info);
        }
    } else {
        propertiesPanel_->showInfo(
            tr("Aucune sélection"),
            tr("Sélectionnez une région, un objet vectoriel ou un objet de broderie."));
    }
}

Vec2um MainWindow::embroideryCentroid(const document::EmbroideryObject& object) const {
    // Satin : moyenne des nœuds des deux rails. Autres : centre du contour
    // extérieur du premier morceau de l'objet vectoriel source.
    std::int64_t sx = 0;
    std::int64_t sy = 0;
    std::int64_t n = 0;
    const auto accumulate = [&](const geometry::Path& path) {
        for (const auto& node : path.nodes) {
            sx += node.pos.x.value;
            sy += node.pos.y.value;
            ++n;
        }
    };
    if (const auto* satin = std::get_if<document::SatinParams>(&object.params)) {
        accumulate(satin->rail_a);
        accumulate(satin->rail_b);
    } else {
        for (const auto& vec : project_.vector_objects) {
            if (vec.id == object.source_vector && !vec.paths.empty()) {
                accumulate(vec.paths.front().outer);
                break;
            }
        }
    }
    if (n == 0) {
        return {};
    }
    return Vec2um{Micrometers{static_cast<std::int32_t>(sx / n)},
                  Micrometers{static_cast<std::int32_t>(sy / n)}};
}

void MainWindow::buildOrderPanel() {
    orderDock_ = new QDockWidget(tr("Ordre de couture"), this);
    orderDock_->setObjectName(QStringLiteral("orderDock"));
    orderDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(orderDock_);
    auto* layout = new QVBoxLayout(panel);

    auto* orderList = new OrderListWidget(panel);
    orderList->setObjectName(QStringLiteral("list_sewingOrder"));
    orderList_ = orderList;
    layout->addWidget(orderList_, 1);
    // HP-SEQ-002 : glisser-déposer d'une ligne = une commande (un pas d'annulation).
    connect(orderList, &OrderListWidget::moveRequested, this, [this](int from, int to) {
        const auto* moved = orderRowObject(from);
        if (moved == nullptr || to < 0) {
            return;
        }
        const ObjectId movedId = moved->id;
        if (!commands::moved_sewing_order(project_, movedId, static_cast<std::size_t>(to))) {
            statusBar()->showMessage(
                tr("Déplacement impossible : un objet ne change pas de calque en se déplaçant."));
            return;
        }
        undoStack_.execute(std::make_unique<commands::MoveInSewingOrderCommand>(
                               movedId, static_cast<std::size_t>(to)),
                           project_);
        refreshImage();
        refreshOrderPanel();
        orderList_->setCurrentRow(to);
        updateActions();
    });
    connect(orderList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (const auto* rowObject = orderRowObject(row)) {
            // sélection d'objet vectoriel distincte
            setSelection({.region = selectedRegion_, .embroidery = rowObject->id, .objects = {}});
            updateActions();
            displayImage(processed_);
        } else {
            editSelection([](Selection& sel) { sel.embroidery.reset(); });
            updateActions();
        }
    });

    orderList_->setAccessibleName(tr("Ordre de couture des objets"));
    auto* buttons = new QHBoxLayout();
    orderUpBtn_ = new QPushButton(tr("↑ Monter"), panel);
    orderUpBtn_->setObjectName(QStringLiteral("button_orderUp"));
    orderUpBtn_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    orderUpBtn_->setToolTip(tr("Coudre cet objet plus tôt (Alt+Haut)."));
    orderDownBtn_ = new QPushButton(tr("↓ Descendre"), panel);
    orderDownBtn_->setObjectName(QStringLiteral("button_orderDown"));
    orderDownBtn_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    orderDownBtn_->setToolTip(tr("Coudre cet objet plus tard (Alt+Bas)."));
    // « Figer l'ordre » : l'optimisation de l'ordre ne déplace pas l'objet (ce n'est PAS un
    // verrou d'édition : il reste déplaçable et modifiable).
    orderLockBtn_ = new QPushButton(tr("Figer l'ordre"), panel);
    orderLockBtn_->setObjectName(QStringLiteral("button_orderLock"));
    orderLockBtn_->setCheckable(true);
    orderLockBtn_->setToolTip(tr("Figer la position de cet objet dans l'ordre de couture : "
                                 "« Optimiser l'ordre » ne le déplace pas. L'objet reste "
                                 "modifiable."));
    connect(orderUpBtn_, &QPushButton::clicked, this, &MainWindow::moveObjectUp);
    connect(orderDownBtn_, &QPushButton::clicked, this, &MainWindow::moveObjectDown);
    connect(orderLockBtn_, &QPushButton::clicked, this, &MainWindow::toggleObjectLock);
    buttons->addWidget(orderUpBtn_);
    buttons->addWidget(orderDownBtn_);
    buttons->addWidget(orderLockBtn_);
    layout->addLayout(buttons);
    connect(orderList_, &QListWidget::currentRowChanged, this, [this] { updateOrderButtons(); });

    orderStrategyCombo_ = new QComboBox(panel);
    orderStrategyCombo_->addItem(tr("Par couleur"),
                                 static_cast<int>(optimization::OrderStrategy::ByColor));
    orderStrategyCombo_->addItem(tr("Par proximité"),
                                 static_cast<int>(optimization::OrderStrategy::ByProximity));
    orderStrategyCombo_->addItem(tr("Couleur puis proximité"),
                                 static_cast<int>(optimization::OrderStrategy::ColorThenProximity));
    layout->addWidget(orderStrategyCombo_);
    // HP-SEQ-008 / HP-SEQ-001 : départ, arrivée du design et amélioration locale.
    auto* orderOptions = new OrderOptionsWidget(panel);
    orderOptions->setObjectName(QStringLiteral("orderOptions"));
    layout->addWidget(orderOptions);
    connect(orderOptions, &OrderOptionsWidget::endpointsChanged, this, [this, orderOptions] {
        document::SequenceFinishing next = project_.finishing;
        next.design_start = orderOptions->start();
        next.design_end = orderOptions->end();
        if (next == project_.finishing) {
            return;
        }
        undoStack_.execute(std::make_unique<commands::SetFinishingCommand>(next), project_);
        updateActions();
    });
    auto* applyBtn = new QPushButton(tr("Optimiser l'ordre"), panel);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applyOrderStrategy);
    layout->addWidget(applyBtn);

    orderCostLabel_ = new QLabel(panel);
    orderCostLabel_->setWordWrap(true);
    layout->addWidget(orderCostLabel_);
    updateOrderButtons();

    orderDock_->setWidget(scrollableDockContent(panel, orderDock_));
    addDockWidget(Qt::LeftDockWidgetArea, orderDock_);
    orderDock_->hide();
}

// HP-ENG-010 : objet à ordonner, avec ses extrémités réelles de couture (repli : le centre).
static optimization::OrderItem
makeOrderItem(const document::EmbroideryObject& obj, Vec2um centroid,
              const std::vector<stitch::ObjectEndpoints>& endpoints) {
    optimization::OrderItem item{obj.id, obj.rgb, centroid, obj.locked};
    for (const auto& e : endpoints) {
        if (e.id == obj.id) {
            item.entry = e.first;
            item.exit = e.last;
            break;
        }
    }
    return item;
}

void MainWindow::refreshOrderPanel() {
    if (orderList_ == nullptr) {
        return;
    }
    const int previousRow = orderList_->currentRow();
    // Même coût que « Appliquer » : extrémités de la séquence affichée.
    const auto endpoints =
        sequence_ ? stitch::object_endpoints(*sequence_) : std::vector<stitch::ObjectEndpoints>{};
    std::vector<optimization::OrderItem> items;
    {
        // Signaux bloqués : clear() émettait currentRowChanged(-1), qui vidait la sélection de
        // broderie avant de la rétablir (clignotement de l'inspecteur et du canevas).
        const QSignalBlocker block(orderList_);
        orderList_->clear();
        int rank = 0;
        orderRows_ = project_.sewingOrder(); // calque d'abord, puis ordre du document
        for (const std::size_t index : orderRows_) {
            const auto& obj = project_.embroidery_objects[index];
            // Même libellé que le panneau Document (rang, type, nom, masqué, ordre figé).
            auto* item = new QListWidgetItem(DocumentPanel::itemText(obj, ++rank));
            item->setIcon(icons::colorSwatch(QColor(obj.rgb[0], obj.rgb[1], obj.rgb[2])));
            orderList_->addItem(item);
            items.push_back(makeOrderItem(obj, embroideryCentroid(obj), endpoints));
        }
        // La ligne courante suit la sélection de broderie si elle existe, sinon l'ancienne.
        int row = previousRow;
        if (selectedEmbroidery_) {
            for (std::size_t i = 0; i < orderRows_.size(); ++i) {
                if (project_.embroidery_objects[orderRows_[i]].id == *selectedEmbroidery_) {
                    row = static_cast<int>(i);
                    break;
                }
            }
        }
        if (row >= 0 && row < orderList_->count()) {
            orderList_->setCurrentRow(row);
        }
    }
    updateOrderButtons();
    if (auto* options =
            orderDock_->findChild<OrderOptionsWidget*>(QStringLiteral("orderOptions"))) {
        options->setEndpoints(project_.finishing.design_start, project_.finishing.design_end);
    }
    applySimulationHighlight();

    const auto cost = optimization::compute_cost(items, project_.finishing.design_start,
                                                 project_.finishing.design_end);
    orderCostLabel_->setText(tr("Trajet estimé : %1 mm — %2 changement(s) de fil")
                                 .arg(cost.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(cost.color_changes));
    orderCostLabel_->setToolTip(tr("Distance à vide entre objets successifs (de la fin de l'un "
                                   "au début du suivant), hors points de couture."));
    setDockAutoVisible(orderDock_, !project_.embroidery_objects.empty());
}

void MainWindow::updateOrderButtons() {
    if (orderList_ == nullptr || orderUpBtn_ == nullptr) {
        return;
    }
    // Monter/Descendre grisés aux bornes ; le bouton Figer reflète l'objet de la ligne.
    const int row = orderList_->currentRow();
    const auto* current = orderRowObject(row);
    // Un objet ne change pas de calque en se déplaçant : Monter/Descendre s'arrêtent à la
    // frontière de son calque (HP-OBJ-024).
    const auto* above = orderRowObject(row - 1);
    const auto* below = orderRowObject(row + 1);
    orderUpBtn_->setEnabled(current != nullptr && above != nullptr &&
                            project_.calqueRankOf(*above) == project_.calqueRankOf(*current));
    orderDownBtn_->setEnabled(current != nullptr && below != nullptr &&
                              project_.calqueRankOf(*below) == project_.calqueRankOf(*current));
    orderLockBtn_->setEnabled(current != nullptr);
    const QSignalBlocker block(orderLockBtn_);
    orderLockBtn_->setChecked(current != nullptr && current->locked);
}

const document::EmbroideryObject* MainWindow::orderRowObject(int row) const {
    if (row < 0 || static_cast<std::size_t>(row) >= orderRows_.size() ||
        orderRows_[static_cast<std::size_t>(row)] >= project_.embroidery_objects.size()) {
        return nullptr;
    }
    return &project_.embroidery_objects[orderRows_[static_cast<std::size_t>(row)]];
}

int MainWindow::stitchTypeIndex(const document::EmbroideryObject& object) {
    return stitchTypeIndexOf(object);
}

double MainWindow::regionAreaMm2(const document::EmbroideryObject& object) const {
    const auto* vec = project_.findObject(object.source_vector);
    return vec == nullptr ? 0.0 : vectorAreaMm2(*vec);
}

double MainWindow::vectorAreaMm2(const document::VectorObject& vec) {
    const auto shoelace_um2 = [](const geometry::Path& path) {
        double a = 0.0;
        const auto& n = path.nodes;
        for (std::size_t i = 0; i < n.size(); ++i) {
            const auto& p0 = n[i].pos;
            const auto& p1 = n[(i + 1) % n.size()].pos;
            a += static_cast<double>(p0.x.value) * p1.y.value -
                 static_cast<double>(p1.x.value) * p0.y.value;
        }
        return std::abs(a) / 2.0;
    };
    double area_um2 = 0.0;
    for (const auto& set : vec.paths) {
        area_um2 += shoelace_um2(set.outer);
        for (const auto& hole : set.holes) {
            area_um2 -= shoelace_um2(hole);
        }
    }
    return std::max(0.0, area_um2) / 1e6;
}

bool MainWindow::calqueShowsShape(const document::VectorObject& object) const {
    const document::Calque* calque = project_.calqueOfShape(object.id);
    return calque == nullptr || calque->visible;
}

bool MainWindow::vectorPassesFilter(const document::VectorObject& object) const {
    if (!calqueShowsShape(object)) {
        return false; // calque masqué (HP-OBJ-024)
    }
    if (!isolatedIds_.empty() &&
        std::find(isolatedIds_.begin(), isolatedIds_.end(), object.id) == isolatedIds_.end()) {
        return false; // mode Isoler : seules les formes isolées sont visibles et saisissables
    }
    if (minAreaMm2_ > 0.0 && vectorAreaMm2(object) < minAreaMm2_) {
        return false;
    }
    bool linked = false;
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.source_vector != object.id) {
            continue;
        }
        linked = true;
        // Type et couleur seulement : la taille vient de la forme elle-même (ci-dessus).
        const int typeIndex = stitchTypeIndex(emb);
        const std::uint32_t key = (static_cast<std::uint32_t>(emb.rgb[0]) << 16) |
                                  (static_cast<std::uint32_t>(emb.rgb[1]) << 8) |
                                  static_cast<std::uint32_t>(emb.rgb[2]);
        if (showType_[static_cast<std::size_t>(stitchFilterFamily(typeIndex))] &&
            hiddenColors_.count(key) == 0) {
            return true;
        }
    }
    return !linked;
}

bool MainWindow::objectPassesFilter(const document::EmbroideryObject& object) const {
    if (!object.visible || !project_.calqueVisible(object)) {
        return false;
    }
    // Filtre « remplissage » : tatami et directionnel ensemble (index 1).
    const int typeIndex = stitchTypeIndex(object);
    if (!showType_[static_cast<std::size_t>(stitchFilterFamily(typeIndex))]) {
        return false;
    }
    const std::uint32_t key = (static_cast<std::uint32_t>(object.rgb[0]) << 16) |
                              (static_cast<std::uint32_t>(object.rgb[1]) << 8) |
                              static_cast<std::uint32_t>(object.rgb[2]);
    if (hiddenColors_.count(key) != 0) {
        return false;
    }
    if (minAreaMm2_ > 0.0 && regionAreaMm2(object) < minAreaMm2_) {
        return false;
    }
    return true;
}

void MainWindow::buildFilterPanel() {
    filterDock_ = new QDockWidget(tr("Filtres d'affichage"), this);
    filterDock_->setObjectName(QStringLiteral("filterDock"));
    filterDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(filterDock_);
    auto* layout = new QVBoxLayout(panel);

    auto* showAllBtn = new QPushButton(tr("Tout afficher"), panel);
    showAllBtn->setObjectName(QStringLiteral("filterShowAll"));
    showAllBtn->setToolTip(
        tr("Réaffiche tous les types, toutes les couleurs et toutes les tailles. "
           "Clic droit sur un type ou une couleur : n'afficher que celui-ci."));
    connect(showAllBtn, &QPushButton::clicked, this, &MainWindow::showAllFilters);
    layout->addWidget(showAllBtn);

    // Types et taille minimale reviennent d'une session à l'autre ; les couleurs masquées non
    // (elles dépendent du projet ouvert).
    if (!qEnvironmentVariableIsSet("OPENSTITCH_NO_PERSISTED_FILTERS")) {
        QSettings settings;
        for (std::size_t t = 0; t < showType_.size(); ++t) {
            showType_[t] =
                settings.value(QStringLiteral("filters/showType%1").arg(t), true).toBool();
        }
        minAreaMm2_ =
            std::max(0.0, settings.value(QStringLiteral("filters/minAreaMm2"), 0.0).toDouble());
    }
    filterIndicator_ = new QPushButton(this);
    filterIndicator_->setObjectName(QStringLiteral("button_filterIndicator"));
    filterIndicator_->setFlat(true);
    filterIndicator_->setCursor(Qt::PointingHandCursor);
    filterIndicator_->setToolTip(tr("Des filtres d'affichage masquent des objets. Cliquer pour "
                                    "tout réafficher."));
    filterIndicator_->hide();
    connect(filterIndicator_, &QPushButton::clicked, this, &MainWindow::showAllFilters);
    statusBar()->addPermanentWidget(filterIndicator_);

    layout->addWidget(new QLabel(tr("Types de points :"), panel));
    const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin")};
    for (int t = 0; t < 3; ++t) {
        auto* check = new QCheckBox(names[t], panel);
        check->setChecked(showType_[static_cast<std::size_t>(t)]);
        check->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(check, &QCheckBox::customContextMenuRequested, this, [this, t, check](QPoint pos) {
            QMenu menu(this);
            connect(menu.addAction(tr("Afficher uniquement ce type")), &QAction::triggered, this,
                    [this, t] { isolateFilterType(t); });
            connect(menu.addAction(tr("Tout afficher")), &QAction::triggered, this,
                    &MainWindow::showAllFilters);
            menu.exec(check->mapToGlobal(pos));
        });
        connect(check, &QCheckBox::toggled, this, [this, t](bool on) {
            showType_[static_cast<std::size_t>(t)] = on;
            saveFilterSettings();
            updateFilterIndicator();
            displayImage(processed_);
        });
        typeChecks_[static_cast<std::size_t>(t)] = check;
        layout->addWidget(check);
    }

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Taille min. des zones :"), panel));
    minAreaSpin_ = new QDoubleSpinBox(panel);
    minAreaSpin_->setRange(0.0, 10'000.0);
    minAreaSpin_->setDecimals(1);
    minAreaSpin_->setSuffix(tr(" mm²"));
    minAreaSpin_->setValue(minAreaMm2_);
    connect(minAreaSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        minAreaMm2_ = v;
        saveFilterSettings();
        updateFilterIndicator();
        displayImage(processed_);
    });
    layout->addWidget(minAreaSpin_);

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Couleurs de fil :"), panel));
    auto* colorBulk = new QHBoxLayout;
    const std::pair<QString, int> bulk[] = {
        {tr("Toutes"), 0}, {tr("Aucune"), 1}, {tr("Inverser"), 2}};
    for (const auto& [label, mode] : bulk) {
        auto* button = new QPushButton(label, panel);
        button->setObjectName(QStringLiteral("button_filterColors%1").arg(mode));
        button->setToolTip(tr("Affiche, masque ou inverse toutes les couleurs de fil."));
        connect(button, &QPushButton::clicked, this, [this, mode] { setAllColorFilters(mode); });
        colorBulk->addWidget(button);
    }
    layout->addLayout(colorBulk);
    auto* colorContainer = new QWidget(panel);
    colorFilterLayout_ = new QVBoxLayout(colorContainer);
    colorFilterLayout_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(colorContainer);

    layout->addStretch(1);
    auto* filterScroll = new QScrollArea(filterDock_);
    filterScroll->setWidgetResizable(true);
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setWidget(panel);
    filterDock_->setWidget(filterScroll);
    addDockWidget(Qt::RightDockWidgetArea, filterDock_);
    filterDock_->hide();
}

void MainWindow::refreshFilterPanel() {
    if (colorFilterLayout_ == nullptr) {
        return;
    }
    // Vide la liste de couleurs.
    while (QLayoutItem* item = colorFilterLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // Couleurs distinctes (ordre stable : première apparition).
    std::vector<std::array<std::uint8_t, 3>> colors;
    for (const auto& emb : project_.embroidery_objects) {
        if (std::find(colors.begin(), colors.end(), emb.rgb) == colors.end()) {
            colors.push_back(emb.rgb);
        }
    }
    for (const auto& rgb : colors) {
        const std::uint32_t key = (static_cast<std::uint32_t>(rgb[0]) << 16) |
                                  (static_cast<std::uint32_t>(rgb[1]) << 8) |
                                  static_cast<std::uint32_t>(rgb[2]);
        auto* check = new QCheckBox();
        check->setChecked(hiddenColors_.count(key) == 0);
        check->setIcon(icons::colorSwatch(QColor(rgb[0], rgb[1], rgb[2])));
        // Nom du fil de nuancier s'il existe, sinon la teinte ; suivi du nombre d'objets.
        int count = 0;
        QString threadName;
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.rgb == rgb) {
                ++count;
                if (threadName.isEmpty() && emb.thread) {
                    threadName = QString::fromStdString(emb.thread->code);
                }
            }
        }
        check->setText(
            tr("%1%2 — %n objet(s)", "", count)
                .arg(QStringLiteral("#%1%2%3")
                         .arg(rgb[0], 2, 16, QLatin1Char('0'))
                         .arg(rgb[1], 2, 16, QLatin1Char('0'))
                         .arg(rgb[2], 2, 16, QLatin1Char('0')),
                     threadName.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(threadName)));
        check->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(check, &QCheckBox::customContextMenuRequested, this,
                [this, key, check](QPoint pos) {
                    QMenu menu(this);
                    connect(menu.addAction(tr("Afficher uniquement cette couleur")),
                            &QAction::triggered, this, [this, key] { isolateFilterColor(key); });
                    connect(menu.addAction(tr("Tout afficher")), &QAction::triggered, this,
                            &MainWindow::showAllFilters);
                    menu.exec(check->mapToGlobal(pos));
                });
        connect(check, &QCheckBox::toggled, this, [this, key](bool on) {
            if (on) {
                hiddenColors_.erase(key);
            } else {
                hiddenColors_.insert(key);
            }
            updateFilterIndicator();
            displayImage(processed_);
        });
        colorFilterLayout_->addWidget(check);
    }
    setDockAutoVisible(filterDock_, !project_.embroidery_objects.empty());
    updateFilterIndicator();
}

void MainWindow::saveFilterSettings() const {
    if (qEnvironmentVariableIsSet("OPENSTITCH_NO_PERSISTED_FILTERS")) {
        return; // tests : aucun état ne fuit d'une suite à l'autre ni vers les réglages de
                // l'utilisateur
    }
    QSettings settings;
    for (std::size_t t = 0; t < showType_.size(); ++t) {
        settings.setValue(QStringLiteral("filters/showType%1").arg(t), showType_[t]);
    }
    settings.setValue(QStringLiteral("filters/minAreaMm2"), minAreaMm2_);
}

void MainWindow::updateFilterIndicator() {
    if (filterIndicator_ == nullptr) {
        return;
    }
    // Objets visibles que SEUL un filtre d'affichage cache : sans cet indicateur, ils semblent
    // avoir disparu.
    int hidden = 0;
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.visible && project_.calqueVisible(emb) && !objectPassesFilter(emb)) {
            ++hidden;
        }
    }
    filterIndicator_->setVisible(hidden > 0);
    filterIndicator_->setText(tr("Filtres : %n objet(s) masqué(s)", "", hidden));
}

void MainWindow::setAllColorFilters(int mode) {
    std::set<std::uint32_t> present;
    for (const auto& emb : project_.embroidery_objects) {
        present.insert(colorKeyOf(emb.rgb));
    }
    if (mode == 0) {
        hiddenColors_.clear();
    } else {
        for (const std::uint32_t key : present) {
            const bool hiddenNow = hiddenColors_.count(key) != 0;
            const bool hide = mode == 1 ? true : !hiddenNow;
            if (hide) {
                hiddenColors_.insert(key);
            } else {
                hiddenColors_.erase(key);
            }
        }
    }
    refreshFilterPanel(); // recoche les cases
    displayImage(processed_);
}

void MainWindow::isolateFilterType(int filterType) {
    for (std::size_t t = 0; t < showType_.size(); ++t) {
        showType_[t] = static_cast<int>(t) == filterType;
        if (typeChecks_[t] != nullptr) {
            const QSignalBlocker blocker(typeChecks_[t]);
            typeChecks_[t]->setChecked(showType_[t]);
        }
    }
    saveFilterSettings();
    updateFilterIndicator();
    displayImage(processed_);
}

void MainWindow::isolateFilterColor(std::uint32_t colorKey) {
    hiddenColors_.clear();
    for (const auto& emb : project_.embroidery_objects) {
        if (colorKeyOf(emb.rgb) != colorKey) {
            hiddenColors_.insert(colorKeyOf(emb.rgb));
        }
    }
    refreshFilterPanel(); // recoche les cases de couleur
    displayImage(processed_);
}

void MainWindow::showAllFilters() {
    showType_ = {true, true, true};
    hiddenColors_.clear();
    minAreaMm2_ = 0.0;
    for (QCheckBox* check : typeChecks_) {
        if (check != nullptr) {
            const QSignalBlocker blocker(check);
            check->setChecked(true);
        }
    }
    if (minAreaSpin_ != nullptr) {
        const QSignalBlocker blocker(minAreaSpin_);
        minAreaSpin_->setValue(0.0);
    }
    saveFilterSettings();
    refreshFilterPanel();
    displayImage(processed_);
}

std::vector<QColor> MainWindow::imageColors() const {
    std::vector<std::uint32_t> seen;
    std::vector<QColor> colors;
    for (const auto& vec : project_.vector_objects) {
        const std::uint32_t key = colorKeyOf(vec.rgb);
        if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
            seen.push_back(key);
            colors.emplace_back(vec.rgb[0], vec.rgb[1], vec.rgb[2]);
        }
    }
    return colors;
}

QColor MainWindow::pickThreadColor(const QColor& initial, const QString& title) {
    // Les « Custom colors » du sélecteur Qt sont globales à l'application : on les remplace par
    // les couleurs de l'image courante (16 emplacements).
    const std::vector<QColor> colors = imageColors();
    for (int i = 0; i < QColorDialog::customCount(); ++i) {
        QColorDialog::setCustomColor(i, static_cast<std::size_t>(i) < colors.size()
                                            ? colors[static_cast<std::size_t>(i)]
                                            : QColor(Qt::white));
    }
    return QColorDialog::getColor(initial, this, title);
}

void MainWindow::moveObjectUp() {
    const int row = orderList_->currentRow();
    if (row <= 0 || !orderUpBtn_->isEnabled()) {
        return;
    }
    std::vector<ObjectId> order;
    for (const std::size_t index : orderRows_) {
        order.push_back(project_.embroidery_objects[index].id);
    }
    std::swap(order[static_cast<std::size_t>(row)], order[static_cast<std::size_t>(row - 1)]);
    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    orderList_->setCurrentRow(row - 1);
    updateActions();
}

void MainWindow::moveObjectDown() {
    const int row = orderList_->currentRow();
    if (row < 0 || !orderDownBtn_->isEnabled()) {
        return;
    }
    std::vector<ObjectId> order;
    for (const std::size_t index : orderRows_) {
        order.push_back(project_.embroidery_objects[index].id);
    }
    std::swap(order[static_cast<std::size_t>(row)], order[static_cast<std::size_t>(row + 1)]);
    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    orderList_->setCurrentRow(row + 1);
    updateActions();
}

void MainWindow::toggleObjectLock() {
    const auto* rowObject = orderRowObject(orderList_->currentRow());
    if (rowObject == nullptr) {
        return;
    }
    const auto& obj = *rowObject;
    undoStack_.execute(std::make_unique<commands::SetEmbroideryLockCommand>(obj.id, !obj.locked),
                       project_);
    refreshOrderPanel();
    updateActions();
}

void MainWindow::applyOrderStrategy() {
    if (project_.embroidery_objects.size() < 2) {
        return;
    }
    // Calques (HP-OBJ-024) : on optimise l'ordre réel de couture, puis on regroupe par calque -- le
    // rang du calque reste la clé principale, l'optimisation ne joue qu'à l'intérieur de chacun.
    // HP-ENG-010 : extrémités réelles de la séquence affichée (repli : centre de l'objet).
    const auto endpoints =
        sequence_ ? stitch::object_endpoints(*sequence_) : std::vector<stitch::ObjectEndpoints>{};
    const auto makeItem = [&](const document::EmbroideryObject& obj) {
        return makeOrderItem(obj, embroideryCentroid(obj), endpoints);
    };
    std::vector<optimization::OrderItem> items;
    for (const std::size_t index : project_.sewingOrder()) {
        items.push_back(makeItem(project_.embroidery_objects[index]));
    }
    const auto strategy =
        static_cast<optimization::OrderStrategy>(orderStrategyCombo_->currentData().toInt());
    // HP-SEQ-001/008 : amélioration locale (borne déterministe) et points de départ / arrivée.
    optimization::OrderOptions options;
    options.start = project_.finishing.design_start;
    options.end = project_.finishing.design_end;
    if (const auto* extra =
            orderDock_->findChild<OrderOptionsWidget*>(QStringLiteral("orderOptions"));
        extra != nullptr && extra->improveEnabled()) {
        options.improve_budget = optimization::kDefaultImproveBudget;
    }
    const auto before = optimization::compute_cost(items, options.start, options.end);
    const auto order =
        project_.groupedByCalque(optimization::optimize_order(items, strategy, options));
    captureOrderJumpsBaseline(endpoints); // surimpression « Sauts à vide » : anciens sauts

    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    updateActions();

    // Coût après (recalculé sur le nouvel ordre).
    std::vector<optimization::OrderItem> reordered;
    for (const auto& obj : project_.embroidery_objects) {
        reordered.push_back(makeItem(obj));
    }
    const auto after = optimization::compute_cost(reordered, options.start, options.end);
    statusBar()->showMessage(tr("Ordre optimisé : trajet %1 → %2 mm, changements de fil %3 → %4")
                                 .arg(before.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(after.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(before.color_changes)
                                 .arg(after.color_changes));
}

void MainWindow::buildSimulationToolbar() {
    simToolbar_ = new QToolBar(tr("Simulation"), this);
    simToolbar_->setObjectName(QStringLiteral("simToolbar"));
    simToolbar_->setMovable(false);
    addToolBar(Qt::BottomToolBarArea, simToolbar_);

    simPlayAct_ = simToolbar_->addAction(tr("▶ Lecture"));
    simPlayAct_->setCheckable(true);
    simPlayAct_->setToolTip(tr("Lit la couture point par point."));
    connect(simPlayAct_, &QAction::toggled, this, &MainWindow::toggleSimulation);
    // Sortie explicite : tant que la simulation est « en cours » (même en pause ou arrivée à la
    // fin), le rendu réaliste reste désactivé (HP-UX-022).
    simExitAct_ = simToolbar_->addAction(tr("⏹ Fin"));
    simExitAct_->setObjectName(QStringLiteral("action_simExit"));
    simExitAct_->setToolTip(
        tr("Quitte la simulation et affiche la couture complète (le rendu réaliste redevient "
           "disponible)."));
    connect(simExitAct_, &QAction::triggered, this, &MainWindow::exitSimulation);
    simPrevColorAct_ = simToolbar_->addAction(tr("⏮ Couleur"));
    simPrevColorAct_->setObjectName(QStringLiteral("action_simPrevColor"));
    simPrevColorAct_->setToolTip(tr("Saute au début du bloc de couleur précédent."));
    connect(simPrevColorAct_, &QAction::triggered, this, [this] { jumpSimulationColor(-1); });
    simNextColorAct_ = simToolbar_->addAction(tr("Couleur ⏭"));
    simNextColorAct_->setObjectName(QStringLiteral("action_simNextColor"));
    simNextColorAct_->setToolTip(tr("Saute au début du bloc de couleur suivant."));
    connect(simNextColorAct_, &QAction::triggered, this, [this] { jumpSimulationColor(+1); });

    simSlider_ = new QSlider(Qt::Horizontal, simToolbar_);
    simSlider_->setMinimum(0);
    simSlider_->setEnabled(false);
    simSlider_->setAccessibleName(tr("Position dans la simulation"));
    connect(simSlider_, &QSlider::valueChanged, this, &MainWindow::onSimSliderMoved);
    simToolbar_->addWidget(simSlider_);

    simLabel_ = new QLabel(tr("— / —"), simToolbar_);
    simLabel_->setMinimumWidth(96);
    simLabel_->setAccessibleName(tr("Progression de la simulation"));
    simToolbar_->addWidget(simLabel_);

    // Vitesse : multiplicateur de l'avance par pas (la base s'adapte déjà à la taille du motif).
    simSpeedCombo_ = new QComboBox(simToolbar_);
    simSpeedCombo_->setAccessibleName(tr("Vitesse de simulation"));
    simSpeedCombo_->setToolTip(tr("Vitesse de lecture de la simulation"));
    simSpeedCombo_->addItem(tr("×0,25"), 0.25);
    simSpeedCombo_->addItem(tr("×1"), 1.0);
    simSpeedCombo_->addItem(tr("×4"), 4.0);
    simSpeedCombo_->addItem(tr("×16"), 16.0);
    simSpeedCombo_->addItem(tr("×64"), 64.0);
    // HP-SIM-001 : cadence réelle d'une machine (700 points/min), indépendante de la taille.
    simSpeedCombo_->addItem(tr("Machine"), -1.0);
    simSpeedCombo_->setCurrentIndex(1);
    simToolbar_->addWidget(simSpeedCombo_);

    // Couleur et objet en cours de couture.
    simSwatch_ = new QLabel(simToolbar_);
    simSwatch_->setFixedSize(14, 14);
    simSwatch_->setAccessibleName(tr("Couleur du fil courant"));
    simToolbar_->addWidget(simSwatch_);
    simObjectLabel_ = new QLabel(simToolbar_);
    simObjectLabel_->setMinimumWidth(140);
    simObjectLabel_->setAccessibleName(tr("Objet en cours de couture"));
    simToolbar_->addWidget(simObjectLabel_);

    // HP-SIM-002/004 : pas à pas, objet précédent/suivant, temps de couture écoulé/total.
    simNavBar_ = new SimulationNavBar(this);
    addToolBar(Qt::BottomToolBarArea, simNavBar_);
    connect(simNavBar_, &SimulationNavBar::stepRequested, this, [this](int step) {
        if (simTimer_->isActive() && simPlayAct_->isChecked()) {
            simPlayAct_->setChecked(false); // un saut manuel met la lecture en pause
        }
        simSlider_->setValue(step);
    });

    simTimer_ = new QTimer(this);
    simTimer_->setInterval(16); // ~60 pas/seconde
    connect(simTimer_, &QTimer::timeout, this, &MainWindow::onSimTick);

    // Un masquage demandé par l'utilisateur (menu Panneaux) l'emporte sur l'affichage
    // automatique de la rangée du bas (cf. refreshThreadStrip) : retenu dans une propriété.
    for (QToolBar* bar : {simToolbar_, static_cast<QToolBar*>(simNavBar_), threadStripBar_}) {
        connect(bar->toggleViewAction(), &QAction::triggered, bar,
                [bar](bool shown) { bar->setProperty("userHidden", !shown); });
    }

    // Les contrôles sont grisés sans séquence ; la rangée n'apparaît qu'avec elle.
    updateSimulationRange();
    refreshThreadStrip();
}

void MainWindow::updateSimulationInfo(int step) {
    if (simNavBar_ != nullptr) {
        simNavBar_->setStep(step);
    }
    {
        std::optional<ObjectId> current;
        if (sequence_ && step >= 0 && step < static_cast<int>(sequence_->commands.size())) {
            const ObjectId source = sequence_->commands[static_cast<std::size_t>(step)].source;
            for (const auto& emb : project_.embroidery_objects) {
                if (emb.id == source) {
                    current = source;
                    break;
                }
            }
        }
        if (current != simHighlightId_) {
            simHighlightId_ = current;
            applySimulationHighlight();
        }
    }
    if (simSwatch_ == nullptr || simObjectLabel_ == nullptr) {
        return;
    }
    if (!sequence_ || step < 0 || step >= static_cast<int>(sequence_->commands.size())) {
        simSwatch_->clear();
        simObjectLabel_->clear();
        return;
    }
    const auto& cmd = sequence_->commands[static_cast<std::size_t>(step)];
    QString name = tr("Design importé");
    std::array<std::uint8_t, 3> rgb{0, 0, 0};
    if (project_.imported_design && !project_.imported_design->color_blocks.empty()) {
        rgb = project_.imported_design->color_blocks.front().rgb;
    }
    for (const auto& emb : project_.embroidery_objects) {
        if (emb.id == cmd.source) {
            name = QString::fromStdString(emb.name);
            rgb = emb.rgb;
            break;
        }
    }
    simSwatch_->setPixmap(
        icons::colorSwatch(QColor(rgb[0], rgb[1], rgb[2]), 12).pixmap(QSize(12, 12)));
    // « fil k/m » : rang du bloc de couleur courant parmi les blocs de la séquence.
    const std::vector<int> starts = colorBlockStarts();
    const auto block = std::upper_bound(starts.begin(), starts.end(), step) - starts.begin();
    const QString thread =
        tr("fil %1/%2").arg(std::max<std::ptrdiff_t>(block, 1)).arg(starts.size());
    simObjectLabel_->setText(simObjectLabel_->fontMetrics().elidedText(
        tr("« %1 » · %2").arg(name, thread), Qt::ElideRight, simObjectLabel_->minimumWidth()));
    simObjectLabel_->setToolTip(tr("%1 — %2").arg(name, thread));
}

void MainWindow::applySimulationHighlight() {
    // Liste Ordre : ligne de l'objet courant en gras sur fond d'accent (la sélection n'est pas
    // touchée).
    if (orderList_ != nullptr) {
        const QColor soft = palette().color(QPalette::Highlight).lighter(170);
        for (int i = 0; i < orderList_->count(); ++i) {
            QListWidgetItem* item = orderList_->item(i);
            const bool current =
                simHighlightId_ && static_cast<std::size_t>(i) < orderRows_.size() &&
                project_.embroidery_objects[orderRows_[std::size_t(i)]].id == *simHighlightId_;
            QFont f = item->font();
            f.setBold(current);
            item->setFont(f);
            item->setData(Qt::BackgroundRole, current ? QVariant(QBrush(soft)) : QVariant());
        }
    }
    if (documentPanel_ != nullptr) {
        documentPanel_->setPlaybackObject(simHighlightId_);
    }
    // Film couleur : pastille du fil de l'objet courant.
    if (threadStrip_ != nullptr) {
        int index = -1;
        if (simHighlightId_ && sequence_) {
            const auto usage = stitch_analysis::thread_usage(project_, *sequence_, &threadLibrary_);
            for (std::size_t u = 0; u < usage.size() && index < 0; ++u) {
                if (std::find(usage[u].objects.begin(), usage[u].objects.end(), *simHighlightId_) !=
                    usage[u].objects.end()) {
                    index = static_cast<int>(u);
                }
            }
        }
        threadStrip_->setCurrent(index);
    }
}

void MainWindow::updateSimulationRange() {
    const bool hasSeq = sequence_ && !sequence_->commands.empty();
    if (simNavBar_ != nullptr) {
        simNavBar_->setSequence(sequence_ ? &*sequence_ : nullptr);
    }
    simPlayAct_->setEnabled(hasSeq);
    simExitAct_->setEnabled(hasSeq);
    simPrevColorAct_->setEnabled(hasSeq);
    simNextColorAct_->setEnabled(hasSeq);
    simSlider_->setEnabled(hasSeq);
    simSpeedCombo_->setEnabled(hasSeq);
    // Le document a changé (cette fonction suit chaque régénération de la séquence) : une
    // simulation en cours porte sur l'ancienne séquence ; elle est réinitialisée plutôt que
    // de continuer sur des points qui n'existent plus.
    if (simulating()) {
        simTimer_->stop();
        if (simPlayAct_->isChecked()) {
            QSignalBlocker block(simPlayAct_);
            simPlayAct_->setChecked(false);
        }
        simPlayAct_->setText(tr("▶ Lecture"));
        simStep_ = -1;
        simWalkValid_ = false;
    }
    if (!hasSeq) {
        simTimer_->stop();
        simPlayAct_->setChecked(false);
        simStep_ = -1;
        simLabel_->setText(tr("— / —"));
        updateSimulationInfo(-1);
        return;
    }
    const int n = static_cast<int>(sequence_->commands.size()) - 1;
    simSlider_->blockSignals(true);
    simSlider_->setMaximum(n);
    simSlider_->setEnabled(true);
    simSlider_->setValue(n);
    simSlider_->blockSignals(false);
    simLabel_->setText(tr("%1 / %2").arg(n).arg(n));
    updateSimulationInfo(-1);
}

void MainWindow::toggleSimulation(/* play */) {
    flushPendingRefresh();
    if (!sequence_ || sequence_->commands.empty()) {
        simPlayAct_->setChecked(false);
        return;
    }
    if (simPlayAct_->isChecked()) {
        // Démarre (ou redémarre depuis le début si on était à la fin).
        if (!simulating() || simStep_ >= static_cast<int>(sequence_->commands.size()) - 1) {
            simStep_ = 0;
            simAccum_ = 0.0;
            renderStitches();
        }
        simPlayAct_->setText(tr("⏸ Pause"));
        simTimer_->start();
    } else {
        simPlayAct_->setText(tr("▶ Lecture"));
        simTimer_->stop();
    }
}

void MainWindow::onSimTick() {
    if (!sequence_) {
        simTimer_->stop();
        return;
    }
    const int n = static_cast<int>(sequence_->commands.size()) - 1;
    // Avance proportionnellement à la taille (motif long = plus rapide), multipliée par la
    // vitesse choisie ; le reste fractionnaire (x0,25) s'accumule d'un pas au suivant.
    const double speed = simSpeedCombo_ != nullptr ? simSpeedCombo_->currentData().toDouble() : 1.0;
    if (speed < 0.0) { // « Machine » : pas par tick = cadence machine x durée du tick
        simAccum_ += stitch_render::simulation_machine_steps_per_second({}) *
                     (static_cast<double>(simTimer_->interval()) / 1000.0);
    } else {
        simAccum_ += std::max(1, (n + 1) / 400) * (speed > 0.0 ? speed : 1.0);
    }
    const int advance = static_cast<int>(simAccum_);
    if (advance < 1) {
        return;
    }
    simAccum_ -= advance;
    simStep_ = std::min(n, simStep_ + advance);
    simSlider_->blockSignals(true);
    simSlider_->setValue(simStep_);
    simSlider_->blockSignals(false);
    simLabel_->setText(tr("%1 / %2").arg(simStep_).arg(n));
    updateSimulationInfo(simStep_);
    appendSimulation(simStep_); // seule la couche points change, et seulement le tronçon neuf
    if (simStep_ >= n) {
        // Fin de lecture : on sort de la simulation (couture complète, rendu réaliste possible).
        exitSimulation();
    }
}

std::vector<int> MainWindow::colorBlockStarts() const {
    std::vector<int> starts{0};
    if (!sequence_) {
        return starts;
    }
    for (std::size_t i = 0; i < sequence_->commands.size(); ++i) {
        if (sequence_->commands[i].type == stitch::CommandType::ColorChange) {
            starts.push_back(static_cast<int>(i));
        }
    }
    return starts;
}

void MainWindow::jumpSimulationColor(int direction) {
    if (!sequence_ || sequence_->commands.empty()) {
        return;
    }
    const int last = static_cast<int>(sequence_->commands.size()) - 1;
    const int current = simulating() ? simStep_ : last;
    const std::vector<int> starts = colorBlockStarts();
    int target = direction > 0 ? last : 0;
    if (direction > 0) {
        const auto it = std::upper_bound(starts.begin(), starts.end(), current);
        target = it == starts.end() ? last : *it;
    } else {
        const auto it = std::lower_bound(starts.begin(), starts.end(), current);
        target = it == starts.begin() ? 0 : *(it - 1);
    }
    simSlider_->setValue(target); // onSimSliderMoved : quitte la simulation si on atteint la fin
}

void MainWindow::exitSimulation() {
    if (!simulating()) {
        return;
    }
    simTimer_->stop();
    if (simPlayAct_->isChecked()) {
        QSignalBlocker block(simPlayAct_);
        simPlayAct_->setChecked(false);
    }
    simPlayAct_->setText(tr("▶ Lecture"));
    simStep_ = -1;
    simWalkValid_ = false;
    if (sequence_ && !sequence_->commands.empty()) {
        const int n = static_cast<int>(sequence_->commands.size()) - 1;
        QSignalBlocker block(simSlider_);
        simSlider_->setValue(n);
        simLabel_->setText(tr("%1 / %2").arg(n).arg(n));
    }
    updateSimulationInfo(-1);
    renderStitches();
}

void MainWindow::onSimSliderMoved(int value) {
    const int n = static_cast<int>(sequence_ ? sequence_->commands.size() : 1) - 1;
    if (value >= n) {
        // Curseur tout à droite : c'est la couture complète, pas une simulation en cours.
        if (simulating()) {
            exitSimulation();
        }
        return;
    }
    simStep_ = value;
    simLabel_->setText(tr("%1 / %2").arg(value).arg(n));
    updateSimulationInfo(value);
    appendSimulation(value);
}

namespace {
// Résumé chiffré d'une séquence, partagé par la fenêtre Statistiques (et lisible par tout
// futur écran qui doit afficher les mêmes chiffres sous la même forme).
QString statsSummaryText(const stitch::StitchStats& stats) {
    const double wMm = to_millimeters(stats.bounds.max.x - stats.bounds.min.x).value;
    const double hMm = to_millimeters(stats.bounds.max.y - stats.bounds.min.y).value;
    return QObject::tr("Points : %1\nSauts : %2\nCoupes : %3\nChangements de couleur : %4\n"
                       "Dimensions : %5 × %6 mm\nFil cousu estimé : %7 m")
        .arg(stats.stitches)
        .arg(stats.jumps)
        .arg(stats.trims)
        .arg(stats.color_changes)
        .arg(wMm, 0, 'f', 1)
        .arg(hMm, 0, 'f', 1)
        .arg(stats.thread_length_um / 1e9, 0, 'f', 2);
}
} // namespace

void MainWindow::showStatistics() {
    flushPendingRefresh();
    if (!sequence_) {
        statusBar()->showMessage(tr("Statistiques indisponibles : générez d'abord des points."),
                                 6000);
        return;
    }
    QMessageBox::information(this, tr("Statistiques de broderie"),
                             statsSummaryText(stitch::compute_stats(*sequence_)));
}

void MainWindow::onCanvasClicked(QPointF posMm) {
    // En mode « Déplacer la vue », un clic ne sélectionne rien.
    if (currentTool_ == Tool::Pan) {
        return;
    }
    if (currentTool_ == Tool::Text) {
        placeTextAt(posMm);
        return;
    }
    if (currentTool_ == Tool::Brush || currentTool_ == Tool::DetailBrush) {
        return; // le pinceau passe par brushPressedMm / brushMovedMm / brushReleasedMm
    }
    if (currentTool_ == Tool::Measure) {
        return; // la règle passe par measureDragMm/measureFinishedMm, jamais par un clic
    }
    if (currentTool_ == Tool::Eyedropper) {
        eyedropperAt(posMm);
        return;
    }
    if (currentTool_ == Tool::Bucket) {
        bucketFillAt(posMm);
        return;
    }
    if (currentTool_ == Tool::ClickToStitch) {
        clickToStitchAt(posMm);
        return;
    }
    if (drawingDirectionGuide()) {
        addDirectionGuidePoint(posMm);
        return;
    }
    if (currentTool_ == Tool::DrawPolygon) {
        if (!snapSuspended()) {
            posMm = findSnapPointMm(posMm).value_or(posMm);
        }
        const Vec2um v = sceneMmToModel(posMm);
        if (!pendingPolygonVertices_.empty()) {
            // Séquence Qt d'un double-clic : press/release/DOUBLECLICK/release —
            // la seconde pression émet aussi un clic simple, quasi au même
            // point que le dernier sommet posé. Ignoré ici pour ne pas poser
            // un sommet fantôme juste avant la fermeture (onCanvasDoubleClicked).
            const auto& last = pendingPolygonVertices_.back();
            const double dx = static_cast<double>(v.x.value - last.x.value);
            const double dy = static_cast<double>(v.y.value - last.y.value);
            if (dx * dx + dy * dy < 4.0) { // < 2 µm de l'un à l'autre
                return;
            }
        }
        pendingPolygonVertices_.push_back(v);
        updatePolygonPreview(posMm);
        updateDrawActionsState();
        statusBar()->showMessage(
            tr("Polygone : %1 sommet(s) — Entrée/double-clic/bouton ✓ pour fermer, Échap pour "
               "annuler, Retour arrière pour retirer le dernier sommet")
                .arg(pendingPolygonVertices_.size()));
        return;
    }
    if (currentTool_ == Tool::DrawSatinColumn) {
        if (!snapSuspended()) {
            posMm = findSnapPointMm(posMm).value_or(posMm);
        }
        const Vec2um v = sceneMmToModel(posMm);
        if (!pendingSatinPoints_.empty()) {
            // Même garde anti-doublon que le polygone : la séquence Qt d'un
            // double-clic pose aussi un clic simple juste avant.
            const auto& last = pendingSatinPoints_.back();
            const double dx = static_cast<double>(v.x.value - last.x.value);
            const double dy = static_cast<double>(v.y.value - last.y.value);
            if (dx * dx + dy * dy < 4.0) {
                return;
            }
        }
        pendingSatinPoints_.push_back(v);
        updateSatinColumnPreview(posMm);
        updateDrawActionsState();
        const bool nextIsSideB = pendingSatinPoints_.size() % 2 == 0;
        const auto pairs = pendingSatinPoints_.size() / 2;
        statusBar()->showMessage(
            nextIsSideB ? tr("Colonne satin : cliquez le côté B de la paire — Échap : annuler")
                        : tr("Colonne satin : %1 paire(s) posée(s) — cliquez le côté A de la paire "
                             "suivante — Entrée/double-clic : terminer (2 paires min.), Retour "
                             "arrière : retirer le dernier point")
                              .arg(pairs));
        return;
    }
    // Couche Vecteurs masquée : un clic « sans effet » sur une forme se dit au lieu de se taire.
    if (!showVectorsAct_->isChecked() && currentTool_ == Tool::Select && !mergeMode_ &&
        !objectsAtPointMm(project_, posMm).empty()) {
        statusBar()->showMessage(tr("Une forme vectorielle est sous le curseur, mais la couche "
                                    "Vecteurs est masquée (touche 3 pour l'afficher)."),
                                 6000);
    }
    // Priorité aux objets vectoriels lorsqu'ils sont affichés (hors fusion).
    if (showVectorsAct_->isChecked() && !mergeMode_) {
        // Même prédicat que Maj/Ctrl+clic et « sélectionner dessous » : visible, filtres
        // d'affichage, tolérance de clic (HP-UX-019). Le plus haut gagne ; un re-clic au même
        // point passe à l'objet suivant dessous (HP-UX-020).
        const std::vector<ObjectId> under = selectableObjectsAt(posMm);
        std::optional<ObjectId> hit;
        if (!under.empty()) {
            hit = cycleTarget(under, posMm);
        }
        if (hit) {
            // la sélection au canevas prime (Replace : T4b route Maj/Ctrl vers
            // applySelectionClick)
            setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {*hit}});
            const auto* object = project_.findObject(*hit);
            QString message = tr("Objet « %1 » — %2 morceau(x), nœuds déplaçables")
                                  .arg(QString::fromStdString(object->name))
                                  .arg(object->paths.size());
            if (under.size() > 1) {
                message += tr(" — %1/%2 sous le curseur (cliquez à nouveau pour le suivant)")
                               .arg(cycleIndex_ + 1)
                               .arg(under.size());
            }
            statusBar()->showMessage(message);
            displayImage(processed_);
            updateActions();
            return;
        }
        // Repli : colonne satin manuelle, dont l'objet vectoriel source est
        // délibérément invisible (§ colonne satin manuelle) — clic direct sur
        // le ruban (rail_a/rail_b), pas sur un objet vectoriel affiché. Un
        // guide satin (SatinGuideItem) n'a PAS le flag ItemIsMovable, donc un
        // clic dessus atteint aussi ce gestionnaire EN PLUS du sien propre —
        // reconstruire la scène ici, synchrone, détruirait l'item pendant son
        // propre mousePressEvent (même piège que les poignées ailleurs dans
        // ce fichier) : différé d'un cycle d'événements, comme elles.
        //
        // Inactif en mode guides/rails (satinGuideModeAct_/railEditModeAct_) :
        // dans ces modes, un clic sur le canevas cible délibérément un guide
        // ou un nœud de l'objet DÉJÀ ciblé, jamais une bascule de sélection
        // vers un autre objet satin (surtout critique si deux objets ont des
        // rubans qui se recouvrent, ex. un réseau à jonctions).
        const bool satinModeActive =
            (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked()) ||
            (railEditModeAct_ != nullptr && railEditModeAct_->isChecked());
        if (!satinModeActive) {
            if (auto* satinHit = satinEmbroideryAt(posMm)) {
                const ObjectId hitId = satinHit->id;
                const std::uint64_t generation = documentGeneration_;
                QTimer::singleShot(0, this, [this, hitId, generation] {
                    if (generation != documentGeneration_) {
                        return;
                    }
                    const auto* emb = project_.findEmbroidery(hitId);
                    if (emb == nullptr) {
                        return;
                    }
                    setSelection({.region = std::nullopt, .embroidery = hitId, .objects = {}});
                    statusBar()->showMessage(tr("Colonne satin « %1 » sélectionnée")
                                                 .arg(QString::fromStdString(emb->name)));
                    displayImage(processed_);
                    updateActions();
                });
                return;
            }
            // Repli « clic dans le vide » : ne touche PAS selectedEmbroidery_ en
            // mode guides/rails (cf. commentaire ci-dessus) — seul selectedObject_
            // (sélection d'objet vectoriel) est concerné dans ce cas, exactement
            // le comportement d'avant l'ajout du satin manuel.
            if (selectedObject_ || selectedEmbroidery_) {
                editSelection([](Selection& sel) {
                    sel.objects.clear();
                    sel.embroidery.reset();
                });
                displayImage(processed_);
                // updateActions() manquait ici (contrairement aux autres chemins
                // de sélection) : nécessaire pour que le mode d'édition des
                // points (Lot 8.2) sorte proprement quand la sélection est
                // perdue par un clic dans le vide plutôt que reportée sur un
                // autre objet.
                updateActions();
            }
        } else if (selectedObject_) {
            editSelection([](Selection& sel) { sel.objects.clear(); });
            displayImage(processed_);
            updateActions();
        }
    }

    if (!showSegAct_->isChecked() || !project_.segmentation) {
        return;
    }
    const auto px = mmToImagePixel(posMm);
    const auto clicked =
        px ? segmentation::region_at(*project_.segmentation, px->x(), px->y()) : std::nullopt;

    if (mergeMode_ && selectedRegion_ && clicked && !isRegionSelected(*clicked)) {
        // Toutes les régions sélectionnées sont absorbées par la région cliquée (couleur gardée).
        // Annulé par l'utilisateur (objets vectoriels liés) : le mode reste actif pour recliquer.
        const std::vector<RegionId> sources = selectedRegionIds();
        mergeAct_->setChecked(false);
        if (!mergeRegions(sources, *clicked)) {
            mergeAct_->setChecked(true);
        }
        return;
    }
    if (mergeMode_) {
        // Le mode reste actif : dire pourquoi ce clic n'a rien fusionné, au lieu de retomber
        // silencieusement dans une sélection simple.
        QString hint = tr("Fusion : cliquez la région cible sur la carte — Échap : annuler");
        if (clicked && selectedRegion_) {
            hint = tr("Fusion : cliquez une région NON sélectionnée (la cible) — Échap : annuler");
        }
        statusBar()->showMessage(hint);
        return;
    }

    // Clic simple : la sélection de régions est remplacée par la région cliquée (ou vidée).
    editSelection([clicked](Selection& sel) {
        sel.region = clicked;
        sel.extraRegions.clear();
    });
    if (clicked) {
        announceRegionSelection();
    }
    displayImage(processed_);
    updateActions();
}

bool MainWindow::regionFullyInside(RegionId id, int x0, int y0, int x1, int y1) const {
    const auto* region = project_.segmentation ? project_.segmentation->find(id) : nullptr;
    if (region == nullptr) {
        return false;
    }
    const auto& seg = *project_.segmentation;
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, seg.width - 1);
    y1 = std::min(y1, seg.height - 1);
    std::size_t inside = 0;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            inside += seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width) +
                                 static_cast<std::size_t>(x)] == id.value;
        }
    }
    return inside == region->pixel_count;
}

bool MainWindow::isRegionSelected(RegionId id) const {
    const std::vector<RegionId> ids = selectedRegionIds();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void MainWindow::onSelectionClicked(QPointF posMm, SelectMode mode) {
    if (currentTool_ != Tool::Select) {
        return;
    }
    if (mode == SelectMode::Replace) {
        onCanvasClicked(posMm); // chemin historique, inchangé
        return;
    }
    // Maj/Ctrl + clic : ajout/bascule d'un objet vectoriel (affiché, hors fusion).
    // Régions : Maj/Ctrl + clic = sélection multiple de régions (cf. plus bas) ; broderies en
    // mono-sélection (Maj/Ctrl = Replace, documenté).
    if (showVectorsAct_->isChecked() && !mergeMode_) {
        const std::vector<ObjectId> under = selectableObjectsAt(posMm);
        if (!under.empty()) {
            applySelectionClick(under.front(), mode);
            return;
        }
        if (satinEmbroideryAt(posMm) != nullptr) {
            onCanvasClicked(posMm);
            return;
        }
    }
    if (showSegAct_->isChecked() && project_.segmentation) {
        const auto px = mmToImagePixel(posMm);
        const auto region =
            px ? segmentation::region_at(*project_.segmentation, px->x(), px->y()) : std::nullopt;
        if (region) {
            if (mergeMode_) {
                onCanvasClicked(posMm); // la région cliquée est la cible de la fusion
            } else {
                // Maj + clic : ajoute la région ; Ctrl + clic : la bascule (sélection multiple).
                selectRegions({*region}, mode);
            }
            return;
        }
    }
    applySelectionClick(std::nullopt, mode); // vide : sans effet pour Maj/Ctrl
}

void MainWindow::onSelectionRectangle(QRectF rectMm, SelectMode mode, bool crossing) {
    // Carte des régions affichée et aucun objet vectoriel touché par le cadre : le cadre
    // sélectionne des régions (fenêtre : régions entièrement dans le cadre ; croisement :
    // régions que le cadre touche). Objets affichés ET touchés : ce sont eux qui sont saisis,
    // et un message dit comment atteindre les régions (sinon le cadre semblait ne rien faire).
    const bool vectorsShown = showVectorsAct_->isChecked();
    std::vector<ObjectId> objectHits;
    if (vectorsShown && currentTool_ == Tool::Select && !mergeMode_) {
        objectHits = objectsInRectangleMm(project_, rectMm, crossing);
        // Même règle que le rendu : un objet masqué par les filtres d'affichage (type, couleur,
        // taille) n'est pas saisi par le cadre, sauf s'il est déjà sélectionné (donc affiché).
        const std::vector<ObjectId> selected = currentSelection().objects;
        objectHits.erase(std::remove_if(objectHits.begin(), objectHits.end(),
                                        [&](ObjectId id) {
                                            const document::VectorObject* vec =
                                                project_.findObject(id);
                                            // Calque verrouillé (HP-OBJ-024) : jamais saisi par le
                                            // cadre.
                                            if (vec != nullptr && project_.shapeEditLocked(id)) {
                                                return true;
                                            }
                                            return vec != nullptr &&
                                                   std::find(selected.begin(), selected.end(),
                                                             id) == selected.end() &&
                                                   !vectorPassesFilter(*vec);
                                        }),
                         objectHits.end());
    }
    const bool regionsUsable = currentTool_ == Tool::Select && !mergeMode_ &&
                               showSegAct_->isChecked() && project_.segmentation &&
                               !processed_.empty();
    if (regionsUsable && vectorsShown && !objectHits.empty()) {
        statusBar()->showMessage(
            tr("Le cadre a saisi des objets vectoriels. Masquez-les (menu Affichage) pour "
               "sélectionner des régions au cadre."),
            6000);
    }
    if (regionsUsable && objectHits.empty()) {
        const double mmPerPx = project_.mm_per_px.value;
        const double left = -processed_.width * mmPerPx / 2.0;
        const double top = -processed_.height * mmPerPx / 2.0;
        const auto toPx = [&](double mm, double origin) {
            return static_cast<int>(std::floor((mm - origin) / mmPerPx));
        };
        const int x0 = toPx(rectMm.left(), left);
        const int y0 = toPx(rectMm.top(), top);
        const int x1 = toPx(rectMm.right(), left);
        const int y1 = toPx(rectMm.bottom(), top);
        std::vector<RegionId> hits =
            segmentation::regions_in_rect(*project_.segmentation, x0, y0, x1, y1);
        if (!crossing) {
            hits.erase(
                std::remove_if(hits.begin(), hits.end(),
                               [&](RegionId id) { return !regionFullyInside(id, x0, y0, x1, y1); }),
                hits.end());
        }
        // Remplacer/Ajouter/Basculer comme pour un clic ; cadre vide en mode Replace : désélection.
        if (hits.empty() && mode == SelectMode::Replace) {
            editSelection([](Selection& sel) {
                sel.region.reset();
                sel.extraRegions.clear();
                sel.objects.clear(); // cadre vide : aussi la sélection d'objets (comme sans carte)
                sel.embroidery.reset();
            });
            displayImage(processed_);
            updateActions();
        } else if (!hits.empty()) {
            selectRegions(hits, mode);
        }
        return;
    }
    if (currentTool_ != Tool::Select || !vectorsShown || mergeMode_) {
        return;
    }
    // Fenêtre (glisser vers la droite) : objets entièrement dans le cadre ;
    // croisement (vers la gauche) : objets que le cadre coupe. Détection pure.
    applySelectionRectangle(objectHits, mode);
}

void MainWindow::onSelectBelow(QPointF posMm, QPoint globalPos, SelectMode mode) {
    if (currentTool_ == Tool::Select && !showVectorsAct_->isChecked() && !mergeMode_) {
        statusBar()->showMessage(
            tr("La couche Vecteurs est masquée (touche 3 pour l'afficher) : rien à sélectionner."),
            6000);
        return;
    }
    if (currentTool_ != Tool::Select || !showVectorsAct_->isChecked() || mergeMode_) {
        return;
    }
    const std::vector<ObjectId> under = selectableObjectsAt(posMm);
    if (under.empty()) {
        statusBar()->showMessage(tr("Aucun objet sous le curseur."));
        return;
    }
    if (selectBelowMenu_ != nullptr) {
        selectBelowMenu_->close();
    }
    // Menu non bloquant (popup) : les objets sous le point, du plus haut au plus bas.
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("selectBelowMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setAccessibleName(tr("Objets sous le curseur"));
    // Noms vides ou en double : suffixe « (#id) » pour les distinguer.
    QMap<QString, int> nameCount;
    for (const ObjectId id : under) {
        if (const auto* object = project_.findObject(id)) {
            ++nameCount[QString::fromStdString(object->name)];
        }
    }
    for (const ObjectId id : under) {
        const auto* object = project_.findObject(id);
        if (object == nullptr) {
            continue;
        }
        QString label = QString::fromStdString(object->name);
        const bool ambiguous = object->name.empty() || nameCount.value(label) > 1;
        if (object->name.empty()) {
            label = tr("(sans nom)");
        }
        if (ambiguous) {
            label += QStringLiteral(" (#%1)").arg(id.value);
        }
        auto* act = menu->addAction(label);
        QPixmap swatch(12, 12);
        swatch.fill(QColor(object->rgb[0], object->rgb[1], object->rgb[2]));
        act->setIcon(QIcon(swatch));
        connect(act, &QAction::triggered, this,
                [this, id, mode] { applySelectionClick(id, mode); });
    }
    selectBelowMenu_ = menu;
    menu->popup(globalPos);
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

void MainWindow::translateObjects(const std::vector<ObjectId>& ids, Vec2um delta, bool coalesce) {
    if (ids.empty()) {
        return;
    }
    if (ids.size() == 1) {
        auto cmd = std::make_unique<commands::TranslateVectorObjectCommand>(ids.front(), delta);
        cmd->setCoalescable(coalesce);
        undoStack_.execute(std::move(cmd), project_);
    } else {
        auto composite = std::make_unique<commands::CompositeCommand>(
            tr("Déplacer %1 objets").arg(ids.size()).toStdString());
        std::string key = "translate-set";
        for (const ObjectId id : ids) {
            auto cmd = std::make_unique<commands::TranslateVectorObjectCommand>(id, delta);
            cmd->setCoalescable(coalesce);
            key += ":" + std::to_string(id.value);
            composite->add(std::move(cmd));
        }
        if (coalesce) {
            composite->setMergeKey(std::move(key));
        }
        undoStack_.execute(std::move(composite), project_);
    }
    refreshImage();
    updateActions();
}

void MainWindow::editSelection(const std::function<void(Selection&)>& edit) {
    Selection sel = currentSelection();
    edit(sel);
    setSelection(std::move(sel));
}

MainWindow::Selection MainWindow::currentSelection() const {
    return Selection{selectedRegion_, selectedEmbroidery_, selectedObjectIds(), extraRegions_};
}

std::vector<ObjectId> MainWindow::selectedObjectIds() const {
    if (!multiSelection_.empty()) {
        return multiSelection_;
    }
    if (selectedObject_) {
        return {*selectedObject_};
    }
    return {};
}

bool MainWindow::isObjectSelected(ObjectId id) const {
    if (!multiSelection_.empty()) {
        return std::find(multiSelection_.begin(), multiSelection_.end(), id) !=
               multiSelection_.end();
    }
    return selectedObject_ && *selectedObject_ == id;
}

void MainWindow::pruneSelection() {
    Selection sel = currentSelection();
    bool changed = false;
    if (sel.region &&
        (!project_.segmentation || project_.segmentation->find(*sel.region) == nullptr)) {
        sel.region.reset();
        changed = true;
    }
    if (sel.embroidery && project_.findEmbroidery(*sel.embroidery) == nullptr) {
        sel.embroidery.reset();
        changed = true;
    }
    const auto dead = [this](ObjectId id) { return project_.findObject(id) == nullptr; };
    const auto removed = std::erase_if(sel.objects, dead);
    changed = changed || removed != 0;
    if (changed) {
        setSelection(std::move(sel)); // le dernier restant devient le principal
    }
}

bool MainWindow::checkSelectionInvariants() const {
    if (!desktop::selection_state_is_consistent(selectedObject_, multiSelection_,
                                                selectedRegion_ || selectedEmbroidery_)) {
        return false;
    }
    for (const ObjectId id : selectedObjectIds()) {
        if (project_.findObject(id) == nullptr) {
            return false;
        }
    }
    if (selectedEmbroidery_ && project_.findEmbroidery(*selectedEmbroidery_) == nullptr) {
        return false;
    }
    if (selectedRegion_ &&
        (!project_.segmentation || project_.segmentation->find(*selectedRegion_) == nullptr)) {
        return false;
    }
    return true;
}

void MainWindow::selectionChanged() {
    Q_ASSERT(checkSelectionInvariants());
    displayImage(processed_);
    updateActions();
}

void MainWindow::applySelectionClick(std::optional<ObjectId> hit, SelectMode mode) {
    Selection sel = currentSelection();
    if (!hit) {
        // Clic dans le vide : Replace désélectionne objets et broderie (la région
        // reste à la charge du repli région de onCanvasClicked) ; Maj/Ctrl : rien.
        if (mode != SelectMode::Replace) {
            return;
        }
        sel.objects.clear();
        sel.embroidery.reset();
    } else {
        if (project_.findObject(*hit) == nullptr) {
            return;
        }
        const auto it = std::find(sel.objects.begin(), sel.objects.end(), *hit);
        switch (mode) {
        case SelectMode::Replace:
            sel.objects = {*hit};
            break;
        case SelectMode::Add:
            if (it != sel.objects.end()) {
                return; // déjà présent : sans effet (le principal ne change pas)
            }
            sel.objects.push_back(*hit);
            break;
        case SelectMode::Toggle:
            if (it != sel.objects.end()) {
                sel.objects.erase(it); // retirer le principal promeut le précédent
            } else {
                sel.objects.push_back(*hit); // devient le principal
            }
            break;
        }
        sel.region.reset();
        sel.embroidery.reset();
    }
    setSelection(std::move(sel));
    selectionChanged();
}

void MainWindow::applySelectionRectangle(const std::vector<ObjectId>& hits, SelectMode mode) {
    Selection sel = currentSelection();
    std::vector<ObjectId> valid;
    for (const ObjectId id : hits) {
        if (project_.findObject(id) != nullptr &&
            std::find(valid.begin(), valid.end(), id) == valid.end()) {
            valid.push_back(id);
        }
    }
    switch (mode) {
    case SelectMode::Replace:
        sel.objects = valid; // rectangle vide : désélectionne les objets
        break;
    case SelectMode::Add: {
        bool added = false;
        for (const ObjectId id : valid) {
            if (std::find(sel.objects.begin(), sel.objects.end(), id) == sel.objects.end()) {
                sel.objects.push_back(id);
                added = true;
            }
        }
        if (!added) {
            return;
        }
        break;
    }
    case SelectMode::Toggle: {
        if (valid.empty()) {
            return;
        }
        for (const ObjectId id : valid) {
            const auto it = std::find(sel.objects.begin(), sel.objects.end(), id);
            if (it != sel.objects.end()) {
                sel.objects.erase(it);
            } else {
                sel.objects.push_back(id);
            }
        }
        break;
    }
    }
    sel.region.reset();
    sel.embroidery.reset();
    setSelection(std::move(sel));
    selectionChanged();
}

void MainWindow::deleteSelection() {
    // Mode « Éditer les points » avec des points sélectionnés : Suppr retire ces POINTS. Sans
    // sélection de points, le comportement historique demeure (supprime l'objet, annulable).
    if (stitchPointEditActive() && !stitchSelection_.empty()) {
        editSelectedStitchPoints(0);
        return;
    }
    // Objet de broderie choisi (dock Ordre/Document) : la broderie seule, forme
    // source conservée -- sauf proxy invisible (colonne satin manuelle), dont la
    // suppression emporte la source (comme « Supprimer » du menu contextuel).
    if (selectedEmbroidery_) {
        const auto* emb = project_.findEmbroidery(*selectedEmbroidery_);
        if (emb == nullptr) {
            return;
        }
        const QString name = QString::fromStdString(emb->name);
        const auto* source = project_.findObject(emb->source_vector);
        if (source != nullptr && !source->visible) {
            undoStack_.execute(std::make_unique<commands::RemoveVectorObjectCommand>(source->id),
                               project_);
        } else {
            undoStack_.execute(removeEmbroideryCommand(emb->id), project_);
        }
        setSelection({});
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("« %1 » supprimé.").arg(name));
        return;
    }
    // Région (comportement historique inchangé, RemoveRegionCommand) : testée APRÈS la
    // broderie car le dock Ordre garde la région en sélectionnant une broderie.
    if (selectedRegion_ && project_.segmentation) {
        deleteSelectedRegion();
        return;
    }
    // Objet(s) vectoriel(s) : un seul pas d'annulation (CompositeCommand si > 1).
    const std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.empty()) {
        return;
    }
    if (ids.size() == 1) {
        deleteVectorObject(ids.front()); // exécute, élague la sélection, rafraîchit
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Supprimer %1 objets").arg(ids.size()).toStdString());
    for (const ObjectId id : ids) {
        composite->add(std::make_unique<commands::RemoveVectorObjectCommand>(id));
    }
    undoStack_.execute(std::move(composite), project_);
    setSelection({});
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 objets supprimés.").arg(ids.size()));
}

void MainWindow::deleteSelectedRegion() {
    deleteSelectedRegions();
}

void MainWindow::recolorSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    const auto* region = project_.segmentation->find(*selectedRegion_);
    if (region == nullptr) {
        return;
    }
    const QColor initial(region->rgb[0], region->rgb[1], region->rgb[2]);
    const std::vector<RegionId> ids = selectedRegionIds();
    const QColor color =
        QColorDialog::getColor(initial, this,
                               ids.size() == 1 ? tr("Couleur de la région")
                                               : tr("Couleur des %1 régions").arg(ids.size()));
    if (!color.isValid()) {
        return;
    }
    recolorRegions(ids, {static_cast<std::uint8_t>(color.red()),
                         static_cast<std::uint8_t>(color.green()),
                         static_cast<std::uint8_t>(color.blue())});
}

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
