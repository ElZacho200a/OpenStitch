// SPDX-License-Identifier: Apache-2.0
// Organiser les objets : alignement sur une référence, répartition, verrou d'édition,
// redimensionnement numérique, décalage de duplication, conversions de type
// (HP-OBJ-005 / 008 / 010 / 012 / 022). Aucune logique métier ici : câblage de
// `commands::arrange_ops` / `convert_ops` / `make_transform_command`.
#include <QActionGroup>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <functional>

#include "main_window.hpp"
#include "openstitch/commands/arrange_ops.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/convert_ops.hpp"
#include "openstitch/commands/transform_ops.hpp"

namespace openstitch::desktop {

void MainWindow::buildArrangeMenu(QMenu* editMenu) {
    auto* refMenu = editMenu->addMenu(tr("Aligner sur"));
    refMenu->setObjectName(QStringLiteral("menu_alignReference"));
    auto* group = new QActionGroup(refMenu);
    const QString names[] = {tr("La sélection"), tr("Le premier objet sélectionné"),
                             tr("Le cadre de broderie")};
    for (int i = 0; i < 3; ++i) {
        QAction* act = refMenu->addAction(names[i]);
        act->setObjectName(QStringLiteral("action_alignReference%1").arg(i));
        act->setCheckable(true);
        act->setChecked(i == alignReference_);
        group->addAction(act);
        connect(act, &QAction::triggered, this, [this, i] { alignReference_ = i; });
    }
    const auto add = [this, editMenu](const QString& text, const QString& name,
                                      const std::function<void()>& run) {
        QAction* act = editMenu->addAction(text);
        act->setObjectName(name);
        connect(act, &QAction::triggered, this, run);
        arrangeActs_.append(act);
        return act;
    };
    add(tr("Répartir horizontalement"), QStringLiteral("action_distributeH"),
        [this] { distributeShapes(true); });
    add(tr("Répartir verticalement"), QStringLiteral("action_distributeV"),
        [this] { distributeShapes(false); });
    add(tr("Redimensionner la sélection…"), QStringLiteral("action_resizeSelection"),
        [this] { resizeSelectionDialog(); });
    add(tr("Dupliquer avec décalage…"), QStringLiteral("action_duplicateOffset"),
        [this] { duplicateWithOffsetDialog(); });
    add(tr("Verrouiller la sélection"), QStringLiteral("action_lockSelection"),
        [this] { setShapesLocked(transformTargets(), true); });
    add(tr("Déverrouiller la sélection"), QStringLiteral("action_unlockSelection"),
        [this] { setShapesLocked(transformTargets(), false); });
    add(tr("Déverrouiller toutes les formes"), QStringLiteral("action_unlockAll"),
        [this] { setShapesLocked(commands::all_object_ids(project_), false); });

    auto* convert = editMenu->addMenu(tr("Convertir le type"));
    convert->setObjectName(QStringLiteral("menu_convertType"));
    const QString labels[] = {tr("Satin vers contour (ligne centrale)"),
                              tr("Satin vers forme remplie"), tr("Contour vers satin de bordure"),
                              tr("Remplissage vers contour")};
    const char* ids[] = {"action_convertSatinCenterline", "action_convertSatinShape",
                         "action_convertContourBorder", "action_convertFillContour"};
    for (int k = 0; k < 4; ++k) {
        QAction* act = convert->addAction(labels[k]);
        act->setObjectName(QString::fromLatin1(ids[k]));
        connect(act, &QAction::triggered, this, [this, k] { convertSelectionType(k); });
        convertActs_.append(act);
    }
}

void MainWindow::updateArrangeActions() {
    const bool any = !transformTargets().empty();
    const bool several = selectedObjectIds().size() >= 3;
    for (QAction* act : arrangeActs_) {
        const QString name = act->objectName();
        const bool distribute = name.startsWith(QStringLiteral("action_distribute"));
        const bool unlockAll = name == QStringLiteral("action_unlockAll");
        const bool enabled =
            unlockAll ? !project_.vector_objects.empty() : (distribute ? several : any);
        act->setEnabled(enabled);
        act->setStatusTip(enabled
                              ? QString()
                              : (distribute ? tr("Sélectionnez au moins trois formes.")
                                            : tr("Sélectionnez d'abord une ou plusieurs formes.")));
    }
    for (int k = 0; k < convertActs_.size(); ++k) {
        bool ok = false;
        for (const ObjectId id : targetEmbroideryIds()) {
            const auto* emb = project_.findEmbroidery(id);
            ok = ok || (emb != nullptr && commands::conversion_applicable(
                                              *emb, static_cast<commands::ConversionKind>(k)));
        }
        convertActs_[k]->setEnabled(ok);
        convertActs_[k]->setStatusTip(ok ? QString()
                                         : tr("Sélectionnez un objet du type de départ."));
    }
}

void MainWindow::alignShapes(int mode) {
    const std::vector<ObjectId> ids = selectedObjectIds();
    const auto reference = static_cast<commands::AlignReference>(std::clamp(alignReference_, 0, 2));
    if (ids.empty() || (ids.size() < 2 && reference != commands::AlignReference::Frame)) {
        return;
    }
    std::optional<commands::BoundsUm> frame;
    if (reference == commands::AlignReference::Frame) {
        const std::int32_t hw = project_.canvas.usable_width().value / 2;
        const std::int32_t hh = project_.canvas.usable_height().value / 2;
        frame = commands::BoundsUm{Vec2um{Micrometers{-hw}, Micrometers{-hh}},
                                   Vec2um{Micrometers{hw}, Micrometers{hh}}};
    }
    auto command = commands::make_shift_command(
        commands::align_shifts(project_, ids, static_cast<commands::AlignMode>(mode), reference,
                               frame),
        tr("Aligner %1 objets").arg(ids.size()).toStdString());
    if (!command) {
        statusBar()->showMessage(tr("Les objets sont déjà alignés."));
        return;
    }
    undoStack_.execute(std::move(command), project_);
    refreshImage();
    updateActions();
}

void MainWindow::distributeShapes(bool horizontal) {
    auto command = commands::make_shift_command(
        commands::distribute_shifts(project_, selectedObjectIds(), horizontal),
        (horizontal ? tr("Répartir horizontalement") : tr("Répartir verticalement")).toStdString());
    if (!command) {
        statusBar()->showMessage(
            tr("Rien à répartir (trois formes au moins, espaces déjà égaux)."));
        return;
    }
    undoStack_.execute(std::move(command), project_);
    refreshImage();
    updateActions();
}

void MainWindow::setShapesLocked(const std::vector<ObjectId>& ids, bool locked) {
    auto command = commands::make_shape_lock_command(project_, ids, locked);
    if (!command) {
        return;
    }
    undoStack_.execute(std::move(command), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(locked ? tr("Formes verrouillées : non cliquables au canevas.")
                                    : tr("Formes déverrouillées."),
                             6000);
}

void MainWindow::resizeSelectionByFactors(double sx, double sy, bool fromCenter) {
    const std::vector<ObjectId> ids = transformTargets();
    const auto box = commands::objects_bounds(project_, ids);
    if (!box || !(sx > 0.0) || !(sy > 0.0) || (sx == 1.0 && sy == 1.0)) {
        return;
    }
    const Vec2um pivot = fromCenter ? box->center() : box->min;
    runTransform(ids, commands::AffineTransform::scale_xy(pivot, sx, sy),
                 tr("Redimensionner la sélection"),
                 tr("Sélection redimensionnée (%1 % × %2 %).")
                     .arg(sx * 100.0, 0, 'f', 1)
                     .arg(sy * 100.0, 0, 'f', 1));
}

void MainWindow::resizeSelectionDialog() {
    const auto box = commands::objects_bounds(project_, transformTargets());
    if (!box) {
        return;
    }
    const double wMm = (static_cast<double>(box->max.x.value) - box->min.x.value) / 1000.0;
    const double hMm = (static_cast<double>(box->max.y.value) - box->min.y.value) / 1000.0;
    if (wMm <= 0.0 || hMm <= 0.0) {
        return;
    }
    QStringList modes{tr("Largeur en mm (proportions conservées)"), tr("Largeur en %")};
    bool ok = false;
    const QString mode = QInputDialog::getItem(
        this, tr("Redimensionner"),
        tr("Saisie de la taille (actuelle : %1 × %2 mm)").arg(wMm, 0, 'f', 2).arg(hMm, 0, 'f', 2),
        modes, 0, false, &ok);
    if (!ok) {
        return;
    }
    double factor = 1.0;
    if (mode == modes.at(0)) {
        const double mm = QInputDialog::getDouble(this, tr("Redimensionner"), tr("Largeur (mm)"),
                                                  wMm, 0.1, 5000.0, 2, &ok);
        factor = mm / wMm;
    } else {
        const double pct = QInputDialog::getDouble(this, tr("Redimensionner"), tr("Échelle (%)"),
                                                   100.0, 1.0, 10000.0, 1, &ok);
        factor = pct / 100.0;
    }
    if (ok) {
        resizeSelectionByFactors(factor, factor, true);
    }
}

void MainWindow::duplicateWithOffsetDialog() {
    bool ok = false;
    const double dx =
        QInputDialog::getDouble(this, tr("Dupliquer avec décalage"), tr("Décalage horizontal (mm)"),
                                duplicateOffset_.x.value / 1000.0, -500.0, 500.0, 2, &ok);
    if (!ok) {
        return;
    }
    const double dy = QInputDialog::getDouble(
        this, tr("Dupliquer avec décalage"), tr("Décalage vertical (mm, positif vers le haut)"),
        duplicateOffset_.y.value / 1000.0, -500.0, 500.0, 2, &ok);
    if (!ok) {
        return;
    }
    // Mémorisé : les Ctrl+D suivants reprennent le même décalage.
    duplicateOffset_ = Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(dx * 1000.0))},
                              Micrometers{static_cast<std::int32_t>(std::lround(dy * 1000.0))}};
    duplicateSelectionWithStitches();
}

