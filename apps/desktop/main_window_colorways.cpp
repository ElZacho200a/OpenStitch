// SPDX-License-Identifier: Apache-2.0
// Coloris multiples (HP-THR-006), tissu de fond (HP-THR-008), réduire / fusionner les fils
// (HP-THR-011) : câblage UNIQUEMENT. Chaque action est une ICommand annulable ; les plans de
// fusion viennent de libs/stitch_analysis.
#include <QAction>
#include <QColor>
#include <QMenu>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>

#include "canvas_view.hpp"
#include "colorway_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/commands/colorway_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/stitch_analysis/thread_reduction.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "thread_merge_dialog.hpp"

namespace openstitch::desktop {

void MainWindow::buildColorwayMenu(QMenu* viewMenu) {
    viewMenu->addSeparator();
    colorwayAct_ = viewMenu->addAction(tr("Coloris et tissu…"));
    colorwayAct_->setObjectName(QStringLiteral("action_colorways"));
    colorwayAct_->setToolTip(
        tr("Plusieurs jeux de fils pour le même dessin, et couleur du tissu."));
    connect(colorwayAct_, &QAction::triggered, this, &MainWindow::showColorwayDialog);
    threadMergeAct_ = viewMenu->addAction(tr("Réduire / fusionner les fils…"));
    threadMergeAct_->setObjectName(QStringLiteral("action_threadMerge"));
    threadMergeAct_->setToolTip(
        tr("Réduire le design à N fils ou fusionner deux fils, avec aperçu."));
    connect(threadMergeAct_, &QAction::triggered, this, &MainWindow::showThreadMergeDialog);
    viewMenu->addSeparator();
}

void MainWindow::showColorwayDialog() {
    if (colorwayDialog_ == nullptr) {
        colorwayDialog_ = new ColorwayDialog(this);
        connect(colorwayDialog_, &ColorwayDialog::addRequested, this, [this](const QString& name) {
            undoStack_.execute(std::make_unique<commands::AddColorwayCommand>(
                                   name.toStdString(), tr("Original").toStdString()),
                               project_);
            refreshImage();
            updateActions();
        });
        connect(colorwayDialog_, &ColorwayDialog::activateRequested, this, [this](int index) {
            undoStack_.execute(std::make_unique<commands::SetActiveColorwayCommand>(index),
                               project_);
            refreshImage();
            updateActions();
        });
        connect(colorwayDialog_, &ColorwayDialog::removeRequested, this, [this](int index) {
            undoStack_.execute(std::make_unique<commands::RemoveColorwayCommand>(index), project_);
            updateActions();
        });
        connect(colorwayDialog_, &ColorwayDialog::renameRequested, this,
                [this](int index, const QString& name) {
                    // Différé : le dialogue se reconstruit pendant son propre signal itemChanged.
                    QTimer::singleShot(0, this, [this, index, name] {
                        if (name.isEmpty()) {
                            updateActions();
                            return;
                        }
                        undoStack_.execute(std::make_unique<commands::RenameColorwayCommand>(
                                               index, name.toStdString()),
                                           project_);
                        updateActions();
                    });
                });
        connect(colorwayDialog_, &ColorwayDialog::fabricChangeRequested, this,
                [this](const QColor& color, const QString& name) {
                    document::FabricSettings fabric;
                    fabric.color = {static_cast<std::uint8_t>(color.red()),
                                    static_cast<std::uint8_t>(color.green()),
                                    static_cast<std::uint8_t>(color.blue())};
                    fabric.name = name.toStdString();
                    if (fabric == project_.fabric) {
                        return;
                    }
                    undoStack_.execute(std::make_unique<commands::SetFabricCommand>(fabric),
                                       project_);
                    refreshImage();
                    updateActions();
                });
    }
    syncColorwayUi();
    colorwayDialog_->show();
    colorwayDialog_->raise();
}

void MainWindow::syncColorwayUi() {
    const auto& f = project_.fabric;
    const QColor fabric(f.color[0], f.color[1], f.color[2]);
    if (view_ != nullptr) {
        view_->setFabricColor(fabric);
    }
    if (colorwayDialog_ != nullptr && colorwayDialog_->isVisible()) {
        QStringList names;
        for (const auto& colorway : project_.colorways) {
            names << QString::fromStdString(colorway.name);
        }
        colorwayDialog_->setState(names, project_.active_colorway, fabric,
                                  QString::fromStdString(f.name));
    }
}

void MainWindow::showThreadMergeDialog() {
    flushPendingRefresh();
    if (!sequence_) {
        statusBar()->showMessage(tr("Générez d'abord des points de broderie."), 5000);
        return;
    }
    auto usage = stitch_analysis::thread_usage(project_, *sequence_, &threadLibrary_);
    if (usage.size() < 2) {
        statusBar()->showMessage(tr("Le motif n'utilise qu'un fil : rien à fusionner."), 5000);
        return;
    }
    ThreadMergeDialog dialog(std::move(usage), this);
    if (dialog.exec() != QDialog::Accepted || dialog.plan().empty()) {
        return;
    }
    std::vector<commands::ObjectThreadAssignment> plan;
    for (const auto& r : dialog.plan().reassignments) {
        plan.push_back({r.object, r.thread, r.rgb});
    }
    const auto before = dialog.plan().before;
    const auto after = dialog.plan().after;
    undoStack_.execute(std::make_unique<commands::SetObjectThreadCommand>(
                           std::move(plan), tr("Fusionner des fils").toStdString()),
                       project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Fils fusionnés : %1 → %2 — Ctrl+Z pour annuler.").arg(before).arg(after), 6000);
}

} // namespace openstitch::desktop
