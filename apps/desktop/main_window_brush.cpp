// SPDX-License-Identifier: Apache-2.0
// Pinceau de déformation (HP-VEC-018) : un glisser avec l'outil Pinceau (Maj+B) pousse / tire le
// contour de la forme sélectionnée dans un rayon réglable, avec une influence décroissante ;
// Maj = lisser localement. Chaque coup de pinceau (appui -> relâchement) est UNE commande
// annulable ; Échap l'annule sans toucher au document. Le calcul est dans libs/geometry
// (`apply_brush_dab`) : ici seulement le geste, l'aperçu, les réglages et la commande.

#include <QAction>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <algorithm>
#include <cmath>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/geometry/brush.hpp"

namespace openstitch::desktop {

namespace {

Vec2um sceneToModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

QIcon brushIcon() {
    QPixmap pm(40, 40);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(AppTheme::instance().tokens().textSecondary);
    pen.setWidthF(2.8);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawEllipse(QPointF(18, 22), 11, 11);      // zone d'influence
    p.drawLine(QPointF(18, 22), QPointF(34, 8)); // flèche : pousser / tirer
    p.drawLine(QPointF(34, 8), QPointF(27, 9));
    p.drawLine(QPointF(34, 8), QPointF(33, 15));
    p.end();
    return QIcon(pm);
}

constexpr double kMinRadiusMm = 0.5;
constexpr double kMaxRadiusMm = 60.0;

} // namespace

void MainWindow::buildBrushTool() {
    QSettings settings;
    brushRadiusMm_ = std::clamp(settings.value(QStringLiteral("brush/radiusMm"), 5.0).toDouble(),
                                kMinRadiusMm, kMaxRadiusMm);
    brushStrength_ =
        std::clamp(settings.value(QStringLiteral("brush/strength"), 0.6).toDouble(), 0.05, 1.0);

    toolBrushAct_ = new QAction(brushIcon(), tr("Pinceau"), this);
    toolBrushAct_->setObjectName(QStringLiteral("tool_brush"));
    toolBrushAct_->setCheckable(true);
    toolBrushAct_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_B));
    toolBrushAct_->setToolTip(tr(
        "Pinceau de déformation (Maj+B)\nGlissez sur le contour de la forme sélectionnée pour le "
        "pousser ou le tirer ; Maj : lisser. [ et ] changent le rayon, Échap annule le coup "
        "de pinceau."));
    connect(toolBrushAct_, &QAction::triggered, this, [this] { setTool(Tool::Brush); });

    brushSettingsAct_ = new QAction(tr("Ré&glages du pinceau…"), this);
    brushSettingsAct_->setObjectName(QStringLiteral("action_brushSettings"));
    connect(brushSettingsAct_, &QAction::triggered, this, [this] {
        bool ok = false;
        const double radius =
            QInputDialog::getDouble(this, tr("Pinceau"), tr("Rayon d'influence (mm) :"),
                                    brushRadiusMm_, kMinRadiusMm, kMaxRadiusMm, 1, &ok);
        if (!ok) {
            return;
        }
        const double percent = QInputDialog::getDouble(this, tr("Pinceau"), tr("Intensité (%) :"),
                                                       brushStrength_ * 100.0, 5.0, 100.0, 0, &ok);
        if (ok) {
            setBrushSettings(radius, percent / 100.0);
        }
    });
    auto* smaller = new QAction(tr("Pinceau : rayon plus petit"), this);
    smaller->setObjectName(QStringLiteral("action_brushSmaller"));
    smaller->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    connect(smaller, &QAction::triggered, this,
            [this] { setBrushSettings(brushRadiusMm_ / 1.25, brushStrength_); });
    auto* larger = new QAction(tr("Pinceau : rayon plus grand"), this);
    larger->setObjectName(QStringLiteral("action_brushLarger"));
    larger->setShortcut(QKeySequence(Qt::Key_BracketRight));
    connect(larger, &QAction::triggered, this,
            [this] { setBrushSettings(brushRadiusMm_ * 1.25, brushStrength_); });
    addActions({smaller, larger}); // raccourcis actifs dans toute la fenêtre
    if (shapeMenu_ != nullptr) {
        shapeMenu_->addAction(toolBrushAct_);
        shapeMenu_->addAction(brushSettingsAct_);
    }

    connect(view_, &CanvasView::brushPressedMm, this, [this](QPointF p) { beginBrushStroke(p); });
    connect(view_, &CanvasView::brushMovedMm, this,
            [this](QPointF p, Qt::KeyboardModifiers mods) { moveBrushStroke(p, mods); });
    connect(view_, &CanvasView::brushReleasedMm, this, [this](QPointF p) { endBrushStroke(p); });
    connect(view_, &CanvasView::brushCancelled, this, [this] { cancelBrushStroke(); });
    connect(view_, &CanvasView::cursorMovedMm, this, [this](QPointF physMm) {
        if (currentTool_ == Tool::Brush) {
            updateBrushRing(QPointF(physMm.x(), -physMm.y()));
        }
    });
}

