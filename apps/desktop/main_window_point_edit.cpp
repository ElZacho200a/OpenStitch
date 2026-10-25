// SPDX-License-Identifier: Apache-2.0
// Édition des points générés d'un objet : sélection, déplacement groupé, suppression, insertion,
// type (point cousu / saut) et coupe de fil (HP-EDIT-001/002/003). S'appuie sur le mode
// « Éditer les points » existant (poignées bâties par renderBase) et sur les commandes
// `EditStitchPointsCommand` / `MoveStitchPointCommand` : aucune logique métier ici.
#include <QAction>
#include <QMenu>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include "app_theme.hpp"
#include "canvas_view.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/stitch_point_edit_commands.hpp"

namespace openstitch::desktop {

namespace {

// Codes des actions de retouche appliquées à la sélection.
enum PointAction : int {
    kDelete = 0,
    kRestore = 1,
    kJump = 2,
    kStitch = 3,
    kTrim = 4,
    kNoTrim = 5,
    kActionCount = 6
};

constexpr double kInsertGrabPx = 14.0; // distance maximale au trait pour insérer un point

QPointF sceneOfModel(Vec2um p) {
    return QPointF(to_millimeters(p.x).value, -to_millimeters(p.y).value);
}

Vec2um modelOfScene(QPointF s) {
    return Vec2um{to_micrometers(Millimeters{s.x()}), to_micrometers(Millimeters{-s.y()})};
}

// Aspect d'une poignée de point : sélectionnée (pleine, trait épais), supprimée (pâle, pointillé).
void styleStitchHandle(NodeHandleItem& handle, bool selected, bool deleted) {
    const auto& tokens = AppTheme::instance().tokens();
    handle.setPen(QPen(selected ? tokens.canvasSelectionLine : tokens.accent, selected ? 2.5 : 1.5,
                       deleted ? Qt::DashLine : Qt::SolidLine));
    handle.setBrush(QBrush(selected ? tokens.accent : tokens.accent.lighter(150)));
    handle.setOpacity(deleted ? 0.35 : 1.0);
}

} // namespace

bool MainWindow::stitchPointEditActive() const {
    return stitchEditModeAct_ != nullptr && stitchEditModeAct_->isChecked() &&
           stitchEditTarget_.has_value() && stitchEditView_.has_value();
}

Vec2um MainWindow::stitchPointDisplayPos(std::size_t index) {
    if (const auto* obj = project_.findEmbroidery(*stitchEditTarget_)) {
        for (const auto& ov : obj->overrides) {
            if (ov.base_index == index && ov.moved_to) {
                return *ov.moved_to;
            }
        }
    }
    return stitchEditView_->raw[index].pos;
}

std::unordered_set<std::size_t> MainWindow::deletedStitchPoints() {
    std::unordered_set<std::size_t> out;
    if (const auto* obj = project_.findEmbroidery(*stitchEditTarget_)) {
        for (const auto& ov : obj->overrides) {
            if (ov.deleted) {
                out.insert(ov.base_index);
            }
        }
    }
    return out;
}

void MainWindow::addStitchPointHandle(std::size_t baseIndex, Vec2um pos, bool selected,
                                      bool deleted) {
    const ObjectId targetId = *stitchEditTarget_;
    const std::uint64_t fingerprint = stitchEditView_->fingerprint;
    const std::uint32_t pointCount = stitchEditView_->point_count;
    const std::uint64_t generation = documentGeneration_;

    auto* handle = new NodeHandleItem(
        sceneOfModel(pos), [](QPointF) {},
        [this](QPointF) {
            statusBar()->showMessage(
                tr("Glisser pour déplacer le point (toute la sélection suit)."));
        },
        [this, baseIndex](QPoint screenPos) { showStitchPointMenu(baseIndex, screenPos); });
    handle->setReleasedWithModifiers([this, targetId, baseIndex, pos, fingerprint, pointCount,
                                      generation,
                                      deleted](QPointF newSceneMm, Qt::KeyboardModifiers mods) {
        const Vec2um newPos = modelOfScene(newSceneMm);
        // Diffère : refreshImage() détruirait cette poignée pendant son propre évènement.
        if (newPos == pos || deleted) { // simple clic : sélection
            QTimer::singleShot(0, this, [this, baseIndex, mods, generation] {
                if (generation != documentGeneration_) {
                    return;
                }
                clickStitchPoint(baseIndex, mods);
            });
            return;
        }
        QTimer::singleShot(
            0, this, [this, targetId, baseIndex, pos, newPos, fingerprint, pointCount, generation] {
                if (generation != documentGeneration_) {
                    return; // document remplacé pendant le délai
                }
                moveStitchPoints(targetId, baseIndex, pos, newPos, fingerprint, pointCount);
            });
    });
    styleStitchHandle(*handle, selected, deleted);
    handle->setData(1,
                    static_cast<qulonglong>(baseIndex)); // retrouvé par restyleStitchPointHandles
    if (deleted) {
        handle->setToolTip(tr("Point #%1 supprimé — clic pour le sélectionner, clic droit pour "
                              "le restaurer")
                               .arg(baseIndex));
    } else {
        handle->setToolTip(tr("Point de couture #%1 — clic : sélectionner (Ctrl/Maj : ajouter), "
                              "glisser : déplacer, clic droit : actions")
                               .arg(baseIndex));
    }
    scene_->addItem(handle);
    baseItems_.append(handle);
}

void MainWindow::clickStitchPoint(std::size_t index, Qt::KeyboardModifiers mods) {
    if (!stitchPointEditActive()) {
        return;
    }
    if ((mods & (Qt::ControlModifier | Qt::ShiftModifier)) != 0) {
        if (stitchSelection_.erase(index) == 0) {
            stitchSelection_.insert(index);
        }
    } else {
        stitchSelection_ = {index};
    }
    restyleStitchPointHandles(); // en place : un clic ne détruit pas la poignée
    updateActions();
}

void MainWindow::restyleStitchPointHandles() {
    if (!stitchPointEditActive()) {
        return;
    }
    const auto deleted = deletedStitchPoints();
    for (QGraphicsItem* item : baseItems_) {
        auto* handle = dynamic_cast<NodeHandleItem*>(item);
        if (handle == nullptr || !handle->data(1).isValid()) {
            continue;
        }
        const auto index = static_cast<std::size_t>(handle->data(1).toULongLong());
        styleStitchHandle(*handle, stitchSelection_.count(index) != 0, deleted.count(index) != 0);
    }
}

void MainWindow::selectAllStitchPoints() {
    if (!stitchPointEditActive()) {
        return;
    }
    const auto deleted = deletedStitchPoints();
    stitchSelection_.clear();
    const auto& raw = stitchEditView_->raw;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (stitch_generation::is_movable_point(raw[i]) && deleted.count(i) == 0) {
            stitchSelection_.insert(i);
        }
    }
    statusBar()->showMessage(tr("%1 point(s) sélectionné(s).").arg(stitchSelection_.size()));
    restyleStitchPointHandles();
    updateActions();
}

