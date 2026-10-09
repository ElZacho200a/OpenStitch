// SPDX-License-Identifier: Apache-2.0
// Édition multi-objets et boîte de forme : inspecteur « Appliquer à N objets »,
// champs X/Y/L/H d'un objet vectoriel, alignement, panneau Historique. Membres de
// MainWindow séparés de main_window.cpp pour la lisibilité. Aucune logique métier
// ici : toute mutation du document passe par une commande annulable (un seul pas
// d'annulation par geste).
#include <QAction>
#include <QDockWidget>
#include <QFont>
#include <QListWidget>
#include <QMenu>
#include <QSignalBlocker>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "properties_panel.hpp"

namespace openstitch::desktop {

namespace {

// Boîte englobante des nœuds d'un objet, en µm (repère du document, Y vers le haut) : la même
// que celle des poignées de redimensionnement (ScaleVectorObjectCommand).
struct BoundsUm {
    std::int64_t minX{std::numeric_limits<std::int64_t>::max()};
    std::int64_t maxX{std::numeric_limits<std::int64_t>::min()};
    std::int64_t minY{std::numeric_limits<std::int64_t>::max()};
    std::int64_t maxY{std::numeric_limits<std::int64_t>::min()};
    bool valid{false};
};

BoundsUm boundsOf(const document::VectorObject& object) {
    BoundsUm b;
    const auto scan = [&b](const geometry::Path& path) {
        for (const auto& node : path.nodes) {
            b.minX = std::min<std::int64_t>(b.minX, node.pos.x.value);
            b.maxX = std::max<std::int64_t>(b.maxX, node.pos.x.value);
            b.minY = std::min<std::int64_t>(b.minY, node.pos.y.value);
            b.maxY = std::max<std::int64_t>(b.maxY, node.pos.y.value);
            b.valid = true;
        }
    };
    for (const auto& set : object.paths) {
        scan(set.outer);
        for (const auto& hole : set.holes) {
            scan(hole);
        }
    }
    return b;
}

Vec2um vec(std::int64_t x, std::int64_t y) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(x)},
                  Micrometers{static_cast<std::int32_t>(y)}};
}

} // namespace

std::optional<QRectF> MainWindow::vectorBoxMm(const document::VectorObject& object) {
    const BoundsUm b = boundsOf(object);
    if (!b.valid) {
        return std::nullopt;
    }
    return QRectF(static_cast<double>(b.minX) / 1000.0, static_cast<double>(b.minY) / 1000.0,
                  static_cast<double>(b.maxX - b.minX) / 1000.0,
                  static_cast<double>(b.maxY - b.minY) / 1000.0);
}

void MainWindow::connectInspectorEditing() {
    connect(propertiesPanel_, &PropertiesPanel::vectorBoxEdited, this,
            [this](ObjectId id, QRectF want) { applyVectorBox(id, want); });
    connect(propertiesPanel_, &PropertiesPanel::applyToSelectionRequested, this,
            &MainWindow::applyToSelection);
}

void MainWindow::applyVectorBox(ObjectId id, QRectF want) {
    const auto* object = project_.findObject(id);
    if (object == nullptr) {
        return;
    }
    const BoundsUm b = boundsOf(*object);
    if (!b.valid || b.maxX == b.minX || b.maxY == b.minY) {
        return;
    }
    const double curW = static_cast<double>(b.maxX - b.minX) / 1000.0;
    const double curH = static_cast<double>(b.maxY - b.minY) / 1000.0;
    const bool resize =
        std::abs(want.width() - curW) >= 0.005 || std::abs(want.height() - curH) >= 0.005;
    const std::int64_t dx = std::llround(want.x() * 1000.0) - b.minX;
    const std::int64_t dy = std::llround(want.y() * 1000.0) - b.minY;
    const bool move = dx != 0 || dy != 0;
    if (!resize && !move) {
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        resize ? (move ? tr("Modifier la taille et la position de la forme")
                       : tr("Modifier la taille de la forme"))
                     .toStdString()
               : tr("Modifier la position de la forme").toStdString());
    if (resize) {
        // Ancrage au coin bas-gauche : la position reste celle de la boîte jusqu'au déplacement.
        composite->add(std::make_unique<commands::ScaleVectorObjectCommand>(
            id, vec(b.minX, b.minY), want.width() / curW, want.height() / curH));
    }
    if (move) {
        composite->add(std::make_unique<commands::TranslateVectorObjectCommand>(id, vec(dx, dy)));
    }
    undoStack_.execute(std::move(composite), project_);
    refreshImage();
    updateActions();
}