void MainWindow::attachBrushTool() {
    if (toolPalette_ != nullptr && toolBrushAct_ != nullptr &&
        !toolPalette_->actions().contains(toolBrushAct_)) {
        toolPalette_->addAction(toolBrushAct_);
    }
}

void MainWindow::setBrushSettings(double radiusMm, double strength) {
    brushRadiusMm_ = std::clamp(radiusMm, kMinRadiusMm, kMaxRadiusMm);
    brushStrength_ = std::clamp(strength, 0.05, 1.0);
    QSettings settings;
    settings.setValue(QStringLiteral("brush/radiusMm"), brushRadiusMm_);
    settings.setValue(QStringLiteral("brush/strength"), brushStrength_);
    if (brushRing_ != nullptr) {
        const QPointF c = brushRing_->pos();
        brushRing_->setRect(-brushRadiusMm_, -brushRadiusMm_, 2 * brushRadiusMm_,
                            2 * brushRadiusMm_);
        brushRing_->setPos(c);
    }
    if (currentTool_ == Tool::Brush) {
        statusBar()->showMessage(tr("Pinceau : rayon %1 mm, intensité %2 %")
                                     .arg(brushRadiusMm_, 0, 'f', 1)
                                     .arg(static_cast<int>(std::lround(brushStrength_ * 100.0))),
                                 3000);
    }
}

void MainWindow::updateBrushRing(QPointF sceneMm) {
    if (currentTool_ != Tool::Brush) {
        return;
    }
    if (brushRing_ == nullptr) {
        auto* ring = new QGraphicsEllipseItem(-brushRadiusMm_, -brushRadiusMm_, 2 * brushRadiusMm_,
                                              2 * brushRadiusMm_);
        QPen pen(AppTheme::instance().tokens().accent, 1.5, Qt::DashLine);
        pen.setCosmetic(true);
        ring->setPen(pen);
        ring->setBrush(Qt::NoBrush);
        ring->setZValue(99);
        ring->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(ring);
        baseItems_.append(ring);
        brushRing_ = ring;
    }
    brushRing_->setPos(sceneMm);
}

void MainWindow::hideBrushRing() {
    if (brushRing_ != nullptr) {
        baseItems_.removeAll(brushRing_);
        delete brushRing_;
        brushRing_ = nullptr;
    }
}

void MainWindow::syncBrushTool(Tool tool) {
    if (toolBrushAct_ != nullptr) {
        QSignalBlocker block(toolBrushAct_);
        toolBrushAct_->setChecked(tool == Tool::Brush);
    }
    if (view_ != nullptr) {
        view_->setBrushMode(tool == Tool::Brush);
    }
    if (tool != Tool::Brush) {
        cancelBrushStroke();
        hideBrushRing();
        return;
    }
    if (toolLabel_ != nullptr) {
        toolLabel_->setText(tr("Outil : %1").arg(tr("Pinceau")));
    }
    statusBar()->showMessage(
        selectedObject_ && !hasMultiSelection()
            ? tr("Pinceau : glissez pour pousser / tirer le contour (Maj : lisser, [ ] : rayon, "
                 "Échap : annuler le coup).")
            : tr("Pinceau : sélectionnez d'abord UNE forme vectorielle."));
}

