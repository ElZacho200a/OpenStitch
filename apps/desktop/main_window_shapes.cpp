// SPDX-License-Identifier: Apache-2.0
// Formes vectorielles : unir, soustraire, intersecter, séparer les morceaux et outil Couteau
// (découpe le long d'une ligne). Aucune géométrie ici : tout est calculé par
// commands/vector_ops.hpp (Clipper2 dans libs/geometry) et appliqué en UN pas d'annulation.
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include <algorithm>
#include <memory>

#include "main_window.hpp"
#include "openstitch/commands/vector_ops.hpp"

namespace openstitch::desktop {

namespace {

void setShapeActionEnabled(QAction* act, bool enabled, const QString& why) {
    if (act == nullptr) {
        return;
    }
    if (!act->property("baseToolTip").isValid()) {
        act->setProperty("baseToolTip", act->toolTip());
    }
    act->setEnabled(enabled);
    const QString base = act->property("baseToolTip").toString();
    act->setToolTip(enabled ? base : base + QStringLiteral("\n") + why);
    act->setStatusTip(enabled ? QString() : why);
}

} // namespace

void MainWindow::buildShapeMenu() {
    shapeMenu_ = menuBar()->addMenu(tr("F&orme"));
    const auto add = [this](QAction*& member, const QString& text, const QString& name,
                            const QKeySequence& key, const QString& help,
                            const std::function<void()>& run) {
        member = shapeMenu_->addAction(text);
        member->setObjectName(name);
        member->setShortcut(key);
        member->setToolTip(help);
        connect(member, &QAction::triggered, this, run);
    };
    add(unionAct_, tr("&Unir"), QStringLiteral("action_shapeUnion"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U),
        tr("Fusionne les formes sélectionnées en une seule (la dernière sélectionnée garde "
           "son identité)."),
        [this] { runBooleanOp(commands::BooleanOp::Union); });
    add(subtractAct_, tr("&Soustraire"), QStringLiteral("action_shapeSubtract"),
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S),
        tr("Retire de la forme la plus basse toutes les autres formes sélectionnées."),
        [this] { runBooleanOp(commands::BooleanOp::Subtract); });
    add(intersectAct_, tr("&Intersecter"), QStringLiteral("action_shapeIntersect"),
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I),
        tr("Ne garde que la partie commune à toutes les formes sélectionnées."),
        [this] { runBooleanOp(commands::BooleanOp::Intersect); });
    shapeMenu_->addSeparator();
    add(breakApartAct_, tr("&Séparer les morceaux"), QStringLiteral("action_shapeBreakApart"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
        tr("Transforme chaque morceau disjoint de la forme en un objet distinct."),
        [this] { breakApartSelected(); });
    shapeMenu_->addSeparator();
    if (toolCutAct_ != nullptr) {
        shapeMenu_->addAction(toolCutAct_);
    }
}

void MainWindow::updateShapeActions() {
    const std::size_t count = selectedObjectIds().size();
    const QString needTwo = tr("Sélectionnez au moins deux formes (Maj + clic).");
    setShapeActionEnabled(unionAct_, count >= 2, needTwo);
    setShapeActionEnabled(subtractAct_, count >= 2, needTwo);
    setShapeActionEnabled(intersectAct_, count >= 2, needTwo);
    bool splittable = false;
    if (count == 1) {
        const auto* object = project_.findObject(selectedObjectIds().front());
        splittable = object != nullptr && object->paths.size() >= 2;
    }
    setShapeActionEnabled(breakApartAct_, splittable,
                          tr("Sélectionnez une forme composée de plusieurs morceaux."));
    setShapeActionEnabled(toolCutAct_, !project_.vector_objects.empty(),
                          tr("Aucune forme vectorielle à découper."));
}

void MainWindow::applyShapeCommand(commands::VectorOpResult result, const QString& done,
                                   const std::vector<ObjectId>& keep) {
    if (!result.command) {
        statusBar()->showMessage(QString::fromUtf8(result.error.c_str()), 8000);
        return;
    }
    undoStack_.execute(std::move(result.command), project_);
    // Sélection : la première des formes d'origine qui existe encore.
    std::optional<ObjectId> survivor;
    for (const ObjectId id : keep) {
        if (project_.findObject(id) != nullptr) {
            survivor = id;
            break;
        }
    }
    if (survivor) {
        setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {*survivor}});
    } else {
        setSelection({});
    }
    showVectorsAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(done, 8000);
}

void MainWindow::runBooleanOp(commands::BooleanOp op) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    const QString label = op == commands::BooleanOp::Union      ? tr("Formes unies.")
                          : op == commands::BooleanOp::Subtract ? tr("Formes soustraites.")
                                                                : tr("Formes intersectées.");
    // Le principal (dernier sélectionné) d'abord : c'est lui qui est retenu s'il survit.
    std::vector<ObjectId> keep(ids.rbegin(), ids.rend());
    applyShapeCommand(commands::make_boolean_command(project_, op, ids),
                      label + tr(" Ctrl+Z pour annuler."), keep);
}

void MainWindow::breakApartSelected() {
    const std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.size() != 1) {
        statusBar()->showMessage(tr("Sélectionnez une seule forme à séparer."), 6000);
        return;
    }
    applyShapeCommand(commands::make_break_apart_command(project_, ids.front()),
                      tr("Morceaux séparés. Ctrl+Z pour annuler."), ids);
}

void MainWindow::finishCut() {
    if (pendingFreeformPoints_.size() < 2) {
        cancelFreeformDraw();
        return;
    }
    const Vec2um a = pendingFreeformPoints_.front();
    const Vec2um b = pendingFreeformPoints_.back();
    cancelFreeformDraw();
    if (length_um(b - a) < 500.0) {
        statusBar()->showMessage(tr("Couteau : tracez une ligne plus longue (glisser)."), 6000);
        return;
    }
    // Sans sélection, le couteau coupe toutes les formes visibles que la ligne traverse.
    std::vector<ObjectId> ids = selectedObjectIds();
    if (ids.empty()) {
        for (const auto& object : project_.vector_objects) {
            if (object.visible) {
                ids.push_back(object.id);
            }
        }
    }
    applyShapeCommand(commands::make_split_command(project_, ids, a, b),
                      tr("Forme découpée. Ctrl+Z pour annuler."), ids);
}

} // namespace openstitch::desktop
