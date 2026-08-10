// SPDX-License-Identifier: Apache-2.0
#include "main_window.hpp"

#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QColorDialog>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStatusBar>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <numbers>
#include <unordered_map>
#include <variant>

#include "ai_preferences.hpp"
#include "ai_preferences_dialog.hpp"
#include "ai_segmentation_dialog.hpp"
#include "app_theme.hpp"
#include "brightness_dialog.hpp"
#include "canvas_view.hpp"
#include "document_panel.hpp"
#include "empty_state_widget.hpp"
#include "import_dialog.hpp"
#include "node_handle.hpp"

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/document/canvas.hpp"
#include "openstitch/document/image_placement.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/geometry/primitives.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/satin.hpp"
#include "openstitch/stitch_generation/satin_guides.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "properties_panel.hpp"
#include "ruler.hpp"
#include "satin_guide_item.hpp"
#include "ui_icons.hpp"
#include "workflow_panel.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QGraphicsPathItem>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace openstitch::desktop {

// Mode d'édition des points générés (Lot 8.2) : au-delà de ce nombre de
// points déplaçables (is_movable_point), aucune poignée n'est proposée --
// un GraphicsItem par poignée coûte trop cher sur un motif dense. Partagé
// entre renderBase (rendu) et updateActions (gating/sortie de mode + message).
constexpr std::size_t kMaxEditableStitchHandles = 2000;

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

// Point sur une cubique de Bézier à l'abscisse locale t (formule directe,
// pas De Casteljau — seul le RÉSULTAT du point compte ici, contrairement à
// `geometry::insert_node_on_segment` qui doit préserver la forme exacte).
Vec2um cubicPointAt(Vec2um p0, Vec2um p1, Vec2um p2, Vec2um p3, double t) {
    const double u = 1.0 - t;
    const double b0 = u * u * u;
    const double b1 = 3.0 * u * u * t;
    const double b2 = 3.0 * u * t * t;
    const double b3 = t * t * t;
    const double x = b0 * p0.x.value + b1 * p1.x.value + b2 * p2.x.value + b3 * p3.x.value;
    const double y = b0 * p0.y.value + b1 * p1.y.value + b2 * p2.y.value + b3 * p3.y.value;
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x))},
                 Micrometers{static_cast<std::int32_t>(std::lround(y))}};
}

struct RailPointHit {
    std::size_t segment{};
    double t{};
    double distUm{};
};

// Point le plus proche de `click` sur `rail`, échantillonné segment par
// segment (24 pas), tous côtés confondus (droit ou cubique). Sert à localiser
// où insérer un nœud (double-clic en mode remodelage) : l'échantillonnage ne
// sert qu'à choisir un t plausible pour l'utilisateur — la subdivision
// elle-même (`geometry::insert_node_on_segment`) reste exacte quel que soit t.
std::optional<RailPointHit> nearestPointOnRail(const geometry::Path& rail, Vec2um click) {
    const std::size_t n = rail.nodes.size();
    if (n < 2) {
        return std::nullopt;
    }
    const std::size_t edges = rail.closed ? n : n - 1;
    std::optional<RailPointHit> best;
    for (std::size_t e = 0; e < edges; ++e) {
        const auto& a = rail.nodes[e];
        const auto& b = rail.nodes[(e + 1) % n];
        const bool curved = a.tan_out.has_value() || b.tan_in.has_value();
        const Vec2um p0 = a.pos;
        const Vec2um p3 = b.pos;
        const Vec2um p1 = a.tan_out ? a.pos + *a.tan_out : a.pos;
        const Vec2um p2 = b.tan_in ? b.pos + *b.tan_in : b.pos;
        constexpr int kSteps = 24;
        for (int i = 0; i <= kSteps; ++i) {
            const double t = static_cast<double>(i) / kSteps;
            const Vec2um pt = curved ? cubicPointAt(p0, p1, p2, p3, t)
                                     : Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(
                                                   p0.x.value + (p3.x.value - p0.x.value) * t))},
                                             Micrometers{static_cast<std::int32_t>(std::lround(
                                                   p0.y.value + (p3.y.value - p0.y.value) * t))}};
            const double d = length_um(pt - click);
            if (!best || d < best->distUm) {
                best = RailPointHit{e, t, d};
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Export debug (menu contextuel « Déboguer… ») : formatage textuel exhaustif
// d'un objet de broderie, pour diagnostiquer sans ouvrir de débogueur. Pure
// mise en forme, aucune logique métier — les valeurs viennent telles quelles
// du document/de la séquence générée.
// ---------------------------------------------------------------------------

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
        case document::SatinShortStitch::Disabled: return QStringLiteral("Désactivés");
        case document::SatinShortStitch::RemoveAndRedistribute:
            return QStringLiteral("Retirer/redistribuer");
        case document::SatinShortStitch::SingleInset: return QStringLiteral("Inset simple");
        case document::SatinShortStitch::MultiLevelInset:
            return QStringLiteral("Inset multi-niveaux");
    }
    return QStringLiteral("?");
}
QString fmtSplit(document::SatinSplit v) {
    switch (v) {
        case document::SatinSplit::Disabled: return QStringLiteral("Désactivé");
        case document::SatinSplit::Simple: return QStringLiteral("Simple");
        case document::SatinSplit::Staggered: return QStringLiteral("Décalé");
        case document::SatinSplit::DeterministicJitter: return QStringLiteral("Jitter déterministe");
    }
    return QStringLiteral("?");
}
QString fmtCap(document::SatinCap v) {
    switch (v) {
        case document::SatinCap::Flat: return QStringLiteral("Plat");
        case document::SatinCap::Rounded: return QStringLiteral("Arrondi");
        case document::SatinCap::Tapered: return QStringLiteral("Effilé");
        case document::SatinCap::Automatic: return QStringLiteral("Auto");
    }
    return QStringLiteral("?");
}
QString fmtLock(document::SatinLock v) {
    switch (v) {
        case document::SatinLock::None: return QStringLiteral("Aucun");
        case document::SatinLock::BackAndForth: return QStringLiteral("Aller-retour");
        case document::SatinLock::Triangle: return QStringLiteral("Triangle");
        case document::SatinLock::MicroZigzag: return QStringLiteral("Micro-zigzag");
    }
    return QStringLiteral("?");
}
QString fmtStitchPointType(document::StitchPointType v) {
    return v == document::StitchPointType::Stitch ? QStringLiteral("Stitch") : QStringLiteral("Jump");
}
QString fmtCommandType(stitch::CommandType v) {
    switch (v) {
        case stitch::CommandType::Stitch: return QStringLiteral("Stitch");
        case stitch::CommandType::Jump: return QStringLiteral("Jump");
        case stitch::CommandType::Trim: return QStringLiteral("Trim");
        case stitch::CommandType::ColorChange: return QStringLiteral("ColorChange");
        case stitch::CommandType::Stop: return QStringLiteral("Stop");
        case stitch::CommandType::End: return QStringLiteral("End");
    }
    return QStringLiteral("?");
}
QString fmtPass(stitch::StitchPass v) {
    switch (v) {
        case stitch::StitchPass::Underlay: return QStringLiteral("Underlay");
        case stitch::StitchPass::TopStitch: return QStringLiteral("TopStitch");
        case stitch::StitchPass::Travel: return QStringLiteral("Travel");
        case stitch::StitchPass::Lock: return QStringLiteral("Lock");
        case stitch::StitchPass::Manual: return QStringLiteral("Manual");
    }
    return QStringLiteral("?");
}
QString fmtEditState(stitch_generation::ObjectEditState v) {
    switch (v) {
        case stitch_generation::ObjectEditState::Clean: return QStringLiteral("Clean");
        case stitch_generation::ObjectEditState::ManuallyEdited:
            return QStringLiteral("ManuallyEdited");
        case stitch_generation::ObjectEditState::Dirty: return QStringLiteral("Dirty");
    }
    return QStringLiteral("?");
}
QString fmtSeverity(stitch_analysis::Severity v) {
    switch (v) {
        case stitch_analysis::Severity::Info: return QStringLiteral("Info");
        case stitch_analysis::Severity::Warning: return QStringLiteral("Warning");
        case stitch_analysis::Severity::Error: return QStringLiteral("Error");
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
                   .arg(fmtVec(n.pos), fmtNodeType(n.type), fmtOptVec(n.tan_in), fmtOptVec(n.tan_out));
    }
}

// Minimum non dégénéré (en mm de scène) pour qu'un rectangle/ellipse dessiné
// à la main produise une forme exploitable — un simple clic sans glisser ne
// doit pas créer un objet plat invisible.
constexpr double kMinDrawExtentMm = 0.5;

// Tolérance de simplification (Douglas-Peucker) du tracé à main levée : lisse
// le tremblement de la souris/main sans effacer l'intention du tracé. Même
// ordre de grandeur que la densité satin par défaut (0,4 mm) — assez fin pour
// rester fidèle, assez grossier pour ne pas garder des centaines de points
// bruts (un évènement de déplacement souris par pixel).
constexpr Micrometers kFreeformSimplifyTolerance{300};

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("%1 [*]").arg(QString::fromUtf8(kAppName)));
    resize(1100, 800);

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
    buildMainToolbar();
    addToolBarBreak();  // la barre contextuelle sur sa propre rangée
    buildContextToolbar();
    buildToolPalette();
    addToolBarBreak();  // la barre de simulation occupe sa propre rangée
    buildSimulationToolbar();

    buildHelpMenu();

    // Noms accessibles (lecteurs d'écran) sur les grandes zones.
    view_->setAccessibleName(tr("Canevas"));
    view_->setAccessibleDescription(tr("Zone de travail du motif, en millimètres."));
    for (auto* d : {documentDock_, propertiesDock_, workflowDock_, orderDock_, filterDock_,
                    analysisDock_}) {
        if (d != nullptr) {
            d->setAccessibleName(d->windowTitle());
        }
    }

    // Restaure la disposition de l'interface (préférences UI, pas de données
    // métier — celles-ci restent dans le .osp). Les panneaux vides seront
    // masqués ensuite par les refresh.
    {
        // QSettings() par défaut : lit l'organisation/application déjà posées sur
        // QCoreApplication (main.cpp). Un test peut ainsi rediriger vers un fichier
        // temporaire (QCoreApplication::setOrganizationName + QSettings::setPath)
        // sans jamais toucher au registre réel de l'utilisateur.
        QSettings s;
        if (s.contains(QStringLiteral("ui/geometry"))) {
            restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
        }
        if (s.contains(QStringLiteral("ui/windowState"))) {
            restoreState(s.value(QStringLiteral("ui/windowState")).toByteArray());
        }
    }

    toolLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(toolLabel_);
    cursorLabel_ = new QLabel(this);
    cursorLabel_->setMinimumWidth(180);
    statusBar()->addPermanentWidget(cursorLabel_);
    setTool(Tool::Select);
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF mm) {
        cursorLabel_->setText(
            tr("x : %1 mm   y : %2 mm").arg(mm.x(), 0, 'f', 1).arg(mm.y(), 0, 'f', 1));
    });
    connect(view_, &CanvasView::cropSelectedMm, this, &MainWindow::onCropSelected);
    connect(view_, &CanvasView::boxDrawnMm, this, &MainWindow::onBoxDrawn);
    connect(view_, &CanvasView::canvasDoubleClickedMm, this, &MainWindow::onCanvasDoubleClicked);
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF physicalMm) {
        // physicalMm est déjà Y-haut (cf. CanvasView::mouseMoveEvent) ; on
        // revient au repère scène (Y-bas) pour les aperçus QGraphicsItem.
        const QPointF sceneMm(physicalMm.x(), -physicalMm.y());
        // Accroche (façon Fusion 360) : seulement pour les outils où le clic
        // pose réellement le point prévisualisé ici -- cf. commentaire de
        // findSnapPointMm (Bézier/rectangle/ellipse exclus).
        const bool snapEligible =
            currentTool_ == Tool::DrawPolygon || currentTool_ == Tool::DrawSatinColumn;
        const std::optional<QPointF> snap = snapEligible ? findSnapPointMm(sceneMm) : std::nullopt;
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
    connect(view_, &CanvasView::canvasContextMenu, this, &MainWindow::onCanvasContextMenu);
    // Flèches clavier : déplace l'objet vectoriel sélectionné (mode Sélection
    // uniquement) — même commande que le glisser du corps de forme.
    connect(view_, &CanvasView::nudgeRequestedMm, this, [this](QPointF deltaSceneMm) {
        if (!selectedObject_ || currentTool_ != Tool::Select) {
            return;
        }
        const Vec2um delta = sceneMmToModel(deltaSceneMm);
        undoStack_.execute(
            std::make_unique<commands::TranslateVectorObjectCommand>(*selectedObject_, delta),
            project_);
        refreshImage();
        updateActions();
    });
    // Un changement de thème redessine les couches (couleurs des points, repères).
    connect(&AppTheme::instance(), &AppTheme::changed, this, [this] { displayImage(processed_); });

    // État d'accueil, flottant au centre du canevas tant qu'aucun document.
    emptyState_ = new EmptyStateWidget(view_);
    connect(emptyState_, &EmptyStateWidget::openImageRequested, this, &MainWindow::openImage);
    connect(emptyState_, &EmptyStateWidget::openProjectRequested, this, &MainWindow::loadProject);
    connect(emptyState_, &EmptyStateWidget::importDstRequested, this, &MainWindow::importDst);
    connect(view_, &CanvasView::viewChanged, this, &MainWindow::positionEmptyState);

    statusBar()->showMessage(tr("Ouvrez une image (PNG, JPEG, BMP, TIFF) pour commencer."));
    view_->fitCanvas();
    updateActions();
}

MainWindow::~MainWindow() {
    // Les actions et la scène sont des enfants QObject détruits après la
    // partie MainWindow. Aucune de leurs notifications tardives ne doit
    // rappeler un slot du type dérivé dont le destructeur a déjà commencé.
    for (QObject* child : findChildren<QObject*>()) {
        QObject::disconnect(child, nullptr, this, nullptr);
    }
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&Fichier"));
    auto* openAct = fileMenu->addAction(tr("&Ouvrir une image…"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openImage);
    fileMenu->addSeparator();
    auto* saveProjectAct = fileMenu->addAction(tr("&Enregistrer le projet…"));
    saveProjectAct->setShortcut(QKeySequence::Save);
    connect(saveProjectAct, &QAction::triggered, this, &MainWindow::saveProject);
    auto* loadProjectAct = fileMenu->addAction(tr("Ou&vrir un projet…"));
    connect(loadProjectAct, &QAction::triggered, this, &MainWindow::loadProject);
    fileMenu->addSeparator();
    exportDstAct_ = fileMenu->addAction(tr("&Exporter en DST…"));
    connect(exportDstAct_, &QAction::triggered, this, &MainWindow::exportDst);
    auto* importDstAct = fileMenu->addAction(tr("&Importer un DST…"));
    connect(importDstAct, &QAction::triggered, this, &MainWindow::importDst);
    fileMenu->addSeparator();
    auto* importDxfAct = fileMenu->addAction(tr("Importer un &DXF…"));
    connect(importDxfAct, &QAction::triggered, this, &MainWindow::importDxf);
    auto* exportDxfAct = fileMenu->addAction(tr("Exporter en D&XF…"));
    connect(exportDxfAct, &QAction::triggered, this, &MainWindow::exportDxf);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Quitter"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Édition"));
    undoAct_ = editMenu->addAction(tr("&Annuler"));
    undoAct_->setObjectName(QStringLiteral("action_undo"));
    undoAct_->setShortcut(QKeySequence::Undo);
    connect(undoAct_, &QAction::triggered, this, &MainWindow::undo);
    redoAct_ = editMenu->addAction(tr("&Rétablir"));
    redoAct_->setObjectName(QStringLiteral("action_redo"));
    redoAct_->setShortcut(QKeySequence::Redo);
    connect(redoAct_, &QAction::triggered, this, &MainWindow::redo);
    editMenu->addSeparator();
    auto* aiPrefsAct = editMenu->addAction(tr("Préférences — &Intelligence artificielle…"));
    connect(aiPrefsAct, &QAction::triggered, this, &MainWindow::openAiPreferences);

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

    addOpAction(tr("Débruitage lé&ger"), image::MedianDenoiseOp{1});
    addOpAction(tr("Débruitage &moyen"), image::MedianDenoiseOp{2});

    auto* quantAct = imageMenu->addAction(tr("&Quantifier les couleurs…"));
    connect(quantAct, &QAction::triggered, this, &MainWindow::quantizeColors);
    imageActions_.append(quantAct);

    imageMenu->addSeparator();
    addOpAction(tr("Symétrie &horizontale"), image::FlipOp{true});
    addOpAction(tr("Symétrie &verticale"), image::FlipOp{false});
    addOpAction(tr("Rotation 90° h&oraire"), image::Rotate90Op{1});
    addOpAction(tr("Rotation 90° &antihoraire"), image::Rotate90Op{3});

    imageMenu->addSeparator();
    cropAct_ = imageMenu->addAction(tr("&Recadrer (sélection)"));
    cropAct_->setCheckable(true);
    connect(cropAct_, &QAction::toggled, this,
            [this](bool on) { setTool(on ? Tool::Rect : Tool::Select); });
    imageActions_.append(cropAct_);

    auto* segMenu = menuBar()->addMenu(tr("&Segmentation"));
    auto* segAct = segMenu->addAction(tr("&Segmenter l'image…"));
    connect(segAct, &QAction::triggered, this, &MainWindow::segmentImage);
    imageActions_.append(segAct);

    showSegAct_ = segMenu->addAction(tr("&Afficher la carte des régions"));
    showSegAct_->setCheckable(true);
    connect(showSegAct_, &QAction::toggled, this, [this] { displayImage(processed_); });

    segMenu->addSeparator();
    mergeAct_ = segMenu->addAction(tr("&Fusionner avec… (cliquer la région cible)"));
    mergeAct_->setCheckable(true);
    connect(mergeAct_, &QAction::toggled, this, [this](bool on) { mergeMode_ = on; });
    regionActions_.append(mergeAct_);

    auto* delRegionAct = segMenu->addAction(tr("&Supprimer la région sélectionnée"));
    delRegionAct->setObjectName(QStringLiteral("action_deleteRegion"));
    delRegionAct->setShortcut(QKeySequence::Delete);
    connect(delRegionAct, &QAction::triggered, this, &MainWindow::deleteSelectedRegion);
    regionActions_.append(delRegionAct);

    auto* recolorAct = segMenu->addAction(tr("&Recolorer la région sélectionnée…"));
    connect(recolorAct, &QAction::triggered, this, &MainWindow::recolorSelectedRegion);
    regionActions_.append(recolorAct);

    segMenu->addSeparator();
    auto* vectorizeAct = segMenu->addAction(tr("Convertir la région en objet &vectoriel"));
    connect(vectorizeAct, &QAction::triggered, this, &MainWindow::vectorizeSelectedRegion);
    regionActions_.append(vectorizeAct);

    auto* embMenu = menuBar()->addMenu(tr("&Broderie"));
    auto* autoAct = embMenu->addAction(tr("Numérisation &automatique"));
    connect(autoAct, &QAction::triggered, this, &MainWindow::autoDigitize);
    auto* aiSegmentAct = embMenu->addAction(icons::aiSegment(), tr("Segmenter avec l'&IA…"));
    aiSegmentAct->setObjectName(QStringLiteral("action_segmentWithAi"));
    connect(aiSegmentAct, &QAction::triggered, this, &MainWindow::segmentWithAi);
    embMenu->addSeparator();
    createStitchAct_ = embMenu->addAction(tr("Créer un objet de &point de contour…"));
    createStitchAct_->setObjectName(QStringLiteral("action_createStitch"));
    connect(createStitchAct_, &QAction::triggered, this, &MainWindow::createRunningStitchObject);
    createTatamiAct_ = embMenu->addAction(tr("Créer un remplissage &tatami…"));
    connect(createTatamiAct_, &QAction::triggered, this, &MainWindow::createTatamiObject);
    createSatinAct_ = embMenu->addAction(tr("Créer une colonne &satin…"));
    connect(createSatinAct_, &QAction::triggered, this, &MainWindow::createSatinObject);
    autoSatinAct_ = embMenu->addAction(tr("Convertir automatiquement en satin…"));
    autoSatinAct_->setToolTip(
        tr("Construit des colonnes satin (rails + barreaux) depuis le squelette de la forme."));
    connect(autoSatinAct_, &QAction::triggered, this, &MainWindow::autoConvertToSatin);
    embMenu->addSeparator();
    fillAngleAct_ = embMenu->addAction(tr("&Orientation du remplissage…"));
    fillAngleAct_->setToolTip(
        tr("Change l'angle des fils du remplissage tatami sélectionné (clic sur la forme "
           "ou dans l'ordre de couture)."));
    connect(fillAngleAct_, &QAction::triggered, this, &MainWindow::changeFillAngle);
    convertSatinAct_ = embMenu->addAction(tr("Convertir les satins auto en &tatami"));
    convertSatinAct_->setToolTip(
        tr("Remplace les colonnes satin automatiques (qui débordent sur les formes "
           "concaves) par des remplissages tatami découpés sur la région."));
    connect(convertSatinAct_, &QAction::triggered, this, &MainWindow::convertSatinsToTatami);
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
    satinEditModeAct_->setShortcut(QKeySequence(Qt::Key_G));
    satinEditModeAct_->setToolTip(
        tr("Affiche ensemble les nœuds des deux rails (glisser pour déplacer, double-clic "
           "pour ajouter un nœud) et les guides transversaux (glisser une extrémité) de la "
           "colonne satin sélectionnée (G)."));
    connect(satinEditModeAct_, &QAction::toggled, this, [this](bool on) {
        satinGuideModeAct_->setChecked(on);
        railEditModeAct_->setChecked(on);
    });
    auto* satinAdvancedMenu = embMenu->addMenu(tr("Remodelage satin (avancé)"));
    satinAdvancedMenu->setToolTip(
        tr("N'affiche qu'un seul des deux aspects (rails ou guides) — utile pour une "
           "colonne dense où les deux ensemble surchargeraient l'affichage."));
    satinGuideModeAct_ = satinAdvancedMenu->addAction(tr("Éditer seulement les guides satin…"));
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
    connect(removeSatinGuideAct_, &QAction::triggered, this,
            &MainWindow::removeSelectedSatinGuide);
    railEditModeAct_ = satinAdvancedMenu->addAction(tr("Éditer seulement les rails satin…"));
    railEditModeAct_->setObjectName(QStringLiteral("action_satinRailEditMode"));
    railEditModeAct_->setCheckable(true);
    railEditModeAct_->setToolTip(
        tr("Afficher les nœuds des deux rails de la colonne satin sélectionnée et les "
           "glisser pour remodeler la colonne. Double-clic sur un rail : ajoute un nœud "
           "(forme exactement conservée)."));
    connect(railEditModeAct_, &QAction::toggled, this, &MainWindow::onSatinRailEditModeToggled);
    embMenu->addSeparator();
    statsAct_ = embMenu->addAction(tr("&Statistiques…"));
    connect(statsAct_, &QAction::triggered, this, &MainWindow::showStatistics);

    auto* viewMenu = menuBar()->addMenu(tr("&Affichage"));
    auto* layersMenu = viewMenu->addMenu(tr("&Calques"));
    showImageAct_ = layersMenu->addAction(tr("&Image"));
    showImageAct_->setCheckable(true);
    showImageAct_->setChecked(true);
    connect(showImageAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    // La carte des régions (segmentation) : action partagée avec le menu Segmentation.
    layersMenu->addAction(showSegAct_);
    showVectorsAct_ = layersMenu->addAction(tr("&Vecteurs"));
    showVectorsAct_->setCheckable(true);
    showVectorsAct_->setChecked(true);
    connect(showVectorsAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    showStitchesAct_ = layersMenu->addAction(tr("&Broderie (points)"));
    showStitchesAct_->setCheckable(true);
    showStitchesAct_->setChecked(true);
    connect(showStitchesAct_, &QAction::toggled, this, [this] { displayImage(processed_); });
    viewMenu->addSeparator();
    auto* zoomInAct = viewMenu->addAction(tr("Zoom &avant"));
    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAct, &QAction::triggered, view_, &CanvasView::zoomIn);
    auto* zoomOutAct = viewMenu->addAction(tr("Zoom a&rrière"));
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAct, &QAction::triggered, view_, &CanvasView::zoomOut);
    auto* fitAct = viewMenu->addAction(tr("A&juster au canevas"));
    fitAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(fitAct, &QAction::triggered, view_, &CanvasView::fitCanvas);

    viewMenu->addSeparator();
    auto* hoopAct = viewMenu->addAction(tr("Taille du &cadre…"));
    hoopAct->setToolTip(tr("Définit la zone physique de broderie (cadre)."));
    connect(hoopAct, &QAction::triggered, this, &MainWindow::setHoopSize);

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

    viewMenu->addSeparator();
    auto* hidePanelsAct = viewMenu->addAction(tr("&Masquer les panneaux"));
    hidePanelsAct->setCheckable(true);
    hidePanelsAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    hidePanelsAct->setToolTip(tr("Mode canevas : masque tous les panneaux."));
    connect(hidePanelsAct, &QAction::toggled, this, [this](bool hide) {
        const std::vector<QDockWidget*> docks{documentDock_,  propertiesDock_, workflowDock_,
                                              orderDock_,     filterDock_,     analysisDock_};
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

void MainWindow::openImage() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Ouvrir une image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;Tous les fichiers (*)"));
    if (file.isEmpty()) {
        return;
    }

    auto loaded = image::load_image(std::filesystem::path(file.toStdWString()));
    if (!loaded) {
        QMessageBox::warning(this, tr("Erreur"), QString::fromStdString(loaded.error().message));
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
        QMessageBox::warning(this, tr("Erreur"), tr("Taille physique invalide."));
        return;
    }

    project_ = document::Project{};
    ++documentGeneration_;  // nouveau document : invalide les commandes différées en vol
    project_.mm_per_px = document::mm_per_pixel(*placement, loaded->width);
    project_.original = std::move(*loaded);
    undoStack_.clear();
    sequence_.reset();
    sequenceImported_ = false;
    selectedRegion_.reset();
    selectedObject_.reset();
    // Nouveau document : sortie propre du mode d'édition des points (Lot 8.2),
    // sans passer par onStitchEditModeToggled (project_ n'est pas encore
    // rafraîchi -- displayImage() y serait prématuré, refreshImage() ci-dessous
    // s'en charge).
    if (stitchEditModeAct_ != nullptr) {
        QSignalBlocker block(stitchEditModeAct_);
        stitchEditModeAct_->setChecked(false);
    }
    stitchEditTarget_.reset();
    stitchEditView_.reset();

    refreshImage();
    view_->fitCanvas();
    statusBar()->showMessage(tr("%1 — %2 × %3 px")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(project_.original.width)
                                 .arg(project_.original.height));
    updateActions();
    setWindowModified(false);  // nouveau document propre
}

void MainWindow::executeOp(image::ImageOp op) {
    if (!project_.hasImage()) {
        return;
    }
    // Validation avant mutation : une opération invalide n'entre pas dans la pile.
    auto preview = image::apply_op(processed_, op);
    if (!preview) {
        QMessageBox::warning(this, tr("Opération impossible"),
                             QString::fromStdString(preview.error().message));
        return;
    }
    undoStack_.execute(std::make_unique<commands::AppendImageOpCommand>(std::move(op)), project_);
    refreshImage();
    updateActions();
}

void MainWindow::undo() {
    if (undoStack_.undo(project_)) {
        refreshImage();
        updateActions();
    }
}

void MainWindow::redo() {
    if (undoStack_.redo(project_)) {
        refreshImage();
        updateActions();
    }
}

// Entrée/sortie du mode d'édition des points (Lot 8.2). L'action n'est
// activable (cf. updateActions -> stitchEditModeAct_->setEnabled) que pour un
// objet de broderie sélectionné et non Dirty ; ce garde-fou reste néanmoins
// revérifié ici (défense en profondeur : un déclenchement programmatique ou
// une course sélection/activation ne doit jamais laisser le mode actif sans
// cible valide).
void MainWindow::onStitchEditModeToggled(bool on) {
    stitchEditTarget_.reset();
    stitchEditView_.reset();
    if (on) {
        if (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked()) {
            satinGuideModeAct_->setChecked(false);
        }
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (auto view = stitch_generation::edit_view(project_, emb->id);
                view && view->state != stitch_generation::ObjectEditState::Dirty) {
                stitchEditTarget_ = emb->id;
                stitchEditView_ = std::move(*view);
            }
        }
        if (!stitchEditTarget_) {
            // Contexte devenu invalide entre le clic et ce traitement (perte de
            // sélection, objet Dirty) : on annule silencieusement l'activation.
            QSignalBlocker block(stitchEditModeAct_);
            stitchEditModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(
                tr("Édition des points : glissez un point pour le déplacer. Échap pour quitter."));
        }
    }
    displayImage(processed_);  // fait apparaître/disparaître les poignées
    updateActions();
}