void MainWindow::convertSelectionType(int kind) {
    const auto conversion = static_cast<commands::ConversionKind>(kind);
    std::vector<ObjectId> done;
    QString firstError;
    auto group =
        std::make_unique<commands::CompositeCommand>(tr("Convertir le type").toStdString());
    // Un seul objet par forme source : les sections satin soeurs sont gérées par la conversion.
    std::vector<ObjectId> seenSources;
    for (const ObjectId id : targetEmbroideryIds()) {
        const auto* emb = project_.findEmbroidery(id);
        if (emb == nullptr || !commands::conversion_applicable(*emb, conversion) ||
            std::find(seenSources.begin(), seenSources.end(), emb->source_vector) !=
                seenSources.end()) {
            continue;
        }
        seenSources.push_back(emb->source_vector);
        auto plan = commands::make_conversion_command(project_, id, conversion);
        if (!plan.command) {
            if (firstError.isEmpty()) {
                firstError = QString::fromStdString(plan.error);
            }
            continue;
        }
        group->add(std::move(plan.command));
        done.push_back(id);
    }
    if (done.empty()) {
        QMessageBox::warning(this, tr("Conversion impossible"),
                             firstError.isEmpty() ? tr("Aucun objet convertible dans la sélection.")
                                                  : firstError);
        return;
    }
    undoStack_.execute(std::move(group), project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("%1 objet(s) converti(s).").arg(done.size()), 6000);
}

} // namespace openstitch::desktop