void MainWindow::applyToSelection(int stitchType, bool setSpacing, double spacingMm, bool setAngle,
                                  double angleDeg) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Appliquer les réglages à %1 objets").arg(ids.size()).toStdString());
    const Micrometers spacing = to_micrometers(Millimeters{spacingMm});
    const Angle angle{angleDeg * std::numbers::pi / 180.0};
    for (const ObjectId vectorId : ids) {
        std::vector<const document::EmbroideryObject*> targets;
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.source_vector == vectorId) {
                targets.push_back(&emb);
            }
        }
        if (targets.empty()) {
            continue;
        }
        if (stitchType >= 0) {
            // Nouveau type : mêmes valeurs par défaut que le choix de type de l'inspecteur,
            // converti pour tout le groupe de sections de la forme (ConvertFillGroupCommand).
            document::StitchParams params;
            std::string label;
            if (stitchType == 0) {
                document::RunningStitchParams rp;
                rp.repeats = 3;
                params = rp;
                label = "Type : contour";
            } else {
                document::TatamiParams tp;
                if (setSpacing) {
                    tp.row_spacing = spacing;
                }
                if (setAngle) {
                    tp.angle = angle;
                }
                params = tp;
                label = "Type : tatami";
            }
            composite->add(std::make_unique<commands::ConvertFillGroupCommand>(
                targets.front()->id, std::move(params), std::move(label)));
            continue;
        }
        for (const auto* emb : targets) {
            document::StitchParams params = emb->params;
            bool changed = false;
            if (auto* t = std::get_if<document::TatamiParams>(&params)) {
                if (setSpacing) {
                    changed = changed || t->row_spacing != spacing;
                    t->row_spacing = spacing;
                }
                if (setAngle) {
                    changed = changed || t->angle.radians != angle.radians;
                    t->angle = angle;
                }
            } else if (auto* d = std::get_if<document::DirectionalFillParams>(&params)) {
                if (setSpacing) {
                    changed = d->row_spacing != spacing;
                    d->row_spacing = spacing;
                }
            } else if (auto* a = std::get_if<document::AutoSatinParams>(&params)) {
                if (setSpacing) {
                    changed = a->spacing != spacing;
                    a->spacing = spacing;
                }
            } else if (auto* s = std::get_if<document::SatinParams>(&params)) {
                if (setSpacing) {
                    changed = s->density != spacing;
                    s->density = spacing;
                }
            }
            if (changed) {
                composite->add(std::make_unique<commands::SetStitchParamsCommand>(
                    emb->id, std::move(params), tr("Réglages multiples").toStdString()));
            }
        }
    }
    if (composite->empty()) {
        statusBar()->showMessage(tr("Rien à modifier : les réglages demandés sont déjà "
                                    "appliqués ou ne concernent pas ces objets."));
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Réglages appliqués à %1 objets (une seule étape annulable).").arg(ids.size()));
}

// ---------------------------------------------------------------------------------------------
// Alignement (Édition > Aligner) : range les objets sélectionnés sur la boîte de la sélection.
// ---------------------------------------------------------------------------------------------

void MainWindow::buildAlignMenu(QMenu* editMenu) {
    auto* menu = editMenu->addMenu(tr("A&ligner la sélection"));
    menu->setObjectName(QStringLiteral("menu_align"));
    struct Entry {
        const char* name;
        QString text;
        int mode;
    };
    const Entry entries[] = {
        {"action_alignLeft", tr("À &gauche"), 0},
        {"action_alignCenter", tr("Centrés &horizontalement"), 1},
        {"action_alignRight", tr("À &droite"), 2},
        {"action_alignTop", tr("En &haut"), 3},
        {"action_alignMiddle", tr("Centrés &verticalement"), 4},
        {"action_alignBottom", tr("En &bas"), 5},
    };
    for (const auto& e : entries) {
        auto* act = menu->addAction(e.text);
        act->setObjectName(QString::fromLatin1(e.name));
        const int mode = e.mode;
        connect(act, &QAction::triggered, this, [this, mode] { alignSelection(mode); });
        alignActs_.append(act);
    }
}

void MainWindow::updateAlignActions() {
    const bool enabled = hasMultiSelection() && selectedObjectIds().size() >= 2;
    for (QAction* act : alignActs_) {
        act->setEnabled(enabled);
        act->setStatusTip(enabled ? QString()
                                  : tr("Sélectionnez au moins deux formes (Maj+clic)."));
    }
}