void MainWindow::onSatinGuideModeToggled(bool on) {
    satinGuideTarget_.reset();
    selectedSatinGuide_.reset();
    if (on) {
        if (stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked()) {
            stitchEditModeAct_->setChecked(false);
        }
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (const auto* satin = std::get_if<document::SatinParams>(&emb->params);
                satin != nullptr && satin->rungs.size() >= 2) {
                satinGuideTarget_ = emb->id;
            }
        }
        if (!satinGuideTarget_) {
            QSignalBlocker block(satinGuideModeAct_);
            satinGuideModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(
                tr("Guides satin : glissez une extrémité le long de son rail (Maj+glisser un "
                   "guide lié déplace tout le groupe). Échap pour quitter."));
        }
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::onSatinRailEditModeToggled(bool on) {
    railEditTarget_.reset();
    if (on) {
        // Ne coupe PLUS le mode guides (satinGuideModeAct_) : les deux
        // coexistent délibérément depuis l'introduction du mode unifié
        // (satinEditModeAct_, § remodelage satin) — rails et guides
        // s'affichent ensemble, plus besoin de choisir entre les deux.
        if (stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked()) {
            stitchEditModeAct_->setChecked(false);
        }
        if (const auto* emb = resolveSelectedEmbroidery()) {
            if (emb->is_satin()) {
                railEditTarget_ = emb->id;
            }
        }
        if (!railEditTarget_) {
            QSignalBlocker block(railEditModeAct_);
            railEditModeAct_->setChecked(false);
        } else {
            statusBar()->showMessage(
                tr("Rails satin : glissez un nœud pour le déplacer, double-clic sur un rail "
                   "pour ajouter un nœud. Échap pour quitter."));
        }
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::addSatinGuide() {
    if (!satinGuideTarget_) {
        return;
    }
    auto* obj = project_.findEmbroidery(*satinGuideTarget_);
    auto* satin = obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
    if (satin == nullptr) {
        return;
    }
    if (selectedSatinGuide_) {
        if (const auto junction = stitch_generation::satin_guide_junction(
                *satin, *selectedSatinGuide_)) {
            const auto refs = stitch_generation::satin_junction_guides(
                project_, obj->source_vector, *junction);
            const auto linkId =
                stitch_generation::next_satin_guide_link_id(project_, obj->source_vector);
            if (!linkId) {
                statusBar()->showMessage(
                    tr("Impossible d'ajouter les guides : identifiants de liaison épuisés."));
                return;
            }
            std::vector<commands::SatinGuideAddition> additions;
            additions.reserve(refs.size());
            std::optional<std::size_t> selected;
            for (const auto& ref : refs) {
                if (std::any_of(additions.begin(), additions.end(), [&](const auto& addition) {
                        return addition.id == ref.embroidery_id;
                    })) {
                    statusBar()->showMessage(
                        tr("Impossible d'ajouter les guides : une section boucle sur la même "
                           "jonction."));
                    return;
                }
                const auto* section = project_.findEmbroidery(ref.embroidery_id);
                const auto* sectionSatin =
                    section != nullptr
                        ? std::get_if<document::SatinParams>(&section->params)
                        : nullptr;
                const auto insertion =
                    sectionSatin != nullptr
                        ? stitch_generation::make_satin_guide_next_to_junction(
                              *sectionSatin, ref.guide_index)
                        : std::nullopt;
                if (!insertion) {
                    statusBar()->showMessage(
                        tr("Impossible d'ajouter les guides de jonction : une section n'a "
                           "aucun intervalle admissible."));
                    return;
                }
                auto linkedGuide = insertion->guide;
                linkedGuide.link_id = *linkId;
                additions.push_back(
                    {ref.embroidery_id, std::move(linkedGuide), insertion->index});
                if (ref.embroidery_id == obj->id) {
                    selected = insertion->index;
                }
            }
            if (additions.empty() || !selected) {
                statusBar()->showMessage(
                    tr("Impossible d'ajouter les guides : réseau de jonction incomplet."));
                return;
            }
            const auto branchCount = additions.size();
            undoStack_.execute(
                std::make_unique<commands::AddSatinGuidesCommand>(std::move(additions)),
                project_);
            selectedSatinGuide_ = selected;
            refreshImage();
            updateActions();
            statusBar()->showMessage(
                tr("%1 guides internes ajoutés autour de la jonction #%2.")
                    .arg(branchCount)
                    .arg(*junction));
            return;
        }
    }
    const auto insertion = stitch_generation::make_satin_guide_in_largest_gap(*satin);
    if (!insertion) {
        statusBar()->showMessage(
            tr("Impossible d'ajouter un guide : aucun intervalle assez large."));
        return;
    }
    const std::size_t selected = insertion->index;
    undoStack_.execute(std::make_unique<commands::AddSatinGuideCommand>(
                           obj->id, insertion->guide, insertion->index),
                       project_);
    selectedSatinGuide_ = selected;
    refreshImage();
    updateActions();
}

void MainWindow::removeSelectedSatinGuide() {
    if (!satinGuideTarget_ || !selectedSatinGuide_) {
        return;
    }
    auto* obj = project_.findEmbroidery(*satinGuideTarget_);
    auto* satin = obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
    if (satin == nullptr || satin->rungs.size() <= 2 || *selectedSatinGuide_ >= satin->rungs.size()) {
        return;
    }
    if (const auto junction = stitch_generation::satin_guide_junction(
            *satin, *selectedSatinGuide_)) {
        statusBar()->showMessage(tr("Guide de jonction #%1 verrouillé : ajoutez un guide interne "
                                    "pour orienter la section.")
                                     .arg(*junction));
        return;
    }
    // Un guide lié (link_id) représente une identité logique unique répartie
    // sur plusieurs sections : le supprimer d'une seule section laisserait les
    // autres avec un lien orphelin. On supprime donc le groupe entier comme
    // une transaction atomique unique, ou on refuse en totalité.
    if (const auto linkId = satin->rungs[*selectedSatinGuide_].link_id) {
        const auto refs =
            stitch_generation::satin_linked_guides(project_, obj->source_vector, *linkId);
        if (refs.size() < 2) {
            statusBar()->showMessage(
                tr("Impossible de supprimer le groupe : identifiants de liaison incohérents."));
            return;
        }
        std::vector<commands::SatinGuideRemoval> removals;
        removals.reserve(refs.size());
        for (const auto& ref : refs) {
            const auto* section = project_.findEmbroidery(ref.embroidery_id);
            const auto* sectionSatin =
                section != nullptr ? std::get_if<document::SatinParams>(&section->params) : nullptr;
            if (sectionSatin == nullptr || sectionSatin->rungs.size() <= 2) {
                statusBar()->showMessage(
                    tr("Impossible de supprimer le groupe : une section perdrait tous ses "
                       "guides internes."));
                return;
            }
            removals.push_back({ref.embroidery_id, ref.guide_index});
        }
        const auto groupCount = removals.size();
        undoStack_.execute(
            std::make_unique<commands::RemoveSatinGuidesCommand>(std::move(removals)), project_);
        selectedSatinGuide_.reset();
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Groupe de %1 guides liés supprimé.").arg(groupCount));
        return;
    }
    undoStack_.execute(std::make_unique<commands::RemoveSatinGuideCommand>(
                           obj->id, *selectedSatinGuide_),
                       project_);
    selectedSatinGuide_.reset();
    refreshImage();
    updateActions();
}

void MainWindow::discardOverrides(ObjectId id) {
    const auto* obj = project_.findEmbroidery(id);
    if (obj == nullptr) {
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Abandonner les retouches"),
        tr("Abandonner définitivement les retouches manuelles de « %1 » ? Les points "
           "reviendront à la géométrie générée. Cette action reste annulable (Ctrl+Z).")
            .arg(QString::fromStdString(obj->name)));
    if (answer != QMessageBox::Yes) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::DiscardOverridesCommand>(id), project_);
    refreshImage();
    updateActions();
}

void MainWindow::applyCanvasToView() {
    const QSizeF want(to_millimeters(project_.canvas.width).value,
                      to_millimeters(project_.canvas.height).value);
    if (view_->canvasSizeMm() != want) {
        view_->setCanvasSizeMm(want);
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
    emptyState_->setVisible(!project_.hasImage());
    positionEmptyState();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Sauve les préférences d'interface (géométrie + disposition des panneaux).
    {
        QSettings s;
        s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
        s.setValue(QStringLiteral("ui/windowState"), saveState());
    }
    if (!isWindowModified()) {
        event->accept();
        return;
    }
    const auto answer = QMessageBox::warning(
        this, tr("Modifications non enregistrées"),
        tr("Le projet a été modifié. Enregistrer avant de quitter ?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) {
        saveProject();
        event->setAccepted(!isWindowModified());  // annulé dans le dialogue -> reste ouvert
    } else if (answer == QMessageBox::Discard) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::setHoopSize() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Taille du cadre de broderie"));
    auto* layout = new QFormLayout(&dialog);
    layout->addRow(new QLabel(
        tr("Zone physique dans laquelle le motif doit tenir.\n"
           "L'analyse signale les points hors de ce cadre."),
        &dialog));
    const auto makeSpin = [&](double valueMm) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setRange(10.0, 500.0);
        spin->setDecimals(1);
        spin->setSuffix(tr(" mm"));
        spin->setValue(valueMm);
        return spin;
    };
    auto* w = makeSpin(to_millimeters(project_.canvas.width).value);
    auto* h = makeSpin(to_millimeters(project_.canvas.height).value);
    layout->addRow(tr("Largeur :"), w);
    layout->addRow(tr("Hauteur :"), h);
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
    undoStack_.execute(std::make_unique<commands::SetCanvasCommand>(c), project_);
    applyCanvasToView();
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Cadre : %1 × %2 mm").arg(w->value(), 0, 'f', 1).arg(h->value(), 0, 'f', 1));
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
        displayImage(processed_);  // retire l'aperçu
    }
}

void MainWindow::quantizeColors() {
    if (!project_.hasImage()) {
        return;
    }
    bool ok = false;
    const int colors = QInputDialog::getInt(this, tr("Quantifier les couleurs"),
                                            tr("Nombre maximal de couleurs :"), 8, 2, 64, 1, &ok);
    if (ok) {
        executeOp(image::QuantizeOp{colors});
    }
}

void MainWindow::onCropSelected(QRectF rectMm) {
    cropAct_->setChecked(false);
    if (!project_.hasImage() || processed_.empty()) {
        return;
    }
    const double mmPerPx = project_.mm_per_px.value;
    const double wMm = processed_.width * mmPerPx;
    const double hMm = processed_.height * mmPerPx;
    // Coin haut-gauche de l'image dans la scène (image centrée sur le canevas).
    const double left = -wMm / 2.0;
    const double top = -hMm / 2.0;

    image::CropOp op;
    op.x = static_cast<int>(std::floor((rectMm.left() - left) / mmPerPx));
    op.y = static_cast<int>(std::floor((rectMm.top() - top) / mmPerPx));
    op.width = static_cast<int>(std::lround(rectMm.width() / mmPerPx));
    op.height = static_cast<int>(std::lround(rectMm.height() / mmPerPx));
    executeOp(op);
}

void MainWindow::onBoxDrawn(QRectF rectMm, Qt::KeyboardModifiers modifiers) {
    if (currentTool_ != Tool::DrawRectangle && currentTool_ != Tool::DrawEllipse) {
        return;  // sécurité : signal reçu hors mode dessin (ne devrait pas arriver)
    }
    if (rectMm.width() < kMinDrawExtentMm || rectMm.height() < kMinDrawExtentMm) {
        statusBar()->showMessage(tr("Forme trop petite — glissez davantage."));
        return;
    }
    QRectF box = rectMm;
    if (currentTool_ == Tool::DrawEllipse && (modifiers & Qt::ShiftModifier)) {
        // Maj enfoncée : contraint à un cercle (côté = le plus petit des deux).
        const double side = std::min(box.width(), box.height());
        box.setWidth(side);
        box.setHeight(side);
    }
    // Accroche des deux coins, indépendamment l'un de l'autre (pas d'aperçu en
    // cours de glisser ici, contrairement au polygone/satin : le glisser
    // élastique rectangle/ellipse est le RubberBandDrag natif de Qt, pas un
    // aperçu personnalisable point par point).
    QPointF topLeft = box.topLeft();
    QPointF bottomRight = box.bottomRight();
    if (const auto snap = findSnapPointMm(topLeft)) {
        topLeft = *snap;
    }
    if (const auto snap = findSnapPointMm(bottomRight)) {
        bottomRight = *snap;
    }
    box = QRectF(topLeft, bottomRight).normalized();
    if (box.width() < kMinDrawExtentMm || box.height() < kMinDrawExtentMm) {
        // L'accroche a ramené les deux coins trop près l'un de l'autre : mieux
        // vaut renoncer que créer une forme dégénérée.
        statusBar()->showMessage(tr("Forme trop petite après accroche — glissez davantage."));
        return;
    }
    const Vec2um c1 = sceneMmToModel(box.topLeft());
    const Vec2um c2 = sceneMmToModel(box.bottomRight());
    if (currentTool_ == Tool::DrawRectangle) {
        addVectorPrimitive(geometry::rectangle_path(c1, c2), tr("Rectangle"));
    } else {
        addVectorPrimitive(geometry::ellipse_path(c1, c2), tr("Ellipse"));
    }
}

void MainWindow::onCanvasDoubleClicked(QPointF posMm) {
    if (currentTool_ == Tool::DrawPolygon) {
        finishPolygon();
        return;
    }
    if (currentTool_ == Tool::DrawSatinColumn) {
        finishSatinColumn();
        return;
    }
    if (currentTool_ == Tool::DrawBezier) {
        finishBezier();
        return;
    }
    // Mode remodelage des rails : double-clic sur un rail insère un nœud par
    // subdivision De Casteljau exacte (§ colonne satin manuelle). Hors de ce
    // mode, un double-clic sur le canevas ne fait rien de plus.
    if (railEditModeAct_ != nullptr && railEditModeAct_->isChecked() && railEditTarget_) {
        auto* obj = project_.findEmbroidery(*railEditTarget_);
        auto* satin = obj != nullptr ? std::get_if<document::SatinParams>(&obj->params) : nullptr;
        if (satin == nullptr) {
            return;
        }
        const Vec2um click = sceneMmToModel(posMm);
        // Tolérance de capture : 2,5 mm à l'écran, indépendante du zoom.
        constexpr double kToleranceUm = 2500.0;
        const auto hitA = nearestPointOnRail(satin->rail_a, click);
        const auto hitB = nearestPointOnRail(satin->rail_b, click);
        std::optional<commands::SatinRailSide> side;
        double bestDist = kToleranceUm;
        std::size_t bestSegment = 0;
        double bestT = 0.0;
        if (hitA && hitA->distUm <= bestDist) {
            side = commands::SatinRailSide::RailA;
            bestDist = hitA->distUm;
            bestSegment = hitA->segment;
            bestT = hitA->t;
        }
        if (hitB && hitB->distUm <= bestDist) {
            side = commands::SatinRailSide::RailB;
            bestSegment = hitB->segment;
            bestT = hitB->t;
        }
        if (!side) {
            return;
        }
        const ObjectId targetId = obj->id;
        undoStack_.execute(std::make_unique<commands::InsertSatinRailNodeCommand>(
                               targetId, *side, bestSegment, bestT),
                           project_);
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Nœud ajouté sur le rail %1.")
                                     .arg(*side == commands::SatinRailSide::RailA ? "A" : "B"));
    }
}

void MainWindow::updatePolygonPreview(QPointF cursorSceneMm) {
    if (pendingPolygonVertices_.empty()) {
        return;
    }
    if (polygonPreviewItem_ == nullptr) {
        polygonPreviewItem_ = new QGraphicsPathItem();
        QPen pen(QColor(80, 120, 200));
        pen.setWidthF(0.15);
        pen.setStyle(Qt::DashLine);
        polygonPreviewItem_->setPen(pen);
        polygonPreviewItem_->setZValue(1000.0);  // toujours au-dessus du reste
        scene_->addItem(polygonPreviewItem_);
    }
    QPainterPath path;
    path.moveTo(modelToSceneMm(pendingPolygonVertices_.front()));
    for (std::size_t i = 1; i < pendingPolygonVertices_.size(); ++i) {
        path.lineTo(modelToSceneMm(pendingPolygonVertices_[i]));
    }
    path.lineTo(cursorSceneMm);  // segment élastique jusqu'au curseur
    polygonPreviewItem_->setPath(path);
}

void MainWindow::finishPolygon() {
    if (pendingPolygonVertices_.size() >= 3) {
        addVectorPrimitive(geometry::polygon_path(pendingPolygonVertices_), tr("Polygone"));
    } else {
        statusBar()->showMessage(tr("Polygone : au moins 3 sommets requis."));
    }
    cancelPolygonDraw();  // nettoie l'état de tracé, succès ou échec
}

void MainWindow::cancelPolygonDraw() {
    pendingPolygonVertices_.clear();
    if (polygonPreviewItem_ != nullptr) {
        scene_->removeItem(polygonPreviewItem_);
        delete polygonPreviewItem_;
        polygonPreviewItem_ = nullptr;
    }
    updateDrawActionsState();
}

void MainWindow::removeLastPolygonVertex() {
    if (pendingPolygonVertices_.empty()) {
        return;
    }
    pendingPolygonVertices_.pop_back();
    if (pendingPolygonVertices_.empty()) {
        cancelPolygonDraw();
        statusBar()->showMessage(tr("Polygone : dernier sommet retiré."));
        return;
    }
    updatePolygonPreview(modelToSceneMm(pendingPolygonVertices_.back()));
    updateDrawActionsState();
    statusBar()->showMessage(
        tr("Polygone : %1 sommet(s) restant(s).").arg(pendingPolygonVertices_.size()));
}

namespace {
constexpr double kSnapRadiusPx = 10.0;  // rayon d'accroche « visé » à l'écran
// Borné en mm (pas seulement en pixels écran) : à fort dézoom (grand hoop, ou
// même la fenêtre pas encore affichée/redimensionnée en test -- pixelsPerMm()
// alors minuscule), 10 px écran peuvent représenter des dizaines de mm, ce qui
// accrocherait à des formes sans rapport visuel avec le curseur. 3 mm reste
// perceptible à l'écran à peu près à tout niveau de zoom réaliste et borne le
// pire cas (défaut trouvé par test : accroche parasite à plus de 10 mm quand
// la fenêtre n'a pas encore de taille réelle).
constexpr double kSnapRadiusMinMm = 0.05;
constexpr double kSnapRadiusMaxMm = 2.0;
}  // namespace

std::optional<QPointF> MainWindow::findSnapPointMm(QPointF cursorSceneMm) const {
    const double radiusMm = std::clamp(kSnapRadiusPx / std::max(view_->pixelsPerMm(), 0.01),
                                       kSnapRadiusMinMm, kSnapRadiusMaxMm);
    double bestDistSq = radiusMm * radiusMm;
    std::optional<QPointF> best;
    const auto consider = [&](QPointF candidate) {
        const double dx = candidate.x() - cursorSceneMm.x();
        const double dy = candidate.y() - cursorSceneMm.y();
        const double distSq = dx * dx + dy * dy;
        if (distSq <= bestDistSq) {
            bestDistSq = distSq;
            best = candidate;
        }
    };
    // Extrémités, milieux de segment (corde -- pas d'évaluation de courbe pour
    // les nœuds Lisse : approximation suffisante, la même que l'aperçu élastique
    // des autres outils) et centre (boîte englobante) des morceaux fermés.
    for (const auto& object : project_.vector_objects) {
        if (!object.visible) {
            continue;
        }
        const auto considerPath = [&](const geometry::Path& path) {
            if (path.nodes.empty()) {
                return;
            }
            double minX = std::numeric_limits<double>::max();
            double maxX = std::numeric_limits<double>::lowest();
            double minY = std::numeric_limits<double>::max();
            double maxY = std::numeric_limits<double>::lowest();
            for (const auto& node : path.nodes) {
                const QPointF p = modelToSceneMm(node.pos);
                consider(p);
                minX = std::min(minX, p.x());
                maxX = std::max(maxX, p.x());
                minY = std::min(minY, p.y());
                maxY = std::max(maxY, p.y());
            }
            const std::size_t count = path.nodes.size();
            const std::size_t segments = path.closed ? count : (count - 1);
            for (std::size_t i = 0; i < segments; ++i) {
                const QPointF a = modelToSceneMm(path.nodes[i].pos);
                const QPointF b = modelToSceneMm(path.nodes[(i + 1) % count].pos);
                consider(QPointF((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0));
            }
            if (path.closed) {
                consider(QPointF((minX + maxX) / 2.0, (minY + maxY) / 2.0));
            }
        };
        for (const auto& pathSet : object.paths) {
            considerPath(pathSet.outer);
            for (const auto& hole : pathSet.holes) {
                considerPath(hole);
            }
        }
    }
    return best;
}

void MainWindow::updateSnapIndicator(std::optional<QPointF> snapSceneMm) {
    if (!snapSceneMm) {
        if (snapIndicatorItem_ != nullptr) {
            snapIndicatorItem_->setVisible(false);
        }
        return;
    }
    if (snapIndicatorItem_ == nullptr) {
        snapIndicatorItem_ = new QGraphicsEllipseItem(-5.0, -5.0, 10.0, 10.0);
        snapIndicatorItem_->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        snapIndicatorItem_->setPen(QPen(QColor(255, 140, 0), 2));
        snapIndicatorItem_->setBrush(Qt::NoBrush);
        snapIndicatorItem_->setZValue(1002.0);  // au-dessus des aperçus élastiques (1000)
        scene_->addItem(snapIndicatorItem_);
    }
    snapIndicatorItem_->setPos(*snapSceneMm);
    snapIndicatorItem_->setVisible(true);
}

void MainWindow::updateSatinColumnPreview(QPointF cursorSceneMm) {
    if (pendingSatinPoints_.empty()) {
        return;
    }
    if (satinPreviewItem_ == nullptr) {
        satinPreviewItem_ = new QGraphicsPathItem();
        satinPreviewItem_->setZValue(1000.0);  // toujours au-dessus du reste
        scene_->addItem(satinPreviewItem_);
    }
    // Rail A (indices pairs) et rail B (indices impairs) en deux couleurs
    // distinctes, les paires déjà complètes reliées en pointillé (aperçu des
    // futures lignes d'angle) ; segment élastique du dernier point au curseur.
    QPainterPath railA;
    QPainterPath railB;
    QPainterPath connectors;
    bool railAStarted = false;
    bool railBStarted = false;
    for (std::size_t i = 0; i < pendingSatinPoints_.size(); ++i) {
        const QPointF p = modelToSceneMm(pendingSatinPoints_[i]);
        if (i % 2 == 0) {
            if (!railAStarted) {
                railA.moveTo(p);
                railAStarted = true;
            } else {
                railA.lineTo(p);
            }
        } else {
            if (!railBStarted) {
                railB.moveTo(p);
                railBStarted = true;
            } else {
                railB.lineTo(p);
            }
            connectors.moveTo(modelToSceneMm(pendingSatinPoints_[i - 1]));
            connectors.lineTo(p);
        }
    }
    // Segment élastique jusqu'au curseur, sur le rail dont c'est le tour.
    const bool nextIsSideB = pendingSatinPoints_.size() % 2 == 1;
    if (nextIsSideB) {
        railB.lineTo(cursorSceneMm);
    } else {
        railA.lineTo(cursorSceneMm);
    }

    QPainterPath combined;
    combined.addPath(railA);
    combined.addPath(railB);
    satinPreviewItem_->setPath(combined);
    QPen railPen(QColor(200, 90, 40));
    railPen.setWidthF(0.2);
    satinPreviewItem_->setPen(railPen);

    // Connecteurs (paires déjà complètes -> futures lignes d'angle), item
    // séparé pointillé, couleur distincte.
    if (satinConnectorPreviewItem_ == nullptr) {
        satinConnectorPreviewItem_ = new QGraphicsPathItem();
        satinConnectorPreviewItem_->setZValue(1000.0);
        scene_->addItem(satinConnectorPreviewItem_);
    }
    satinConnectorPreviewItem_->setPath(connectors);
    QPen connectorPen(QColor(80, 120, 200));
    connectorPen.setWidthF(0.12);
    connectorPen.setStyle(Qt::DashLine);
    satinConnectorPreviewItem_->setPen(connectorPen);
}

void MainWindow::removeLastSatinColumnPoint() {
    if (pendingSatinPoints_.empty()) {
        return;
    }
    pendingSatinPoints_.pop_back();
    if (pendingSatinPoints_.empty()) {
        cancelSatinColumnDraw();
        statusBar()->showMessage(tr("Colonne satin : dernier point retiré."));
        return;
    }
    updateSatinColumnPreview(modelToSceneMm(pendingSatinPoints_.back()));
    updateDrawActionsState();
    statusBar()->showMessage(
        tr("Colonne satin : %1 point(s) restant(s) — Retour arrière pour continuer à retirer")
            .arg(pendingSatinPoints_.size()));
}

void MainWindow::finishSatinColumn() {
    // Un dernier point orphelin (nombre impair) est abandonné plutôt que de
    // planter ou de forcer l'utilisateur à annuler toute la création (§ mode
    // de création principal, cas nombre impair).
    if (pendingSatinPoints_.size() % 2 != 0) {
        pendingSatinPoints_.pop_back();
        statusBar()->showMessage(
            tr("Colonne satin : dernier point sans correspondance ignoré (paire incomplète)."));
    }
    if (pendingSatinPoints_.size() < 4) {  // < 2 paires complètes
        statusBar()->showMessage(
            tr("Colonne satin : au moins deux paires complètes (4 points) sont nécessaires."));
        cancelSatinColumnDraw();
        return;
    }

    geometry::Path railA;
    geometry::Path railB;
    railA.closed = false;
    railB.closed = false;
    std::vector<document::SatinRung> rungs;
    for (std::size_t i = 0; i < pendingSatinPoints_.size(); i += 2) {
        const Vec2um a = pendingSatinPoints_[i];
        const Vec2um b = pendingSatinPoints_[i + 1];
        railA.nodes.push_back(geometry::PathNode{a, geometry::NodeType::Corner, {}, {}});
        railB.nodes.push_back(geometry::PathNode{b, geometry::NodeType::Corner, {}, {}});
        rungs.push_back(document::SatinRung{a, b, std::nullopt});
    }

    // Les rails ne doivent jamais se croiser (§ invariants) : avertit sans
    // bloquer la création — l'objet reste éditable, pas de disparition
    // silencieuse d'un geste que l'utilisateur vient de faire.
    const auto flatA = geometry::flatten(railA, Micrometers{50});
    const auto flatB = geometry::flatten(railB, Micrometers{50});
    if (geometry::polylines_cross(flatA.points, flatB.points)) {
        QMessageBox::warning(
            this, tr("Rails croisés"),
            tr("Les deux rails de cette colonne satin se croisent — l'objet est créé, mais "
               "la couture résultante sera probablement incohérente. Corrigez les rails en "
               "mode remodelage (Maj+R) avant d'exporter."));
    }

    // Contour source synthétique (rail A aller + rail B retour) : uniquement
    // pour le rattachement `source_vector` (obligatoire sur EmbroideryObject),
    // sans autre rôle — les rails de SatinParams restent la géométrie réelle.
    geometry::Path outline;
    outline.closed = true;
    outline.nodes = railA.nodes;
    for (auto it = railB.nodes.rbegin(); it != railB.nodes.rend(); ++it) {
        outline.nodes.push_back(*it);
    }

    document::VectorObject sourceObject;
    sourceObject.id = project_.object_ids.next();
    sourceObject.name = tr("Colonne satin (rails)").toStdString();
    sourceObject.rgb = {200, 90, 40};
    sourceObject.visible = false;  // le rendu passe par les rails/points du satin, pas ce contour
    sourceObject.paths.push_back(geometry::PathSet{std::move(outline), {}});

    const std::size_t pairCount = rungs.size();

    document::EmbroideryObject embObject;
    embObject.id = project_.object_ids.next();
    embObject.name = tr("Colonne satin manuelle").toStdString();
    embObject.source_vector = sourceObject.id;
    embObject.rgb = {200, 90, 40};
    document::SatinParams params;
    params.rail_a = std::move(railA);
    params.rail_b = std::move(railB);
    params.rungs = std::move(rungs);
    embObject.params = std::move(params);

    const ObjectId newEmbId = embObject.id;
    std::vector<document::VectorObject> vectors{std::move(sourceObject)};
    std::vector<document::EmbroideryObject> embroideries{std::move(embObject)};
    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::move(vectors), std::move(embroideries),
                           "Colonne satin (création manuelle)"),
                       project_);

    selectedEmbroidery_ = newEmbId;
    selectedObject_.reset();
    selectedRegion_.reset();
    showStitchesAct_->setChecked(true);
    cancelSatinColumnDraw();
    setTool(Tool::Select);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("Colonne satin créée : %1 paire(s), %2 points générés.")
                                     .arg(pairCount)
                                     .arg(stats.stitches));
    }
}

