// SPDX-License-Identifier: Apache-2.0
// Remodelage direct d'un segment du contour (HP-VEC-016) : glisser sur le CONTOUR d'une forme
// sélectionnée tire le segment (les nœuds d'extrémité ne bougent pas, seules les poignées de
// Bézier s'adaptent). Le calcul est dans libs/geometry (`reshape_path_set_segment`) ; ici
// seulement le geste, l'aperçu et la commande. Échap annule sans toucher au document.

#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QPen>
#include <QStatusBar>
#include <QTimer>

#include <cmath>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/geometry/reshape.hpp"

namespace openstitch::desktop {

namespace {

// Même convention que main_window.cpp (scène : mm, Y vers le bas ; modèle : µm, Y vers le haut).
Vec2um sceneToModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

// Distance d'accroche au contour, en pixels écran.
constexpr double kReshapeGrabPx = 6.0;
// Tolérance d'aplatissement pour la détection de croisements.
constexpr Micrometers kReshapeCrossingTolerance{50};

} // namespace

bool MainWindow::beginSegmentReshape(ObjectId objectId, QPointF sceneMm) {
    reshape_.reset();
    if (currentTool_ != Tool::Select || hasMultiSelection() || !selectedObject_ ||
        *selectedObject_ != objectId) {
        return false;
    }
    const auto* object = project_.findObject(objectId);
    if (object == nullptr) {
        return false;
    }
    const double maxUm = kReshapeGrabPx / std::max(view_->pixelsPerMm(), 0.01) * 1000.0;
    const Vec2um at = sceneToModel(sceneMm);
    std::optional<ReshapeGesture> best;
    double bestDist = maxUm;
    for (std::size_t s = 0; s < object->paths.size(); ++s) {
        const auto& set = object->paths[s];
        for (std::size_t p = 0; p <= set.holes.size(); ++p) {
            const geometry::Path& path = p == 0 ? set.outer : set.holes[p - 1];
            const auto hit = geometry::nearest_segment(path, at, bestDist);
            if (hit && hit->distance_um <= bestDist) {
                bestDist = hit->distance_um;
                best = ReshapeGesture{objectId, s, p, hit->segment, hit->t, sceneMm, set};
            }
        }
    }
    if (!best) {
        return false; // appui à l'intérieur : déplacement de la forme entière
    }
    reshape_ = std::move(best);
    return true;
}

QPointF MainWindow::reshapeDragPoint(QPointF sceneMm, Qt::KeyboardModifiers mods) const {
    // Maj : verrou d'axe, comme pour le glisser d'un nœud.
    if (reshape_ && (mods & Qt::ShiftModifier) != 0) {
        return axisLockedPos(reshape_->startSceneMm, sceneMm);
    }
    return sceneMm;
}

void MainWindow::updateSegmentReshape(QPointF sceneMm, Qt::KeyboardModifiers mods) {
    if (!reshape_) {
        return;
    }
    const auto* object = project_.findObject(reshape_->object);
    if (object == nullptr) {
        cancelSegmentReshape();
        return;
    }
    const QPointF dragged = reshapeDragPoint(sceneMm, mods);
    const Vec2um delta = sceneToModel(dragged) - sceneToModel(reshape_->startSceneMm);
    const auto reshaped =
        geometry::reshape_path_set_segment(reshape_->base, reshape_->path, reshape_->segment,
                                           reshape_->t, delta, kReshapeCrossingTolerance);

    document::VectorObject shown = *object;
    if (reshaped) {
        shown.paths[reshape_->set] = *reshaped;
    }
    if (reshapePreview_ == nullptr) {
        auto* item = new QGraphicsPathItem();
        item->setBrush(Qt::NoBrush);
        item->setZValue(98);
        item->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(item);
        baseItems_.append(item);
        reshapePreview_ = item;
    }
    // Refusé (le contour se croiserait) : l'aperçu garde la forme d'origine, en rouge.
    QPen pen(reshaped ? AppTheme::instance().tokens().accent : QColor(200, 40, 40), 2.0,
             reshaped ? Qt::SolidLine : Qt::DashLine);
    pen.setCosmetic(true);
    reshapePreview_->setPen(pen);
    reshapePreview_->setPath(objectPainterPath(shown));
    statusBar()->showMessage(reshaped ? tr("Remodelage du segment — Échap pour annuler")
                                      : tr("Remodelage impossible : le contour se croiserait"));
}

void MainWindow::cancelSegmentReshape() {
    if (reshapePreview_ != nullptr) {
        baseItems_.removeAll(reshapePreview_);
        delete reshapePreview_;
        reshapePreview_ = nullptr;
    }
    if (reshape_) {
        statusBar()->showMessage(tr("Remodelage annulé."), 2000);
    }
    reshape_.reset();
}

void MainWindow::commitSegmentReshape(QPointF sceneMm, Qt::KeyboardModifiers mods) {
    if (!reshape_) {
        return;
    }
    const ReshapeGesture gesture = *reshape_;
    const QPointF dragged = reshapeDragPoint(sceneMm, mods);
    const Vec2um delta = sceneToModel(dragged) - sceneToModel(gesture.startSceneMm);
    if (reshapePreview_ != nullptr) {
        baseItems_.removeAll(reshapePreview_);
        delete reshapePreview_;
        reshapePreview_ = nullptr;
    }
    reshape_.reset();
    const std::uint64_t generation = documentGeneration_;
    // Différé : refreshImage() détruirait l'item de corps pendant son propre évènement souris.
    QTimer::singleShot(0, this, [this, gesture, delta, generation] {
        if (generation != documentGeneration_) {
            return; // document remplacé entre-temps : aucune commande périmée
        }
        auto result = commands::make_reshape_segment_command(
            project_, gesture.object, gesture.set, gesture.path, gesture.segment, gesture.t, delta);
        if (!result.command) {
            statusBar()->showMessage(QString::fromStdString(result.error), 4000);
            refreshImage(); // efface tout reste d'aperçu
            return;
        }
        undoStack_.execute(std::move(result.command), project_);
        refreshImage();
        updateActions();
        statusBar()->showMessage(tr("Segment remodelé."), 2000);
    });
}

} // namespace openstitch::desktop
