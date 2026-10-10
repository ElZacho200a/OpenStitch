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
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/stitch_generation/directional_fill.hpp"
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
    connect(propertiesPanel_, &PropertiesPanel::groupParamsEdited, this,
            &MainWindow::applyGroupParams);
    connect(propertiesPanel_, &PropertiesPanel::groupActionRequested, this,
            &MainWindow::applyGroupAction);
    connect(propertiesPanel_, &PropertiesPanel::groupGuideAngleRequested, this,
            &MainWindow::applyGroupGuideAngle);
}

MainWindow::EmbroideryGroup MainWindow::selectedEmbroideryGroup() const {
    EmbroideryGroup group;
    std::optional<std::size_t> kind;
    group.sameType = true;
    for (const ObjectId vectorId : selectedObjectIds()) {
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.source_vector != vectorId) {
                continue;
            }
            group.ids.push_back(emb.id);
            if (!kind) {
                kind = emb.params.index();
            } else if (*kind != emb.params.index()) {
                group.sameType = false;
            }
        }
    }
    if (group.ids.size() < 2) {
        group.sameType = false;
    }
    return group;
}

void MainWindow::applyGroupParams(const QString& field,
                                  const std::function<void(document::StitchParams&)>& apply) {
    const EmbroideryGroup group = selectedEmbroideryGroup();
    if (!group.sameType) {
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Modifier : %1 (%2 objets)").arg(field).arg(group.ids.size()).toStdString());
    for (const ObjectId id : group.ids) {
        const auto* emb = project_.findEmbroidery(id);
        if (emb == nullptr) {
            continue;
        }
        document::StitchParams params = emb->params;
        apply(params);
        if (params == emb->params) {
            continue;
        }
        composite->add(std::make_unique<commands::SetStitchParamsCommand>(id, std::move(params),
                                                                          field.toStdString()));
    }
    if (composite->empty()) {
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    // Le formulaire montre le représentant : il reste cohérent sans reconstruction.
    refreshImage();
    updateActions();
}

void MainWindow::applyGroupGuideAngle(double angleDeg, bool absolute) {
    const EmbroideryGroup group = selectedEmbroideryGroup();
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Poser un guide d'orientation (%1 objets)").arg(group.ids.size()).toStdString());
    const Angle angle{angleDeg * std::numbers::pi / 180.0};
    for (const ObjectId id : group.ids) {
        const auto* emb = project_.findEmbroidery(id);
        const auto* sat =
            emb != nullptr ? std::get_if<document::AutoSatinParams>(&emb->params) : nullptr;
        const auto* source = emb != nullptr ? project_.findObject(emb->source_vector) : nullptr;
        if (sat == nullptr || source == nullptr) {
            continue;
        }
        const BoundsUm b = boundsOf(*source);
        if (!b.valid) {
            continue;
        }
        document::AutoSatinParams params = *sat;
        // Guide unique, ancré au centre de la forme : angle relatif à l'axe ou absolu.
        params.guides = {document::AutoSatinGuide{vec((b.minX + b.maxX) / 2, (b.minY + b.maxY) / 2),
                                                  angle, absolute}};
        if (params == *sat) {
            continue;
        }
        composite->add(std::make_unique<commands::EditAutoSatinCommand>(
            id, std::move(params), "Poser un guide d'orientation"));
    }
    if (composite->empty()) {
        statusBar()->showMessage(tr("Rien à modifier : ces guides sont déjà posés."));
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Guide posé sur %1 forme(s), en une seule étape annulable.").arg(group.ids.size()),
        8000);
}

void MainWindow::applyGroupAction(const QString& action) {
    const EmbroideryGroup group = selectedEmbroideryGroup();
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Guides (%1 objets)").arg(group.ids.size()).toStdString());
    int touched = 0;
    for (const ObjectId id : group.ids) {
        const auto* emb = project_.findEmbroidery(id);
        if (emb == nullptr) {
            continue;
        }
        if (action == QLatin1String("clearSatinGuides")) {
            const auto* sat = std::get_if<document::AutoSatinParams>(&emb->params);
            if (sat == nullptr || sat->guides.empty()) {
                continue;
            }
            document::AutoSatinParams params = *sat;
            params.guides.clear();
            composite->add(std::make_unique<commands::EditAutoSatinCommand>(
                id, std::move(params), "Retirer les guides d'orientation"));
            ++touched;
        } else if (action == QLatin1String("clearDirectionGuides")) {
            const auto* dir = std::get_if<document::DirectionalFillParams>(&emb->params);
            if (dir == nullptr || (dir->guides.empty() && dir->break_lines.empty())) {
                continue;
            }
            document::DirectionalFillParams params = *dir;
            params.guides.clear();
            params.break_lines.clear();
            composite->add(std::make_unique<commands::EditDirectionalFillCommand>(
                id, std::move(params), "Retirer guides et ruptures"));
            ++touched;
        } else if (action == QLatin1String("autoDirectionGuides")) {
            const auto* dir = std::get_if<document::DirectionalFillParams>(&emb->params);
            const auto* source = project_.findObject(emb->source_vector);
            if (dir == nullptr || source == nullptr) {
                continue;
            }
            std::vector<geometry::Path> generated;
            for (const auto& set : source->paths) {
                auto guides = stitch_generation::directional_guides_from_region(set);
                generated.insert(generated.end(), std::make_move_iterator(guides.begin()),
                                 std::make_move_iterator(guides.end()));
            }
            if (generated.empty()) {
                continue;
            }
            document::DirectionalFillParams params = *dir;
            params.guides = std::move(generated); // remplace : un lot reproductible
            composite->add(std::make_unique<commands::EditDirectionalFillCommand>(
                id, std::move(params), "Générer les guides de direction"));
            ++touched;
        }
    }
    if (composite->empty()) {
        statusBar()->showMessage(tr("Rien à modifier pour cette action."));
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("%1 forme(s) modifiée(s), en une seule étape annulable.").arg(touched), 8000);
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

void MainWindow::setStitchTypeForSelection(
    int type, const std::function<void(document::StitchParams&)>& tweak) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.empty()) {
        if (selectedEmbroidery_) {
            setStitchType(*selectedEmbroidery_, type);
        }
        return;
    }
    const QString typeNames[] = {tr("Contour cousu"), tr("Remplissage tatami"), tr("Colonne satin"),
                                 tr("Remplissage directionnel")};
    const QString typeName = type >= 0 && type < 4 ? typeNames[type] : tr("Type de points");
    auto group = std::make_unique<commands::CompositeCommand>(
        tr("Type : %1 (%2 formes)").arg(typeName).arg(ids.size()).toStdString());
    QStringList skipped;
    QString firstError;
    int done = 0;
    for (const ObjectId id : ids) {
        const auto* vec = project_.findObject(id);
        if (vec == nullptr) {
            continue;
        }
        const document::EmbroideryObject* existing = nullptr;
        for (const auto& emb : project_.embroidery_objects) {
            if (emb.source_vector == id) {
                existing = &emb;
                break;
            }
        }
        // Forme sans couture : un objet provisoire donne sa forme et ses couleurs au calcul.
        document::EmbroideryObject provisional;
        if (existing == nullptr) {
            provisional.source_vector = id;
            provisional.rgb = vec->rgb;
            provisional.params = document::TatamiParams{};
        }
        document::StitchParams params;
        std::string label;
        std::optional<std::vector<geometry::PathSet>> restored;
        QString error;
        if (!stitchParamsForType(existing != nullptr ? *existing : provisional, type, params, label,
                                 restored, error)) {
            skipped << QString::fromStdString(vec->name);
            if (firstError.isEmpty()) {
                firstError = error;
            }
            continue;
        }
        if (tweak) {
            tweak(params);
        }
        if (restored) {
            group->add(std::make_unique<commands::SetVectorPathsCommand>(
                id, *restored, "Contour brut de la région"));
        }
        if (existing != nullptr) {
            group->add(std::make_unique<commands::ConvertFillGroupCommand>(
                existing->id, std::move(params), std::move(label)));
        } else {
            document::EmbroideryObject object;
            object.id = project_.object_ids.next();
            object.name =
                tr("%1 de %2").arg(typeName, QString::fromStdString(vec->name)).toStdString();
            object.source_vector = id;
            object.rgb = vec->rgb;
            object.params = std::move(params);
            object.intent = document::EmbroideryIntent::ForcedUserChoice;
            group->add(std::make_unique<commands::AddEmbroideryObjectCommand>(std::move(object)));
        }
        ++done;
    }
    if (group->empty()) {
        QMessageBox::warning(this, tr("Changement de type impossible"),
                             firstError.isEmpty() ? tr("Aucune forme à modifier.") : firstError);
        return;
    }
    undoStack_.execute(std::move(group), project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    QString message =
        tr("%1 : appliqué à %2 forme(s), en une seule étape annulable.").arg(typeName).arg(done);
    if (!skipped.isEmpty()) {
        message +=
            tr(" Ignorées (%1) : %2.").arg(skipped.size()).arg(skipped.join(QStringLiteral(", ")));
    }
    statusBar()->showMessage(message, 12000);
}

void MainWindow::applyToSelection(int stitchType, bool setSpacing, double spacingMm, bool setAngle,
                                  double angleDeg) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    const Micrometers spacing = to_micrometers(Millimeters{spacingMm});
    const Angle angle{angleDeg * std::numbers::pi / 180.0};
    if (stitchType >= 0) {
        // Nouveau type pour toutes les formes (0 contour, 1 tatami, 2 satin, 3 directionnel),
        // avec l'espacement / l'angle cochés appliqués aux paramètres de départ.
        setStitchTypeForSelection(stitchType, [&](document::StitchParams& params) {
            if (auto* t = std::get_if<document::TatamiParams>(&params)) {
                if (setSpacing) {
                    t->row_spacing = spacing;
                }
                if (setAngle) {
                    t->angle = angle;
                }
            } else if (auto* d = std::get_if<document::DirectionalFillParams>(&params)) {
                if (setSpacing) {
                    d->row_spacing = spacing;
                }
            } else if (auto* a = std::get_if<document::AutoSatinParams>(&params)) {
                if (setSpacing) {
                    a->spacing = spacing;
                }
            }
        });
        return;
    }
    auto composite = std::make_unique<commands::CompositeCommand>(
        tr("Appliquer les réglages à %1 objets").arg(ids.size()).toStdString());
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
