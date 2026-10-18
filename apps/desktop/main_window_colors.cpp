// SPDX-License-Identifier: Apache-2.0
// Segmentation : fusion de COULEURS (panneau « Couleurs »). Membres de MainWindow séparés de
// main_window.cpp. Aucune logique métier ici : les teintes, les plans de fusion et leur application
// viennent de libs/segmentation (`color_merge.hpp`) ; le document n'est modifié que par une
// commande annulable (un seul pas d'annulation), jamais par l'aperçu, qui travaille sur une copie.
#include <QDockWidget>
#include <QStatusBar>

#include <algorithm>
#include <memory>

#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "responsive.hpp"
#include "segmentation_colors_panel.hpp"

namespace openstitch::desktop {

void MainWindow::showColorsPanel() {
    if (colorsDock_ == nullptr) {
        colorsDock_ = new QDockWidget(tr("Couleurs"), this);
        colorsDock_->setObjectName(QStringLiteral("colorsDock"));
        colorsDock_->setAccessibleName(colorsDock_->windowTitle());
        colorsPanel_ = new SegmentationColorsPanel(colorsDock_);
        colorsDock_->setWidget(scrollableDockContent(colorsPanel_, colorsDock_));
        addDockWidget(Qt::RightDockWidgetArea, colorsDock_);

        connect(colorsPanel_, &SegmentationColorsPanel::colorsSelected, this,
                &MainWindow::selectRegionsOfColors);
        connect(colorsPanel_, &SegmentationColorsPanel::mergeRequested, this,
                [this](const std::vector<SegmentationColorsPanel::Rgb>& sources,
                       SegmentationColorsPanel::Rgb target, bool adjacent) {
                    if (project_.segmentation) {
                        applyColorMergePlan(segmentation::plan_merge_colors(
                            *project_.segmentation, sources, target, adjacent));
                    }
                });
        connect(colorsPanel_, &SegmentationColorsPanel::reductionPreviewRequested, this,
                [this](int maxColors, double maxDistance, bool adjacent) {
                    if (project_.segmentation) {
                        previewColorMerge(segmentation::plan_reduce_colors(
                            *project_.segmentation, static_cast<std::size_t>(maxColors),
                            maxDistance, adjacent));
                    }
                });
        connect(colorsPanel_, &SegmentationColorsPanel::reductionPreviewCleared, this,
                [this] { clearSegmentationPreview(true); });
        connect(colorsPanel_, &SegmentationColorsPanel::reductionApplyRequested, this,
                [this](int maxColors, double maxDistance, bool adjacent) {
                    if (project_.segmentation) {
                        applyColorMergePlan(segmentation::plan_reduce_colors(
                            *project_.segmentation, static_cast<std::size_t>(maxColors),
                            maxDistance, adjacent));
                    }
                });
    }
    colorsDock_->show();
    colorsDock_->raise();
    refreshColorsPanel();
}

void MainWindow::refreshColorsPanel() {
    // Tout rafraîchissement vient d'un changement du document (commande, undo, chargement) : un
    // aperçu calculé sur l'ancienne segmentation est caduc. La carte est redessinée par l'appelant.
    previewSegmentation_.reset();
    if (colorsPanel_ == nullptr) {
        return;
    }
    if (!project_.segmentation) {
        colorsPanel_->setClasses({}, 0);
        return;
    }
    std::vector<segmentation::ColorClass> classes =
        segmentation::color_classes(*project_.segmentation);
    std::size_t total = 0;
    for (const segmentation::ColorClass& cls : classes) {
        total += cls.pixel_count;
    }
    colorsPanel_->setClasses(std::move(classes), total);
}

void MainWindow::clearSegmentationPreview(bool redisplay) {
    if (!previewSegmentation_) {
        return;
    }
    previewSegmentation_.reset();
    if (redisplay) {
        displayImage(processed_);
        statusBar()->clearMessage();
    }
}

void MainWindow::previewColorMerge(const segmentation::ColorMergePlan& plan) {
    if (!project_.segmentation) {
        return;
    }
    if (plan.empty()) {
        clearSegmentationPreview(true);
        statusBar()->showMessage(tr("Rien à fusionner avec ces réglages."), 3000);
        return;
    }
    segmentation::Segmentation copy = *project_.segmentation;
    if (!segmentation::apply_color_merge(copy, plan)) {
        clearSegmentationPreview(true);
        return;
    }
    const std::size_t regions = copy.region_count();
    previewSegmentation_ = std::move(copy);
    if (!showSegAct_->isChecked()) {
        showSegAct_->setChecked(true); // redessine la carte (signal toggled)
    } else {
        displayImage(processed_);
    }
    statusBar()->showMessage(
        tr("Aperçu : %1 → %2 couleurs, %n région(s) — « Appliquer » pour valider, « Annuler "
           "l'aperçu » pour revenir.",
           "", static_cast<int>(regions))
            .arg(plan.colors_before)
            .arg(plan.colors_after));
}

void MainWindow::applyColorMergePlan(const segmentation::ColorMergePlan& plan) {
    if (!project_.segmentation) {
        return;
    }
    if (plan.empty()) {
        statusBar()->showMessage(tr("Rien à fusionner avec ces réglages."), 3000);
        return;
    }
    // Les objets vectoriels créés depuis une région absorbée ne suivent pas la fusion : même choix
    // que pour « Fusionner » (les conserver ou les supprimer, ou annuler).
    std::vector<RegionId> absorbed;
    for (const auto& merge : plan.merges) {
        absorbed.push_back(merge.second);
    }
    std::vector<ObjectId> toRemove;
    if (!resolveLinkedVectorObjects(absorbed, tr("fusionnées"), toRemove)) {
        return;
    }
    auto group =
        std::make_unique<commands::CompositeCommand>(tr("Fusionner des couleurs").toStdString());
    for (const ObjectId object : toRemove) {
        group->add(std::make_unique<commands::RemoveVectorObjectCommand>(object));
    }
    group->add(std::make_unique<commands::MergeColorsCommand>(plan));
    previewSegmentation_.reset();
    undoStack_.execute(std::move(group), project_);
    refreshImage();
    updateActions();
    statusBar()->showMessage(
        tr("Couleurs fusionnées : %1 → %2.").arg(plan.colors_before).arg(plan.colors_after));
}

void MainWindow::selectRegionsOfColors(const std::vector<std::array<std::uint8_t, 3>>& colors) {
    if (!project_.segmentation) {
        return;
    }
    std::vector<RegionId> ids;
    for (const auto& rgb : colors) {
        const std::vector<RegionId> same =
            segmentation::regions_with_color(*project_.segmentation, rgb);
        ids.insert(ids.end(), same.begin(), same.end());
    }
    selectRegions(ids, SelectMode::Replace);
}

} // namespace openstitch::desktop