void MainWindow::alignSelection(int mode) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.size() < 2) {
        return;
    }
    std::vector<std::pair<ObjectId, BoundsUm>> boxes;
    BoundsUm all;
    for (const ObjectId id : ids) {
        const auto* object = project_.findObject(id);
        if (object == nullptr) {
            continue;
        }
        const BoundsUm b = boundsOf(*object);
        if (!b.valid) {
            continue;
        }
        boxes.emplace_back(id, b);
        all.minX = std::min(all.minX, b.minX);
        all.maxX = std::max(all.maxX, b.maxX);
        all.minY = std::min(all.minY, b.minY);
        all.maxY = std::max(all.maxY, b.maxY);
        all.valid = true;
    }
    if (boxes.size() < 2) {
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Aligner %1 objets").arg(boxes.size()).toStdString());
    for (const auto& [id, b] : boxes) {
        std::int64_t dx = 0;
        std::int64_t dy = 0;
        switch (mode) {
        case 0:
            dx = all.minX - b.minX;
            break;
        case 1:
            dx = (all.minX + all.maxX) / 2 - (b.minX + b.maxX) / 2;
            break;
        case 2:
            dx = all.maxX - b.maxX;
            break;
        case 3: // Y vers le haut : « en haut » = Y maximal
            dy = all.maxY - b.maxY;
            break;
        case 4:
            dy = (all.minY + all.maxY) / 2 - (b.minY + b.maxY) / 2;
            break;
        case 5:
            dy = all.minY - b.minY;
            break;
        default:
            return;
        }
        if (dx != 0 || dy != 0) {
            composite->add(
                std::make_unique<commands::TranslateVectorObjectCommand>(id, vec(dx, dy)));
        }
    }
    if (composite->empty()) {
        statusBar()->showMessage(tr("Les objets sont déjà alignés."));
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    refreshImage();
    updateActions();
}

// ---------------------------------------------------------------------------------------------
// Panneau Historique : liste des pas d'annulation, un clic saute à cet état.
// ---------------------------------------------------------------------------------------------

void MainWindow::buildHistoryPanel() {
    historyDock_ = new QDockWidget(tr("Historique"), this);
    historyDock_->setObjectName(QStringLiteral("historyDock"));
    historyDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    historyList_ = new QListWidget(historyDock_);
    historyList_->setObjectName(QStringLiteral("list_history"));
    historyList_->setAccessibleName(tr("Historique des modifications"));
    historyList_->setToolTip(tr("Cliquez sur une ligne pour revenir à cet état (les étapes "
                                "suivantes restent rétablissables)."));
    historyDock_->setWidget(historyList_);
    addDockWidget(Qt::RightDockWidgetArea, historyDock_);
    historyDock_->setAccessibleName(historyDock_->windowTitle());
    historyDock_->hide();
    connect(historyList_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { jumpToHistory(historyList_->row(item)); });
}

void MainWindow::refreshHistoryPanel() {
    if (historyList_ == nullptr || historyDock_ == nullptr || !historyDock_->isVisible()) {
        return;
    }
    const auto undoNames = undoStack_.undoNames();
    const auto redoNames = undoStack_.redoNames();
    const QSignalBlocker block(historyList_);
    historyList_->clear();
    historyList_->addItem(tr("État initial"));
    for (const auto& name : undoNames) {
        historyList_->addItem(QString::fromStdString(name));
    }
    QFont dim = historyList_->font();
    dim.setItalic(true);
    for (const auto& name : redoNames) {
        auto* item = new QListWidgetItem(QString::fromStdString(name));
        item->setFont(dim);
        item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
        item->setToolTip(tr("Étape annulée : cliquez pour la rétablir."));
        historyList_->addItem(item);
    }
    historyList_->setCurrentRow(static_cast<int>(undoNames.size()));
}

void MainWindow::jumpToHistory(int row) {
    if (row < 0) {
        return;
    }
    const int applied = static_cast<int>(undoStack_.undoNames().size());
    if (row == applied) {
        return;
    }
    int steps = 0;
    while (static_cast<int>(undoStack_.undoNames().size()) > row && undoStack_.undo(project_)) {
        ++steps;
    }
    while (static_cast<int>(undoStack_.undoNames().size()) < row && undoStack_.redo(project_)) {
        ++steps;
    }
    if (steps > 0) {
        refreshImage();
        updateActions();
    }
}

} // namespace openstitch::desktop
