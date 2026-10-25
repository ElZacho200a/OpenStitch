// SPDX-License-Identifier: Apache-2.0
// Édition de formes vectorielles : insertion d'un nœud par double-clic (HP-VEC-001), suppression
// d'UN trou par Alt + double-clic (HP-VEC-005), contour de broderie autour de la sélection
// (HP-VEC-006). Aucune géométrie ici : tout est calculé par libs/commands (libs/geometry) et
// appliqué en UN pas d'annulation.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QSettings>
#include <QStatusBar>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "openstitch/commands/outline_ops.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "openstitch/geometry/reshape.hpp"

namespace openstitch::desktop {

namespace {

Vec2um sceneToModelUm(QPointF sceneMm) {
    return Vec2um{to_micrometers(Millimeters{sceneMm.x()}),
                  to_micrometers(Millimeters{-sceneMm.y()})};
}

// Distance d'accroche au contour, en pixels écran.
constexpr double kInsertGrabPx = 8.0;

Micrometers mmUm(double mm) {
    return Micrometers{static_cast<std::int32_t>(std::lround(mm * 1000.0))};
}

} // namespace

void MainWindow::buildVectorEditActions() {
    if (shapeMenu_ == nullptr) {
        return;
    }
    outlineAct_ = shapeMenu_->addAction(tr("&Contour autour de la sélection…"));
    outlineAct_->setObjectName(QStringLiteral("action_shapeOutline"));
    outlineAct_->setToolTip(tr("Crée un contour de broderie (point droit, triple ou satin) autour "
                               "des formes sélectionnées, à l'écart choisi. Les formes d'origine "
                               "ne changent pas."));
    connect(outlineAct_, &QAction::triggered, this, [this] { outlineSelectionDialog(); });

    // Magnétisme à la grille (HP-VEC-009), réglages persistés.
    QSettings settings;
    gridStepMm_ =
        std::clamp(settings.value(QStringLiteral("edit/gridStepMm"), 1.0).toDouble(), 0.1, 100.0);
    shapeMenu_->addSeparator();
    gridSnapAct_ = shapeMenu_->addAction(tr("Accrocher à la &grille"));
    gridSnapAct_->setObjectName(QStringLiteral("action_gridSnap"));
    gridSnapAct_->setCheckable(true);
    gridSnapAct_->setChecked(settings.value(QStringLiteral("edit/gridSnap"), false).toBool());
    gridSnapAct_->setToolTip(tr("Pendant le tracé, le curseur s'accroche aux points de la grille "
                                "quand aucun sommet n'est assez proche (Ctrl suspend)."));
    connect(gridSnapAct_, &QAction::toggled, this,
            [](bool on) { QSettings().setValue(QStringLiteral("edit/gridSnap"), on); });
    gridStepAct_ = shapeMenu_->addAction(tr("Pas de la grille…"));
    gridStepAct_->setObjectName(QStringLiteral("action_gridStep"));
    connect(gridStepAct_, &QAction::triggered, this, [this] {
        bool ok = false;
        const double mm =
            QInputDialog::getDouble(this, tr("Pas de la grille"), tr("Pas de la grille (mm) :"),
                                    gridStepMm_, 0.1, 100.0, 2, &ok);
        if (ok) {
            gridStepMm_ = mm;
            QSettings().setValue(QStringLiteral("edit/gridStepMm"), mm);
        }
    });
}

std::optional<QPointF> MainWindow::gridSnapPointMm(QPointF cursorSceneMm, double radiusMm) const {
    if (gridSnapAct_ == nullptr || !gridSnapAct_->isChecked() || snapSuspended() ||
        gridStepMm_ <= 0.0) {
        return std::nullopt;
    }
    const QPointF snapped(std::round(cursorSceneMm.x() / gridStepMm_) * gridStepMm_,
                          std::round(cursorSceneMm.y() / gridStepMm_) * gridStepMm_);
    const double dx = snapped.x() - cursorSceneMm.x();
    const double dy = snapped.y() - cursorSceneMm.y();
    if (dx * dx + dy * dy > radiusMm * radiusMm) {
        return std::nullopt;
    }
    return snapped;
}

void MainWindow::updateVectorEditActions() {
    if (outlineAct_ == nullptr) {
        return;
    }
    const bool any = !selectedObjectIds().empty();
    outlineAct_->setEnabled(any);
    outlineAct_->setStatusTip(any ? QString() : tr("Sélectionnez au moins une forme."));
}

bool MainWindow::insertNodeAt(QPointF sceneMm) {
    if (!selectedObject_ || hasMultiSelection()) {
        return false;
    }
    const auto* object = project_.findObject(*selectedObject_);
    if (object == nullptr) {
        return false;
    }
    const double pxPerMm = view_ != nullptr ? std::max(view_->pixelsPerMm(), 0.01) : 1.0;
    const double maxUm = kInsertGrabPx / pxPerMm * 1000.0;
    const Vec2um at = sceneToModelUm(sceneMm);
    struct Best {
        std::size_t set, path, segment;
        double t, dist;
    };
    std::optional<Best> best;
    for (std::size_t s = 0; s < object->paths.size(); ++s) {
        const auto& set = object->paths[s];
        for (std::size_t p = 0; p <= set.holes.size(); ++p) {
            const geometry::Path& path = p == 0 ? set.outer : set.holes[p - 1];
            const auto hit = geometry::nearest_segment(path, at, maxUm);
            if (hit && hit->distance_um <= maxUm && (!best || hit->distance_um < best->dist)) {
                best = Best{s, p, hit->segment, hit->t, hit->distance_um};
            }
        }
    }
    if (!best) {
        return false;
    }
    auto result = commands::make_insert_node_command(project_, *selectedObject_, best->set,
                                                     best->path, best->segment, best->t);
    if (!result.command) {
        statusBar()->showMessage(QString::fromStdString(result.error), 4000);
        return true; // le double-clic visait bien le contour
    }
    undoStack_.execute(std::move(result.command), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Nœud inséré sur le segment."), 3000);
    return true;
}

bool MainWindow::removeHoleAt(QPointF sceneMm) {
    if (!selectedObject_ || hasMultiSelection()) {
        return false;
    }
    auto result =
        commands::make_remove_hole_at_command(project_, *selectedObject_, sceneToModelUm(sceneMm));
    if (!result.command) {
        return false;
    }
    undoStack_.execute(std::move(result.command), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Trou supprimé."), 3000);
    return true;
}

void MainWindow::outlineSelection(int kind, double gapMm, double satinWidthMm, bool aroundHoles) {
    commands::OutlineOptions options;
    options.gap = mmUm(gapMm);
    options.kind = static_cast<commands::OutlineKind>(kind);
    options.satin_width = mmUm(satinWidthMm);
    options.around_holes = aroundHoles;
    auto result = commands::make_outline_command(project_, selectedObjectIds(), options);
    if (!result.command) {
        statusBar()->showMessage(QString::fromStdString(result.error), 8000);
        return;
    }
    undoStack_.execute(std::move(result.command), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Contour créé. Ctrl+Z pour annuler."), 5000);
}

void MainWindow::outlineSelectionDialog() {
    if (selectedObjectIds().empty()) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Contour autour de la sélection"));
    auto* form = new QFormLayout(&dialog);
    auto* gap = new QDoubleSpinBox(&dialog);
    gap->setRange(0.0, 50.0);
    gap->setDecimals(2);
    gap->setSuffix(tr(" mm"));
    gap->setValue(1.0);
    auto* kindBox = new QComboBox(&dialog);
    kindBox->addItem(tr("Point droit"), static_cast<int>(commands::OutlineKind::Running));
    kindBox->addItem(tr("Point triple"), static_cast<int>(commands::OutlineKind::Triple));
    kindBox->addItem(tr("Satin (anneau)"), static_cast<int>(commands::OutlineKind::Satin));
    auto* width = new QDoubleSpinBox(&dialog);
    width->setRange(0.5, 20.0);
    width->setDecimals(2);
    width->setSuffix(tr(" mm"));
    width->setValue(2.5);
    auto* holes = new QCheckBox(tr("Contourner aussi les trous"), &dialog);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(tr("Écart"), gap);
    form->addRow(tr("Type"), kindBox);
    form->addRow(tr("Largeur du satin"), width);
    form->addRow(holes);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    outlineSelection(kindBox->currentData().toInt(), gap->value(), width->value(),
                     holes->isChecked());
}

} // namespace openstitch::desktop
