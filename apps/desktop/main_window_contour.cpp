// SPDX-License-Identifier: Apache-2.0
// Interface du remplissage concentrique / contour (HP-STI-007) : conversion d'un remplissage en
// anneaux concentriques. Aucune logique métier ici : les anneaux viennent du cœur
// (`stitch_generation::fill_contour`) et la mutation passe par une commande annulable.
#include <QStatusBar>

#include "main_window.hpp"
#include "openstitch/commands/project_commands.hpp"

namespace openstitch::desktop {

void MainWindow::convertToContourFill(ObjectId embroideryId) {
    const auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr || emb->is_contour_fill() || emb->is_satin() || emb->is_auto_satin()) {
        return;
    }
    document::ContourFillParams params;
    // Les réglages communs du remplissage actuel sont conservés (densité, longueur, retrait).
    if (const auto* tatami = std::get_if<document::TatamiParams>(&emb->params)) {
        params.row_spacing = tatami->row_spacing;
        params.stitch_length = tatami->stitch_length;
        params.inset = tatami->inset;
        params.hidden_underpath = tatami->hidden_underpath;
    } else if (const auto* dir = std::get_if<document::DirectionalFillParams>(&emb->params)) {
        params.row_spacing = dir->row_spacing;
        params.stitch_length = dir->stitch_length;
        params.inset = dir->inset;
        params.hidden_underpath = dir->hidden_underpath;
    }
    undoStack_.execute(
        std::make_unique<commands::ConvertFillGroupCommand>(
            embroideryId, document::StitchParams{params}, "Type : remplissage concentrique"),
        project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(tr("Remplissage concentrique : anneaux décalés depuis le bord."));
}

} // namespace openstitch::desktop