void MainWindow::moveStitchPoints(ObjectId targetId, std::size_t anchor, Vec2um from, Vec2um to,
                                  std::uint64_t fingerprint, std::uint32_t pointCount) {
    if (!stitchPointEditActive() || *stitchEditTarget_ != targetId) {
        return;
    }
    if (stitchSelection_.count(anchor) != 0 && stitchSelection_.size() > 1) {
        // Déplacement groupé : chaque point sélectionné (non supprimé) suit le même décalage.
        const auto deleted = deletedStitchPoints();
        const std::int32_t dx = to.x.value - from.x.value;
        const std::int32_t dy = to.y.value - from.y.value;
        std::vector<commands::EditStitchPointsCommand::Item> items;
        for (const std::size_t i : stitchSelection_) {
            if (deleted.count(i) != 0) {
                continue;
            }
            const Vec2um at = stitchPointDisplayPos(i);
            items.push_back(
                {i, Vec2um{Micrometers{at.x.value + dx}, Micrometers{at.y.value + dy}}});
        }
        undoStack_.execute(std::make_unique<commands::EditStitchPointsCommand>(
                               commands::EditStitchPointsCommand::move_points(
                                   targetId, std::move(items), fingerprint, pointCount)),
                           project_);
    } else {
        undoStack_.execute(std::make_unique<commands::MoveStitchPointCommand>(
                               targetId, anchor, to, fingerprint, pointCount),
                           project_);
    }
    refreshImage();
    updateActions();
}

