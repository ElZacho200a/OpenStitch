// SPDX-License-Identifier: Apache-2.0
// Sélection multiple de nœuds et déplacement groupé (HP-VEC-017). Alt + cadre sur le canevas
// sélectionne les nœuds de la forme courante (Maj : ajouter, Ctrl : basculer) ; un clic sur une
// poignée la sélectionne (Maj / Ctrl : ajouter / basculer) ; glisser une poignée sélectionnée
// déplace tout le groupe en un seul pas d'annulation. Le calcul est dans libs/geometry
// (`transform_path_set_nodes`) ; ici seulement l'état de sélection, l'aperçu et la commande.
// La sélection est limitée à UN chemin (extérieur ou un trou) d'un morceau.

#include <QAction>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QMenu>
#include <QPen>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/geometry/node_transform.hpp"

namespace openstitch::desktop {

namespace {

Vec2um sceneToModel(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

bool sameNode(const document::NodeRef& a, const document::NodeRef& b) {
    return a.set == b.set && a.path == b.path && a.node == b.node;
}

// Rayon de la transition douce (nœuds voisins non sélectionnés), en µm.
constexpr double kSoftRadiusUm = 8'000.0;
constexpr Micrometers kCrossingTolerance{50};

} // namespace

void MainWindow::buildNodeSelectionActions() {
    if (shapeMenu_ == nullptr) {
        return;
    }
    softNodesAct_ = shapeMenu_->addAction(tr("Transition &douce des nœuds voisins"));
    softNodesAct_->setObjectName(QStringLiteral("action_softNodes"));
    softNodesAct_->setCheckable(true);
    softNodesAct_->setToolTip(
        tr("Quand on déplace un groupe de nœuds, les nœuds voisins non sélectionnés (à moins de "
           "8 mm) suivent avec une influence décroissante."));
}

bool MainWindow::isNodeSelected(const document::NodeRef& ref) const {
    return std::any_of(selectedNodes_.begin(), selectedNodes_.end(),
                       [&](const document::NodeRef& n) { return sameNode(n, ref); });
}

void MainWindow::restyleNodeHandles() {
    const auto& tokens = AppTheme::instance().tokens();
    for (const auto& [ref, item] : nodeHandles_) {
        item->setBrush(QBrush(isNodeSelected(ref) ? tokens.accent : tokens.canvasSelectionHalo));
    }
}

void MainWindow::clearNodeSelection() {
    selectedNodes_.clear();
    nodeSelObject_.reset();
}

void MainWindow::selectNodes(ObjectId object, const std::vector<document::NodeRef>& nodes,
                             SelectMode mode) {
    if (!nodeSelObject_ || *nodeSelObject_ != object) {
        selectedNodes_.clear();
    }
    nodeSelObject_ = object;
    // Une sélection de nœuds vit dans un seul chemin : un autre chemin la remplace.
    if (!nodes.empty() && !selectedNodes_.empty() &&
        (selectedNodes_.front().set != nodes.front().set ||
         selectedNodes_.front().path != nodes.front().path)) {
        selectedNodes_.clear();
    }
    if (mode == SelectMode::Replace) {
        selectedNodes_.clear();
    }
    for (const document::NodeRef& n : nodes) {
        const auto it = std::find_if(selectedNodes_.begin(), selectedNodes_.end(),
                                     [&](const document::NodeRef& s) { return sameNode(s, n); });
        if (it == selectedNodes_.end()) {
            selectedNodes_.push_back(n);
        } else if (mode == SelectMode::Toggle) {
            selectedNodes_.erase(it);
        }
    }
    if (selectedNodes_.empty()) {
        nodeSelObject_.reset();
    }
}

void MainWindow::onNodeRectangle(QRectF rectMm, SelectMode mode) {
    if (currentTool_ != Tool::Select || hasMultiSelection() || !selectedObject_) {
        return;
    }
    const auto* object = project_.findObject(*selectedObject_);
    if (object == nullptr) {
        return;
    }
    const Vec2um a = sceneToModel(rectMm.topLeft());
    const Vec2um b = sceneToModel(rectMm.bottomRight());
    const Vec2um lo{std::min(a.x, b.x), std::min(a.y, b.y)};
    const Vec2um hi{std::max(a.x, b.x), std::max(a.y, b.y)};
    // Le chemin qui a le plus de nœuds dans le cadre l'emporte (selon la limite d'un chemin).
    std::vector<document::NodeRef> best;
    for (std::size_t s = 0; s < object->paths.size(); ++s) {
        const auto& set = object->paths[s];
        for (std::size_t p = 0; p <= set.holes.size(); ++p) {
            const geometry::Path& path = p == 0 ? set.outer : set.holes[p - 1];
            const auto hits = geometry::nodes_in_rect(path, lo, hi);
            if (hits.size() > best.size()) {
                best.clear();
                for (const std::size_t i : hits) {
                    best.push_back({s, p, i});
                }
            }
        }
    }
    selectNodes(*selectedObject_, best, mode);
    refreshImage();
    statusBar()->showMessage(
        tr("%n nœud(s) sélectionné(s)", "", static_cast<int>(selectedNodes_.size())), 3000);
}

void MainWindow::previewGroupMove(ObjectId objectId, Vec2um delta) {
    const auto* object = project_.findObject(objectId);
    if (object == nullptr || selectedNodes_.empty()) {
        return;
    }
    const document::NodeRef first = selectedNodes_.front();
    if (first.set >= object->paths.size()) {
        return;
    }
    std::vector<std::size_t> indices;
    for (const document::NodeRef& n : selectedNodes_) {
        indices.push_back(n.node);
    }
    geometry::NodeTransform t;
    t.translation = delta;
    const double soft =
        softNodesAct_ != nullptr && softNodesAct_->isChecked() ? kSoftRadiusUm : 0.0;
    const auto moved = geometry::transform_path_set_nodes(object->paths[first.set], first.path,
                                                          indices, t, soft, kCrossingTolerance);
    document::VectorObject shown = *object;
    if (moved) {
        shown.paths[first.set] = *moved;
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
    QPen pen(moved ? AppTheme::instance().tokens().accent : QColor(200, 40, 40), 2.0,
             moved ? Qt::SolidLine : Qt::DashLine);
    pen.setCosmetic(true);
    reshapePreview_->setPen(pen);
    reshapePreview_->setPath(objectPainterPath(shown));
    statusBar()->showMessage(moved ? tr("Déplacement groupé de %n nœud(s) — Échap pour annuler", "",
                                        static_cast<int>(selectedNodes_.size()))
                                   : tr("Déplacement impossible : le contour se croiserait"));
}

void MainWindow::hideGroupPreview() {
    if (reshapePreview_ != nullptr) {
        baseItems_.removeAll(reshapePreview_);
        delete reshapePreview_;
        reshapePreview_ = nullptr;
    }
}

void MainWindow::commitGroupMove(ObjectId objectId, Vec2um delta) {
    hideGroupPreview();
    if (selectedNodes_.empty() || delta == Vec2um{}) {
        return;
    }
    const document::NodeRef first = selectedNodes_.front();
    std::vector<std::size_t> indices;
    for (const document::NodeRef& n : selectedNodes_) {
        indices.push_back(n.node);
    }
    const bool soft = softNodesAct_ != nullptr && softNodesAct_->isChecked();
    const std::uint64_t generation = documentGeneration_;
    // Différé : refreshImage() détruirait la poignée pendant son propre évènement souris.
    QTimer::singleShot(0, this, [this, objectId, first, indices, delta, soft, generation] {
        if (generation != documentGeneration_) {
            return;
        }
        geometry::NodeTransform t;
        t.translation = delta;
        auto result = commands::make_transform_nodes_command(
            project_, objectId, first.set, first.path, indices, t, soft ? kSoftRadiusUm : 0.0);
        if (!result.command) {
            statusBar()->showMessage(QString::fromStdString(result.error), 4000);
            refreshImage();
            return;
        }
        undoStack_.execute(std::move(result.command), project_);
        refreshImage();
        updateActions();
    });
}

} // namespace openstitch::desktop