void MainWindow::cancelSatinColumnDraw() {
    pendingSatinPoints_.clear();
    if (satinPreviewItem_ != nullptr) {
        scene_->removeItem(satinPreviewItem_);
        delete satinPreviewItem_;
        satinPreviewItem_ = nullptr;
    }
    if (satinConnectorPreviewItem_ != nullptr) {
        scene_->removeItem(satinConnectorPreviewItem_);
        delete satinConnectorPreviewItem_;
        satinConnectorPreviewItem_ = nullptr;
    }
    updateDrawActionsState();
}

// ---------------------------------------------------------------------------
// Courbes de Bézier (outil DrawBezier, "plume") — voir main_window.hpp pour
// le principe : clic = nœud Coin, clic-glisser = nœud Lisse à poignées
// symétriques dérivées du vecteur de glisser.
// ---------------------------------------------------------------------------

void MainWindow::updateBezierPreview(QPointF cursorSceneMm) {
    if (pendingBezierNodes_.empty()) {
        return;
    }
    if (bezierPreviewItem_ == nullptr) {
        bezierPreviewItem_ = new QGraphicsPathItem();
        QPen pen(QColor(80, 120, 200));
        pen.setWidthF(0.15);
        pen.setStyle(Qt::DashLine);
        bezierPreviewItem_->setPen(pen);
        bezierPreviewItem_->setZValue(1000.0);
        scene_->addItem(bezierPreviewItem_);
    }
    QPainterPath path;
    path.moveTo(modelToSceneMm(pendingBezierNodes_.front().pos));
    for (std::size_t i = 1; i < pendingBezierNodes_.size(); ++i) {
        const auto& a = pendingBezierNodes_[i - 1];
        const auto& b = pendingBezierNodes_[i];
        if (a.tan_out || b.tan_in) {
            const QPointF c1 =
                a.tan_out ? modelToSceneMm(a.pos + *a.tan_out) : modelToSceneMm(a.pos);
            const QPointF c2 = b.tan_in ? modelToSceneMm(b.pos + *b.tan_in) : modelToSceneMm(b.pos);
            path.cubicTo(c1, c2, modelToSceneMm(b.pos));
        } else {
            path.lineTo(modelToSceneMm(b.pos));
        }
    }
    // Segment élastique jusqu'au curseur (prochain nœud) : toujours droit ici
    // — sa courbure réelle dépendra du glisser au moment de la pose.
    path.lineTo(cursorSceneMm);
    bezierPreviewItem_->setPath(path);

    if (bezierHandlePreviewItem_ != nullptr) {
        bezierHandlePreviewItem_->setPath(QPainterPath());  // efface l'aperçu de poignée
    }
}

void MainWindow::onBezierPointDragging(QPointF anchorMm, QPointF currentMm) {
    if (currentTool_ != Tool::DrawBezier) {
        return;
    }
    updateBezierPreview(anchorMm);  // tracé confirmé + segment élastique jusqu'à l'ancre

    // Poignée en cours de glisser : segment plein ancre -> curseur (poignée
    // sortante) + reflet symétrique en pointillé (poignée entrante), pour que
    // l'utilisateur voie exactement le nœud Lisse qu'il est en train de poser.
    if (bezierHandlePreviewItem_ == nullptr) {
        bezierHandlePreviewItem_ = new QGraphicsPathItem();
        bezierHandlePreviewItem_->setZValue(1001.0);
        scene_->addItem(bezierHandlePreviewItem_);
    }
    QPainterPath handlePath;
    handlePath.moveTo(anchorMm);
    handlePath.lineTo(currentMm);
    const QPointF mirrored = anchorMm - (currentMm - anchorMm);
    handlePath.moveTo(anchorMm);
    handlePath.lineTo(mirrored);
    bezierHandlePreviewItem_->setPath(handlePath);
    QPen pen(QColor(200, 90, 40));
    pen.setWidthF(0.15);
    bezierHandlePreviewItem_->setPen(pen);

    statusBar()->showMessage(
        tr("Courbe : relâchez pour poser le nœud lisse (poignées symétriques)"));
}

void MainWindow::onBezierPointCommitted(QPointF anchorMm, QPointF handleMm) {
    if (currentTool_ != Tool::DrawBezier) {
        return;
    }
    const Vec2um anchor = sceneMmToModel(anchorMm);
    if (!pendingBezierNodes_.empty()) {
        // Garde anti-doublon : la séquence Qt d'un double-clic pose aussi un
        // clic simple juste avant (cf. polygone/satin).
        const auto& last = pendingBezierNodes_.back().pos;
        const double dx = static_cast<double>(anchor.x.value - last.x.value);
        const double dy = static_cast<double>(anchor.y.value - last.y.value);
        if (dx * dx + dy * dy < 4.0) {
            return;
        }
    }
    const Vec2um handle = sceneMmToModel(handleMm);
    const Vec2um drag = handle - anchor;
    // En dessous : clic net (souris n'a quasi pas bougé) -> nœud Coin. Au
    // dessus : glisser volontaire -> nœud Lisse, poignées symétriques.
    constexpr double kDragThresholdUm = 300.0;
    geometry::PathNode node;
    node.pos = anchor;
    if (length_um(drag) < kDragThresholdUm) {
        node.type = geometry::NodeType::Corner;
    } else {
        node.type = geometry::NodeType::Smooth;
        node.tan_out = drag;
        node.tan_in = Vec2um{-drag.x, -drag.y};
    }
    pendingBezierNodes_.push_back(node);

    if (bezierHandlePreviewItem_ != nullptr) {
        bezierHandlePreviewItem_->setPath(QPainterPath());
    }
    updateBezierPreview(anchorMm);
    updateDrawActionsState();
    statusBar()->showMessage(
        tr("Courbe : %1 nœud(s) — clic (coin) ou clic-glisser (lisse) pour continuer, "
           "Entrée/double-clic/bouton ✓ : terminer, Retour arrière : retirer le dernier nœud")
            .arg(pendingBezierNodes_.size()));
}

void MainWindow::finishBezier() {
    if (pendingBezierNodes_.size() >= 2) {
        geometry::Path path;
        path.closed = true;
        path.nodes = pendingBezierNodes_;
        addVectorPrimitive(std::move(path), tr("Courbe"));
    } else {
        statusBar()->showMessage(tr("Courbe : au moins deux nœuds requis."));
    }
    cancelBezierDraw();  // nettoie l'état de tracé, succès ou échec
}

void MainWindow::cancelBezierDraw() {
    pendingBezierNodes_.clear();
    if (bezierPreviewItem_ != nullptr) {
        scene_->removeItem(bezierPreviewItem_);
        delete bezierPreviewItem_;
        bezierPreviewItem_ = nullptr;
    }
    if (bezierHandlePreviewItem_ != nullptr) {
        scene_->removeItem(bezierHandlePreviewItem_);
        delete bezierHandlePreviewItem_;
        bezierHandlePreviewItem_ = nullptr;
    }
    updateDrawActionsState();
}

void MainWindow::removeLastBezierPoint() {
    if (pendingBezierNodes_.empty()) {
        return;
    }
    pendingBezierNodes_.pop_back();
    if (pendingBezierNodes_.empty()) {
        cancelBezierDraw();
        statusBar()->showMessage(tr("Courbe : dernier nœud retiré."));
        return;
    }
    updateBezierPreview(modelToSceneMm(pendingBezierNodes_.back().pos));
    updateDrawActionsState();
    statusBar()->showMessage(
        tr("Courbe : %1 nœud(s) restant(s).").arg(pendingBezierNodes_.size()));
}

void MainWindow::updateDrawActionsState() {
    if (finishDrawAct_ == nullptr || cancelDrawAct_ == nullptr) {
        return;
    }
    const bool drawing = (currentTool_ == Tool::DrawPolygon && !pendingPolygonVertices_.empty()) ||
                         (currentTool_ == Tool::DrawBezier && !pendingBezierNodes_.empty()) ||
                         (currentTool_ == Tool::DrawSatinColumn && !pendingSatinPoints_.empty());
    finishDrawAct_->setEnabled(drawing);
    cancelDrawAct_->setEnabled(drawing);
}

void MainWindow::onFreeformPointAdded(QPointF posMm) {
    if (currentTool_ != Tool::DrawFreeform) {
        return;  // sécurité : signal reçu hors mode dessin (ne devrait pas arriver)
    }
    pendingFreeformPoints_.push_back(sceneMmToModel(posMm));
    if (freeformPreviewItem_ == nullptr) {
        freeformPreviewItem_ = new QGraphicsPathItem();
        QPen pen(QColor(80, 120, 200));
        pen.setWidthF(0.15);
        pen.setStyle(Qt::DashLine);
        freeformPreviewItem_->setPen(pen);
        freeformPreviewItem_->setZValue(1000.0);  // toujours au-dessus du reste
        scene_->addItem(freeformPreviewItem_);
    }
    QPainterPath path;
    path.moveTo(modelToSceneMm(pendingFreeformPoints_.front()));
    for (std::size_t i = 1; i < pendingFreeformPoints_.size(); ++i) {
        path.lineTo(modelToSceneMm(pendingFreeformPoints_[i]));
    }
    freeformPreviewItem_->setPath(path);
}

void MainWindow::finishFreeform() {
    // La simplification (Douglas-Peucker) peut retomber sous 3 sommets même
    // avec un tracé brut suffisant (points quasi colinéaires, jitter sous la
    // tolérance) : on vérifie le résultat RÉEL, pas seulement le nombre de
    // points bruts, pour toujours donner un message clair plutôt qu'un
    // échec silencieux.
    geometry::Path path = pendingFreeformPoints_.size() >= 3
                              ? geometry::freeform_path(pendingFreeformPoints_,
                                                        kFreeformSimplifyTolerance)
                              : geometry::Path{};
    if (!path.nodes.empty()) {
        addVectorPrimitive(std::move(path), tr("Forme libre"));
    } else {
        statusBar()->showMessage(tr("Forme libre : tracé trop court."));
    }
    cancelFreeformDraw();  // nettoie l'état de tracé, succès ou échec
}

void MainWindow::cancelFreeformDraw() {
    pendingFreeformPoints_.clear();
    if (freeformPreviewItem_ != nullptr) {
        scene_->removeItem(freeformPreviewItem_);
        delete freeformPreviewItem_;
        freeformPreviewItem_ = nullptr;
    }
}

void MainWindow::addVectorPrimitive(geometry::Path path, const QString& name) {
    if (path.nodes.empty()) {
        return;  // dégénéré (ex. polygone à moins de 3 sommets) : rien à créer
    }
    document::VectorObject object;
    object.id = project_.object_ids.next();
    object.name = name.toStdString();
    object.rgb = {80, 120, 200};
    object.paths.push_back(geometry::PathSet{std::move(path), {}});

    undoStack_.execute(std::make_unique<commands::AddVectorObjectCommand>(std::move(object)),
                       project_);
    selectedObject_ = project_.vector_objects.back().id;
    selectedRegion_.reset();
    selectedEmbroidery_.reset();
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Forme créée — menu Broderie pour la convertir en points de couture"));
}

void MainWindow::refreshImage() {
    applyCanvasToView();  // taille du cadre (peut avoir changé : réglage, undo, chargement)
    setWindowModified(true);  // toute régénération suit une mutation du document
    if (!project_.hasImage()) {
        processed_ = {};
        displayImage(processed_);  // affiche quand même une séquence importée
        return;
    }
    // Une sélection qui ne correspond plus à une région ou un objet vivant
    // est annulée (undo/redo, nouvelle segmentation, suppression…).
    if (selectedRegion_ &&
        (!project_.segmentation || project_.segmentation->find(*selectedRegion_) == nullptr)) {
        selectedRegion_.reset();
    }
    if (selectedObject_ && project_.findObject(*selectedObject_) == nullptr) {
        selectedObject_.reset();
    }
    const auto result = image::apply_pipeline(project_.original, project_.ops);
    if (!result) {
        QMessageBox::warning(this, tr("Erreur"), QString::fromStdString(result.error().message));
        return;
    }
    processed_ = *result;

    // Régénération des points depuis le document (fonction pure). Une
    // séquence importée d'un DST n'est pas régénérable : elle est conservée.
    // État Clean/ManuallyEdited/Dirty par objet retouché (Lot 8.2) et vue
    // brute de l'objet en cours d'édition (Lot 8.2, source unique pour le
    // placement des poignées dans renderBase et la construction des
    // commandes) : tenus à jour ici. Ni displayImage() ni updateActions() ne
    // les recalculent : ils lisent editStates_/stitchEditView_.
    editStates_.clear();
    if (!sequenceImported_) {
        sequence_.reset();
    }
    stitchEditView_.reset();
    if (!sequenceImported_ && !project_.embroidery_objects.empty()) {
        // refresh_context (pas effective_sequence + classify_all_edit_states +
        // edit_view séparément) : un seul appel interne à generate_sequence
        // produit à la fois la séquence effective -- retouches appliquées,
        // contenu identique à effective_sequence(project), seul point d'entree
        // autorise pour l'affichage/export (cf.
        // tests/check_no_raw_sequence_bypass.cmake) --, les états par objet et
        // la vue d'édition de l'objet ciblé, au lieu de jusqu'à trois
        // régénérations indépendantes après chaque glisser sur un motif dense.
        // stitchEditTarget_ ne peut être défini que sur un objet du document
        // (cf. onStitchEditModeToggled/resolveSelectedEmbroidery) : inutile de
        // rien calculer ici quand embroidery_objects est vide ou qu'une
        // séquence importée est affichée -- editStates_/stitchEditView_
        // resteraient de toute façon vides/absents dans ces cas.
        if (auto ctx = stitch_generation::refresh_context(project_, stitchEditTarget_)) {
            sequence_ = std::move(ctx->effective);
            editStates_ = std::move(ctx->edit_states);
            if (ctx->target_view &&
                ctx->target_view->state != stitch_generation::ObjectEditState::Dirty) {
                stitchEditView_ = std::move(*ctx->target_view);
            }
        } else {
            statusBar()->showMessage(
                tr("Génération des points impossible : %1")
                    .arg(QString::fromStdString(ctx.error().message)));
        }
    }
    if (simToolbar_ != nullptr) {
        updateSimulationRange();
    }
    if (orderDock_ != nullptr) {
        refreshOrderPanel();
    }
    if (filterDock_ != nullptr) {
        refreshFilterPanel();
    }
    refreshDocumentPanel();
    displayImage(processed_);
}

void MainWindow::displayImage(const image::Image& img) {
    renderBase(img);
    renderStitches();
}