bool MainWindow::editSelectedStitchPoints(int action) {
    if (!stitchPointEditActive() || stitchSelection_.empty()) {
        return false;
    }
    const ObjectId id = *stitchEditTarget_;
    const std::uint64_t fp = stitchEditView_->fingerprint;
    const std::uint32_t count = stitchEditView_->point_count;
    const std::vector<std::size_t> indices(stitchSelection_.begin(), stitchSelection_.end());
    using Edit = commands::EditStitchPointsCommand;
    std::unique_ptr<commands::ICommand> command;
    switch (action) {
    case kDelete:
        command = std::make_unique<Edit>(Edit::delete_points(id, indices, fp, count));
        break;
    case kRestore:
        command = std::make_unique<Edit>(Edit::restore_points(id, indices, fp, count));
        break;
    case kJump:
        command = std::make_unique<Edit>(
            Edit::set_type(id, indices, document::StitchPointType::Jump, fp, count));
        break;
    case kStitch:
        command = std::make_unique<Edit>(
            Edit::set_type(id, indices, document::StitchPointType::Stitch, fp, count));
        break;
    case kTrim:
        command = std::make_unique<Edit>(Edit::set_trim(id, indices, true, fp, count));
        break;
    case kNoTrim:
        command = std::make_unique<Edit>(Edit::set_trim(id, indices, false, fp, count));
        break;
    default:
        return false;
    }
    undoStack_.execute(std::move(command), project_);
    refreshImage();
    updateActions();
    return true;
}

bool MainWindow::insertStitchPointAt(QPointF sceneMm) {
    if (!stitchPointEditActive()) {
        return false;
    }
    const auto& raw = stitchEditView_->raw;
    const double pxPerMm = view_ != nullptr ? std::max(view_->pixelsPerMm(), 0.01) : 1.0;
    const double maxMm = kInsertGrabPx / pxPerMm;
    const auto deleted = deletedStitchPoints();

    double bestDist = std::numeric_limits<double>::max();
    std::optional<std::size_t> bestIndex;
    std::optional<std::size_t> prev;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (!stitch_generation::is_movable_point(raw[i]) || deleted.count(i) != 0) {
            continue;
        }
        if (prev) {
            // Segment cousu seulement : rien d'autre qu'un point cousu entre les deux.
            bool continuous = true;
            for (std::size_t k = *prev + 1; k < i; ++k) {
                if (raw[k].type != stitch::CommandType::Stitch) {
                    continuous = false;
                    break;
                }
            }
            if (continuous) {
                const QPointF a = sceneOfModel(stitchPointDisplayPos(*prev));
                const QPointF b = sceneOfModel(stitchPointDisplayPos(i));
                const QPointF ab = b - a;
                const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
                const double t = len2 <= 0.0 ? 0.0
                                             : std::clamp(((sceneMm.x() - a.x()) * ab.x() +
                                                           (sceneMm.y() - a.y()) * ab.y()) /
                                                              len2,
                                                          0.0, 1.0);
                const QPointF nearest(a.x() + t * ab.x(), a.y() + t * ab.y());
                const double d = std::hypot(sceneMm.x() - nearest.x(), sceneMm.y() - nearest.y());
                if (d < bestDist) {
                    bestDist = d;
                    bestIndex = *prev;
                }
            }
        }
        prev = i;
    }
    if (!bestIndex || bestDist > maxMm) {
        statusBar()->showMessage(
            tr("Double-cliquez près d'un trait de couture pour y insérer un point."));
        return false;
    }
    undoStack_.execute(std::make_unique<commands::EditStitchPointsCommand>(
                           commands::EditStitchPointsCommand::insert_point(
                               *stitchEditTarget_, *bestIndex, modelOfScene(sceneMm),
                               stitchEditView_->fingerprint, stitchEditView_->point_count)),
                       project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Point inséré (annulable)."));
    return true;
}

