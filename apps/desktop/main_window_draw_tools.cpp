// SPDX-License-Identifier: Apache-2.0
// Outils de dessin : polygone, colonne satin, Bézier, main levée, accroche, aperçus.
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
            const Vec2um pt =
                curved ? cubicPointAt(p0, p1, p2, p3, t)
                       : Vec2um{Micrometers{static_cast<std::int32_t>(
                                    std::lround(p0.x.value + (p3.x.value - p0.x.value) * t))},
                                Micrometers{static_cast<std::int32_t>(
                                    std::lround(p0.y.value + (p3.y.value - p0.y.value) * t))}};
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

void MainWindow::onCropSelected(QRectF rectMm) {
    cropAct_->setChecked(false);
    if (!project_.hasImage() || processed_.empty()) {
        return;
    }
    // Les objets sont placés en mm sur le cadre : recadrer recentre l'image et les laisse où ils
    // sont, donc décalés par rapport à elle. Un glisser involontaire ne doit pas le faire seul.
    if (!project_.vector_objects.empty() || !project_.embroidery_objects.empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Recadrer l'image"),
            tr("Recadrer l'image la recentre sur le cadre : les objets déjà créés ne la suivront "
               "pas et seront décalés par rapport à elle. Recadrer quand même ?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
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
    if (currentTool_ != Tool::DrawRectangle && currentTool_ != Tool::DrawEllipse &&
        currentTool_ != Tool::DrawPolygonRegular) {
        return; // sécurité : signal reçu hors mode dessin (ne devrait pas arriver)
    }
    QRectF box = rectMm;
    const bool fromCenter = (modifiers & Qt::AltModifier) != 0;
    if (fromCenter) {
        // Alt (ligne D4) : le VRAI point d'appui (fourni par le canevas) est le centre du cadre.
        const QPointF anchor = view_->lastBoxPressMm();
        const double halfW =
            std::max(std::abs(rectMm.left() - anchor.x()), std::abs(rectMm.right() - anchor.x()));
        const double halfH =
            std::max(std::abs(rectMm.top() - anchor.y()), std::abs(rectMm.bottom() - anchor.y()));
        box = QRectF(anchor.x() - halfW, anchor.y() - halfH, 2.0 * halfW, 2.0 * halfH);
    }
    if (box.width() < kMinDrawExtentMm || box.height() < kMinDrawExtentMm) {
        statusBar()->showMessage(tr("Forme trop petite — glissez davantage."));
        return;
    }
    if (currentTool_ == Tool::DrawEllipse && (modifiers & Qt::ShiftModifier)) {
        // Maj enfoncée : contraint à un cercle (côté = le plus petit des deux).
        const QPointF center = box.center();
        const double side = std::min(box.width(), box.height());
        box.setWidth(side);
        box.setHeight(side);
        if (fromCenter) {
            box.moveCenter(center); // le centre reste le point d'appui
        }
    }
    // Accroche des deux coins, indépendamment l'un de l'autre (pas d'aperçu en
    // cours de glisser ici, contrairement au polygone/satin : le glisser
    // élastique rectangle/ellipse est le RubberBandDrag natif de Qt, pas un
    // aperçu personnalisable point par point).
    QPointF topLeft = box.topLeft();
    QPointF bottomRight = box.bottomRight();
    // Ctrl tenu au relâchement : accroche suspendue (ligne D5).
    if ((modifiers & Qt::ControlModifier) == 0) {
        if (const auto snap = findSnapPointMm(topLeft)) {
            topLeft = *snap;
        }
        if (const auto snap = findSnapPointMm(bottomRight)) {
            bottomRight = *snap;
        }
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
    } else if (currentTool_ == Tool::DrawEllipse) {
        addVectorPrimitive(geometry::ellipse_path(c1, c2), tr("Ellipse"));
    } else {
        // Polygone régulier inscrit dans le cadre glissé (cercle circonscrit
        // = la moitié du plus petit côté) : même principe que Maj sur
        // l'ellipse (contrainte au plus petit côté), sans exiger Maj puisque
        // ce n'est pas un choix optionnel ici mais la définition de l'outil.
        const Vec2um center{Micrometers{(c1.x.value + c2.x.value) / 2},
                            Micrometers{(c1.y.value + c2.y.value) / 2}};
        const std::int32_t w = std::abs(c2.x.value - c1.x.value);
        const std::int32_t h = std::abs(c2.y.value - c1.y.value);
        const Micrometers radius{std::min(w, h) / 2};
        const int sides = polygonSidesSpin_ != nullptr ? polygonSidesSpin_->value() : 6;
        addVectorPrimitive(geometry::regular_polygon_path(center, radius, sides),
                           tr("Polygone régulier"));
    }
}

void MainWindow::onCanvasDoubleClicked(QPointF posMm) {
    // Mode « Éditer les points » : double-clic près d'un trait de couture = nouveau point.
    if (stitchPointEditActive()) {
        insertStitchPointAt(posMm);
        return;
    }
    // Double-clic sur une lettre : rouvre le texte (le premier clic l'a sélectionnée).
    if (currentTool_ == Tool::Select && selectedTextId()) {
        editSelectedText();
        return;
    }
    // Un double-clic sur un satin sélectionné entre directement dans le remodelage
    // unifié (rails et guides). Il ne doit PAS insérer un nœud vectoriel par
    // surprise : la création de nœud reste disponible une fois le mode activé.
    if (currentTool_ == Tool::Select && satinEditModeAct_ != nullptr &&
        !satinEditModeAct_->isChecked()) {
        const auto* embroidery = resolveSelectedEmbroidery();
        if (embroidery != nullptr && embroidery->is_satin()) {
            satinEditModeAct_->setChecked(true);
            if (satinEditModeAct_->isChecked()) {
                statusBar()->showMessage(
                    tr("Remodelage satin : rails et guides actifs. Échap pour quitter."), 6000);
                return;
            }
        }
    }
    // HP-VEC-001/005 : outil Sélection, double-clic sur le contour = nouveau nœud ; Alt +
    // double-clic dans un trou = supprimer ce trou (hors mode remodelage des rails).
    if (currentTool_ == Tool::Select &&
        !(railEditModeAct_ != nullptr && railEditModeAct_->isChecked())) {
        const bool alt = (QGuiApplication::keyboardModifiers() & Qt::AltModifier) != 0;
        if (alt ? removeHoleAt(posMm) : insertNodeAt(posMm)) {
            return;
        }
    }
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
    if (drawingDirectionGuide()) {
        finishDirectionGuide();
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
        polygonPreviewItem_->setZValue(1000.0); // toujours au-dessus du reste
        scene_->addItem(polygonPreviewItem_);
    }
    QPainterPath path;
    path.moveTo(modelToSceneMm(pendingPolygonVertices_.front()));
    for (std::size_t i = 1; i < pendingPolygonVertices_.size(); ++i) {
        path.lineTo(modelToSceneMm(pendingPolygonVertices_[i]));
    }
    path.lineTo(cursorSceneMm); // segment élastique jusqu'au curseur
    polygonPreviewItem_->setPath(path);
}

void MainWindow::finishPolygon() {
    const bool open = openPathAct_ != nullptr && openPathAct_->isChecked();
    if (open && pendingPolygonVertices_.size() >= 2) {
        // Ligne brisée ouverte (HP-VEC-002) : mêmes sommets, sans segment de fermeture.
        geometry::Path path = geometry::polygon_path(pendingPolygonVertices_);
        path.closed = false;
        addVectorPrimitive(std::move(path), tr("Ligne"));
    } else if (!open && pendingPolygonVertices_.size() >= 3) {
        addVectorPrimitive(geometry::polygon_path(pendingPolygonVertices_), tr("Polygone"));
    } else {
        statusBar()->showMessage(open ? tr("Ligne : au moins 2 points requis.")
                                      : tr("Polygone : au moins 3 sommets requis."));
    }
    cancelPolygonDraw(); // nettoie l'état de tracé, succès ou échec
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
constexpr double kSnapRadiusPx = 10.0; // rayon d'accroche « visé » à l'écran
// Borné en mm (pas seulement en pixels écran) : à fort dézoom (grand hoop, ou
// même la fenêtre pas encore affichée/redimensionnée en test -- pixelsPerMm()
// alors minuscule), 10 px écran peuvent représenter des dizaines de mm, ce qui
// accrocherait à des formes sans rapport visuel avec le curseur. 3 mm reste
// perceptible à l'écran à peu près à tout niveau de zoom réaliste et borne le
// pire cas (défaut trouvé par test : accroche parasite à plus de 10 mm quand
// la fenêtre n'a pas encore de taille réelle).
constexpr double kSnapRadiusMinMm = 0.05;
constexpr double kSnapRadiusMaxMm = 2.0;
constexpr double kNodeSnapMaxMm = 1.0; // accroche des nœuds au glisser : jamais > 1 mm
} // namespace

std::optional<QPointF> MainWindow::findSnapPointMm(QPointF cursorSceneMm,
                                                   std::optional<ObjectId> excludeObject) const {
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
        if (!object.visible || !calqueShowsShape(object) ||
            (excludeObject && object.id == *excludeObject)) {
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
    if (!best) {
        best = gridSnapPointMm(cursorSceneMm, radiusMm); // HP-VEC-009 : grille, si activée
    }
    return best;
}

std::optional<QPointF> MainWindow::findNodeSnapMm(QPointF cursorSceneMm, ObjectId exclude) const {
    // Accroche des nœuds au glisser (réglage `edit/snapNodesOnDrag`) : SOMMETS des autres
    // objets visibles seulement (ni milieux de corde, ni centres), rayon <= 1 mm et <= 10 px.
    const double radiusMm =
        std::min(kNodeSnapMaxMm, kSnapRadiusPx / std::max(view_->pixelsPerMm(), 0.01));
    double bestDistSq = radiusMm * radiusMm;
    std::optional<QPointF> best;
    for (const auto& object : project_.vector_objects) {
        if (!object.visible || !calqueShowsShape(object) || object.id == exclude) {
            continue;
        }
        const auto consider = [&](const geometry::Path& path) {
            for (const auto& node : path.nodes) {
                const QPointF candidate = modelToSceneMm(node.pos);
                const double dx = candidate.x() - cursorSceneMm.x();
                const double dy = candidate.y() - cursorSceneMm.y();
                const double distSq = dx * dx + dy * dy;
                if (distSq <= bestDistSq) {
                    bestDistSq = distSq;
                    best = candidate;
                }
            }
        };
        for (const auto& set : object.paths) {
            consider(set.outer);
            for (const auto& hole : set.holes) {
                consider(hole);
            }
        }
    }
    return best;
}

void MainWindow::showNodeDragFeedback(QPointF sceneMm, ObjectId objectId) {
    // Ctrl suspend l'accroche (comme au relâchement) ; Maj ne la suspend pas.
    const bool snapActive = snapNodesAct_ != nullptr && snapNodesAct_->isChecked() &&
                            (QGuiApplication::keyboardModifiers() & Qt::ControlModifier) == 0;
    std::optional<QPointF> snap;
    if (snapActive) {
        snap = findNodeSnapMm(sceneMm, objectId);
    }
    const QPointF shown = snap.value_or(sceneMm);
    statusBar()->showMessage(tr("Nœud : %1 mm ; %2 mm%3")
                                 .arg(shown.x(), 0, 'f', 2)
                                 .arg(-shown.y(), 0, 'f', 2)
                                 .arg(snap ? tr("  — accroché à un sommet") : QString()));
    if (!snap) {
        hideNodeDragFeedback();
        return;
    }
    if (snapIndicator_ == nullptr) {
        auto* ring = new QGraphicsEllipseItem(-9.0, -9.0, 18.0, 18.0);
        ring->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        ring->setPen(QPen(AppTheme::instance().tokens().accent, 2.0));
        ring->setBrush(Qt::NoBrush);
        ring->setZValue(103);
        ring->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(ring);
        baseItems_.append(ring);
        snapIndicator_ = ring;
    }
    snapIndicator_->setPos(*snap);
}

void MainWindow::hideNodeDragFeedback() {
    if (snapIndicator_ != nullptr) {
        baseItems_.removeAll(snapIndicator_);
        delete snapIndicator_;
        snapIndicator_ = nullptr;
    }
}

void MainWindow::showResizePreview(QPointF anchorSceneMm, QPointF cornerSceneMm) {
    const QRectF box = QRectF(anchorSceneMm, cornerSceneMm).normalized();
    if (resizePreview_ == nullptr) {
        auto* rect = new QGraphicsRectItem();
        QPen pen(AppTheme::instance().tokens().canvasSelectionLine, 1.5, Qt::DashLine);
        pen.setCosmetic(true);
        rect->setPen(pen);
        rect->setBrush(Qt::NoBrush);
        rect->setZValue(99);
        rect->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(rect);
        baseItems_.append(rect);
        resizePreview_ = rect;
    }
    resizePreview_->setRect(box);
    statusBar()->showMessage(
        tr("Taille : %1 × %2 mm").arg(box.width(), 0, 'f', 2).arg(box.height(), 0, 'f', 2));
}

void MainWindow::hideResizePreview() {
    if (resizePreview_ != nullptr) {
        baseItems_.removeAll(resizePreview_);
        delete resizePreview_;
        resizePreview_ = nullptr;
    }
}

void MainWindow::updateHoverHighlight(std::optional<QPointF> sceneMm) {
    // Surbrillance de pré-sélection (ligne S11) : contour de l'objet non sélectionné le plus
    // haut sous le curseur, outil Sélection seulement. Un seul item, indépendant de baseItems_,
    // qui n'accepte aucun bouton (le clic atteint l'objet dessous). Les contours viennent d'un
    // cache reconstruit après chaque rendu de la couche base (renderBase).
    ++hoverComputations_;
    const HoverShape* under = nullptr;
    if (sceneMm && currentTool_ == Tool::Select && interactionContext() == Context::Select &&
        showVectorsAct_ != nullptr && showVectorsAct_->isChecked() && !mergeMode_) {
        if (!hoverCacheValid_) {
            hoverCache_.clear();
            for (const auto& object : project_.vector_objects) {
                if (object.visible && vectorPassesFilter(object)) {
                    QPainterPath path = objectScenePath(object);
                    const QRectF bounds = path.boundingRect();
                    hoverCache_.push_back({object.id, std::move(path), bounds});
                    ++hoverPathBuilds_;
                }
            }
            hoverCacheValid_ = true;
        }
        // Du plus haut (dessiné en dernier) au plus bas ; pré-rejet par boîte englobante.
        for (auto it = hoverCache_.rbegin(); it != hoverCache_.rend(); ++it) {
            if (it->bounds.contains(*sceneMm) && it->path.contains(*sceneMm)) {
                if (!isObjectSelected(it->id)) {
                    under = &*it;
                }
                break;
            }
        }
    }
    // Région sous le curseur (carte des régions) quand aucun objet vectoriel n'est survolé.
    updateRegionHover(under == nullptr ? sceneMm : std::nullopt);
    if (under == nullptr) {
        if (hoverItem_ != nullptr) {
            hoverItem_->setVisible(false);
        }
        return;
    }
    if (hoverItem_ == nullptr) {
        hoverItem_ = new QGraphicsPathItem();
        QPen pen(AppTheme::instance().tokens().accent, 2.0, Qt::DashLine);
        pen.setCosmetic(true);
        hoverItem_->setPen(pen);
        hoverItem_->setBrush(Qt::NoBrush);
        hoverItem_->setZValue(11.0); // au-dessus des formes (10), sous les poignées (100+)
        hoverItem_->setAcceptedMouseButtons(Qt::NoButton);
        hoverItem_->setAcceptHoverEvents(false);
        scene_->addItem(hoverItem_);
    }
    hoverItem_->setPath(under->path);
    hoverItem_->setVisible(true);
}

void MainWindow::scheduleHoverHighlight(QPointF sceneMm) {
    // Coalescence : le premier mouvement est traité aussitôt (réactivité), les suivants
    // attendent la fin du pas de 16 ms, avec seulement le dernier point retenu.
    if (hoverTimer_ == nullptr) {
        hoverTimer_ = new QTimer(this);
        hoverTimer_->setSingleShot(true);
        hoverTimer_->setInterval(16);
        connect(hoverTimer_, &QTimer::timeout, this, [this] {
            if (hoverPending_) {
                const QPointF pos = *hoverPending_;
                hoverPending_.reset();
                updateHoverHighlight(pos);
                hoverTimer_->start();
            }
        });
    }
    if (hoverTimer_->isActive()) {
        hoverPending_ = sceneMm;
        return;
    }
    hoverPending_.reset();
    updateHoverHighlight(sceneMm);
    hoverTimer_->start();
}

void MainWindow::hideHoverHighlight() {
    hoverPending_.reset();
    if (hoverItem_ != nullptr) {
        hoverItem_->setVisible(false);
    }
    hideRegionHover();
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
        snapIndicatorItem_->setPen(QPen(AppTheme::instance().tokens().warning, 2));
        snapIndicatorItem_->setBrush(Qt::NoBrush);
        snapIndicatorItem_->setZValue(1002.0); // au-dessus des aperçus élastiques (1000)
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
        satinPreviewItem_->setZValue(1000.0); // toujours au-dessus du reste
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
    if (pendingSatinPoints_.size() < 4) { // < 2 paires complètes
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
    sourceObject.visible = false; // le rendu passe par les rails/points du satin, pas ce contour
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
    // HP-ENG-004 : une colonne créée à la main est fractionnée d'office au-delà de
    // `max_stitch_length` (les projets existants gardent leur réglage persisté).
    params.split_stitch = document::SatinSplit::Staggered;
    embObject.params = std::move(params);

    const ObjectId newEmbId = embObject.id;
    std::vector<document::VectorObject> vectors{std::move(sourceObject)};
    std::vector<document::EmbroideryObject> embroideries{std::move(embObject)};
    undoStack_.execute(
        std::make_unique<commands::AddObjectBatchCommand>(
            std::move(vectors), std::move(embroideries), "Colonne satin (création manuelle)"),
        project_);

    setSelection({.region = std::nullopt, .embroidery = newEmbId, .objects = {}});
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
        bezierHandlePreviewItem_->setPath(QPainterPath()); // efface l'aperçu de poignée
    }
}

void MainWindow::onBezierPointDragging(QPointF anchorMm, QPointF currentMm) {
    if (currentTool_ != Tool::DrawBezier) {
        return;
    }
    updateBezierPreview(anchorMm); // tracé confirmé + segment élastique jusqu'à l'ancre

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
        const bool open = openPathAct_ != nullptr && openPathAct_->isChecked();
        geometry::Path path;
        path.closed = !open;
        path.nodes = pendingBezierNodes_;
        addVectorPrimitive(std::move(path), open ? tr("Courbe ouverte") : tr("Courbe"));
    } else {
        statusBar()->showMessage(tr("Courbe : au moins deux nœuds requis."));
    }
    cancelBezierDraw(); // nettoie l'état de tracé, succès ou échec
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
    statusBar()->showMessage(tr("Courbe : %1 nœud(s) restant(s).").arg(pendingBezierNodes_.size()));
}

void MainWindow::updateDrawActionsState() {
    if (finishDrawAct_ == nullptr || cancelDrawAct_ == nullptr) {
        return;
    }
    const bool drawing = (currentTool_ == Tool::DrawPolygon && !pendingPolygonVertices_.empty()) ||
                         (currentTool_ == Tool::DrawBezier && !pendingBezierNodes_.empty()) ||
                         (currentTool_ == Tool::DrawSatinColumn && !pendingSatinPoints_.empty()) ||
                         (drawingDirectionGuide() && !pendingGuidePoints_.empty());
    finishDrawAct_->setEnabled(drawing);
    cancelDrawAct_->setEnabled(drawing);
    updateShortcutsState();
}

void MainWindow::onFreeformPointAdded(QPointF posMm) {
    if (currentTool_ != Tool::DrawFreeform && currentTool_ != Tool::Cut) {
        return; // sécurité : signal reçu hors mode dessin (ne devrait pas arriver)
    }
    pendingFreeformPoints_.push_back(sceneMmToModel(posMm));
    if (freeformPreviewItem_ == nullptr) {
        freeformPreviewItem_ = new QGraphicsPathItem();
        QPen pen(QColor(80, 120, 200));
        pen.setWidthF(0.15);
        pen.setStyle(Qt::DashLine);
        freeformPreviewItem_->setPen(pen);
        freeformPreviewItem_->setZValue(1000.0); // toujours au-dessus du reste
        scene_->addItem(freeformPreviewItem_);
    }
    QPainterPath path;
    path.moveTo(modelToSceneMm(pendingFreeformPoints_.front()));
    if (currentTool_ == Tool::Cut) {
        // Couteau : droite du point d'appui au curseur.
        path.lineTo(modelToSceneMm(pendingFreeformPoints_.back()));
    } else {
        for (std::size_t i = 1; i < pendingFreeformPoints_.size(); ++i) {
            path.lineTo(modelToSceneMm(pendingFreeformPoints_[i]));
        }
    }
    freeformPreviewItem_->setPath(path);
}

void MainWindow::finishFreeform() {
    if (currentTool_ == Tool::Cut) {
        finishCut();
        return;
    }
    if (lassoSelectArmed_) {
        finishLassoSelection(); // même geste que « main levée », mais il saisit au lieu de créer
        return;
    }
    // La simplification (Douglas-Peucker) peut retomber sous 3 sommets même
    // avec un tracé brut suffisant (points quasi colinéaires, jitter sous la
    // tolérance) : on vérifie le résultat RÉEL, pas seulement le nombre de
    // points bruts, pour toujours donner un message clair plutôt qu'un
    // échec silencieux.
    geometry::Path path =
        pendingFreeformPoints_.size() >= 3
            ? geometry::freeform_path(pendingFreeformPoints_, kFreeformSimplifyTolerance)
            : geometry::Path{};
    if (!path.nodes.empty()) {
        addVectorPrimitive(std::move(path), tr("Forme libre"));
    } else {
        statusBar()->showMessage(tr("Forme libre : tracé trop court."));
    }
    cancelFreeformDraw(); // nettoie l'état de tracé, succès ou échec
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
        return; // dégénéré (ex. polygone à moins de 3 sommets) : rien à créer
    }
    document::VectorObject object;
    object.id = project_.object_ids.next();
    object.name = name.toStdString();
    object.rgb = {80, 120, 200};
    object.paths.push_back(geometry::PathSet{std::move(path), {}});

    undoStack_.execute(std::make_unique<commands::AddVectorObjectCommand>(std::move(object)),
                       project_);
    setSelection({.region = std::nullopt,
                  .embroidery = std::nullopt,
                  .objects = {project_.vector_objects.back().id}});
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Forme créée — menu Broderie pour la convertir en points de couture"));
}

} // namespace openstitch::desktop
