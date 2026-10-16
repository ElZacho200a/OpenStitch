// SPDX-License-Identifier: Apache-2.0
// Formes vectorielles : unir, soustraire, intersecter, séparer les morceaux et outil Couteau
// (découpe le long d'une ligne). Aucune géométrie ici : tout est calculé par
// commands/vector_ops.hpp (Clipper2 dans libs/geometry) et appliqué en UN pas d'annulation.
#include <QAction>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "app_settings.hpp"
#include "main_window.hpp"
#include "openstitch/commands/vector_ops.hpp"

namespace openstitch::desktop {

namespace {

Micrometers mmToUm(double mm) {
    return Micrometers{static_cast<std::int32_t>(std::lround(mm * 1000.0))};
}

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
    add(excludeAct_, tr("E&xclure"), QStringLiteral("action_shapeExclude"),
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_X),
        tr("Ne garde que les parties qui n'appartiennent qu'à une seule des formes sélectionnées."),
        [this] { runBooleanOp(commands::BooleanOp::Exclude); });
    add(blendZonesAct_, tr("&Fondre deux zones"), QStringLiteral("action_shapeBlendZones"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F),
        tr("Unit deux zones voisines de couleurs différentes en une seule : la première "
           "sélectionnée reste le fond, la seconde devient un remplissage de dessus de plus en "
           "plus dense vers son côté, pour un dégradé de fils. Annulable en une fois."),
        [this] { blendSelectedZones(); });
    shapeMenu_->addSeparator();
    add(breakApartAct_, tr("Sépa&rer les morceaux"), QStringLiteral("action_shapeBreakApart"),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
        tr("Transforme chaque morceau disjoint de la forme en un objet distinct."),
        [this] { breakApartSelected(); });
    add(removeHolesAct_, tr("Supprimer les &trous"), QStringLiteral("action_shapeRemoveHoles"),
        QKeySequence(), tr("Retire tous les trous des formes sélectionnées."),
        [this] { removeHolesOfSelection(0.0); });
    add(fillSmallHolesAct_, tr("Combler les &petits trous…"),
        QStringLiteral("action_shapeFillSmallHoles"), QKeySequence(),
        tr("Retire seulement les trous plus petits qu'une aire donnée."), [this] {
            bool ok = false;
            const double mm2 = QInputDialog::getDouble(
                this, tr("Combler les petits trous"),
                tr("Aire maximale d'un trou à combler (mm²) :"), 4.0, 0.1, 10'000.0, 1, &ok);
            if (ok) {
                removeHolesOfSelection(mm2);
            }
        });
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
    setShapeActionEnabled(excludeAct_, count >= 2, needTwo);
    bool anyHole = false;
    for (const ObjectId id : selectedObjectIds()) {
        if (const auto* object = project_.findObject(id)) {
            for (const auto& set : object->paths) {
                anyHole = anyHole || !set.holes.empty();
            }
        }
    }
    const QString needHole = tr("Sélectionnez une forme qui a au moins un trou.");
    setShapeActionEnabled(removeHolesAct_, anyHole, needHole);
    setShapeActionEnabled(fillSmallHolesAct_, anyHole, needHole);
    const auto blendIds = selectedObjectIds();
    const bool canBlend =
        count == 2 && blendFillFor(blendIds[0]) != nullptr && blendFillFor(blendIds[1]) != nullptr;
    setShapeActionEnabled(blendZonesAct_, canBlend,
                          tr("Sélectionnez exactement deux formes, chacune avec un seul "
                             "remplissage tatami ou directionnel (Maj + clic)."));
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
    const QString label = op == commands::BooleanOp::Union       ? tr("Formes unies.")
                          : op == commands::BooleanOp::Subtract  ? tr("Formes soustraites.")
                          : op == commands::BooleanOp::Intersect ? tr("Formes intersectées.")
                                                                 : tr("Formes exclues.");
    // Le principal (dernier sélectionné) d'abord : c'est lui qui est retenu s'il survit.
    std::vector<ObjectId> keep(ids.rbegin(), ids.rend());
    applyShapeCommand(commands::make_boolean_command(project_, op, ids,
                                                     mmToUm(loadEditSettings().unionGapMm)),
                      label + tr(" Ctrl+Z pour annuler."), keep);
}

void MainWindow::removeHolesOfSelection(double maxAreaMm2) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    auto result = commands::make_remove_holes_command(project_, ids, maxAreaMm2 * 1e6);
    const std::size_t removed = result.resulting_objects;
    applyShapeCommand(std::move(result),
                      tr("%1 trou(s) supprimé(s). Ctrl+Z pour annuler.").arg(removed),
                      std::vector<ObjectId>(ids.rbegin(), ids.rend()));
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
    const EditSettings settings = loadEditSettings();
    if (length_um(b - a) < settings.knifeMinLengthMm * 1000.0) {
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
    applyShapeCommand(commands::make_split_command(project_, ids, a, b,
                                                   mmToUm(settings.knifeWidthMm)),
                      tr("Forme découpée. Ctrl+Z pour annuler."), ids);
}

} // namespace openstitch::desktop
