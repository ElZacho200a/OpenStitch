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

void MainWindow::convertToFillKind(ObjectId embroideryId, int kind) {
    const auto* emb = project_.findEmbroidery(embroideryId);
    if (emb == nullptr || kind < 0 || kind > 2 || emb->is_satin() || emb->is_auto_satin()) {
        return;
    }
    // Réglages communs repris du remplissage actuel (tatami ou directionnel).
    Micrometers spacing{400};
    Micrometers length{3'000};
    Micrometers inset{200};
    bool underpath = true;
    Angle angle{0.0};
    if (const auto* tatami = std::get_if<document::TatamiParams>(&emb->params)) {
        spacing = tatami->row_spacing;
        length = tatami->stitch_length;
        inset = tatami->inset;
        underpath = tatami->hidden_underpath;
        angle = tatami->angle;
    } else if (const auto* dir = std::get_if<document::DirectionalFillParams>(&emb->params)) {
        spacing = dir->row_spacing;
        length = dir->stitch_length;
        inset = dir->inset;
        underpath = dir->hidden_underpath;
    }
    document::StitchParams params;
    QString name;
    if (kind == 0) {
        if (emb->is_radial_fill()) {
            return;
        }
        document::RadialFillParams p;
        p.row_spacing = spacing;
        p.stitch_length = length;
        p.inset = inset;
        p.hidden_underpath = underpath;
        params = p;
        name = tr("Type : remplissage radial");
    } else if (kind == 1) {
        if (emb->is_spiral_fill()) {
            return;
        }
        document::SpiralFillParams p;
        p.row_spacing = spacing;
        p.stitch_length = length;
        p.inset = inset;
        p.hidden_underpath = underpath;
        params = p;
        name = tr("Type : remplissage en spirale");
    } else {
        if (emb->is_crosshatch_fill()) {
            return;
        }
        document::CrosshatchFillParams p;
        // Très espacé : au moins 3 fois l'écart du remplissage plein.
        p.row_spacing = Micrometers{std::max(1'500, spacing.value * 3)};
        p.stitch_length = length;
        p.inset = inset;
        p.hidden_underpath = underpath;
        p.angle = angle;
        params = p;
        name = tr("Type : remplissage croisé");
    }
    undoStack_.execute(std::make_unique<commands::ConvertFillGroupCommand>(embroideryId, params,
                                                                           name.toStdString()),
                       project_);
    showStitchesAct_->setChecked(true);
    refreshImage();
    updateActions();
    statusBar()->showMessage(name);
}

} // namespace openstitch::desktop
