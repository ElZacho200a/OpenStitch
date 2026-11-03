// SPDX-License-Identifier: Apache-2.0
// Affichage > Machine… (HP-HOOP-002) : choix du profil machine du projet. Le dialogue
// (machine_dialog.cpp) ne fait que choisir ; la mutation passe par des ICommand (un seul pas
// d'annulation pour machine + cadre). Les vérifications viennent de
// `stitch_analysis::check_machine` (panneau Analyse, résumé d'export).
#include <QStatusBar>

#include <memory>

#include "machine_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/machine_commands.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/document/hoop_library.hpp"

namespace openstitch::desktop {

void MainWindow::chooseMachine() {
    MachineDialog dialog(QString::fromStdString(project_.machine_id), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    applyMachineChoice(dialog.machineId(), dialog.hoopId());
}

void MainWindow::applyMachineChoice(const QString& machineId, const QString& hoopId) {
    auto composite = std::make_unique<commands::CompositeCommand>(
        machineId.isEmpty() ? "Retirer la machine" : "Choisir la machine");
    if (machineId.toStdString() != project_.machine_id) {
        composite->add(std::make_unique<commands::SetMachineCommand>(machineId.toStdString()));
    }
    if (!hoopId.isEmpty()) {
        if (const auto* hoop = document::find_hoop(hoopId.toStdString())) {
            const document::Canvas canvas =
                document::canvas_from_hoop(*hoop, project_.canvas.margin);
            if (!(canvas == project_.canvas)) {
                composite->add(std::make_unique<commands::SetCanvasCommand>(canvas));
            }
        }
    }
    if (composite->empty()) {
        return;
    }
    undoStack_.execute(std::move(composite), project_);
    applyCanvasToView();
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        machineId.isEmpty()
            ? tr("Aucune machine : plus de vérification liée à la machine.")
            : tr("Machine : %1")
                  .arg(MachineDialog::describe(machineId).section(QLatin1Char('\n'), 0, 0)));
}

} // namespace openstitch::desktop