void MainWindow::renderBase(const image::Image& img) {
    // Retire uniquement la couche base ; la couche points est gérée à part.
    for (QGraphicsItem* it : baseItems_) {
        scene_->removeItem(it);
        delete it;
    }
    baseItems_.clear();

    if (!img.empty() && (showImageAct_ == nullptr || showImageAct_->isChecked())) {
        const QImage qimg(img.rgba.data(), img.width, img.height, img.width * 4,
                          QImage::Format_RGBA8888);
        const QPixmap pixmap = QPixmap::fromImage(qimg.copy());
        auto* item = scene_->addPixmap(pixmap);
        const double mmPerPx = project_.mm_per_px.value;
        const double wMm = img.width * mmPerPx;
        const double hMm = img.height * mmPerPx;
        item->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
        item->setPos(-wMm / 2.0, -hMm / 2.0);
        baseItems_.append(item);
    }

    // Objets vectoriels (remplissage translucide + contour).
    if (showVectorsAct_ != nullptr && showVectorsAct_->isChecked()) {
        for (const auto& object : project_.vector_objects) {
            if (!object.visible) {
                continue;
            }
            const bool selected = selectedObject_ && object.id == *selectedObject_;
            const QColor color(object.rgb[0], object.rgb[1], object.rgb[2]);
            const QPainterPath outline = objectPainterPath(object);
            // Sélection à DOUBLE contraste : un halo clair sous un trait d'accent,
            // lisible quel que soit le fond du canevas.
            if (selected) {
                QPen halo(AppTheme::instance().tokens().canvasSelectionHalo);
                halo.setCosmetic(true);
                halo.setWidth(6);
                auto* haloItem = scene_->addPath(outline, halo, Qt::NoBrush);
                haloItem->setZValue(9);
                baseItems_.append(haloItem);
            }
            QPen pen(selected ? AppTheme::instance().tokens().canvasSelectionLine
                              : color.darker(150));
            pen.setCosmetic(true);
            pen.setWidth(selected ? 3 : 2);
            const QBrush brush(QColor(color.red(), color.green(), color.blue(), 90));
            QGraphicsPathItem* pathItem = nullptr;
            // Forme SÉLECTIONNÉE, mode Sélection : glisser n'importe où sur le
            // corps la déplace entière (tous nœuds, tous morceaux) au lieu
            // d'exiger de glisser chaque nœud un par un (défaut remonté en
            // usage réel — cf. VectorObjectBodyItem).
            if (selected && currentTool_ == Tool::Select) {
                const ObjectId objectId = object.id;
                auto* bodyItem = new VectorObjectBodyItem(outline, pen, brush, [this, objectId](QPointF deltaSceneMm) {
                    const Vec2um delta = sceneMmToModel(deltaSceneMm);
                    // Diffère : refreshImage() détruirait cet item pendant son
                    // propre événement souris (même défaut que NodeHandleItem).
                    QTimer::singleShot(0, this, [this, objectId, delta] {
                        undoStack_.execute(
                            std::make_unique<commands::TranslateVectorObjectCommand>(objectId, delta),
                            project_);
                        refreshImage();
                        updateActions();
                    });
                });
                scene_->addItem(bodyItem);
                pathItem = bodyItem;
            } else {
                pathItem = scene_->addPath(outline, pen, brush);
                // Curseur contextuel : une main pointeuse au survol d'une forme
                // cliquable, mais SEULEMENT en mode Sélection — avec un outil de
                // dessin actif, survoler une forme existante ne doit pas laisser
                // croire qu'un clic la sélectionnerait (il en dessinerait une
                // nouvelle) : la croix du mode dessin doit rester seule visible.
                if (currentTool_ == Tool::Select) {
                    pathItem->setCursor(Qt::PointingHandCursor);
                }
            }
            pathItem->setZValue(10);
            baseItems_.append(pathItem);
        }
        // Poignées de nœuds de l'objet sélectionné (+ poignées Bézier pour les
        // nœuds Lisse : glisser une poignée édite la courbe sans déplacer le
        // nœud — même principe que le remodelage des rails satin, § courbes
        // de Bézier).
        if (selectedObject_) {
            if (const auto* object = project_.findObject(*selectedObject_)) {
                for (std::size_t s = 0; s < object->paths.size(); ++s) {
                    const auto& set = object->paths[s];
                    const auto addHandles = [&](const geometry::Path& path, std::size_t pathIdx) {
                        for (std::size_t n = 0; n < path.nodes.size(); ++n) {
                            const auto& node = path.nodes[n];
                            const Vec2um pos = node.pos;
                            const QPointF sceneMm(to_millimeters(pos.x).value,
                                                  -to_millimeters(pos.y).value);
                            const ObjectId objectId = object->id;
                            const document::NodeRef ref{s, pathIdx, n};
                            auto* handle = new NodeHandleItem(
                                sceneMm,
                                [this, objectId, ref, pos](QPointF newSceneMm) {
                                    const Vec2um newPos{
                                        to_micrometers(Millimeters{newSceneMm.x()}),
                                        to_micrometers(Millimeters{-newSceneMm.y()})};
                                    if (newPos == pos) {
                                        return;
                                    }
                                    // Diffère : refreshImage() détruirait cette poignée
                                    // pendant son propre événement souris (crash).
                                    QTimer::singleShot(0, this, [this, objectId, ref, pos, newPos] {
                                        undoStack_.execute(
                                            std::make_unique<commands::MoveNodeCommand>(
                                                objectId, ref, pos, newPos),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                    });
                                },
                                {},
                                // Clic droit sur un nœud : simplification manuelle d'une
                                // forme (typiquement après vectorisation d'une région
                                // segmentée — contour trop détaillé pour être exploitable
                                // tel quel).
                                [this, objectId, ref](QPoint globalPos) {
                                    QMenu nodeMenu(this);
                                    auto* deleteAct = nodeMenu.addAction(tr("Supprimer le nœud"));
                                    bool canDelete = false;
                                    if (auto* obj = project_.findObject(objectId)) {
                                        if (const auto* path =
                                                document::path_in(*obj, ref.set, ref.path)) {
                                            // 3 nœuds min. pour un chemin fermé, 2 pour un
                                            // chemin ouvert (cf. RemoveNodeCommand::apply).
                                            const std::size_t minNodes = path->closed ? 3 : 2;
                                            canDelete = path->nodes.size() > minNodes;
                                        }
                                    }
                                    deleteAct->setEnabled(canDelete);
                                    connect(deleteAct, &QAction::triggered, this,
                                           [this, objectId, ref] {
                                               undoStack_.execute(
                                                   std::make_unique<commands::RemoveNodeCommand>(
                                                       objectId, ref),
                                                   project_);
                                               refreshImage();
                                               updateActions();
                                           });
                                    nodeMenu.exec(globalPos);
                                });
                            scene_->addItem(handle);
                            baseItems_.append(handle);

                            if (node.type != geometry::NodeType::Smooth) {
                                continue;
                            }
                            const auto addBezierHandle = [&](bool isOut,
                                                             const std::optional<Vec2um>& tan) {
                                if (!tan) {
                                    return;
                                }
                                const Vec2um handlePos = pos + *tan;
                                const QPointF handleSceneMm(to_millimeters(handlePos.x).value,
                                                            -to_millimeters(handlePos.y).value);
                                auto* link = scene_->addLine(
                                    QLineF(sceneMm, handleSceneMm),
                                    QPen(AppTheme::instance().tokens().accent, 1.0));
                                link->setZValue(97);
                                baseItems_.append(link);

                                auto* handleItem = new NodeHandleItem(
                                    handleSceneMm,
                                    [this, objectId, ref, isOut,
                                     oldTan = tan](QPointF newSceneMm) {
                                        const Vec2um newHandlePos{
                                            to_micrometers(Millimeters{newSceneMm.x()}),
                                            to_micrometers(Millimeters{-newSceneMm.y()})};
                                        const auto* obj = project_.findObject(objectId);
                                        const auto* p = obj != nullptr
                                                           ? document::path_in(*obj, ref.set, ref.path)
                                                           : nullptr;
                                        if (p == nullptr || ref.node >= p->nodes.size()) {
                                            return;
                                        }
                                        const Vec2um newTan =
                                            newHandlePos - p->nodes[ref.node].pos;
                                        if (newTan == *oldTan) {
                                            return;
                                        }
                                        QTimer::singleShot(
                                            0, this,
                                            [this, objectId, ref, isOut, oldTan, newTan] {
                                                undoStack_.execute(
                                                    std::make_unique<commands::SetNodeHandleCommand>(
                                                        objectId, ref, isOut, oldTan, newTan),
                                                    project_);
                                                refreshImage();
                                                updateActions();
                                            });
                                    });
                                handleItem->setPen(
                                    QPen(AppTheme::instance().tokens().accent, 1.5));
                                handleItem->setBrush(
                                    QBrush(AppTheme::instance().tokens().accent.lighter(160)));
                                handleItem->setToolTip(
                                    isOut ? tr("Poignée sortante — glisser pour incurver la "
                                              "courbe")
                                         : tr("Poignée entrante — glisser pour incurver la "
                                              "courbe"));
                                scene_->addItem(handleItem);
                                baseItems_.append(handleItem);
                            };
                            addBezierHandle(true, node.tan_out);
                            addBezierHandle(false, node.tan_in);
                        }
                    };
                    addHandles(set.outer, 0);
                    for (std::size_t h = 0; h < set.holes.size(); ++h) {
                        addHandles(set.holes[h], h + 1);
                    }
                }
            }
        }

        // Poignées de redimensionnement (4 coins de la boîte englobante) de
        // l'objet sélectionné, mode Sélection uniquement : glisser un coin
        // redimensionne la forme entière autour du coin diagonalement
        // opposé (fixe) — jusqu'ici, redimensionner exigeait de déplacer
        // chaque nœud un par un (même défaut remonté que pour le déplacement
        // de forme entière, § VectorObjectBodyItem plus haut).
        if (selectedObject_ && currentTool_ == Tool::Select) {
            if (const auto* object = project_.findObject(*selectedObject_)) {
                std::optional<Micrometers> minX, maxX, minY, maxY;
                const auto scan = [&](const geometry::Path& path) {
                    for (const auto& node : path.nodes) {
                        if (!minX || node.pos.x < *minX) minX = node.pos.x;
                        if (!maxX || node.pos.x > *maxX) maxX = node.pos.x;
                        if (!minY || node.pos.y < *minY) minY = node.pos.y;
                        if (!maxY || node.pos.y > *maxY) maxY = node.pos.y;
                    }
                };
                for (const auto& set : object->paths) {
                    scan(set.outer);
                    for (const auto& hole : set.holes) {
                        scan(hole);
                    }
                }
                // Boîte dégénérée sur un axe (segment/point) : aucun coin
                // opposé exploitable comme ancrage sur cet axe, on renonce
                // plutôt que de diviser par zéro.
                if (minX && maxX && minY && maxY && *minX != *maxX && *minY != *maxY) {
                    const ObjectId objectId = object->id;
                    const Vec2um corners[4] = {
                        Vec2um{*minX, *minY}, Vec2um{*maxX, *minY}, Vec2um{*maxX, *maxY},
                        Vec2um{*minX, *maxY}};
                    for (std::size_t i = 0; i < 4; ++i) {
                        const Vec2um corner = corners[i];
                        const Vec2um anchor = corners[(i + 2) % 4];  // coin diagonalement opposé
                        auto* handle = new ResizeHandleItem(
                            modelToSceneMm(corner), [this, objectId, corner, anchor](QPointF newSceneMm) {
                                const Vec2um newCorner = sceneMmToModel(newSceneMm);
                                if (newCorner == corner) {
                                    return;
                                }
                                const double sx = static_cast<double>((newCorner.x - anchor.x).value) /
                                                  static_cast<double>((corner.x - anchor.x).value);
                                const double sy = static_cast<double>((newCorner.y - anchor.y).value) /
                                                  static_cast<double>((corner.y - anchor.y).value);
                                // Diffère : refreshImage() détruirait cet item pendant
                                // son propre événement souris (même défaut que
                                // NodeHandleItem/VectorObjectBodyItem).
                                QTimer::singleShot(0, this, [this, objectId, anchor, sx, sy] {
                                    undoStack_.execute(
                                        std::make_unique<commands::ScaleVectorObjectCommand>(
                                            objectId, anchor, sx, sy),
                                        project_);
                                    refreshImage();
                                    updateActions();
                                });
                            });
                        handle->setToolTip(tr("Glisser pour redimensionner (coin opposé fixe)"));
                        scene_->addItem(handle);
                        baseItems_.append(handle);
                    }
                }
            }
        }
    }

    // Poignée de rotation du remplissage tatami sélectionné : un axe montrant
    // l'orientation des fils, avec un bouton à faire glisser. Le nouvel angle est
    // validé au relâchement (commande annulable) — pas de recalcul par pixel.
    if (auto* fill = currentFillObject()) {
        const auto& tatami = std::get<document::TatamiParams>(fill->params);
        const Vec2um cUm = embroideryCentroid(*fill);
        const QPointF c(to_millimeters(cUm.x).value, -to_millimeters(cUm.y).value);
        double radius = 12.0;  // mm
        if (const auto* vec = project_.findObject(fill->source_vector)) {
            const QRectF bb = objectPainterPath(*vec).boundingRect();
            radius = std::clamp(0.45 * std::min(bb.width(), bb.height()), 6.0, 60.0);
        }
        const double theta = tatami.angle.radians;  // repère physique (Y haut)
        const double dx = radius * std::cos(theta);
        const double dy = -radius * std::sin(theta);  // scène : Y vers le bas
        QPen axisPen(AppTheme::instance().tokens().canvasHandle);
        axisPen.setCosmetic(true);
        axisPen.setWidth(2);
        auto* axis = scene_->addLine(QLineF(c.x() - dx, c.y() - dy, c.x() + dx, c.y() + dy), axisPen);
        axis->setZValue(98);
        baseItems_.append(axis);

        const ObjectId fillId = fill->id;
        auto* knob = new NodeHandleItem(
            QPointF(c.x() + dx, c.y() + dy), [this, fillId, c](QPointF released) {
                double angle = std::atan2(-(released.y() - c.y()), released.x() - c.x());
                angle = std::fmod(angle, std::numbers::pi);
                if (angle < 0.0) {
                    angle += std::numbers::pi;
                }
                // Diffère la commande : refreshImage() reconstruit la scène et
                // détruirait cette poignée pendant son propre événement souris
                // (usage après libération → crash).
                QTimer::singleShot(0, this, [this, fillId, angle] {
                    undoStack_.execute(
                        std::make_unique<commands::SetFillAngleCommand>(fillId, Angle{angle}),
                        project_);
                    refreshImage();
                    updateActions();
                });
            },
            [this, c](QPointF moved) {  // aperçu : angle affiché pendant le glisser
                double angle = std::atan2(-(moved.y() - c.y()), moved.x() - c.x());
                angle = std::fmod(angle, std::numbers::pi);
                if (angle < 0.0) {
                    angle += std::numbers::pi;
                }
                statusBar()->showMessage(
                    tr("Orientation : %1°").arg(angle * 180.0 / std::numbers::pi, 0, 'f', 0));
            });
        knob->setPen(QPen(AppTheme::instance().tokens().canvasHandle, 1.5));
        knob->setBrush(QBrush(AppTheme::instance().tokens().canvasHandle.lighter(160)));
        knob->setCursor(Qt::CrossCursor);
        knob->setToolTip(tr("Glisser pour tourner l'orientation des fils"));
        scene_->addItem(knob);
        baseItems_.append(knob);
    }

    // Guides satin paramétriques. Chaque extrémité est indépendante mais reste
    // contrainte à son rail ; la validation monotone est partagée avec les
    // tests du cœur, donc un geste accepté ne disparaît pas à la génération.
    if (satinGuideModeAct_ != nullptr && satinGuideModeAct_->isChecked() &&
        satinGuideTarget_) {
        if (const auto* obj = project_.findEmbroidery(*satinGuideTarget_)) {
            if (const auto* satin = std::get_if<document::SatinParams>(&obj->params)) {
                const ObjectId targetId = obj->id;
                const std::uint64_t generation = documentGeneration_;
                const auto guideJunctions = stitch_generation::satin_guide_junctions(*satin);
                for (std::size_t i = 0; i < satin->rungs.size(); ++i) {
                    const auto rung = satin->rungs[i];
                    const auto junction = guideJunctions[i];
                    const QPointF a(to_millimeters(rung.a.x).value,
                                    -to_millimeters(rung.a.y).value);
                    const QPointF b(to_millimeters(rung.b.x).value,
                                    -to_millimeters(rung.b.y).value);
                    QPen guidePen(AppTheme::instance().tokens().accent);
                    guidePen.setCosmetic(true);
                    const bool selected = selectedSatinGuide_ && *selectedSatinGuide_ == i;
                    guidePen.setWidthF(selected ? 3.0 : 1.5);
                    if (selected) {
                        guidePen.setColor(AppTheme::instance().tokens().accent.lighter(150));
                    }
                    auto* line = new SatinGuideItem(QLineF(a, b), [this, targetId, i, generation] {
                        QTimer::singleShot(0, this, [this, targetId, i, generation] {
                            if (generation != documentGeneration_ || !satinGuideTarget_ ||
                                *satinGuideTarget_ != targetId) {
                                return;
                            }
                            selectedSatinGuide_ = i;
                            displayImage(processed_);
                            updateActions();
                        });
                    });
                    line->setPen(guidePen);
                    line->setZValue(99);
                    line->setToolTip(
                        junction
                            ? tr("Guide satin #%1 — jonction structurelle verrouillée").arg(i + 1)
                        : rung.link_id
                            ? tr("Guide satin #%1 — lié : Maj+glisser une extrémité déplace tout "
                                 "le groupe, glisser seul ne modifie que cette section")
                                  .arg(i + 1)
                            : tr("Guide satin #%1 — cliquer pour sélectionner").arg(i + 1));
                    scene_->addItem(line);
                    baseItems_.append(line);

                    // Geste explicite du groupe lié (§ guides liés) : Maj+glisser une
                    // extrémité d'un guide portant un link_id déplace TOUT le groupe
                    // (une section par jonction) en préservant, pour chaque section,
                    // sa position normalisée entre la jonction et son propre voisin —
                    // jamais de coordonnée/angle recopié d'une section à l'autre. Un
                    // glisser sans Maj reste un geste local (édition d'angle) qui ne
                    // touche que cette section, comme avant.
                    const auto addEndpoint = [this, targetId, i, rung, generation](
                                                 QPointF scenePos,
                                                 stitch_generation::SatinGuideSide side) {
                        auto* handle = new NodeHandleItem(
                            scenePos,
                            [this, targetId, i, rung, generation, side](QPointF released) {
                                const Vec2um desired{
                                    to_micrometers(Millimeters{released.x()}),
                                    to_micrometers(Millimeters{-released.y()})};
                                const auto* current = project_.findEmbroidery(targetId);
                                const auto* satinNow =
                                    current != nullptr
                                        ? std::get_if<document::SatinParams>(&current->params)
                                        : nullptr;
                                if (satinNow == nullptr) {
                                    return;
                                }
                                const bool groupGesture =
                                    rung.link_id.has_value() &&
                                    (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier);
                                if (groupGesture) {
                                    const auto edits = stitch_generation::move_satin_guide_group(
                                        project_, current->source_vector, *rung.link_id, targetId,
                                        side, desired);
                                    if (!edits) {
                                        statusBar()->showMessage(
                                            tr("Déplacement du groupe refusé : intervalle "
                                               "épuisé ou jonction modifiée entretemps."));
                                        displayImage(processed_);
                                        return;
                                    }
                                    const bool changed = std::any_of(
                                        edits->begin(), edits->end(), [this](const auto& edit) {
                                            const auto* section =
                                                project_.findEmbroidery(edit.embroidery_id);
                                            const auto* sectionSatin =
                                                section != nullptr
                                                    ? std::get_if<document::SatinParams>(
                                                          &section->params)
                                                    : nullptr;
                                            return sectionSatin == nullptr ||
                                                   edit.guide_index >= sectionSatin->rungs.size() ||
                                                   sectionSatin->rungs[edit.guide_index] !=
                                                       edit.guide;
                                        });
                                    if (!changed) {
                                        return; // Maj+clic sans mouvement : aucun historique
                                                // fantôme
                                    }
                                    QTimer::singleShot(0, this, [this, edits = *edits, generation] {
                                        if (generation != documentGeneration_) {
                                            return;
                                        }
                                        std::vector<commands::SatinGuideEdit> commandEdits;
                                        commandEdits.reserve(edits.size());
                                        for (const auto& edit : edits) {
                                            commandEdits.push_back(
                                                {edit.embroidery_id, edit.guide_index, edit.guide});
                                        }
                                        const auto groupCount = commandEdits.size();
                                        undoStack_.execute(
                                            std::make_unique<commands::MoveSatinGuidesCommand>(
                                                std::move(commandEdits)),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                        statusBar()->showMessage(
                                            tr("%1 guides liés déplacés ensemble.")
                                                .arg(groupCount));
                                    });
                                    return;
                                }
                                const auto moved = stitch_generation::move_satin_guide_endpoint(
                                    *satinNow, i, side, desired);
                                if (!moved) {
                                    statusBar()->showMessage(tr(
                                        "Guide refusé : il doit rester ordonné sur les deux rails."));
                                    displayImage(processed_);
                                    return;
                                }
                                if (*moved == rung) {
                                    return;
                                }
                                QTimer::singleShot(
                                    0, this, [this, targetId, i, moved = *moved, generation] {
                                        if (generation != documentGeneration_) {
                                            return;
                                        }
                                        undoStack_.execute(
                                            std::make_unique<commands::MoveSatinGuideCommand>(
                                                targetId, i, moved),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                    });
                            });
                        handle->setPen(QPen(AppTheme::instance().tokens().accent, 1.5));
                        handle->setBrush(
                            QBrush(AppTheme::instance().tokens().accent.lighter(160)));
                        handle->setToolTip(
                            side == stitch_generation::SatinGuideSide::RailA
                                ? tr("Guide satin #%1 — extrémité rail A").arg(i + 1)
                                : tr("Guide satin #%1 — extrémité rail B").arg(i + 1));
                        scene_->addItem(handle);
                        baseItems_.append(handle);
                    };
                    if (!junction) {
                        addEndpoint(a, stitch_generation::SatinGuideSide::RailA);
                        addEndpoint(b, stitch_generation::SatinGuideSide::RailB);
                    }
                }
            }
        }
    }

    // Nœuds des rails d'une colonne satin (mode remodelage) : distinct du
    // mode guides ci-dessus (qui édite les barreaux transversaux) — ici, les
    // nœuds de rail_a/rail_b eux-mêmes. Rail A et rail B dans deux couleurs
    // dédiées pour rester lisibles quand les deux rails se croisent de près.
    if (railEditModeAct_ != nullptr && railEditModeAct_->isChecked() && railEditTarget_) {
        if (const auto* obj = project_.findEmbroidery(*railEditTarget_)) {
            if (const auto* satin = std::get_if<document::SatinParams>(&obj->params)) {
                const ObjectId targetId = obj->id;
                const std::uint64_t generation = documentGeneration_;
                const auto drawRail = [&](const geometry::Path& rail, commands::SatinRailSide side,
                                          const QColor& color) {
                    QPainterPath outline;
                    bool started = false;
                    for (const auto& node : rail.nodes) {
                        const QPointF p = modelToSceneMm(node.pos);
                        if (!started) {
                            outline.moveTo(p);
                            started = true;
                        } else {
                            outline.lineTo(p);
                        }
                    }
                    QPen railPen(color);
                    railPen.setCosmetic(true);
                    railPen.setWidth(2);
                    railPen.setStyle(Qt::DashLine);
                    auto* railItem = scene_->addPath(outline, railPen, Qt::NoBrush);
                    railItem->setZValue(98);
                    baseItems_.append(railItem);

                    for (std::size_t n = 0; n < rail.nodes.size(); ++n) {
                        const Vec2um pos = rail.nodes[n].pos;
                        const QPointF sceneMm = modelToSceneMm(pos);
                        auto* handle = new NodeHandleItem(
                            sceneMm,
                            [this, targetId, side, n, pos, generation](QPointF newSceneMm) {
                                const Vec2um newPos = sceneMmToModel(newSceneMm);
                                if (newPos == pos) {
                                    return;
                                }
                                // Diffère : refreshImage() détruirait cette poignée
                                // pendant son propre événement souris (crash) — même
                                // précaution que les autres poignées ci-dessus.
                                QTimer::singleShot(
                                    0, this, [this, targetId, side, n, pos, newPos, generation] {
                                        if (generation != documentGeneration_) {
                                            return;
                                        }
                                        undoStack_.execute(
                                            std::make_unique<commands::MoveSatinRailNodeCommand>(
                                                targetId, side, n, pos, newPos),
                                            project_);
                                        refreshImage();
                                        updateActions();
                                    });
                            });
                        handle->setPen(QPen(color, 1.5));
                        handle->setBrush(QBrush(color.lighter(160)));
                        handle->setToolTip(
                            tr("Rail %1, nœud #%2 — glisser pour déplacer")
                                .arg(side == commands::SatinRailSide::RailA ? "A" : "B")
                                .arg(n + 1));
                        scene_->addItem(handle);
                        baseItems_.append(handle);
                    }
                };
                drawRail(satin->rail_a, commands::SatinRailSide::RailA, QColor(200, 90, 40));
                drawRail(satin->rail_b, commands::SatinRailSide::RailB, QColor(40, 130, 200));
            }
        }
    }

    // Poignées de points de couture (Lot 8.2, mode d'édition des points) :
    // un point déplaçable par entrée TopStitch/Stitch de la vue brute de
    // l'objet ciblé (stitchEditView_, tenu à jour par refreshImage). Visuellement
    // et conceptuellement distinctes des poignées de nœuds vectoriels
    // ci-dessus (couleur dédiée) : ici on déplace le point COUSU (retouche
    // locale), pas le nœud source (qui régénère tout).
    if (stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked() && stitchEditView_ &&
        stitchEditTarget_) {
        // Un GraphicsItem par poignée coûte cher (mémoire + événements) : au-delà
        // de ce seuil, le mode reste actif mais n'affiche aucune poignée plutôt
        // que de dégrader l'interface (même logique que `drawDots` ci-dessous
        // pour la broderie complète). `updateActions()` (appelé juste après
        // tout `refreshImage()`) sort proprement du mode et l'explique en barre
        // de statut dès qu'il détecte ce dépassement — ce garde-fou ici couvre
        // seulement le tout premier rendu, avant qu'`updateActions()` ait pu
        // tourner (cf. son bloc `stitchEditTooManyPoints`).
        const auto& view = *stitchEditView_;
        const ObjectId targetId = *stitchEditTarget_;
        // Le seuil porte sur les points RÉELLEMENT déplaçables (une passe
        // TopStitch/Stitch), pas sur `view.raw.size()` qui compte aussi les
        // sauts/points de sous-couche jamais dotés d'une poignée.
        const auto movableCount = std::count_if(view.raw.begin(), view.raw.end(),
                                                 stitch_generation::is_movable_point);
        if (const auto* obj = project_.findEmbroidery(targetId);
            obj != nullptr &&
            static_cast<std::size_t>(movableCount) <= kMaxEditableStitchHandles) {
            // Overrides indexés une fois par `base_index` : la boucle sur les
            // poignées ci-dessous ne doit pas redevenir O(points*overrides) en
            // rescannant `obj->overrides` pour chaque point (motif dense).
            std::unordered_map<std::size_t, Vec2um> movedOverrides;
            movedOverrides.reserve(obj->overrides.size());
            for (const auto& ov : obj->overrides) {
                if (ov.moved_to) {
                    movedOverrides.emplace(ov.base_index, *ov.moved_to);
                }
            }
            for (std::size_t i = 0; i < view.raw.size(); ++i) {
                const auto& cmd = view.raw[i];
                if (!stitch_generation::is_movable_point(cmd)) {
                    continue;
                }
                Vec2um pos = cmd.pos;  // position affichée : retouche existante sinon brute
                if (auto it = movedOverrides.find(i); it != movedOverrides.end()) {
                    pos = it->second;
                }
                const QPointF sceneMm(to_millimeters(pos.x).value, -to_millimeters(pos.y).value);
                const std::size_t baseIndex = i;
                const std::uint64_t rawFingerprint = view.fingerprint;
                const std::uint32_t rawPointCount = view.point_count;
                // Génération du document au moment où la poignée est construite :
                // si elle a changé quand le QTimer différé ci-dessous se déclenche,
                // `project_` a été intégralement remplacé entretemps (nouveau
                // document/projet chargé/import DST) -- abandonner plutôt que de
                // muter un projet qui n'est plus celui visé par ce glisser.
                const std::uint64_t generation = documentGeneration_;
                auto* handle = new NodeHandleItem(
                    sceneMm, [this, targetId, baseIndex, pos, rawFingerprint, rawPointCount,
                              generation](QPointF newSceneMm) {
                        const Vec2um newPos{to_micrometers(Millimeters{newSceneMm.x()}),
                                            to_micrometers(Millimeters{-newSceneMm.y()})};
                        if (newPos == pos) {
                            return;  // pas de mouvement : aucune commande (pas de pollution undo)
                        }
                        // Diffère : refreshImage() détruirait cette poignée pendant
                        // son propre événement souris (même précaution que les
                        // poignées de nœuds/rotation ci-dessus).
                        QTimer::singleShot(
                            0, this,
                            [this, targetId, baseIndex, newPos, rawFingerprint, rawPointCount,
                             generation] {
                                if (generation != documentGeneration_) {
                                    return; // document remplacé pendant le délai : commande
                                            // abandonnée
                                }
                                undoStack_.execute(
                                    std::make_unique<commands::MoveStitchPointCommand>(
                                        targetId, baseIndex, newPos, rawFingerprint,
                                        rawPointCount),
                                    project_);
                                refreshImage();
                                updateActions();
                            });
                    },
                    [this](QPointF) {
                        statusBar()->showMessage(tr("Glisser pour déplacer le point de couture."));
                    });
                handle->setPen(QPen(AppTheme::instance().tokens().accent, 1.5));
                handle->setBrush(QBrush(AppTheme::instance().tokens().accent.lighter(150)));
                handle->setToolTip(tr("Point de couture #%1 — glisser pour déplacer").arg(i));
                scene_->addItem(handle);
                baseItems_.append(handle);
            }
        }
    }

    // Carte des régions par-dessus l'image (mode d'affichage segmentation).
    if (showSegAct_ != nullptr && showSegAct_->isChecked() && project_.segmentation) {
        const double mmPerPx = project_.mm_per_px.value;
        const auto map = segmentation::render_map(*project_.segmentation, selectedRegion_);
        const QImage mapImg(map.rgba.data(), map.width, map.height, map.width * 4,
                            QImage::Format_RGBA8888);
        auto* mapItem = scene_->addPixmap(QPixmap::fromImage(mapImg.copy()));
        mapItem->setTransform(QTransform::fromScale(mmPerPx, mmPerPx));
        mapItem->setPos(-map.width * mmPerPx / 2.0, -map.height * mmPerPx / 2.0);
        mapItem->setOpacity(0.9);
        baseItems_.append(mapItem);
    }
}

void MainWindow::renderStitches() {
    for (QGraphicsItem* it : stitchItems_) {
        scene_->removeItem(it);
        delete it;
    }
    stitchItems_.clear();

    if (showStitchesAct_ == nullptr || !showStitchesAct_->isChecked() || !sequence_) {
        return;
    }

    const int total = static_cast<int>(sequence_->commands.size());
    const int limit = simulating() ? simStep_ : total;
    // Les pastilles de pénétration (une ellipse par point) coûtent cher : on
    // ne les dessine que pour les petits motifs et hors simulation.
    const bool drawDots = !simulating() && total <= 4000;

    // Visibilité (filtres) et couleur de fil par objet de broderie.
    std::unordered_map<std::uint64_t, bool> visible;
    std::unordered_map<std::uint64_t, QRgb> colorOf;
    for (const auto& emb : project_.embroidery_objects) {
        visible[emb.id.value] = objectPassesFilter(emb);
        colorOf[emb.id.value] = qRgb(emb.rgb[0], emb.rgb[1], emb.rgb[2]);
    }
    const auto isVisible = [&](std::uint64_t src) {
        const auto it = visible.find(src);
        return it != visible.end() && it->second;
    };

    // Un tracé cousu par couleur de fil (pour afficher la broderie en couleur).
    std::map<QRgb, QPainterPath> sewByColor;
    QPainterPath jumpPath;
    QPainterPath dots;
    bool hasPos = false;
    QPointF last;
    std::uint64_t lastSource = 0;
    QPointF needle;
    bool hasNeedle = false;
    for (int i = 0; i < total && i <= limit; ++i) {
        const auto& cmd = sequence_->commands[static_cast<std::size_t>(i)];
        const QPointF p(to_millimeters(cmd.pos.x).value, -to_millimeters(cmd.pos.y).value);
        const std::uint64_t src = cmd.source.value;
        const bool vis = isVisible(src);
        switch (cmd.type) {
        case stitch::CommandType::Stitch:
            // On ne relie que deux points du MÊME objet (jamais entre objets).
            if (vis && hasPos && lastSource == src) {
                QPainterPath& path = sewByColor[colorOf[src]];
                path.moveTo(last);
                path.lineTo(p);
            }
            if (vis && drawDots) {
                dots.addEllipse(p, 0.15, 0.15);
            }
            last = p;
            hasPos = true;
            lastSource = src;
            if (vis) {
                needle = p;
                hasNeedle = true;
            }
            break;
        case stitch::CommandType::Jump:
            if (vis && hasPos && lastSource == src) {
                jumpPath.moveTo(last);
                jumpPath.lineTo(p);
            }
            last = p;
            hasPos = true;
            lastSource = src;
            if (vis) {
                needle = p;
                hasNeedle = true;
            }
            break;
        default:
            hasPos = false;  // ColorChange / End : rompt la continuité
            break;
        }
    }

    for (const auto& [rgb, path] : sewByColor) {
        QColor c = QColor::fromRgb(rgb);
        // Un fil très clair serait invisible sur fond blanc : on l'assombrit un peu.
        if (c.lightnessF() > 0.85) {
            c = c.darker(140);
        }
        QPen sewPen(c);
        sewPen.setCosmetic(true);
        sewPen.setWidth(2);
        auto* sewItem = scene_->addPath(path, sewPen);
        sewItem->setZValue(20);
        stitchItems_.append(sewItem);
    }

    const Tokens& tk = AppTheme::instance().tokens();
    QPen jumpPen(tk.canvasJump);
    jumpPen.setCosmetic(true);
    jumpPen.setStyle(Qt::DashLine);
    auto* jumpItem = scene_->addPath(jumpPath, jumpPen);
    jumpItem->setZValue(20);
    stitchItems_.append(jumpItem);

    if (drawDots) {
        auto* dotsItem = scene_->addPath(dots, Qt::NoPen, QBrush(tk.canvasStitch));
        dotsItem->setZValue(21);
        stitchItems_.append(dotsItem);
    }

    if (simulating() && hasNeedle) {
        auto* marker = scene_->addEllipse(needle.x() - 0.6, needle.y() - 0.6, 1.2, 1.2,
                                          QPen(Qt::NoPen), QBrush(tk.error));
        marker->setZValue(30);
        stitchItems_.append(marker);
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
    if (!project_.hasImage() || processed_.empty()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Segmenter l'image"));
    auto* layout = new QFormLayout(&dialog);
    auto* colorsSpin = new QSpinBox(&dialog);
    colorsSpin->setRange(2, 64);
    colorsSpin->setValue(8);
    auto* minSizeSpin = new QSpinBox(&dialog);
    minSizeSpin->setRange(1, 100'000);
    minSizeSpin->setValue(16);
    minSizeSpin->setSuffix(tr(" px"));
    // L'affectation pixel-à-pixel au centre de couleur le plus proche est
    // intrinsèquement bruitée (effet « poivre et sel » sur les photos,
    // dégradés, artefacts JPEG) : lissée par défaut par vote local
    // majoritaire (cf. segmentation::segment) pour obtenir des formes
    // nettes directement exploitables, sans étape de nettoyage manuel.
    auto* smoothingSpin = new QSpinBox(&dialog);
    smoothingSpin->setRange(0, 20);
    smoothingSpin->setValue(3);
    smoothingSpin->setSuffix(tr(" px"));
    smoothingSpin->setToolTip(
        tr("0 = désactivé. Arrondit les frontières et absorbe le bruit pixel à pixel ; "
           "une valeur trop élevée efface les détails plus fins qu'elle."));
    layout->addRow(tr("Nombre maximal de couleurs :"), colorsSpin);
    layout->addRow(tr("Taille minimale de région :"), minSizeSpin);
    layout->addRow(tr("Lissage des formes :"), smoothingSpin);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Calcul synchrone (curseur d'attente) : le passage en tâche de fond est
    // prévu quand les images de travail deviendront grandes.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    auto seg = segmentation::segment(processed_, {.max_colors = colorsSpin->value(),
                                                  .min_region_px = minSizeSpin->value(),
                                                  .smoothing_radius_px = smoothingSpin->value()});
    QGuiApplication::restoreOverrideCursor();
    if (!seg) {
        QMessageBox::warning(this, tr("Segmentation impossible"),
                             QString::fromStdString(seg.error().message));
        return;
    }
    const auto regionCount = seg->region_count();
    undoStack_.execute(std::make_unique<commands::SetSegmentationCommand>(std::move(*seg)),
                       project_);
    selectedRegion_.reset();
    showSegAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Segmentation : %1 régions").arg(regionCount));
}

QPainterPath MainWindow::objectPainterPath(const document::VectorObject& object) {
    QPainterPath painterPath;
    painterPath.setFillRule(Qt::OddEvenFill);
    // Scène en mm, Y vers le bas : inversion du repère physique.
    const auto toScene = [](Vec2um p) {
        return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
    };
    const auto addPath = [&](const geometry::Path& path) {
        const std::size_t n = path.nodes.size();
        if (n == 0) {
            return;
        }
        painterPath.moveTo(toScene(path.nodes[0].pos));
        const std::size_t edges = path.closed ? n : n - 1;
        for (std::size_t e = 0; e < edges; ++e) {
            const auto& a = path.nodes[e];
            const auto& b = path.nodes[(e + 1) % n];
            // Segment courbe (au moins une tangente) -> cubique de Bézier
            // réelle, jamais une approximation par segments droits : c'est ce
            // même contour qui sert à l'affichage ET au hit-test des clics.
            if (a.tan_out || b.tan_in) {
                const QPointF c1 = a.tan_out ? toScene(a.pos + *a.tan_out) : toScene(a.pos);
                const QPointF c2 = b.tan_in ? toScene(b.pos + *b.tan_in) : toScene(b.pos);
                painterPath.cubicTo(c1, c2, toScene(b.pos));
            } else {
                painterPath.lineTo(toScene(b.pos));
            }
        }
        if (path.closed) {
            painterPath.closeSubpath();
        }
    };
    for (const auto& set : object.paths) {
        addPath(set.outer);
        for (const auto& hole : set.holes) {
            addPath(hole);
        }
    }
    return painterPath;
}

void MainWindow::vectorizeSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    const auto* region = project_.segmentation->find(*selectedRegion_);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    auto sets = vectorization::vectorize_region(
        *project_.segmentation, *selectedRegion_,
        {.mm_per_px = project_.mm_per_px, .simplify_tolerance = Micrometers{200}});
    QGuiApplication::restoreOverrideCursor();
    if (!sets) {
        QMessageBox::warning(this, tr("Vectorisation impossible"),
                             QString::fromStdString(sets.error().message));
        return;
    }

    document::VectorObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Région %1").arg(selectedRegion_->value).toStdString();
    object.source_region = *selectedRegion_;
    object.rgb = region->rgb;
    object.paths = std::move(*sets);

    undoStack_.execute(std::make_unique<commands::AddVectorObjectCommand>(std::move(object)),
                       project_);
    selectedObject_ = project_.vector_objects.back().id;
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Objet vectoriel créé — cliquez-le pour éditer ses nœuds"));
}

void MainWindow::createRunningStitchObject() {
    if (!selectedObject_) {
        return;
    }
    const auto* source = project_.findObject(*selectedObject_);
    if (source == nullptr) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Objet de point de contour"));
    auto* layout = new QFormLayout(&dialog);
    auto* lengthSpin = new QDoubleSpinBox(&dialog);
    lengthSpin->setRange(0.5, 12.0);
    lengthSpin->setValue(3.0);
    lengthSpin->setDecimals(1);
    lengthSpin->setSuffix(tr(" mm"));
    auto* typeCombo = new QComboBox(&dialog);
    typeCombo->addItem(tr("Point simple"), 1);
    typeCombo->addItem(tr("Aller-retour (double)"), 2);
    typeCombo->addItem(tr("Point triple"), 3);
    layout->addRow(tr("Longueur de point :"), lengthSpin);
    layout->addRow(tr("Type :"), typeCombo);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    document::RunningStitchParams params;
    params.stitch_length = to_micrometers(Millimeters{lengthSpin->value()});
    params.repeats = typeCombo->currentData().toInt();

    document::EmbroideryObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Contour de %1").arg(QString::fromStdString(source->name)).toStdString();
    object.source_vector = source->id;
    object.rgb = source->rgb;
    object.params = params;

    undoStack_.execute(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("Points générés : %1 points, %2 saut(s)")
                                     .arg(stats.stitches)
                                     .arg(stats.jumps));
    }
}

void MainWindow::createTatamiObject() {
    if (!selectedObject_) {
        return;
    }
    const auto* source = project_.findObject(*selectedObject_);
    if (source == nullptr) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Remplissage tatami"));
    auto* layout = new QFormLayout(&dialog);
    auto* densitySpin = new QDoubleSpinBox(&dialog);
    densitySpin->setRange(0.1, 2.0);
    densitySpin->setValue(0.4);
    densitySpin->setDecimals(2);
    densitySpin->setSuffix(tr(" mm"));
    auto* lengthSpin = new QDoubleSpinBox(&dialog);
    lengthSpin->setRange(1.0, 8.0);
    lengthSpin->setValue(3.0);
    lengthSpin->setDecimals(1);
    lengthSpin->setSuffix(tr(" mm"));
    auto* angleSpin = new QSpinBox(&dialog);
    angleSpin->setRange(0, 179);
    angleSpin->setValue(0);
    angleSpin->setSuffix(tr(" °"));
    layout->addRow(tr("Espacement des rangées :"), densitySpin);
    layout->addRow(tr("Longueur de point :"), lengthSpin);
    layout->addRow(tr("Angle :"), angleSpin);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    document::TatamiParams params;
    params.row_spacing = to_micrometers(Millimeters{densitySpin->value()});
    params.stitch_length = to_micrometers(Millimeters{lengthSpin->value()});
    params.angle = Angle{angleSpin->value() * std::numbers::pi / 180.0};

    document::EmbroideryObject object;
    object.id = project_.object_ids.next();
    object.name = tr("Remplissage de %1").arg(QString::fromStdString(source->name)).toStdString();
    object.source_vector = source->id;
    object.rgb = source->rgb;
    object.params = params;

    // Sélectionne le nouveau remplissage pour que « Orientation du remplissage… »
    // s'applique directement à lui.
    selectedEmbroidery_ = object.id;
    undoStack_.execute(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(tr("Remplissage généré : %1 points").arg(stats.stitches));
    }
}

void MainWindow::autoDigitize() {
    if (!project_.segmentation) {
        QMessageBox::information(
            this, tr("Numérisation automatique"),
            tr("Segmentez d'abord l'image (menu Segmentation), puis relancez."));
        return;
    }
    if (!project_.embroidery_objects.empty() || !project_.vector_objects.empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Numérisation automatique"),
            tr("Des objets existent déjà ; la numérisation ajoute de nouveaux objets. "
               "Continuer ?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    autodigitize::AutoOptions opts;
    opts.mm_per_px = project_.mm_per_px;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    auto result = autodigitize::auto_digitize(*project_.segmentation, project_.object_ids, opts);
    QGuiApplication::restoreOverrideCursor();
    if (!result) {
        QMessageBox::warning(this, tr("Numérisation impossible"),
                             QString::fromStdString(result.error().message));
        return;
    }
    const std::size_t vecCount = result->vectors.size();
    const std::size_t embCount = result->embroideries.size();

    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::move(result->vectors), std::move(result->embroideries)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Numérisation automatique : %1 objet(s) vectoriel(s), %2 objet(s) de broderie — "
           "tous éditables")
            .arg(vecCount)
            .arg(embCount));
}

void MainWindow::segmentWithAi() {
    if (!project_.hasImage() || processed_.empty()) {
        QMessageBox::information(this, tr("Segmenter avec l'IA"), tr("Importez d'abord une image."));
        return;
    }
    const AiPreferences prefs = loadAiPreferences();
    if (!prefs.enabled) {
        const auto answer = QMessageBox::question(
            this, tr("Segmenter avec l'IA"),
            tr("La segmentation par IA n'est pas activée. Ouvrir les préférences maintenant ?"));
        if (answer == QMessageBox::Yes) {
            openAiPreferences();
        }
        return;
    }

    AiSegmentationDialog dialog(processed_, project_.mm_per_px, prefs, this);
    if (dialog.exec() != QDialog::Accepted || !dialog.hasResult()) {
        return;
    }
    auto seg = dialog.takeSegmentation();
    if (!seg) {
        return;
    }

    autodigitize::AutoOptions opts;
    opts.mm_per_px = project_.mm_per_px;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    auto result = autodigitize::auto_digitize(*seg, project_.object_ids, opts);
    QGuiApplication::restoreOverrideCursor();
    if (!result) {
        QMessageBox::warning(this, tr("Numérisation impossible"),
                             QString::fromStdString(result.error().message));
        return;
    }
    const std::size_t vecCount = result->vectors.size();
    const std::size_t embCount = result->embroideries.size();

    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::move(result->vectors), std::move(result->embroideries),
                           "Segmentation IA"),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Segmentation IA : %1 objet(s) vectoriel(s), %2 objet(s) de broderie créés.")
            .arg(vecCount)
            .arg(embCount));
}

void MainWindow::openAiPreferences() {
    AiPreferencesDialog dialog(this);
    dialog.setPreferences(loadAiPreferences());
    if (dialog.exec() == QDialog::Accepted) {
        saveAiPreferences(dialog.preferences());
    }
}

void MainWindow::createSatinObject() {
    if (!selectedObject_) {
        return;
    }
    const auto* source = project_.findObject(*selectedObject_);
    if (source == nullptr || source->paths.empty()) {
        return;
    }

    // Le moteur squelette (auto_satin::build_satin_columns) gère les formes
    // concaves/branchues sans faire sortir les barreaux de la région (cf.
    // docs/source/satin.md, § Rails automatiques : l'heuristique naïve
    // rails_from_contour — deux sommets les plus éloignés — en fait déborder
    // jusqu'à 57 % sur une forme réelle). Préféré ici, en mode Parametric
    // (rails Bézier épars, jonctions plus propres — § Objets satin
    // paramétriques) ; les anneaux et refus retombent automatiquement sur
    // Legacy À L'INTÉRIEUR de build_satin_columns (`columns` peuplé au lieu
    // de `parametric_columns`). L'heuristique naïve ne sert plus que de repli
    // ultime, sur une forme trop simple/dégénérée pour que l'analyse de
    // squelette produise quoi que ce soit (les deux champs vides).
    auto_satin::SatinColumnsParameters skeletonParams;
    skeletonParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
    const auto skeletonResult = auto_satin::build_satin_columns(source->paths.front(), skeletonParams);
    const bool useParametric = !skeletonResult.parametric_columns.empty();
    const std::size_t skeletonColumnCount =
        useParametric ? skeletonResult.parametric_columns.size() : skeletonResult.columns.size();

    std::optional<std::pair<geometry::Path, geometry::Path>> fallbackRails;
    if (skeletonColumnCount == 0) {
        fallbackRails = stitch_generation::rails_from_contour(source->paths.front().outer);
        if (!fallbackRails) {
            QMessageBox::warning(this, tr("Satin impossible"),
                                 tr("La forme est trop petite ou trop complexe pour une colonne "
                                    "satin. Essayez un remplissage tatami."));
            return;
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Colonne satin"));
    auto* layout = new QFormLayout(&dialog);
    auto* warn = new QLabel(
        tr("⚠ Le satin automatique est expérimental. Vérifiez impérativement le "
           "résultat (densité, virages, extrémités) avant tout passage sur machine."),
        &dialog);
    warn->setWordWrap(true);
    warn->setStyleSheet("color:#8a5a00;");
    layout->addRow(warn);
    auto* densitySpin = new QDoubleSpinBox(&dialog);
    densitySpin->setRange(0.1, 1.5);
    densitySpin->setValue(0.4);
    densitySpin->setDecimals(2);
    densitySpin->setSuffix(tr(" mm"));
    auto* compSpin = new QDoubleSpinBox(&dialog);
    compSpin->setRange(0.0, 1.0);
    compSpin->setValue(0.0);
    compSpin->setDecimals(2);
    compSpin->setSuffix(tr(" mm"));
    auto* underlayCheck = new QCheckBox(tr("Sous-couche centrale"), &dialog);
    underlayCheck->setChecked(true);
    layout->addRow(tr("Densité (écart) :"), densitySpin);
    layout->addRow(tr("Compensation de tirage :"), compSpin);
    layout->addRow(underlayCheck);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const Micrometers density = to_micrometers(Millimeters{densitySpin->value()});
    const Micrometers compensation = to_micrometers(Millimeters{compSpin->value()});
    const bool underlay = underlayCheck->isChecked();
    const document::SatinParams defaults;  // pour le seuil max_width (§5.3)

    std::vector<document::EmbroideryObject> objects;
    double worstWidthUm = 0.0;
    if (skeletonColumnCount > 0) {
        int idx = 0;
        const auto addColumn = [&](const auto& col) {
            const auto sp = autodigitize::satin_params_from_column(col, density, compensation, underlay,
                                                                    defaults.max_width);
            stitch_generation::SatinConfig probe;
            probe.density = density;
            worstWidthUm = std::max(
                worstWidthUm, stitch_generation::fill_satin(sp.rail_a, sp.rail_b, probe).max_width_um);

            document::EmbroideryObject object;
            object.id = project_.object_ids.next();
            object.name = (skeletonColumnCount > 1
                              ? tr("Satin de %1 (%2)")
                                    .arg(QString::fromStdString(source->name))
                                    .arg(++idx)
                              : tr("Satin de %1").arg(QString::fromStdString(source->name)))
                             .toStdString();
            object.source_vector = source->id;
            object.rgb = source->rgb;
            object.params = sp;
            objects.push_back(std::move(object));
        };
        if (useParametric) {
            for (const auto& col : skeletonResult.parametric_columns) {
                addColumn(col);
            }
        } else {
            for (const auto& col : skeletonResult.columns) {
                addColumn(col);
            }
        }
    } else {
        document::SatinParams sp;
        sp.rail_a = fallbackRails->first;
        sp.rail_b = fallbackRails->second;
        sp.density = density;
        sp.pull_compensation = compensation;
        sp.center_underlay = underlay;
        // Barreaux par défaut (correspondance ladder) : sans eux, la génération
        // retombe sur `fill_satin`, qui n'implémente qu'un sous-ensemble des
        // réglages exposés dans l'inspecteur (défaut trouvé par revue — voir
        // `default_rungs`).
        for (const auto& seg : stitch_generation::default_rungs(sp.rail_a, sp.rail_b, sp.density)) {
            sp.rungs.push_back({seg.first, seg.second, std::nullopt});
        }
        stitch_generation::SatinConfig probe;
        probe.density = density;
        worstWidthUm = stitch_generation::fill_satin(sp.rail_a, sp.rail_b, probe).max_width_um;

        document::EmbroideryObject object;
        object.id = project_.object_ids.next();
        object.name = tr("Satin de %1").arg(QString::fromStdString(source->name)).toStdString();
        object.source_vector = source->id;
        object.rgb = source->rgb;
        object.params = sp;
        objects.push_back(std::move(object));
    }

    // Avertissement de largeur excessive (§5.3) : ne masque jamais la limite
    // physique — on prévient et on suggère le tatami.
    if (worstWidthUm > static_cast<double>(defaults.max_width.value)) {
        const auto answer = QMessageBox::warning(
            this, tr("Satin large"),
            tr("La colonne atteint %1 mm de large — au-delà de la limite recommandée "
               "(%2 mm), le fil risque d'accrocher. Un remplissage tatami serait plus "
               "solide. Créer quand même le satin ?")
                .arg(worstWidthUm / 1000.0, 0, 'f', 1)
                .arg(defaults.max_width.value / 1000.0, 0, 'f', 1),
            QMessageBox::Yes | QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

    const std::size_t count = objects.size();
    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::vector<document::VectorObject>{}, std::move(objects),
                           "Colonne satin (création manuelle)"),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    if (sequence_) {
        const auto stats = stitch::compute_stats(*sequence_);
        statusBar()->showMessage(
            tr("%1 colonne(s) satin générée(s) : %2 points").arg(count).arg(stats.stitches));
    }
}

document::EmbroideryObject* MainWindow::currentFillObject() {
    if (selectedEmbroidery_) {
        if (auto* emb = project_.findEmbroidery(*selectedEmbroidery_);
            emb != nullptr && emb->is_tatami()) {
            return emb;
        }
    }
    if (selectedObject_) {
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
        if (object.visible && objectPainterPath(object).contains(posMm)) {
            hit = object.id;  // le dernier dessiné (au-dessus) gagne
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
            hit = &emb;  // le dernier de la liste gagne, comme objectAt
        }
    }
    return hit;
}

document::EmbroideryObject* MainWindow::embroideryForVector(ObjectId vectorId) {
    for (auto& emb : project_.embroidery_objects) {
        if (emb.source_vector == vectorId) {
            return &emb;
        }
    }
    return nullptr;
}

void MainWindow::autoConvertToSatin() {
    if (!selectedObject_) {
        return;
    }
    const auto* source = project_.findObject(*selectedObject_);
    if (source == nullptr || source->paths.empty()) {
        return;
    }

    // Mode Parametric (rails Bézier épars) préféré : jonctions plus propres,
    // validé visuellement sur 6 formes (cf. docs/source/satin.md, § Objets
    // satin paramétriques). Les anneaux et cas refusés retombent
    // automatiquement sur Legacy À L'INTÉRIEUR de build_satin_columns
    // (`columns` peuplé au lieu de `parametric_columns`) — on lit simplement
    // celui des deux qui a été rempli.
    auto_satin::SatinColumnsParameters skeletonParams;
    skeletonParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
    const auto result = auto_satin::build_satin_columns(source->paths.front(), skeletonParams);
    const bool useParametric = !result.parametric_columns.empty();
    const std::size_t columnCount =
        useParametric ? result.parametric_columns.size() : result.columns.size();

    const auto& rep = result.report;
    QString info = tr("Satinabilité : %1 (confiance %2)\n"
                      "Largeur min / moy / max : %3 / %4 / %5 mm\n"
                      "Branches : %6   ·   Colonnes proposées : %7")
                       .arg(QString::fromUtf8(auto_satin::to_string(rep.status)))
                       .arg(rep.confidence, 0, 'f', 2)
                       .arg(rep.minimum_width_mm, 0, 'f', 2)
                       .arg(rep.mean_width_mm, 0, 'f', 2)
                       .arg(rep.maximum_width_mm, 0, 'f', 2)
                       .arg(rep.branch_count)
                       .arg(columnCount);
    for (const auto& w : result.warnings) {
        info += tr("\nAttention : %1").arg(QString::fromStdString(w));
    }

    if (columnCount == 0) {
        QMessageBox::information(
            this, tr("Conversion en satin"),
            tr("Conversion impossible : %1\n\n%2")
                .arg(QString::fromStdString(result.refusal), info));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Convertir en satin"),
        tr("%1\n\nCréer %2 colonne(s) satin ? (annulable)").arg(info).arg(columnCount));
    if (answer != QMessageBox::Yes) {
        return;
    }

    const document::SatinParams defaults;
    std::vector<document::EmbroideryObject> objects;
    int idx = 0;
    const auto addColumn = [&](const auto& col) {
        const auto sp = autodigitize::satin_params_from_column(
            col, defaults.density, defaults.pull_compensation, defaults.center_underlay,
            defaults.max_width);
        document::EmbroideryObject emb;
        emb.id = project_.object_ids.next();
        emb.name = tr("Satin auto de %1 (%2)")
                       .arg(QString::fromStdString(source->name))
                       .arg(++idx)
                       .toStdString();
        emb.source_vector = source->id;
        emb.rgb = source->rgb;
        emb.params = sp;
        objects.push_back(std::move(emb));
    };
    if (useParametric) {
        for (const auto& col : result.parametric_columns) {
            addColumn(col);
        }
    } else {
        for (const auto& col : result.columns) {
            addColumn(col);
        }
    }
    undoStack_.execute(std::make_unique<commands::AddObjectBatchCommand>(
                           std::vector<document::VectorObject>{}, std::move(objects)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 colonne(s) satin créée(s).").arg(columnCount));
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

void MainWindow::setStitchType(ObjectId embroideryId, int type) {
    auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr) {
        return;
    }
    document::StitchParams params;
    std::string label;
    switch (type) {
    case 0: {  // contour cousu
        document::RunningStitchParams rp;
        rp.repeats = 3;
        params = rp;
        label = "Type : contour";
        break;
    }
    case 1:  // tatami
        params = document::TatamiParams{};
        label = "Type : tatami";
        break;
    case 2: {  // satin : exige deux rails, construits depuis le contour source
        const auto* source = project_.findObject(emb->source_vector);
        if (source == nullptr || source->paths.empty()) {
            QMessageBox::warning(this, tr("Satin impossible"),
                                 tr("Aucun contour source pour construire les rails."));
            return;
        }
        // Préfère le moteur squelette en mode Parametric (gère les formes
        // concaves/branchues sans faire déborder les barreaux, jonctions plus
        // propres — cf. docs/source/satin.md) ; repli sur l'heuristique naïve
        // seulement si le squelette ne produit aucune colonne UNIQUE
        // exploitable (forme trop simple, ou décomposée en plusieurs sections
        // — hors du modèle « un objet, un type » de cette action).
        auto_satin::SatinColumnsParameters skeletonParams;
        skeletonParams.geometry_mode = auto_satin::SatinGeometryMode::Parametric;
        const auto skeletonResult = auto_satin::build_satin_columns(source->paths.front(), skeletonParams);
        const document::SatinParams defaults;  // densité/compensation/sous-couche inchangées ici
        document::SatinParams sp;
        if (skeletonResult.parametric_columns.size() == 1) {
            sp = autodigitize::satin_params_from_column(
                skeletonResult.parametric_columns.front(), defaults.density, defaults.pull_compensation,
                defaults.center_underlay, defaults.max_width);
        } else if (skeletonResult.columns.size() == 1) {
            sp = autodigitize::satin_params_from_column(
                skeletonResult.columns.front(), defaults.density, defaults.pull_compensation,
                defaults.center_underlay, defaults.max_width);
        } else {
            auto rails = stitch_generation::rails_from_contour(source->paths.front().outer);
            if (!rails) {
                QMessageBox::warning(this, tr("Satin impossible"),
                                     tr("La forme est trop petite ou trop complexe pour une colonne "
                                        "satin. Essayez un tatami."));
                return;
            }
            sp.rail_a = rails->first;
            sp.rail_b = rails->second;
        }
        params = sp;
        label = "Type : satin";
        break;
    }
    default:
        return;
    }
    undoStack_.execute(std::make_unique<commands::SetStitchTypeCommand>(embroideryId, std::move(params),
                                                                        std::move(label)),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
}

void MainWindow::onCanvasContextMenu(QPointF posMm, QPoint globalPos) {
    const auto hit = objectAt(posMm);
    QMenu menu(this);
    document::EmbroideryObject* emb = nullptr;
    std::optional<ObjectId> vecId;  // objet vectoriel à supprimer/dupliquer, si pertinent

    if (hit) {
        selectedObject_ = hit;
        selectedRegion_.reset();
        selectedEmbroidery_.reset();
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
        vecId = emb->source_vector;  // proxy caché : sa suppression entraîne le satin
        selectedEmbroidery_ = emb->id;
        selectedObject_.reset();
        selectedRegion_.reset();
        auto* title = menu.addAction(QString::fromStdString(emb->name));
        title->setEnabled(false);
        menu.addSeparator();
    }

    if (emb != nullptr) {
        const ObjectId embId = emb->id;
        auto* typeMenu = menu.addMenu(tr("Type de points"));
        const int current = emb->is_tatami() ? 1 : emb->is_satin() ? 2 : 0;
        const char* labels[] = {"Contour cousu", "Remplissage tatami", "Colonne satin"};
        for (int t = 0; t < 3; ++t) {
            auto* act = typeMenu->addAction(tr(labels[t]));
            act->setCheckable(true);
            act->setChecked(t == current);
            connect(act, &QAction::triggered, this, [this, embId, t] { setStitchType(embId, t); });
        }
        if (emb->is_tatami()) {
            auto* rot = menu.addAction(tr("Orientation du remplissage…"));
            connect(rot, &QAction::triggered, this, &MainWindow::changeFillAngle);
        }
        menu.addSeparator();
        // Supprimer la broderie seule n'a de sens que si la forme source
        // reste utilisable après (cas d'un vrai objet vectoriel visible,
        // convertible en un autre type) — pas pour le proxy caché du satin
        // manuel, où « Supprimer » plus bas fait déjà tout disparaître.
        if (hit) {
            auto* removeStitchAct = menu.addAction(tr("Supprimer la broderie (garder la forme)"));
            connect(removeStitchAct, &QAction::triggered, this,
                    [this, embId] { deleteEmbroideryObjectOnly(embId); });
        }
        auto* debugAct = menu.addAction(tr("&Déboguer : afficher toutes les données…"));
        connect(debugAct, &QAction::triggered, this, [this, embId] { showDebugDump(embId); });
    }

    if (vecId) {
        menu.addSeparator();
        const ObjectId targetVecId = *vecId;
        if (hit) {
            auto* dupAct = menu.addAction(tr("Dupliquer"));
            connect(dupAct, &QAction::triggered, this,
                    [this, targetVecId] { duplicateVectorObject(targetVecId); });
        }
        // Pas de raccourci Suppr ici : déjà utilisé par la suppression de
        // région (action toujours active au niveau fenêtre) — un second
        // raccourci identique créerait une ambiguïté Qt au lieu d'agir.
        auto* deleteAct = menu.addAction(tr("&Supprimer"));
        connect(deleteAct, &QAction::triggered, this,
                [this, targetVecId] { deleteVectorObject(targetVecId); });
    }

    if (!menu.isEmpty()) {
        menu.addSeparator();
    }
    // Calques accessibles partout (même sans objet sous le curseur).
    auto* layers = menu.addMenu(tr("Calques"));
    for (QAction* act : {showImageAct_, showSegAct_, showVectorsAct_, showStitchesAct_}) {
        if (act != nullptr) {
            layers->addAction(act);
        }
    }

    displayImage(processed_);  // reflète la sélection éventuelle
    updateActions();
    menu.exec(globalPos);
}

void MainWindow::deleteVectorObject(ObjectId id) {
    const auto* obj = project_.findObject(id);
    const QString name = obj != nullptr ? QString::fromStdString(obj->name) : tr("objet");
    undoStack_.execute(std::make_unique<commands::RemoveVectorObjectCommand>(id), project_);
    if (selectedObject_ == id) {
        selectedObject_.reset();
    }
    // La broderie rattachée disparaît avec l'objet vectoriel : si elle était
    // ciblée par la sélection, celle-ci ne doit pas pointer dans le vide.
    if (selectedEmbroidery_ && project_.findEmbroidery(*selectedEmbroidery_) == nullptr) {
        selectedEmbroidery_.reset();
    }
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("« %1 » supprimé.").arg(name));
}

void MainWindow::deleteEmbroideryObjectOnly(ObjectId id) {
    const auto* obj = project_.findEmbroidery(id);
    const QString name = obj != nullptr ? QString::fromStdString(obj->name) : tr("objet");
    undoStack_.execute(std::make_unique<commands::RemoveEmbroideryObjectCommand>(id), project_);
    if (selectedEmbroidery_ == id) {
        selectedEmbroidery_.reset();
    }
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
    // Léger décalage (2 mm) pour que la copie reste visible distinctement au
    // lieu de se superposer exactement à l'original.
    constexpr Micrometers kOffset{2'000};
    const auto shift = [&](geometry::Path& path) {
        for (auto& node : path.nodes) {
            node.pos = node.pos + Vec2um{kOffset, -kOffset};
        }
    };
    for (auto& set : copy.paths) {
        shift(set.outer);
        for (auto& hole : set.holes) {
            shift(hole);
        }
    }
    const QString sourceName = QString::fromStdString(source->name);
    undoStack_.execute(std::make_unique<commands::AddVectorObjectCommand>(copy), project_);
    selectedObject_ = copy.id;
    selectedRegion_.reset();
    selectedEmbroidery_.reset();
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("« %1 » dupliqué.").arg(sourceName));
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
               .arg(emb->is_satin()  ? QStringLiteral("Satin")
                    : emb->is_tatami() ? QStringLiteral("Tatami")
                                       : QStringLiteral("Contour (running stitch)"))
        << QStringLiteral("Visible : %1").arg(fmtBool(emb->visible))
        << QStringLiteral("Verrouillé : %1").arg(fmtBool(emb->locked))
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
            } else if constexpr (std::is_same_v<T, document::SatinParams>) {
                out << QStringLiteral("Variant : SatinParams")
                    << QStringLiteral("density : %1").arg(fmtUm(p.density))
                    << QStringLiteral("pull_compensation : %1").arg(fmtUm(p.pull_compensation))
                    << QStringLiteral("center_underlay : %1").arg(fmtBool(p.center_underlay))
                    << QStringLiteral("max_width : %1").arg(fmtUm(p.max_width))
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
                    << QStringLiteral("exit_point : %1").arg(fmtOptVec(p.exit_point))
                    << QString();

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
            out << QStringLiteral("  (cet objet ne produit aucune commande — vérifier params/rails)");
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

            std::array<std::size_t, 5> perPass{};  // Underlay,TopStitch,Travel,Lock,Manual
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
        stitch_analysis::AnalysisOptions opts;
        const document::Canvas& canvas = project_.canvas;
        opts.hoop = stitch::BoundsUm{
            Vec2um{Micrometers{-canvas.width.value / 2}, Micrometers{-canvas.height.value / 2}},
            Vec2um{Micrometers{canvas.width.value / 2}, Micrometers{canvas.height.value / 2}}};
        const auto findings = stitch_analysis::analyze(*sequence_, opts);
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
        const QString path = QFileDialog::getSaveFileName(
            &dialog, tr("Enregistrer les données de débogage"), QStringLiteral("debug.txt"),
            tr("Texte (*.txt)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(text.toUtf8());
        } else {
            QMessageBox::warning(this, tr("Erreur"), tr("Impossible d'écrire le fichier."));
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::buildHelpMenu() {
    // « Ajuster au canevas » aussi sur la touche F (en plus de Ctrl+0). On
    // n'intercepte PAS Tab (réservé à la navigation clavier, accessibilité).
    auto* fitShortcut = new QShortcut(QKeySequence(Qt::Key_F), this);
    connect(fitShortcut, &QShortcut::activated, view_, &CanvasView::fitCanvas);

    auto* helpMenu = menuBar()->addMenu(tr("Aid&e"));
    auto* shortcutsAct = helpMenu->addAction(tr("&Raccourcis clavier…"));
    connect(shortcutsAct, &QAction::triggered, this, [this] {
        QMessageBox::information(
            this, tr("Raccourcis clavier"),
            tr("Ctrl+O   Ouvrir une image\n"
               "Ctrl+S   Enregistrer le projet\n"
               "Ctrl+Z / Ctrl+Y   Annuler / Rétablir\n"
               "Suppr   Supprimer la région sélectionnée\n"
               "Ctrl++ / Ctrl+- / Ctrl+0   Zoom avant / arrière / ajuster\n"
               "F   Ajuster au canevas\n"
               "F5   Analyser le motif\n"
               "V / H / M   Outils : Sélection / Déplacer la vue / Rectangle\n"
               "Échap   Revenir à la Sélection\n"
               "Ctrl+Shift+P   Masquer / afficher les panneaux"));
    });
    auto* aboutAct = helpMenu->addAction(tr("À &propos"));
    connect(aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(
            this, tr("À propos"),
            tr("%1 %2\nNumérisation de broderie machine, logiciel libre (Apache-2.0).")
                .arg(QString::fromUtf8(kAppName), QString::fromUtf8(kAppVersion)));
    });
}

void MainWindow::buildMainToolbar() {
    mainToolbar_ = addToolBar(tr("Barre principale"));
    mainToolbar_->setObjectName(QStringLiteral("mainToolbar"));
    mainToolbar_->setMovable(false);
    mainToolbar_->setIconSize(QSize(18, 18));

    const auto add = [this](const QIcon& icon, const QString& text, auto slot) {
        auto* act = mainToolbar_->addAction(icon, text);
        act->setToolTip(text);
        connect(act, &QAction::triggered, this, slot);
        return act;
    };
    add(icons::openImage(), tr("Ouvrir une image"), &MainWindow::openImage);
    add(icons::openProject(), tr("Ouvrir un projet"), &MainWindow::loadProject);
    add(icons::save(), tr("Enregistrer le projet"), &MainWindow::saveProject);
    mainToolbar_->addSeparator();
    undoAct_->setIcon(icons::undo());
    redoAct_->setIcon(icons::redo());
    mainToolbar_->addAction(undoAct_);
    mainToolbar_->addAction(redoAct_);
    mainToolbar_->addSeparator();
    add(icons::zoomOut(), tr("Zoom arrière"), [this] { view_->zoomOut(); });
    add(icons::fit(), tr("Ajuster au canevas"), [this] { view_->fitCanvas(); });
    add(icons::zoomIn(), tr("Zoom avant"), [this] { view_->zoomIn(); });
    mainToolbar_->addSeparator();
    analyzeAct_->setIcon(icons::analyze());
    mainToolbar_->addAction(analyzeAct_);
    showStitchesAct_->setIcon(icons::stitches());
    mainToolbar_->addAction(showStitchesAct_);
    exportDstAct_->setIcon(icons::exportDst());
    mainToolbar_->addAction(exportDstAct_);
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
    if (emb == nullptr && selectedObject_) {
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
    return stitch_generation::ObjectEditState::Clean;  // absent du cache = Clean (jamais retouché)
}

void MainWindow::updateContextToolbar() {
    if (contextToolbar_ == nullptr) {
        return;
    }
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();
    const bool hasVec = selectedObject_.has_value();
    const bool hasReg = selectedRegion_ && project_.segmentation;
    const int pts = sequence_ ? static_cast<int>(sequence_->commands.size()) : 0;

    // Signature de l'état : évite de reconstruire (et de faire clignoter) la barre
    // quand rien de pertinent n'a changé.
    QString sig;
    if (emb != nullptr) {
        // Etat retouche (Lot 8.2) et mode d'edition inclus dans la signature :
        // sans eux, la barre ne se reconstruirait pas quand seul l'un des deux
        // change (meme objet/type toujours selectionne).
        sig = QStringLiteral("E%1t%2s%3m%4g%5i%6r%7")
                  .arg(emb->id.value)
                  .arg(stitchTypeIndex(*emb))
                  .arg(static_cast<int>(editStateOf(emb->id)))
                  .arg(stitchEditModeAct_->isChecked() ? 1 : 0)
                  .arg(satinGuideModeAct_->isChecked() ? 1 : 0)
                  .arg(selectedSatinGuide_ ? static_cast<qlonglong>(*selectedSatinGuide_) : -1)
                  .arg(railEditModeAct_->isChecked() ? 1 : 0);
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
    contextToolbar_->clear();

    if (emb != nullptr) {
        const ObjectId id = emb->id;
        contextToolbar_->addWidget(new QLabel(tr("Type de points :  "), contextToolbar_));
        const int current = stitchTypeIndex(*emb);
        const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin")};
        for (int t = 0; t < 3; ++t) {
            auto* btn = new QToolButton(contextToolbar_);
            btn->setText(names[t]);
            btn->setCheckable(true);
            btn->setChecked(t == current);
            connect(btn, &QToolButton::clicked, this, [this, id, t] { setStitchType(id, t); });
            contextToolbar_->addWidget(btn);
        }
        if (emb->is_tatami()) {
            contextToolbar_->addSeparator();
            auto* rot = contextToolbar_->addAction(tr("Orientation…"));
            connect(rot, &QAction::triggered, this, &MainWindow::changeFillAngle);
        }
        contextToolbar_->addSeparator();
        contextToolbar_->addAction(stitchEditModeAct_);
        if (emb->is_satin()) {
            // Un seul bouton visible par défaut (rails + guides ensemble) ;
            // le sous-menu Broderie ▸ Remodelage satin (avancé) reste
            // disponible pour n'afficher qu'un seul des deux aspects.
            contextToolbar_->addAction(satinEditModeAct_);
            if (satinGuideModeAct_->isChecked()) {
                contextToolbar_->addAction(addSatinGuideAct_);
                contextToolbar_->addAction(removeSatinGuideAct_);
            }
        }
        const auto editState = editStateOf(id);
        if (editState == stitch_generation::ObjectEditState::ManuallyEdited) {
            contextToolbar_->addWidget(new QLabel(tr("  ✎ retouché  "), contextToolbar_));
        } else if (editState == stitch_generation::ObjectEditState::Dirty) {
            auto* warn = new QLabel(tr("  ⚠ retouches obsolètes  "), contextToolbar_);
            warn->setToolTip(
                tr("La géométrie ou l'ordre de couture a changé depuis les dernières "
                   "retouches : elles ne sont plus appliquées."));
            contextToolbar_->addWidget(warn);
        }
        if (editState != stitch_generation::ObjectEditState::Clean) {
            auto* discard = contextToolbar_->addAction(tr("Abandonner les retouches"));
            connect(discard, &QAction::triggered, this, [this, id] { discardOverrides(id); });
        }
    } else if (hasVec) {
        contextToolbar_->addWidget(
            new QLabel(tr("Objet vectoriel  ·  créer un objet de broderie :  "), contextToolbar_));
        const auto addCreate = [this](const QString& text, void (MainWindow::*slot)()) {
            auto* act = contextToolbar_->addAction(text);
            connect(act, &QAction::triggered, this, slot);
        };
        addCreate(tr("Contour"), &MainWindow::createRunningStitchObject);
        addCreate(tr("Tatami"), &MainWindow::createTatamiObject);
        addCreate(tr("Satin"), &MainWindow::createSatinObject);
    } else if (hasReg) {
        const auto* region = project_.segmentation->find(*selectedRegion_);
        if (region != nullptr) {
            const double mmPerPx = project_.mm_per_px.value;
            const double areaMm2 = region->pixel_count * mmPerPx * mmPerPx;
            contextToolbar_->addWidget(new QLabel(
                tr("Région %1  ·  %2 mm²    ").arg(region->id.value).arg(areaMm2, 0, 'f', 1),
                contextToolbar_));
        }
        contextToolbar_->addAction(mergeAct_);  // Fusionner (mode)
        auto* del = contextToolbar_->addAction(tr("Supprimer"));
        connect(del, &QAction::triggered, this, &MainWindow::deleteSelectedRegion);
        auto* vec = contextToolbar_->addAction(tr("Vectoriser"));
        connect(vec, &QAction::triggered, this, &MainWindow::vectorizeSelectedRegion);
    } else {
        if (project_.hasImage() && sequence_) {
            const auto st = stitch::compute_stats(*sequence_);
            const double w = to_millimeters(st.bounds.max.x - st.bounds.min.x).value;
            const double h = to_millimeters(st.bounds.max.y - st.bounds.min.y).value;
            contextToolbar_->addWidget(new QLabel(
                tr("Motif : %1 × %2 mm   ·   %3 points   ·   %4 changement(s) de couleur    ")
                    .arg(w, 0, 'f', 1)
                    .arg(h, 0, 'f', 1)
                    .arg(st.stitches)
                    .arg(st.color_changes),
                contextToolbar_));
        } else {
            contextToolbar_->addWidget(new QLabel(
                tr("Ouvrez une image, ou sélectionnez une région ou un objet.    "),
                contextToolbar_));
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
        act->setToolTip(tr("%1 (%2)").arg(text, key.toString()));
        act->setShortcut(key);
        group->addAction(act);
        connect(act, &QAction::triggered, this, [this, tool] { setTool(tool); });
        return act;
    };
    toolSelectAct_ = addTool(icons::select(), tr("Sélection"), Tool::Select,
                             QKeySequence(Qt::Key_V));
    toolPanAct_ = addTool(icons::pan(), tr("Déplacer la vue"), Tool::Pan, QKeySequence(Qt::Key_H));
    toolRectAct_ = addTool(icons::rect(), tr("Rectangle / Recadrage"), Tool::Rect,
                           QKeySequence(Qt::Key_M));
    toolDrawRectAct_ = addTool(icons::drawRect(), tr("Dessiner un rectangle"),
                               Tool::DrawRectangle, QKeySequence(Qt::Key_R));
    toolDrawEllipseAct_ =
        addTool(icons::ellipse(), tr("Dessiner une ellipse (Maj = cercle)"), Tool::DrawEllipse,
               QKeySequence(Qt::Key_O));
    toolDrawPolygonAct_ =
        addTool(icons::polygon(), tr("Dessiner un polygone (segments droits)"), Tool::DrawPolygon,
               QKeySequence(Qt::Key_P));
    toolDrawBezierAct_ = addTool(
        icons::bezierCurve(),
        tr("Dessiner une courbe de Bézier (clic = coin, clic-glisser = nœud lisse)"),
        Tool::DrawBezier, QKeySequence(Qt::Key_B));
    toolDrawFreeformAct_ = addTool(icons::freeform(), tr("Dessiner à main levée (lasso)"),
                                   Tool::DrawFreeform, QKeySequence(Qt::Key_L));
    toolDrawSatinColumnAct_ =
        addTool(icons::satinColumn(), tr("Colonne satin (clics alternés côté A / côté B)"),
               Tool::DrawSatinColumn, QKeySequence(Qt::Key_S));
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
        }
    });

    // Échap : revient à la Sélection, annule le mode fusion en cours et
    // quitte le mode d'édition des points (Lot 8.2) s'il est actif -- pas
    // d'annulation du glisser en cours (l'architecture ne le permet pas
    // proprement : les poignées sont détruites/reconstruites à chaque
    // rafraîchissement, cf. renderBase), seulement une sortie propre du mode.
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] {
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
        }
    };
    auto* drawFinishReturn = new QShortcut(QKeySequence(Qt::Key_Return), this);
    connect(drawFinishReturn, &QShortcut::activated, this, finishDrawShortcut);
    auto* drawFinishEnter = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    connect(drawFinishEnter, &QShortcut::activated, this, finishDrawShortcut);
    auto* drawBackspace = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    connect(drawBackspace, &QShortcut::activated, this, [this] {
        if (currentTool_ == Tool::DrawPolygon) {
            removeLastPolygonVertex();
        } else if (currentTool_ == Tool::DrawBezier) {
            removeLastBezierPoint();
        } else if (currentTool_ == Tool::DrawSatinColumn) {
            removeLastSatinColumnPoint();
        }
    });
}

void MainWindow::setTool(Tool tool) {
    // Quitter l'outil Polygone en cours de tracé annule proprement le tracé
    // (aperçu détruit, sommets oubliés) — jamais de polygone à moitié posé
    // qui survivrait à un changement d'outil.
    if (currentTool_ == Tool::DrawPolygon && tool != Tool::DrawPolygon) {
        cancelPolygonDraw();
    }
    if (currentTool_ == Tool::DrawFreeform && tool != Tool::DrawFreeform) {
        cancelFreeformDraw();
    }
    if (currentTool_ == Tool::DrawSatinColumn && tool != Tool::DrawSatinColumn) {
        cancelSatinColumnDraw();
    }
    if (currentTool_ == Tool::DrawBezier && tool != Tool::DrawBezier) {
        cancelBezierDraw();
    }
    // Un changement d'outil peut laisser le repère d'accroche affiché à une
    // position qui ne correspond plus à rien (ex. sorti du mode Polygone
    // sans bouger la souris) : masqué jusqu'au prochain mouvement pertinent.
    updateSnapIndicator(std::nullopt);
    currentTool_ = tool;

    // Synchronise les cases d'outils (sans réémettre).
    const auto sync = [](QAction* act, bool on) {
        if (act == nullptr) return;
        QSignalBlocker block(act);
        act->setChecked(on);
    };
    sync(toolSelectAct_, tool == Tool::Select);
    sync(toolPanAct_, tool == Tool::Pan);
    sync(toolRectAct_, tool == Tool::Rect);
    sync(toolDrawRectAct_, tool == Tool::DrawRectangle);
    sync(toolDrawEllipseAct_, tool == Tool::DrawEllipse);
    sync(toolDrawPolygonAct_, tool == Tool::DrawPolygon);
    sync(toolDrawBezierAct_, tool == Tool::DrawBezier);
    sync(toolDrawFreeformAct_, tool == Tool::DrawFreeform);
    sync(toolDrawSatinColumnAct_, tool == Tool::DrawSatinColumn);
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
    view_->setBoxDrawMode(tool == Tool::DrawRectangle || tool == Tool::DrawEllipse);
    view_->setPolygonDrawMode(tool == Tool::DrawPolygon);
    view_->setBezierDrawMode(tool == Tool::DrawBezier);
    view_->setFreeformDrawMode(tool == Tool::DrawFreeform);
    view_->setSatinPairDrawMode(tool == Tool::DrawSatinColumn);
    if (tool == Tool::Pan) {
        view_->setCursor(Qt::OpenHandCursor);
        view_->viewport()->setCursor(Qt::OpenHandCursor);
    } else if (tool == Tool::Select) {
        view_->setCursor(Qt::ArrowCursor);
        view_->viewport()->setCursor(Qt::ArrowCursor);
    }

    if (toolLabel_ != nullptr) {
        const QString name = tool == Tool::Select        ? tr("Sélection")
                             : tool == Tool::Pan          ? tr("Déplacer la vue")
                             : tool == Tool::Rect          ? tr("Rectangle")
                             : tool == Tool::DrawRectangle ? tr("Dessiner un rectangle")
                             : tool == Tool::DrawEllipse   ? tr("Dessiner une ellipse")
                             : tool == Tool::DrawPolygon   ? tr("Dessiner un polygone")
                             : tool == Tool::DrawBezier    ? tr("Dessiner une courbe de Bézier")
                             : tool == Tool::DrawFreeform  ? tr("Dessiner à main levée")
                                                           : tr("Colonne satin");
        toolLabel_->setText(tr("Outil : %1").arg(name));
    }
    // Rappel explicite du geste attendu à l'activation de l'outil : défaut
    // trouvé en usage réel (rectangle/ellipse ne réagissaient « à rien » en
    // apparence pour qui cliquait sans glisser — le mécanisme fonctionnait,
    // mais rien n'indiquait qu'un GLISSER était nécessaire).
    if (tool == Tool::DrawRectangle || tool == Tool::DrawEllipse) {
        statusBar()->showMessage(
            tr("Cliquez-glissez sur le canevas pour dessiner le cadre, puis relâchez."));
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
    } else if (tool == Tool::DrawSatinColumn) {
        statusBar()->showMessage(
            tr("Cliquez alternativement côté A puis côté B de chaque paire — Entrée/"
               "double-clic/bouton ✓ pour terminer (2 paires min.), Échap pour annuler."));
    }
    updateDrawActionsState();
}

void MainWindow::buildWorkflowPanel() {
    workflowDock_ = new QDockWidget(tr("Workflow"), this);
    workflowDock_->setObjectName(QStringLiteral("workflowDock"));
    workflowDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    workflowPanel_ = new WorkflowPanel(workflowDock_);
    workflowDock_->setWidget(workflowPanel_);
    if (documentDock_ != nullptr) {
        splitDockWidget(documentDock_, workflowDock_, Qt::Vertical);  // sous Document
    } else {
        addDockWidget(Qt::LeftDockWidgetArea, workflowDock_);
    }
    connect(&AppTheme::instance(), &AppTheme::changed, workflowPanel_, &WorkflowPanel::applyTheme);
    connect(workflowPanel_, &WorkflowPanel::stepClicked, this, [this](int step) {
        static const char* hints[] = {
            QT_TR_NOOP("Menu Fichier ▸ Ouvrir une image pour commencer."),
            QT_TR_NOOP("Menu Segmentation ▸ Segmenter l'image, ou Broderie ▸ Segmenter avec l'IA."),
            QT_TR_NOOP("Sélectionnez une région, puis Segmentation ▸ Vectoriser (ou passez par "
                       "l'IA, qui vectorise directement)."),
            QT_TR_NOOP("Menu Broderie ▸ Numérisation automatique, ou créez un objet."),
            QT_TR_NOOP("Appuyez sur F5 (Analyse) pour vérifier le motif."),
            QT_TR_NOOP("Menu Fichier ▸ Exporter en DST.")};
        statusBar()->showMessage(tr(hints[step]), 6000);
    });
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
    const bool seq = sequence_.has_value();
    // La segmentation par IA (menu Broderie ▸ Segmenter avec l'IA) vectorise
    // directement les formes retenues sans jamais passer par
    // project_.segmentation (cf. AiSegmentationDialog + autodigitize::
    // auto_digitize) : ne jamais gater « Régions »/« Vecteurs » sur `seg`
    // seul, sous peine d'afficher « à faire » alors que des objets existent
    // déjà (défaut trouvé par revue ergonomie).
    const bool regionsDone = seg || vec;

    st[0] = img ? S::Done : S::NotStarted;
    st[1] = !img ? S::NotStarted : (regionsDone ? S::Done : S::Available);
    st[2] = !regionsDone ? S::NotStarted : (vec ? S::Done : S::Available);
    st[3] = emb ? S::Done : ((vec || seg) ? S::Available : S::NotStarted);
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
        tabifyDockWidget(documentDock_, orderDock_);  // Document et Ordre en onglets
        documentDock_->raise();
    }

    connect(documentPanel_, &DocumentPanel::embroiderySelected, this, [this](ObjectId id) {
        selectedEmbroidery_ = id;
        selectedRegion_.reset();
        if (const auto* e = project_.findEmbroidery(id);
            e != nullptr && project_.findObject(e->source_vector) != nullptr) {
            selectedObject_ = e->source_vector;  // met en évidence la forme au canevas
        } else {
            selectedObject_.reset();
        }
        displayImage(processed_);
        updateActions();
    });
    connect(documentPanel_, &DocumentPanel::regionSelected, this, [this](RegionId id) {
        selectedRegion_ = id;
        selectedObject_.reset();
        selectedEmbroidery_.reset();
        displayImage(processed_);
        updateActions();
    });
}

void MainWindow::refreshDocumentPanel() {
    if (documentPanel_ == nullptr) {
        return;
    }
    documentPanel_->refresh(project_, editStates_);
    documentDock_->setVisible(project_.hasImage() || !project_.embroidery_objects.empty());
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
        documentPanel_->syncSelection(DocumentPanel::Kind::Region, selectedRegion_->value);
    } else {
        documentPanel_->syncSelection(DocumentPanel::Kind::None, 0);
    }
}

void MainWindow::buildPropertiesPanel() {
    propertiesDock_ = new QDockWidget(tr("Propriétés"), this);
    propertiesDock_->setObjectName(QStringLiteral("propertiesDock"));
    propertiesDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    propertiesPanel_ = new PropertiesPanel(propertiesDock_);
    propertiesDock_->setWidget(propertiesPanel_);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);

    // Édition d'un paramètre -> commande annulable -> régénération.
    connect(propertiesPanel_, &PropertiesPanel::paramsEdited, this,
            [this](ObjectId id, document::StitchParams params) {
                undoStack_.execute(
                    std::make_unique<commands::SetStitchParamsCommand>(id, std::move(params)),
                    project_);
                refreshImage();
                // updateActions() (pas updateInspector() seul) : un changement de
                // paramètres peut faire passer un objet retouché à Dirty (Lot 8.2)
                // -- undo/redo, mode d'édition des points et indicateurs doivent
                // suivre. Ne reconstruit PAS le formulaire de paramètres (qui
                // perdrait le focus en cours de frappe) : le garde-fou
                // kind==inspectedKind_/id==inspectedId_ dans updateInspector()
                // s'applique toujours, seul l'état Clean/ManuallyEdited/Dirty
                // (mis à jour avant ce garde-fou) peut changer ici.
                updateActions();
            });
    connect(propertiesPanel_, &PropertiesPanel::discardOverridesRequested, this,
            &MainWindow::discardOverrides);
}

void MainWindow::updateInspector() {
    if (propertiesPanel_ == nullptr) {
        return;
    }
    // Cible : objet de broderie (dock Ordre) > remplissage rattaché à l'objet
    // vectoriel sélectionné > objet vectoriel > région > rien.
    const document::EmbroideryObject* emb = resolveSelectedEmbroidery();

    int kind = -1;
    std::uint64_t id = 0;
    if (emb != nullptr) {
        kind = 0;
        id = emb->id.value;
    } else if (selectedObject_) {
        kind = 1;
        id = selectedObject_->value;
    } else if (selectedRegion_ && project_.segmentation) {
        kind = 2;
        id = selectedRegion_->value;
    }

    // Indicateur Clean/ManuallyEdited/Dirty (Lot 8.2) : mis à jour à CHAQUE
    // appel, y compris quand kind/id n'ont pas changé (une retouche/undo/redo
    // peut faire évoluer l'état sans changer la sélection) -- volontairement
    // AVANT le "ne reconstruit que si la sélection a changé" ci-dessous, pour
    // ne jamais dépendre de lui.
    propertiesPanel_->setEditState(emb != nullptr ? std::optional<ObjectId>(emb->id) : std::nullopt,
                                   emb != nullptr ? editStateOf(emb->id)
                                                  : stitch_generation::ObjectEditState::Clean);

    // Ne reconstruit que si la sélection a changé (n'interrompt pas une édition).
    if (kind == inspectedKind_ && id == inspectedId_) {
        return;
    }
    inspectedKind_ = kind;
    inspectedId_ = id;

    if (kind == 0) {
        propertiesPanel_->showEmbroidery(*emb);
    } else if (kind == 1) {
        const auto* vec = project_.findObject(*selectedObject_);
        int nodes = 0;
        if (vec != nullptr) {
            for (const auto& set : vec->paths) {
                nodes += static_cast<int>(set.outer.nodes.size());
                for (const auto& h : set.holes) nodes += static_cast<int>(h.nodes.size());
            }
        }
        propertiesPanel_->showInfo(
            vec != nullptr ? QString::fromStdString(vec->name) : tr("Objet vectoriel"),
            tr("Objet vectoriel — %1 morceau(x), %2 nœud(s).\n"
               "Créez un objet de broderie (menu Broderie) pour régler la couture.")
                .arg(vec != nullptr ? vec->paths.size() : 0)
                .arg(nodes));
    } else if (kind == 2) {
        const auto* region = project_.segmentation->find(*selectedRegion_);
        if (region != nullptr) {
            const double mmPerPx = project_.mm_per_px.value;
            const double areaMm2 =
                region->pixel_count * mmPerPx * mmPerPx;
            propertiesPanel_->showInfo(
                tr("Région %1").arg(region->id.value),
                tr("Aire : %1 mm²   ·   %2 pixels\nCouleur : #%3%4%5")
                    .arg(areaMm2, 0, 'f', 1)
                    .arg(region->pixel_count)
                    .arg(region->rgb[0], 2, 16, QLatin1Char('0'))
                    .arg(region->rgb[1], 2, 16, QLatin1Char('0'))
                    .arg(region->rgb[2], 2, 16, QLatin1Char('0')));
        }
    } else {
        propertiesPanel_->showInfo(
            tr("Aucune sélection"),
            tr("Sélectionnez une région, un objet vectoriel ou un objet de broderie."));
    }
}

void MainWindow::buildAnalysisPanel() {
    analysisDock_ = new QDockWidget(tr("Analyse"), this);
    analysisDock_->setObjectName(QStringLiteral("analysisDock"));
    analysisDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);
    analysisList_ = new QListWidget(analysisDock_);
    analysisDock_->setWidget(analysisList_);
    addDockWidget(Qt::RightDockWidgetArea, analysisDock_);
    analysisDock_->hide();

    // Double-clic sur un problème : centre la vue sur sa localisation.
    connect(analysisList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QPointF mm = item->data(Qt::UserRole).toPointF();
        if (!mm.isNull()) {
            view_->centerOn(mm);
        }
    });

    auto* analyseMenu = menuBar()->addMenu(tr("A&nalyse"));
    analyzeAct_ = analyseMenu->addAction(tr("&Analyser le motif"));
    analyzeAct_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(analyzeAct_, &QAction::triggered, this, &MainWindow::runAnalysis);
}

void MainWindow::runAnalysis() {
    if (!sequence_) {
        QMessageBox::information(this, tr("Analyse"),
                                 tr("Générez d'abord des points de broderie."));
        return;
    }
    stitch_analysis::AnalysisOptions opts;
    const document::Canvas& canvas = project_.canvas;  // cadre défini par l'utilisateur
    opts.hoop = stitch::BoundsUm{Vec2um{Micrometers{-canvas.width.value / 2},
                                        Micrometers{-canvas.height.value / 2}},
                                 Vec2um{Micrometers{canvas.width.value / 2},
                                        Micrometers{canvas.height.value / 2}}};
    const auto findings = stitch_analysis::analyze(*sequence_, opts);

    analysisList_->clear();
    if (findings.empty()) {
        analysisList_->addItem(tr("✓ Aucun problème détecté."));
    } else {
        for (const auto& f : findings) {
            const QString prefix = f.severity == stitch_analysis::Severity::Error ? tr("⛔ ")
                                   : f.severity == stitch_analysis::Severity::Warning
                                       ? tr("⚠ ")
                                       : tr("ℹ ");
            auto* item = new QListWidgetItem(prefix + QString::fromStdString(f.message));
            item->setData(Qt::UserRole,
                          QPointF(to_millimeters(f.location.x).value,
                                  -to_millimeters(f.location.y).value));
            analysisList_->addItem(item);
        }
    }
    analysisDock_->show();
    analysisDock_->raise();
    statusBar()->showMessage(tr("Analyse : %1 problème(s) détecté(s)").arg(findings.size()));
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

    orderList_ = new QListWidget(panel);
    layout->addWidget(orderList_, 1);
    connect(orderList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < static_cast<int>(project_.embroidery_objects.size())) {
            selectedObject_.reset();  // sélection d'objet vectoriel distincte
            selectedEmbroidery_ = project_.embroidery_objects[static_cast<std::size_t>(row)].id;
            updateActions();
            displayImage(processed_);
        } else {
            selectedEmbroidery_.reset();
            updateActions();
        }
    });

    auto* buttons = new QHBoxLayout();
    auto* upBtn = new QPushButton(tr("↑ Monter"), panel);
    auto* downBtn = new QPushButton(tr("↓ Descendre"), panel);
    auto* lockBtn = new QPushButton(tr("🔒 Verrou"), panel);
    connect(upBtn, &QPushButton::clicked, this, &MainWindow::moveObjectUp);
    connect(downBtn, &QPushButton::clicked, this, &MainWindow::moveObjectDown);
    connect(lockBtn, &QPushButton::clicked, this, &MainWindow::toggleObjectLock);
    buttons->addWidget(upBtn);
    buttons->addWidget(downBtn);
    buttons->addWidget(lockBtn);
    layout->addLayout(buttons);

    orderStrategyCombo_ = new QComboBox(panel);
    orderStrategyCombo_->addItem(tr("Par couleur"),
                                 static_cast<int>(optimization::OrderStrategy::ByColor));
    orderStrategyCombo_->addItem(tr("Par proximité"),
                                 static_cast<int>(optimization::OrderStrategy::ByProximity));
    orderStrategyCombo_->addItem(tr("Couleur puis proximité"),
                                 static_cast<int>(optimization::OrderStrategy::ColorThenProximity));
    layout->addWidget(orderStrategyCombo_);
    auto* applyBtn = new QPushButton(tr("Optimiser l'ordre"), panel);
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::applyOrderStrategy);
    layout->addWidget(applyBtn);

    orderCostLabel_ = new QLabel(panel);
    layout->addWidget(orderCostLabel_);

    orderDock_->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, orderDock_);
    orderDock_->hide();
}