void MainWindow::beginBrushStroke(QPointF sceneMm) {
    brushStroke_.reset();
    if (currentTool_ != Tool::Brush) {
        return; // le même geste sert aux zones de détail (main_window_detail_zones.cpp)
    }
    if (hasMultiSelection() || !selectedObject_) {
        statusBar()->showMessage(tr("Pinceau : sélectionnez d'abord UNE forme vectorielle."), 4000);
        return;
    }
    const auto* object = project_.findObject(*selectedObject_);
    if (object == nullptr) {
        return;
    }
    brushStroke_ = BrushStroke{*selectedObject_, object->paths, sceneMm};
    updateBrushRing(sceneMm);
}

void MainWindow::moveBrushStroke(QPointF sceneMm, Qt::KeyboardModifiers mods) {
    if (currentTool_ != Tool::Brush) {
        return;
    }
    updateBrushRing(sceneMm);
    if (!brushStroke_) {
        return;
    }
    const auto* object = project_.findObject(brushStroke_->object);
    if (object == nullptr) {
        cancelBrushStroke();
        return;
    }
    // Touches espacées d'au moins un huitième du rayon : borne le coût du geste.
    const QPointF step = sceneMm - brushStroke_->lastSceneMm;
    if (std::hypot(step.x(), step.y()) < brushRadiusMm_ / 8.0) {
        return;
    }
    geometry::BrushDab dab;
    dab.center = sceneToModel(sceneMm);
    dab.radius_um = brushRadiusMm_ * 1000.0;
    dab.strength = brushStrength_;
    dab.mode =
        (mods & Qt::ShiftModifier) != 0 ? geometry::BrushMode::Smooth : geometry::BrushMode::Push;
    dab.delta = sceneToModel(sceneMm) - sceneToModel(brushStroke_->lastSceneMm);
    for (auto& set : brushStroke_->working) {
        set = geometry::apply_brush_dab(set, dab);
    }
    brushStroke_->lastSceneMm = sceneMm;

    document::VectorObject shown = *object;
    shown.paths = brushStroke_->working;
    if (reshapePreview_ == nullptr) {
        auto* item = new QGraphicsPathItem();
        item->setBrush(Qt::NoBrush);
        item->setZValue(98);
        item->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(item);
        baseItems_.append(item);
        reshapePreview_ = item;
    }
    QPen pen(AppTheme::instance().tokens().accent, 2.0);
    pen.setCosmetic(true);
    reshapePreview_->setPen(pen);
    reshapePreview_->setPath(objectPainterPath(shown));
}

void MainWindow::cancelBrushStroke() {
    const bool wasActive = brushStroke_.has_value();
    brushStroke_.reset();
    hideGroupPreview();
    if (wasActive) {
        statusBar()->showMessage(tr("Coup de pinceau annulé."), 2000);
    }
}

void MainWindow::endBrushStroke(QPointF sceneMm) {
    Q_UNUSED(sceneMm);
    if (!brushStroke_) {
        return;
    }
    const BrushStroke stroke = std::move(*brushStroke_);
    brushStroke_.reset();
    hideGroupPreview();
    const std::uint64_t generation = documentGeneration_;
    // Différé : refreshImage() reconstruit la scène pendant l'évènement souris.
    QTimer::singleShot(0, this, [this, stroke, generation] {
        if (generation != documentGeneration_) {
            return;
        }
        auto result = commands::make_brush_stroke_command(project_, stroke.object, stroke.working);
        if (!result.command) {
            if (stroke.working != project_.findObject(stroke.object)->paths) {
                statusBar()->showMessage(QString::fromStdString(result.error), 4000);
            }
            refreshImage();
            return;
        }
        undoStack_.execute(std::move(result.command), project_);
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Contour retouché."), 2000);
    });
}

} // namespace openstitch::desktop