void MainWindow::showStitchPointMenu(std::size_t index, QPoint screenPos) {
    if (!stitchPointEditActive()) {
        return;
    }
    if (stitchSelection_.count(index) == 0) {
        stitchSelection_ = {index}; // le clic droit sélectionne le point visé
    }
    QMenu menu(this);
    QAction* actions[kActionCount] = {menu.addAction(tr("Supprimer les points sélectionnés")),
                                      menu.addAction(tr("Restaurer les points sélectionnés")),
                                      menu.addAction(tr("Passer en saut (sans couture)")),
                                      menu.addAction(tr("Passer en point cousu")),
                                      menu.addAction(tr("Couper le fil après")),
                                      menu.addAction(tr("Ne pas couper le fil après"))};
    menu.insertSeparator(actions[kJump]);
    menu.insertSeparator(actions[kTrim]);
    QAction* chosen = menu.exec(screenPos);
    int code = -1;
    for (int a = 0; a < kActionCount; ++a) {
        if (chosen == actions[a]) {
            code = a;
        }
    }
    const std::uint64_t generation = documentGeneration_;
    // Différé : le menu est ouvert depuis l'évènement de la poignée, que refreshImage() détruit.
    QTimer::singleShot(0, this, [this, code, generation] {
        if (generation != documentGeneration_) {
            return;
        }
        if (code >= 0) {
            editSelectedStitchPoints(code);
        } else {
            restyleStitchPointHandles(); // montre la sélection posée par le clic droit
            updateActions();
        }
    });
}

void MainWindow::buildStitchPointEditActions(QMenu* menu) {
    struct Def {
        const char* name;
        const char* text;
        int code;
    };
    const Def defs[] = {
        {"action_stitchPointDelete", QT_TR_NOOP("Supprimer les points sélectionnés"), kDelete},
        {"action_stitchPointRestore", QT_TR_NOOP("Restaurer les points sélectionnés"), kRestore},
        {"action_stitchPointJump", QT_TR_NOOP("Passer en saut (sans couture)"), kJump},
        {"action_stitchPointStitch", QT_TR_NOOP("Passer en point cousu"), kStitch},
        {"action_stitchPointTrim", QT_TR_NOOP("Couper le fil après"), kTrim},
        {"action_stitchPointNoTrim", QT_TR_NOOP("Ne pas couper le fil après"), kNoTrim}};
    selectAllStitchPointsAct_ = menu->addAction(tr("Sélectionner tous les points"));
    selectAllStitchPointsAct_->setObjectName(QStringLiteral("action_stitchPointSelectAll"));
    selectAllStitchPointsAct_->setToolTip(
        tr("En mode « Éditer les points » : sélectionne tous les points de l'objet (Ctrl+A)."));
    connect(selectAllStitchPointsAct_, &QAction::triggered, this,
            &MainWindow::selectAllStitchPoints);
    for (const Def& def : defs) {
        QAction* act = menu->addAction(tr(def.text));
        act->setObjectName(QString::fromLatin1(def.name));
        act->setToolTip(tr("En mode « Éditer les points » : s'applique aux points sélectionnés "
                           "(un seul pas d'annulation)."));
        const int code = def.code;
        connect(act, &QAction::triggered, this, [this, code] { editSelectedStitchPoints(code); });
        stitchPointActs_[static_cast<std::size_t>(code)] = act;
    }
    updateStitchPointActions();
}

void MainWindow::updateStitchPointActions() {
    const bool active = stitchPointEditActive();
    if (selectAllStitchPointsAct_ != nullptr) {
        selectAllStitchPointsAct_->setEnabled(active);
    }
    for (QAction* act : stitchPointActs_) {
        if (act != nullptr) {
            act->setEnabled(active && !stitchSelection_.empty());
        }
    }
}

} // namespace openstitch::desktop
