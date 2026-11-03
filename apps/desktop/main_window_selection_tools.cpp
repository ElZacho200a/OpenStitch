// SPDX-License-Identifier: Apache-2.0
// Outils de sélection (ergonomie sélection/filtres) : inverser, même type de point / fil / calque,
// forme suivante ou précédente dans l'ordre de couture, plage d'ordre, formes voisines, lasso,
// retrouver par état et sélections nommées. Les calculs sont dans `selection_queries.*` (pur) ;
// ici seulement le câblage, qui passe toujours par `setSelection` (aucune mutation du document).
#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QStatusBar>

#include <algorithm>

#include "canvas_view.hpp"
#include "main_window.hpp"

namespace openstitch::desktop {

namespace {

// Marge de « formes voisines » : 2 mm autour de la boîte de la sélection.
constexpr std::int32_t kTouchingMarginUm = 2'000;

void setEnabledWithReason(QAction* act, bool enabled, const QString& whyDisabled) {
    if (act == nullptr) {
        return;
    }
    if (!act->property("baseToolTip").isValid()) {
        act->setProperty("baseToolTip", act->toolTip());
        act->setProperty("baseStatusTip", act->statusTip());
    }
    act->setEnabled(enabled);
    const QString baseTip = act->property("baseToolTip").toString();
    act->setToolTip(enabled ? baseTip : baseTip + QStringLiteral("\n") + whyDisabled);
    act->setStatusTip(enabled ? act->property("baseStatusTip").toString() : whyDisabled);
}

} // namespace

ShapePredicate MainWindow::selectablePredicate() const {
    // Même règle que le clic : visible, non verrouillé (forme ou calque), filtres d'affichage.
    return [this](const document::VectorObject& shape) {
        return shape.visible && !project_.shapeEditLocked(shape.id) && vectorPassesFilter(shape);
    };
}

void MainWindow::buildSelectionToolsMenu(QMenu* editMenu) {
    QMenu* menu = editMenu->addMenu(tr("S&élection"));
    menu->setObjectName(QStringLiteral("menu_selection"));
    const auto add = [this, menu](const QString& text, const QString& name, const QKeySequence& key,
                                  const QString& help, bool needsShape,
                                  const std::function<void()>& run) {
        QAction* act = menu->addAction(text);
        act->setObjectName(name);
        if (!key.isEmpty()) {
            act->setShortcut(key);
        }
        act->setToolTip(help);
        connect(act, &QAction::triggered, this, run);
        (needsShape ? selectionNeedsShapeActs_ : selectionToolActs_).push_back(act);
        return act;
    };
    add(tr("&Inverser la sélection"), QStringLiteral("action_invertSelection"),
        QKeySequence(Qt::CTRL | Qt::Key_I),
        tr("Sélectionne les formes sélectionnables qui ne l'étaient pas (Ctrl+I)."), false,
        [this] { invertShapeSelection(); });
    menu->addSeparator();
    add(tr("Même &type de point"), QStringLiteral("action_selectSameStitchType"), {},
        tr("Sélectionne les formes brodées du même type de point que la forme active (tatami, "
           "satin, contour…)."),
        true, [this] { selectSameKind(0); });
    add(tr("Même &fil"), QStringLiteral("action_selectSameThread"), {},
        tr("Sélectionne les formes du même fil : même référence de nuancier si la forme active en "
           "a une, sinon même couleur libre."),
        true, [this] { selectSameKind(1); });
    add(tr("Même &calque"), QStringLiteral("action_selectSameCalque"), {},
        tr("Sélectionne les formes du calque de la forme active."), true,
        [this] { selectSameKind(2); });
    add(tr("Formes &voisines (2 mm)"), QStringLiteral("action_selectTouching"), {},
        tr("Ajoute les formes dont la boîte touche la sélection, à 2 mm près."), true,
        [this] { selectTouchingShapes(); });
    menu->addSeparator();
    add(tr("Forme &suivante dans l'ordre de couture"), QStringLiteral("action_selectNextInOrder"),
        QKeySequence(Qt::CTRL | Qt::Key_PageDown),
        tr("Passe à la forme brodée suivante dans l'ordre de couture (Ctrl+Page suivante). "
           "Sans sélection : la première."),
        false, [this] { stepSelection(+1); });
    add(tr("Forme &précédente dans l'ordre de couture"),
        QStringLiteral("action_selectPreviousInOrder"), QKeySequence(Qt::CTRL | Qt::Key_PageUp),
        tr("Passe à la forme brodée précédente dans l'ordre de couture (Ctrl+Page précédente). "
           "Sans sélection : la dernière."),
        false, [this] { stepSelection(-1); });
    add(tr("&Plage de l'ordre de couture…"), QStringLiteral("action_selectSewingRange"), {},
        tr("Sélectionne les formes cousues du rang N au rang M."), false,
        [this] { selectSewingRangeDialog(); });
    menu->addSeparator();
    add(tr("&Lasso de sélection"), QStringLiteral("action_lassoSelect"), {},
        tr("Tracez une boucle à main levée : les formes dont le centre est dedans sont "
           "sélectionnées."),
        false, [this] { armLassoSelection(); });
    menu->addSeparator();
    QMenu* find = menu->addMenu(tr("&Retrouver"));
    find->setObjectName(QStringLiteral("menu_selectByState"));
    const std::pair<QString, int> states[] = {
        {tr("Formes verrouillées (édition)"), 2},
        {tr("Ordre de couture figé"), 0},
        {tr("Broderie masquée"), 1},
        {tr("Formes sans broderie"), 3},
    };
    for (const auto& [label, kind] : states) {
        QAction* act = find->addAction(label);
        act->setObjectName(QStringLiteral("action_selectState%1").arg(kind));
        connect(act, &QAction::triggered, this, [this, kind] { selectByState(kind); });
        selectionToolActs_.push_back(act);
    }
    menu->addSeparator();
    add(tr("&Mémoriser la sélection…"), QStringLiteral("action_storeSelection"), {},
        tr("Garde la sélection sous un nom pour la rappeler plus tard (jusqu'à la fermeture du "
           "projet, non enregistré dans le fichier)."),
        true, [this] { storeNamedSelection(); });
    namedSelectionMenu_ = menu->addMenu(tr("&Rappeler une sélection"));
    namedSelectionMenu_->setObjectName(QStringLiteral("menu_recallSelection"));
    rebuildNamedSelectionMenu();
}

void MainWindow::updateSelectionToolActions() {
    const bool anyShape = !project_.vector_objects.empty();
    const bool anyTarget = !transformTargets().empty();
    for (QAction* act : selectionToolActs_) {
        setEnabledWithReason(act, anyShape, tr("Le design ne contient aucune forme."));
    }
    for (QAction* act : selectionNeedsShapeActs_) {
        setEnabledWithReason(act, anyTarget, tr("Sélectionnez d'abord une ou plusieurs formes."));
    }
    if (namedSelectionMenu_ != nullptr) {
        namedSelectionMenu_->setEnabled(!namedSelections_.empty());
    }
}

void MainWindow::applyShapeSelection(std::vector<ObjectId> ids, const QString& what) {
    if (ids.empty()) {
        statusBar()->showMessage(tr("%1 : aucune forme sélectionnable.").arg(what), 5000);
        return;
    }
    showVectorsAct_->setChecked(true);
    setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = std::move(ids)});
    displayImage(processed_);
    updateActions();
    statusBar()->showMessage(
        tr("%1 : %2 forme(s) sélectionnée(s).").arg(what).arg(currentSelection().objects.size()),
        6000);
}

