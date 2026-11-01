// SPDX-License-Identifier: Apache-2.0
// Repère du centre des remplissages radial et en spirale (HP-STI-008 / 009) : visible sur le
// canevas dès que l'objet est sélectionné, se glisse pour déplacer le centre ; clic droit pour
// revenir au centre automatique. Câblage seulement : le centre est validé au relâchement par une
// commande annulable (SetStitchParamsCommand), le calcul des points reste dans le cœur.
#include <QGraphicsScene>
#include <QMenu>
#include <QPainterPath>
#include <QPen>
#include <QStatusBar>
#include <QTimer>

#include <cmath>
#include <optional>
#include <variant>

#include "app_theme.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/project_commands.hpp"

namespace openstitch::desktop {

namespace {

QPointF toSceneMm(Vec2um p) {
    return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
}

Vec2um toModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

std::optional<Vec2um> centerOf(const document::EmbroideryObject& object) {
    if (const auto* radial = std::get_if<document::RadialFillParams>(&object.params)) {
        return radial->center;
    }
    if (const auto* spiral = std::get_if<document::SpiralFillParams>(&object.params)) {
        return spiral->center;
    }
    return std::nullopt;
}

// Remplace le centre d'un radial / d'une spirale ; faux si le type ne convient pas ou si rien
// ne change.
bool withCenter(document::StitchParams& params, const std::optional<Vec2um>& center) {
    if (auto* radial = std::get_if<document::RadialFillParams>(&params)) {
        const bool changed = radial->center != center;
        radial->center = center;
        return changed;
    }
    if (auto* spiral = std::get_if<document::SpiralFillParams>(&params)) {
        const bool changed = spiral->center != center;
        spiral->center = center;
        return changed;
    }
    return false;
}

} // namespace

void MainWindow::setFillCenter(ObjectId id, std::optional<Vec2um> center) {
    const auto* object = project_.findEmbroidery(id);
    if (object == nullptr) {
        return;
    }
    document::StitchParams next = object->params;
    if (!withCenter(next, center)) {
        return; // même centre (ou autre type) : aucun historique fantôme
    }
    undoStack_.execute(std::make_unique<commands::SetStitchParamsCommand>(
                           id, std::move(next), tr("Centre").toStdString()),
                       project_);
    refreshImage();
    updateActions();
}

void MainWindow::addFillCenterHandle() {
    if (!selectedEmbroidery_) {
        return;
    }
    const auto* object = project_.findEmbroidery(*selectedEmbroidery_);
    if (object == nullptr || !(object->is_radial_fill() || object->is_spiral_fill())) {
        return;
    }
    const auto* source = project_.findObject(object->source_vector);
    if (source == nullptr) {
        return;
    }
    const auto& tokens = AppTheme::instance().tokens();
    const std::optional<Vec2um> fixed = centerOf(*object);
    // Centre automatique : milieu de la boîte englobante de la forme (comme le générateur).
    const QPointF where =
        fixed ? toSceneMm(*fixed) : objectPainterPath(*source).boundingRect().center();

    // Viseur : deux traits en croix de 3 mm de demi-longueur, en tirets tant que le centre est
    // automatique (la position n'est alors pas mémorisée).
    QPen pen(tokens.canvasHandle);
    pen.setCosmetic(true);
    pen.setWidthF(1.6);
    if (!fixed) {
        pen.setStyle(Qt::DashLine);
    }
    constexpr double kArm = 3.0; // mm
    QPainterPath cross;
    cross.moveTo(where + QPointF(-kArm, 0.0));
    cross.lineTo(where + QPointF(kArm, 0.0));
    cross.moveTo(where + QPointF(0.0, -kArm));
    cross.lineTo(where + QPointF(0.0, kArm));
    auto* viewfinder = scene_->addPath(cross, pen, Qt::NoBrush);
    viewfinder->setZValue(98);
    baseItems_.append(viewfinder);

    const ObjectId id = object->id;
    const auto generation = documentGeneration_;
    auto* handle = new NodeHandleItem(
        where,
        [this, id, generation, where, fixed](QPointF released) {
            if (!fixed && QLineF(released, where).length() < 0.01) {
                return; // simple clic sur le centre automatique : on ne le fige pas
            }
            // Différé : refreshImage() reconstruit la scène et détruirait cette poignée pendant
            // son propre événement souris.
            QTimer::singleShot(0, this, [this, id, generation, released] {
                if (generation == documentGeneration_) {
                    setFillCenter(id, toModel(released));
                }
            });
        },
        [this](QPointF moved) { // aperçu : coordonnées affichées pendant le glisser
            const Vec2um um = toModel(moved);
            statusBar()->showMessage(tr("Centre : X %1 mm, Y %2 mm")
                                         .arg(to_millimeters(um.x).value, 0, 'f', 1)
                                         .arg(to_millimeters(um.y).value, 0, 'f', 1));
        },
        [this, id, generation, fixed](QPoint globalPos) {
            QTimer::singleShot(0, this, [this, id, generation, fixed, globalPos] {
                if (generation != documentGeneration_) {
                    return;
                }
                QMenu menu(this);
                QAction* autoCenter = menu.addAction(tr("Centre automatique"));
                autoCenter->setEnabled(fixed.has_value());
                if (menu.exec(globalPos) == autoCenter) {
                    setFillCenter(id, std::nullopt);
                }
            });
        });
    handle->setData(0, QStringLiteral("fillCenterHandle"));
    handle->setPen(QPen(tokens.canvasHandle, 1.5));
    handle->setBrush(fixed ? QBrush(tokens.canvasHandle.lighter(160)) : QBrush(Qt::NoBrush));
    handle->setCursor(Qt::CrossCursor);
    handle->setToolTip(fixed ? tr("Centre du remplissage — glisser pour le déplacer, "
                                  "clic droit : centre automatique")
                             : tr("Centre automatique (milieu de la forme) — glisser pour le "
                                  "fixer ailleurs"));
    scene_->addItem(handle);
    baseItems_.append(handle);
}

} // namespace openstitch::desktop