void MainWindow::refreshOrderPanel() {
    if (orderList_ == nullptr) {
        return;
    }
    const int previousRow = orderList_->currentRow();
    orderList_->clear();
    std::vector<optimization::OrderItem> items;
    for (const auto& obj : project_.embroidery_objects) {
        QString label = QString::fromStdString(obj.name);
        if (obj.locked) {
            label = tr("🔒 ") + label;
        }
        auto* item = new QListWidgetItem(label);
        QPixmap swatch(12, 12);
        swatch.fill(QColor(obj.rgb[0], obj.rgb[1], obj.rgb[2]));
        item->setIcon(QIcon(swatch));
        orderList_->addItem(item);
        items.push_back({obj.id, obj.rgb, embroideryCentroid(obj), obj.locked});
    }
    if (previousRow >= 0 && previousRow < orderList_->count()) {
        orderList_->setCurrentRow(previousRow);
    }

    const auto cost = optimization::compute_cost(items);
    orderCostLabel_->setText(tr("Trajet : %1 mm — %2 changement(s) de fil")
                                 .arg(cost.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(cost.color_changes));
    orderDock_->setVisible(!project_.embroidery_objects.empty());
}

int MainWindow::stitchTypeIndex(const document::EmbroideryObject& object) {
    return object.is_tatami() ? 1 : object.is_satin() ? 2 : 0;
}

double MainWindow::regionAreaMm2(const document::EmbroideryObject& object) const {
    const auto* vec = project_.findObject(object.source_vector);
    if (vec == nullptr) {
        return 0.0;
    }
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
    for (const auto& set : vec->paths) {
        area_um2 += shoelace_um2(set.outer);
        for (const auto& hole : set.holes) {
            area_um2 -= shoelace_um2(hole);
        }
    }
    return std::max(0.0, area_um2) / 1e6;
}

bool MainWindow::objectPassesFilter(const document::EmbroideryObject& object) const {
    if (!object.visible) {
        return false;
    }
    if (!showType_[static_cast<std::size_t>(stitchTypeIndex(object))]) {
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

    layout->addWidget(new QLabel(tr("Types de points :"), panel));
    const QString names[] = {tr("Contour"), tr("Tatami"), tr("Satin")};
    for (int t = 0; t < 3; ++t) {
        auto* check = new QCheckBox(names[t], panel);
        check->setChecked(true);
        connect(check, &QCheckBox::toggled, this, [this, t](bool on) {
            showType_[static_cast<std::size_t>(t)] = on;
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
    minAreaSpin_->setValue(0.0);
    connect(minAreaSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        minAreaMm2_ = v;
        displayImage(processed_);
    });
    layout->addWidget(minAreaSpin_);

    layout->addSpacing(6);
    layout->addWidget(new QLabel(tr("Couleurs de fil :"), panel));
    auto* colorContainer = new QWidget(panel);
    colorFilterLayout_ = new QVBoxLayout(colorContainer);
    colorFilterLayout_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(colorContainer);

    layout->addStretch(1);
    filterDock_->setWidget(panel);
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
        QPixmap swatch(12, 12);
        swatch.fill(QColor(rgb[0], rgb[1], rgb[2]));
        check->setIcon(QIcon(swatch));
        check->setText(QStringLiteral("#%1%2%3")
                           .arg(rgb[0], 2, 16, QLatin1Char('0'))
                           .arg(rgb[1], 2, 16, QLatin1Char('0'))
                           .arg(rgb[2], 2, 16, QLatin1Char('0')));
        connect(check, &QCheckBox::toggled, this, [this, key](bool on) {
            if (on) {
                hiddenColors_.erase(key);
            } else {
                hiddenColors_.insert(key);
            }
            displayImage(processed_);
        });
        colorFilterLayout_->addWidget(check);
    }
    filterDock_->setVisible(!project_.embroidery_objects.empty());
}

void MainWindow::moveObjectUp() {
    const int row = orderList_->currentRow();
    if (row <= 0) {
        return;
    }
    std::vector<ObjectId> order;
    for (const auto& obj : project_.embroidery_objects) {
        order.push_back(obj.id);
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
    if (row < 0 || row >= static_cast<int>(project_.embroidery_objects.size()) - 1) {
        return;
    }
    std::vector<ObjectId> order;
    for (const auto& obj : project_.embroidery_objects) {
        order.push_back(obj.id);
    }
    std::swap(order[static_cast<std::size_t>(row)], order[static_cast<std::size_t>(row + 1)]);
    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    orderList_->setCurrentRow(row + 1);
    updateActions();
}

void MainWindow::toggleObjectLock() {
    const int row = orderList_->currentRow();
    if (row < 0 || row >= static_cast<int>(project_.embroidery_objects.size())) {
        return;
    }
    const auto& obj = project_.embroidery_objects[static_cast<std::size_t>(row)];
    undoStack_.execute(
        std::make_unique<commands::SetEmbroideryLockCommand>(obj.id, !obj.locked), project_);
    refreshOrderPanel();
    updateActions();
}

void MainWindow::applyOrderStrategy() {
    if (project_.embroidery_objects.size() < 2) {
        return;
    }
    std::vector<optimization::OrderItem> items;
    for (const auto& obj : project_.embroidery_objects) {
        items.push_back({obj.id, obj.rgb, embroideryCentroid(obj), obj.locked});
    }
    const auto strategy = static_cast<optimization::OrderStrategy>(
        orderStrategyCombo_->currentData().toInt());
    const auto before = optimization::compute_cost(items);
    const auto order = optimization::optimize_order(items, strategy);

    undoStack_.execute(std::make_unique<commands::ReorderEmbroideryCommand>(order), project_);
    refreshImage();
    refreshOrderPanel();
    updateActions();

    // Coût après (recalculé sur le nouvel ordre).
    std::vector<optimization::OrderItem> reordered;
    for (const auto& obj : project_.embroidery_objects) {
        reordered.push_back({obj.id, obj.rgb, embroideryCentroid(obj), obj.locked});
    }
    const auto after = optimization::compute_cost(reordered);
    statusBar()->showMessage(tr("Ordre optimisé : trajet %1 → %2 mm, changements de fil %3 → %4")
                                 .arg(before.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(after.travel_um / 1000.0, 0, 'f', 1)
                                 .arg(before.color_changes)
                                 .arg(after.color_changes));
}

void MainWindow::buildSimulationToolbar() {
    simToolbar_ = addToolBar(tr("Simulation"));
    simToolbar_->setObjectName(QStringLiteral("simToolbar"));
    simToolbar_->setMovable(false);

    simPlayAct_ = simToolbar_->addAction(tr("▶ Lecture"));
    simPlayAct_->setCheckable(true);
    connect(simPlayAct_, &QAction::toggled, this, &MainWindow::toggleSimulation);

    simSlider_ = new QSlider(Qt::Horizontal, simToolbar_);
    simSlider_->setMinimum(0);
    simSlider_->setEnabled(false);
    connect(simSlider_, &QSlider::valueChanged, this, &MainWindow::onSimSliderMoved);
    simToolbar_->addWidget(simSlider_);

    simLabel_ = new QLabel(tr("— / —"), simToolbar_);
    simLabel_->setMinimumWidth(120);
    simToolbar_->addWidget(simLabel_);

    simTimer_ = new QTimer(this);
    simTimer_->setInterval(16);  // ~60 pas/seconde
    connect(simTimer_, &QTimer::timeout, this, &MainWindow::onSimTick);

    // Zone réservée : la barre reste visible (contrôles grisés sans séquence)
    // pour ne pas faire sauter la mise en page à l'apparition des points.
    updateSimulationRange();
}

void MainWindow::updateSimulationRange() {
    const bool hasSeq = sequence_ && !sequence_->commands.empty();
    simPlayAct_->setEnabled(hasSeq);
    simSlider_->setEnabled(hasSeq);
    if (!hasSeq) {
        simTimer_->stop();
        simPlayAct_->setChecked(false);
        simStep_ = -1;
        simLabel_->setText(tr("— / —"));
        return;
    }
    const int n = static_cast<int>(sequence_->commands.size()) - 1;
    simSlider_->blockSignals(true);
    simSlider_->setMaximum(n);
    simSlider_->setEnabled(true);
    if (simStep_ > n || simStep_ < 0) {
        simStep_ = -1;  // hors simulation : tout affiché
        simSlider_->setValue(n);
    }
    simSlider_->blockSignals(false);
    simLabel_->setText(tr("%1 / %2").arg(simulating() ? simStep_ : n).arg(n));
}

void MainWindow::toggleSimulation(/* play */) {
    if (!sequence_ || sequence_->commands.empty()) {
        simPlayAct_->setChecked(false);
        return;
    }
    if (simPlayAct_->isChecked()) {
        // Démarre (ou redémarre depuis le début si on était à la fin).
        if (!simulating() || simStep_ >= static_cast<int>(sequence_->commands.size()) - 1) {
            simStep_ = 0;
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
    // Avance proportionnellement à la taille (motif long = plus rapide).
    simStep_ = std::min(n, simStep_ + std::max(1, (n + 1) / 400));
    simSlider_->blockSignals(true);
    simSlider_->setValue(simStep_);
    simSlider_->blockSignals(false);
    simLabel_->setText(tr("%1 / %2").arg(simStep_).arg(n));
    renderStitches();  // seule la couche points change pendant la simulation
    if (simStep_ >= n) {
        simTimer_->stop();
        simPlayAct_->setChecked(false);
        simPlayAct_->setText(tr("▶ Lecture"));
    }
}

void MainWindow::onSimSliderMoved(int value) {
    simStep_ = value;
    const int n = static_cast<int>(sequence_ ? sequence_->commands.size() : 1) - 1;
    simLabel_->setText(tr("%1 / %2").arg(value).arg(n));
    renderStitches();
}

void MainWindow::showStatistics() {
    if (!sequence_) {
        return;
    }
    const auto stats = stitch::compute_stats(*sequence_);
    const double wMm = to_millimeters(stats.bounds.max.x - stats.bounds.min.x).value;
    const double hMm = to_millimeters(stats.bounds.max.y - stats.bounds.min.y).value;
    QMessageBox::information(
        this, tr("Statistiques de broderie"),
        tr("Points : %1\nSauts : %2\nCoupes : %3\nChangements de couleur : %4\n"
           "Dimensions : %5 × %6 mm\nFil cousu estimé : %7 m")
            .arg(stats.stitches)
            .arg(stats.jumps)
            .arg(stats.trims)
            .arg(stats.color_changes)
            .arg(wMm, 0, 'f', 1)
            .arg(hMm, 0, 'f', 1)
            .arg(stats.thread_length_um / 1e9, 0, 'f', 2));
}

void MainWindow::saveProject() {
    if (!project_.hasImage() && project_.vector_objects.empty()) {
        QMessageBox::information(this, tr("Rien à enregistrer"),
                                 tr("Ouvrez une image et créez des objets d'abord."));
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"), QString(),
                                                      tr("Projet OpenStitch (*.osp)"));
    if (file.isEmpty()) {
        return;
    }
    const auto written = project_io::save_project(std::filesystem::path(file.toStdWString()),
                                                  project_);
    if (!written) {
        QMessageBox::warning(this, tr("Enregistrement impossible"),
                             QString::fromStdString(written.error().message));
        return;
    }
    statusBar()->showMessage(tr("Projet enregistré : %1").arg(QFileInfo(file).fileName()));
    setWindowModified(false);
}

void MainWindow::loadProject() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"), QString(),
                                                      tr("Projet OpenStitch (*.osp)"));
    if (file.isEmpty()) {
        return;
    }
    auto loaded = project_io::load_project(std::filesystem::path(file.toStdWString()));
    if (!loaded) {
        QMessageBox::warning(this, tr("Ouverture impossible"),
                             QString::fromStdString(loaded.error().message));
        return;
    }
    applyLoadedProject(std::move(*loaded));
    statusBar()->showMessage(tr("Projet ouvert : %1 — %2 objet(s) vectoriel(s), %3 objet(s) de "
                                "broderie")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(project_.vector_objects.size())
                                 .arg(project_.embroidery_objects.size()));
}

void MainWindow::applyLoadedProject(document::Project project) {
    project_ = std::move(project);
    ++documentGeneration_;  // projet remplacé : invalide les commandes différées en vol
    undoStack_.clear();
    sequence_.reset();
    sequenceImported_ = false;
    selectedRegion_.reset();
    selectedObject_.reset();
    selectedEmbroidery_.reset();
    // Nouveau projet chargé : sortie propre du mode d'édition des points
    // (Lot 8.2), même raison que openImage() ci-dessus.
    if (stitchEditModeAct_ != nullptr) {
        QSignalBlocker block(stitchEditModeAct_);
        stitchEditModeAct_->setChecked(false);
    }
    stitchEditTarget_.reset();
    stitchEditView_.reset();
    refreshImage();
    view_->fitCanvas();
    updateActions();
    setWindowModified(false);  // projet fraîchement chargé = propre
}

void MainWindow::exportDst() {
    if (!sequence_) {
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Exporter en DST"), QString(),
                                                      tr("Broderie Tajima (*.dst)"));
    if (file.isEmpty()) {
        return;
    }

    // Résumé pré-export : décision réelle -> dialogue de confirmation.
    const auto stats = stitch::compute_stats(*sequence_);
    const double wMm = to_millimeters(stats.bounds.max.x - stats.bounds.min.x).value;
    const double hMm = to_millimeters(stats.bounds.max.y - stats.bounds.min.y).value;
    const auto& cv = project_.canvas;
    const bool overflow = stats.bounds.min.x.value < -cv.width.value / 2 ||
                          stats.bounds.max.x.value > cv.width.value / 2 ||
                          stats.bounds.min.y.value < -cv.height.value / 2 ||
                          stats.bounds.max.y.value > cv.height.value / 2;
    QString summary =
        tr("Dimensions : %1 × %2 mm\nPoints : %3\nSauts : %4\nCoupes : %5\n"
           "Changements de couleur : %6\nFil estimé : %7 m\nCadre : %8 × %9 mm\nFichier : %10")
            .arg(wMm, 0, 'f', 1)
            .arg(hMm, 0, 'f', 1)
            .arg(stats.stitches)
            .arg(stats.jumps)
            .arg(stats.trims)
            .arg(stats.color_changes)
            .arg(stats.thread_length_um / 1e9, 0, 'f', 2)
            .arg(to_millimeters(cv.width).value, 0, 'f', 0)
            .arg(to_millimeters(cv.height).value, 0, 'f', 0)
            .arg(QFileInfo(file).fileName());
    if (overflow) {
        summary += tr("\n\nAttention : le motif dépasse le cadre.");
    }
    summary += tr("\n\nLe DST ne conserve pas les objets éditables.");
    if (!project_.embroidery_objects.empty()) {
        summary += tr(" Pensez à enregistrer aussi le projet (.osp).");
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Exporter en DST"));
    box.setText(tr("Résumé de l'export"));
    box.setInformativeText(summary);
    box.setIcon(overflow ? QMessageBox::Warning : QMessageBox::Information);
    box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Ok);
    if (box.exec() != QMessageBox::Ok) {
        return;
    }

    // Rappel honnête (§17) : le DST ne conserve ni objets ni couleurs réelles.
    const auto written = formats::write_dst_file(std::filesystem::path(file.toStdWString()),
                                                 *sequence_);
    if (!written) {
        QMessageBox::warning(this, tr("Export impossible"),
                             QString::fromStdString(written.error().message));
        return;
    }
    statusBar()->showMessage(tr("DST exporté : %1 (%2 points).")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(stats.stitches));
}

void MainWindow::importDst() {
    const QString file = QFileDialog::getOpenFileName(this, tr("Importer un DST"), QString(),
                                                      tr("Broderie Tajima (*.dst)"));
    if (file.isEmpty()) {
        return;
    }
    if (project_.hasImage() || sequence_) {
        const auto answer = QMessageBox::question(
            this, tr("Importer un DST"),
            tr("L'import remplace le document en cours. Continuer ?"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    auto seq = formats::read_dst_file(std::filesystem::path(file.toStdWString()));
    if (!seq) {
        QMessageBox::warning(this, tr("Import impossible"),
                             QString::fromStdString(seq.error().message));
        return;
    }

    project_ = document::Project{};
    ++documentGeneration_;  // document remplacé : invalide les commandes différées en vol
    undoStack_.clear();
    processed_ = {};
    selectedRegion_.reset();
    selectedObject_.reset();
    sequence_ = std::move(*seq);
    sequenceImported_ = true;
    showStitchesAct_->setChecked(true);
    updateSimulationRange();
    displayImage(processed_);
    view_->fitCanvas();
    updateActions();

    const auto stats = stitch::compute_stats(*sequence_);
    statusBar()->showMessage(tr("%1 — %2 points, %3 saut(s), %4 changement(s) de fil")
                                 .arg(QFileInfo(file).fileName())
                                 .arg(stats.stitches)
                                 .arg(stats.jumps)
                                 .arg(stats.color_changes));
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
        QMessageBox::information(this, tr("Exporter en DXF"), tr("Aucun objet vectoriel à exporter."));
        return;
    }
    const QString file = QFileDialog::getSaveFileName(this, tr("Exporter en DXF"), QString(),
                                                      tr("Dessin AutoCAD (*.dxf)"));
    if (file.isEmpty()) {
        return;
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
        QMessageBox::warning(this, tr("Export impossible"),
                             QString::fromStdString(result.error().message));
        return;
    }
    statusBar()->showMessage(
        tr("%1 — %2 tracé(s) exporté(s)").arg(QFileInfo(file).fileName()).arg(paths.size()));
}

void MainWindow::onCanvasClicked(QPointF posMm) {
    // En mode « Déplacer la vue », un clic ne sélectionne rien.
    if (currentTool_ == Tool::Pan) {
        return;
    }
    if (currentTool_ == Tool::DrawPolygon) {
        posMm = findSnapPointMm(posMm).value_or(posMm);
        const Vec2um v = sceneMmToModel(posMm);
        if (!pendingPolygonVertices_.empty()) {
            // Séquence Qt d'un double-clic : press/release/DOUBLECLICK/release —
            // la seconde pression émet aussi un clic simple, quasi au même
            // point que le dernier sommet posé. Ignoré ici pour ne pas poser
            // un sommet fantôme juste avant la fermeture (onCanvasDoubleClicked).
            const auto& last = pendingPolygonVertices_.back();
            const double dx = static_cast<double>(v.x.value - last.x.value);
            const double dy = static_cast<double>(v.y.value - last.y.value);
            if (dx * dx + dy * dy < 4.0) {  // < 2 µm de l'un à l'autre
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
        posMm = findSnapPointMm(posMm).value_or(posMm);
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
            nextIsSideB
                ? tr("Colonne satin : cliquez le côté B de la paire — Échap : annuler")
                : tr("Colonne satin : %1 paire(s) posée(s) — cliquez le côté A de la paire "
                     "suivante — Entrée/double-clic : terminer (2 paires min.), Retour "
                     "arrière : retirer le dernier point")
                      .arg(pairs));
        return;
    }
    // Priorité aux objets vectoriels lorsqu'ils sont affichés (hors fusion).
    if (showVectorsAct_->isChecked() && !mergeMode_) {
        std::optional<ObjectId> hit;
        for (const auto& object : project_.vector_objects) {
            if (object.visible && objectPainterPath(object).contains(posMm)) {
                hit = object.id;  // le dernier dessiné (au-dessus) gagne
            }
        }
        if (hit) {
            selectedObject_ = hit;
            selectedRegion_.reset();
            selectedEmbroidery_.reset();  // la sélection au canevas prime
            const auto* object = project_.findObject(*hit);
            statusBar()->showMessage(tr("Objet « %1 » — %2 morceau(x), nœuds déplaçables")
                                         .arg(QString::fromStdString(object->name))
                                         .arg(object->paths.size()));
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
                    selectedEmbroidery_ = hitId;
                    selectedObject_.reset();
                    selectedRegion_.reset();
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
                selectedObject_.reset();
                selectedEmbroidery_.reset();
                displayImage(processed_);
                // updateActions() manquait ici (contrairement aux autres chemins
                // de sélection) : nécessaire pour que le mode d'édition des
                // points (Lot 8.2) sorte proprement quand la sélection est
                // perdue par un clic dans le vide plutôt que reportée sur un
                // autre objet.
                updateActions();
            }
        } else if (selectedObject_) {
            selectedObject_.reset();
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

    if (mergeMode_ && selectedRegion_ && clicked && *clicked != *selectedRegion_) {
        undoStack_.execute(
            std::make_unique<commands::MergeRegionsCommand>(*selectedRegion_, *clicked), project_);
        mergeAct_->setChecked(false);
        refreshImage();
        updateActions();
        return;
    }

    selectedRegion_ = clicked;
    if (clicked) {
        const auto* region = project_.segmentation->find(*clicked);
        const double mm2 = static_cast<double>(region->pixel_count) * project_.mm_per_px.value *
                           project_.mm_per_px.value;
        statusBar()->showMessage(tr("Région %1 — %2 px (%3 mm²) — RGB(%4, %5, %6)")
                                     .arg(region->id.value)
                                     .arg(region->pixel_count)
                                     .arg(mm2, 0, 'f', 1)
                                     .arg(region->rgb[0])
                                     .arg(region->rgb[1])
                                     .arg(region->rgb[2]));
    }
    displayImage(processed_);
    updateActions();
}

void MainWindow::deleteSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    undoStack_.execute(std::make_unique<commands::RemoveRegionCommand>(*selectedRegion_), project_);
    selectedRegion_.reset();
    refreshImage();
    updateActions();
}

void MainWindow::recolorSelectedRegion() {
    if (!selectedRegion_ || !project_.segmentation) {
        return;
    }
    const auto* region = project_.segmentation->find(*selectedRegion_);
    const QColor initial(region->rgb[0], region->rgb[1], region->rgb[2]);
    const QColor color = QColorDialog::getColor(initial, this, tr("Couleur de la région"));
    if (!color.isValid()) {
        return;
    }
    undoStack_.execute(
        std::make_unique<commands::RecolorRegionCommand>(
            *selectedRegion_, std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(color.red()),
                                                          static_cast<std::uint8_t>(color.green()),
                                                          static_cast<std::uint8_t>(color.blue())}),
        project_);
    refreshImage();
    updateActions();
}

void MainWindow::updateActions() {
    const bool hasImage = project_.hasImage();
    for (QAction* act : imageActions_) {
        act->setEnabled(hasImage);
    }
    showSegAct_->setEnabled(project_.segmentation.has_value());
    showVectorsAct_->setEnabled(!project_.vector_objects.empty());
    showStitchesAct_->setEnabled(!project_.embroidery_objects.empty());
    createStitchAct_->setEnabled(selectedObject_.has_value());
    createTatamiAct_->setEnabled(selectedObject_.has_value());
    createSatinAct_->setEnabled(selectedObject_.has_value());
    autoSatinAct_->setEnabled(selectedObject_.has_value());
    fillAngleAct_->setEnabled(currentFillObject() != nullptr);
    convertSatinAct_->setEnabled(std::any_of(project_.embroidery_objects.begin(),
                                             project_.embroidery_objects.end(),
                                             [](const auto& e) { return e.is_satin(); }));
    statsAct_->setEnabled(sequence_.has_value());
    exportDstAct_->setEnabled(sequence_.has_value());
    const bool hasSelection = selectedRegion_.has_value() && project_.segmentation.has_value();
    for (QAction* act : regionActions_) {
        act->setEnabled(hasSelection);
    }
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
        static_cast<std::size_t>(std::count_if(stitchEditView_->raw.begin(),
                                                stitchEditView_->raw.end(),
                                                stitch_generation::is_movable_point)) >
            kMaxEditableStitchHandles;
    bool stitchEditNeedsRerender = false;
    if (stitchEditModeAct_->isChecked() &&
        (stitchEditEmb == nullptr || !stitchEditSameTarget ||
         stitchEditState == stitch_generation::ObjectEditState::Dirty ||
         stitchEditTooManyPoints)) {
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
    if (stitchEditNeedsRerender) {
        displayImage(processed_);  // retire les poignées de l'objet quitté
    }

    document::EmbroideryObject* satinGuideEmb = resolveSelectedEmbroidery();
    const auto* selectedSatin =
        satinGuideEmb != nullptr
            ? std::get_if<document::SatinParams>(&satinGuideEmb->params)
            : nullptr;
    const bool satinGuideContext = selectedSatin != nullptr && selectedSatin->rungs.size() >= 2;
    const bool satinGuideSameTarget =
        satinGuideTarget_.has_value() && satinGuideEmb != nullptr &&
        *satinGuideTarget_ == satinGuideEmb->id;
    if (satinGuideModeAct_->isChecked() &&
        (!satinGuideContext || !satinGuideSameTarget)) {
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
                                     selectedSatin->rungs.size() > 2 &&
                                     !structuralJunctionGuide);

    document::EmbroideryObject* railEditEmb = resolveSelectedEmbroidery();
    const bool railEditContext = railEditEmb != nullptr && railEditEmb->is_satin();
    const bool railEditSameTarget =
        railEditTarget_.has_value() && railEditEmb != nullptr && *railEditTarget_ == railEditEmb->id;
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
    refreshWorkflow();
    updateEmptyState();
}

}  // namespace openstitch::desktop