void MainWindow::invertShapeSelection() {
    applyShapeSelection(
        invert_selection(project_, currentSelection().objects, selectablePredicate()),
        tr("Sélection inversée"));
}

void MainWindow::selectSameKind(int kind) {
    const std::vector<ObjectId> targets = transformTargets();
    if (targets.empty()) {
        return;
    }
    const ObjectId active = targets.back();
    const ShapePredicate selectable = selectablePredicate();
    switch (kind) {
    case 0:
        applyShapeSelection(select_same_stitch_type(project_, active, selectable),
                            tr("Même type de point"));
        break;
    case 1:
        applyShapeSelection(select_same_thread(project_, active, selectable), tr("Même fil"));
        break;
    default:
        applyShapeSelection(select_same_calque(project_, active, selectable), tr("Même calque"));
        break;
    }
}

void MainWindow::stepSelection(int step) {
    const std::vector<ObjectId> targets = transformTargets();
    const std::optional<ObjectId> current =
        targets.empty() ? std::nullopt : std::optional<ObjectId>(targets.back());
    const std::optional<ObjectId> next =
        step_in_sewing_order(project_, current, step, selectablePredicate());
    if (!next) {
        statusBar()->showMessage(tr("Aucune forme brodée à parcourir."), 4000);
        return;
    }
    applyShapeSelection({*next}, tr("Ordre de couture"));
    const std::size_t rank = sewing_rank_of_shape(project_, *next);
    if (rank != 0) {
        statusBar()->showMessage(tr("Forme n° %1 dans l'ordre de couture.").arg(rank), 5000);
    }
}

