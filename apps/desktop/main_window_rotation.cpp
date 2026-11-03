// SPDX-License-Identifier: Apache-2.0
// Poignée de rotation libre de la sélection (HP-OBJ-004) : une ou plusieurs formes, pivot
// déplaçable, aperçu pendant le glisser. Câblage seulement : l'angle est validé au relâchement
// par MainWindow::rotateObjects (une commande annulable, libs/commands/transform_ops).
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGraphicsPolygonItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QPen>
#include <QPolygonF>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/transform_ops.hpp"
#include "rotation_handle_items.hpp"

namespace openstitch::desktop {

namespace {

QPointF toSceneMm(Vec2um p) {
    return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
}

Vec2um toModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

} // namespace

Vec2um MainWindow::rotationPivotFor(const std::vector<ObjectId>& ids,
                                    const commands::BoundsUm& box) const {
    if (rotationPivot_ && rotationPivotTargets_ == ids) {
        return *rotationPivot_;
    }
    return box.center();
}

void MainWindow::setRotationPivot(const std::vector<ObjectId>& ids, Vec2um pivot) {
    rotationPivot_ = pivot;
    rotationPivotTargets_ = ids;
    refreshImage();
}

void MainWindow::editRotationPivot() {
    const std::vector<ObjectId> ids = transformTargets();
    const auto box = commands::objects_bounds(project_, ids);
    if (ids.empty() || !box) {
        return;
    }
    const Vec2um current = rotationPivotFor(ids, *box);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Centre de rotation"));
    auto* form = new QFormLayout(&dialog);
    auto makeSpin = [&dialog](Micrometers value) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setRange(-10000.0, 10000.0);
        spin->setDecimals(3);
        spin->setSuffix(QStringLiteral(" mm"));
        spin->setValue(to_millimeters(value).value);
        return spin;
    };
    auto* xSpin = makeSpin(current.x);
    auto* ySpin = makeSpin(current.y);
    form->addRow(tr("X :"), xSpin);
    form->addRow(tr("Y (vers le haut) :"), ySpin);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted) {
        setRotationPivot(ids, Vec2um{to_micrometers(Millimeters{xSpin->value()}),
                                     to_micrometers(Millimeters{ySpin->value()})});
    }
}

void MainWindow::showRotationPreview(const commands::BoundsUm& box, Vec2um pivot, double degrees) {
    const double rad = degrees * std::numbers::pi / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const QPointF pv(to_millimeters(pivot.x).value, to_millimeters(pivot.y).value);
    QPolygonF polygon;
    for (const Vec2um corner :
         {box.min, Vec2um{box.max.x, box.min.y}, box.max, Vec2um{box.min.x, box.max.y}}) {
        const double dx = to_millimeters(corner.x).value - pv.x();
        const double dy = to_millimeters(corner.y).value - pv.y();
        // Repère modèle (Y haut) : rotation anti-horaire ; la scène a Y vers le bas.
        polygon << QPointF(pv.x() + c * dx - s * dy, -(pv.y() + s * dx + c * dy));
    }
    if (rotationPreview_ == nullptr) {
        auto* item = new QGraphicsPolygonItem();
        QPen pen(AppTheme::instance().tokens().canvasSelectionLine, 1.5, Qt::DashLine);
        pen.setCosmetic(true);
        item->setPen(pen);
        item->setBrush(Qt::NoBrush);
        item->setZValue(99);
        item->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(item);
        baseItems_.append(item);
        rotationPreview_ = item;
    }
    rotationPreview_->setPolygon(polygon);
    statusBar()->showMessage(tr("Rotation : %1°").arg(degrees, 0, 'f', 1));
}

void MainWindow::hideRotationPreview() {
    if (rotationPreview_ != nullptr) {
        baseItems_.removeAll(rotationPreview_);
        delete rotationPreview_;
        rotationPreview_ = nullptr;
    }
}