void MainWindow::selectSewingRangeDialog() {
    const std::size_t total = project_.embroidery_objects.size();
    if (total == 0) {
        statusBar()->showMessage(tr("Aucun objet brodé : rien à sélectionner par rang."), 5000);
        return;
    }
    bool ok = false;
    const int first =
        QInputDialog::getInt(this, tr("Plage de couture"), tr("Du rang (1 = cousu en premier) :"),
                             1, 1, static_cast<int>(total), 1, &ok);
    if (!ok) {
        return;
    }
    const int last =
        QInputDialog::getInt(this, tr("Plage de couture"), tr("Au rang :"), static_cast<int>(total),
                             1, static_cast<int>(total), 1, &ok);
    if (!ok) {
        return;
    }
    applyShapeSelection(select_sewing_range(project_, static_cast<std::size_t>(first),
                                            static_cast<std::size_t>(last), selectablePredicate()),
                        tr("Rang %1 à %2").arg(std::min(first, last)).arg(std::max(first, last)));
}

void MainWindow::selectTouchingShapes() {
    const std::vector<ObjectId> current =
        currentSelection().objects.empty() ? transformTargets() : currentSelection().objects;
    std::vector<ObjectId> near =
        select_touching(project_, current, Micrometers{kTouchingMarginUm}, selectablePredicate());
    if (near.empty()) {
        statusBar()->showMessage(tr("Aucune forme voisine à moins de 2 mm."), 5000);
        return;
    }
    std::vector<ObjectId> all = current;
    all.insert(all.end(), near.begin(), near.end());
    applyShapeSelection(std::move(all), tr("Formes voisines"));
}

void MainWindow::selectByState(int kind) {
    static constexpr StateKind kKinds[] = {StateKind::OrderLocked, StateKind::HiddenEmbroidery,
                                           StateKind::EditLocked, StateKind::NoEmbroidery};
    // Retrouver, c'est justement atteindre ce que le clic ne saisit pas : pas de prédicat.
    applyShapeSelection(select_by_state(project_, kKinds[std::clamp(kind, 0, 3)]), tr("Retrouver"));
}

void MainWindow::armLassoSelection() {
    setTool(Tool::DrawFreeform); // efface l'armement si l'outil change : on le repose après
    lassoSelectArmed_ = true;
    statusBar()->showMessage(
        tr("Lasso : tracez une boucle autour des formes à sélectionner (Entrée pour valider)."));
}

void MainWindow::finishLassoSelection() {
    const std::vector<Vec2um> polygon = pendingFreeformPoints_;
    lassoSelectArmed_ = false;
    cancelFreeformDraw();
    setTool(Tool::Select);
    if (polygon.size() < 3) {
        statusBar()->showMessage(tr("Lasso : tracé trop court."), 4000);
        return;
    }
    applyShapeSelection(select_in_lasso(project_, polygon, selectablePredicate()), tr("Lasso"));
}

void MainWindow::storeNamedSelection() {
    const std::vector<ObjectId> targets = transformTargets();
    if (targets.empty()) {
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Mémoriser la sélection"), tr("Nom de la sélection :"),
                              QLineEdit::Normal,
                              tr("Sélection %1").arg(namedSelections_.size() + 1), &ok)
            .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    // Un nom déjà pris est remplacé : pas de doublon dans le menu.
    std::erase_if(namedSelections_, [&](const auto& entry) { return entry.first == name; });
    namedSelections_.emplace_back(name, targets);
    rebuildNamedSelectionMenu();
    updateSelectionToolActions();
    statusBar()->showMessage(
        tr("Sélection « %1 » mémorisée (%2 forme(s)).").arg(name).arg(targets.size()), 5000);
}

void MainWindow::recallNamedSelection(const QString& name) {
    for (const auto& [entry, ids] : namedSelections_) {
        if (entry != name) {
            continue;
        }
        // Les formes supprimées depuis sont ignorées, comme celles que le clic ne saisirait plus.
        const ShapePredicate selectable = selectablePredicate();
        std::vector<ObjectId> alive;
        for (const ObjectId id : ids) {
            const document::VectorObject* shape = project_.findObject(id);
            if (shape != nullptr && selectable(*shape)) {
                alive.push_back(id);
            }
        }
        applyShapeSelection(std::move(alive), tr("Sélection « %1 »").arg(name));
        return;
    }
}

void MainWindow::rebuildNamedSelectionMenu() {
    if (namedSelectionMenu_ == nullptr) {
        return;
    }
    namedSelectionMenu_->clear();
    for (const auto& [name, ids] : namedSelections_) {
        QAction* act = namedSelectionMenu_->addAction(tr("%1 (%2)").arg(name).arg(ids.size()));
        connect(act, &QAction::triggered, this, [this, name] { recallNamedSelection(name); });
    }
    namedSelectionMenu_->setEnabled(!namedSelections_.empty());
}

} // namespace openstitch::desktop