void MainWindow::addRotationHandles() {
    if (currentTool_ != Tool::Select) {
        return;
    }
    const std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.empty()) {
        return;
    }
    const auto box = commands::objects_bounds(project_, ids);
    // Boîte dégénérée (segment, point) : pas de poignée exploitable.
    if (!box || box->min.x == box->max.x || box->min.y == box->max.y) {
        return;
    }
    // Boîte englobante commune d'une sélection multiple (HP-OBJ-001) : cadre en tirets.
    if (ids.size() > 1) {
        auto* frame = new SelectionFrameItem(QRectF(toSceneMm(Vec2um{box->min.x, box->max.y}),
                                                    toSceneMm(Vec2um{box->max.x, box->min.y})));
        QPen pen(AppTheme::instance().tokens().canvasSelectionLine, 1.0, Qt::DashLine);
        pen.setCosmetic(true);
        frame->setPen(pen);
        frame->setBrush(Qt::NoBrush);
        frame->setZValue(98);
        frame->setAcceptedMouseButtons(Qt::NoButton);
        scene_->addItem(frame);
        baseItems_.append(frame);
        commonBox_ = frame;

        // Quatre coins de la boîte commune : toutes les formes sélectionnées restent
        // à leur place relative. Une transformation = un seul pas d'annulation.
        const Vec2um corners[4] = {box->min, Vec2um{box->max.x, box->min.y}, box->max,
                                   Vec2um{box->min.x, box->max.y}};
        const auto generation = documentGeneration_;
        for (std::size_t i = 0; i < 4; ++i) {
            const Vec2um corner = corners[i];
            const Vec2um anchor = corners[(i + 2) % 4];
            const QPointF anchorScene = toSceneMm(anchor);
            auto* handle = new ResizeHandleItem(
                toSceneMm(corner),
                [this, ids, corner, anchor, generation](QPointF releasedSceneMm) {
                    hideResizePreview();
                    const Vec2um moved = toModel(releasedSceneMm);
                    if (moved == corner) {
                        return;
                    }
                    const double dx = double((moved.x - anchor.x).value);
                    const double dy = double((moved.y - anchor.y).value);
                    const double originalDx = double((corner.x - anchor.x).value);
                    const double originalDy = double((corner.y - anchor.y).value);
                    const double sx = std::max(0.05, dx / originalDx);
                    const double sy = std::max(0.05, dy / originalDy);
                    // Un refresh immédiat détruirait la poignée pendant son événement souris.
                    QTimer::singleShot(0, this, [this, ids, anchor, sx, sy, generation] {
                        if (generation != documentGeneration_ || selectedObjectIds() != ids) {
                            return;
                        }
                        if (sx == 1.0 && sy == 1.0) {
                            refreshImage();
                            return;
                        }
                        runTransform(ids, commands::AffineTransform::scale_xy(anchor, sx, sy),
                                     tr("Redimensionner la sélection"),
                                     tr("%1 formes redimensionnées ensemble.").arg(ids.size()));
                    });
                },
                anchorScene, /*hasAnchor=*/true,
                i % 2 == 0 ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor,
                [this, anchorScene](QPointF movedSceneMm) {
                    showResizePreview(anchorScene, movedSceneMm);
                });
            handle->setCancelled([this] { hideResizePreview(); });
            handle->setToolTip(
                tr("Redimensionner les %1 formes ensemble — Maj : conserver les proportions, "
                   "Échap : annuler")
                    .arg(ids.size()));
            scene_->addItem(handle);
            baseItems_.append(handle);
        }
    }
    const Vec2um pivot = rotationPivotFor(ids, *box);
    const QPointF pivotScene = toSceneMm(pivot);
    const auto generation = documentGeneration_;
    const commands::BoundsUm boxCopy = *box;

    // Poignée de rotation : au-dessus du milieu du bord haut, à distance fixe à l'écran.
    const double offsetMm = 26.0 / std::max(view_->pixelsPerMm(), 0.01);
    const Vec2um center = box->center();
    const QPointF handleScene(toSceneMm(center).x(),
                              toSceneMm(Vec2um{center.x, box->max.y}).y() - offsetMm);
    auto angleOf = [pivotScene, handleScene](QPointF scenePos) {
        return rotationDragDegrees(pivotScene, handleScene, scenePos,
                                   (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0);
    };
    auto* rotation = new ResizeHandleItem(
        handleScene,
        [this, ids, pivot, angleOf, generation](QPointF releasedSceneMm) {
            hideRotationPreview();
            const double degrees = angleOf(releasedSceneMm);
            // Différé : refreshImage() détruirait cet item pendant son propre évènement.
            QTimer::singleShot(0, this, [this, ids, pivot, degrees, generation] {
                if (generation != documentGeneration_) {
                    return;
                }
                if (degrees == 0.0) {
                    refreshImage(); // remet la poignée à sa place
                    return;
                }
                rotateObjects(ids, degrees, pivot);
            });
        },
        QPointF(), /*hasAnchor=*/false, Qt::CrossCursor,
        [this, angleOf, boxCopy, pivot](QPointF moved) {
            showRotationPreview(boxCopy, pivot, angleOf(moved));
        });
    rotation->setBrush(QBrush(AppTheme::instance().tokens().canvasSelectionLine));
    rotation->setToolTip(tr("Glisser pour faire pivoter la sélection autour du repère rond "
                            "(par défaut son centre) — Maj : pas de 15°, Échap : annuler"));
    rotation->setCancelled([this] { hideRotationPreview(); });
    scene_->addItem(rotation);
    baseItems_.append(rotation);

    // Pivot déplaçable : le glisser fixe un nouveau centre de rotation (mémorisé pour cette
    // sélection) ; le double-clic le ramène au centre.
    const bool pivotDisplaced = rotationPivot_.has_value() && rotationPivotTargets_ == ids;
    auto* pivotItem = new PivotHandleItem(
        pivotScene, pivotDisplaced,
        [this, ids, generation](QPointF releasedSceneMm) {
            QTimer::singleShot(0, this, [this, ids, releasedSceneMm, generation] {
                if (generation != documentGeneration_) {
                    return;
                }
                rotationPivot_ = toModel(releasedSceneMm);
                rotationPivotTargets_ = ids;
                refreshImage();
            });
        },
        [this, generation] {
            QTimer::singleShot(0, this, [this, generation] {
                if (generation != documentGeneration_) {
                    return;
                }
                rotationPivot_.reset();
                rotationPivotTargets_.clear();
                refreshImage();
            });
        });
    pivotItem->setToolTip(tr("Centre de rotation : Ctrl+Maj+glisser pour le déplacer, "
                             "double-clic pour le ramener au centre de la sélection"));
    scene_->addItem(pivotItem);
    baseItems_.append(pivotItem);
}

} // namespace openstitch::desktop
