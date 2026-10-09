// SPDX-License-Identifier: Apache-2.0
// Segmentation : sélection multiple de régions et édition par groupes (fusion, couleur,
// suppression). Membres de MainWindow séparés de main_window.cpp pour la lisibilité. Aucune
// logique métier ici : la carte des régions, les voisinages et les couleurs viennent de
// libs/segmentation, et toute mutation du document passe par une commande annulable (un seul
// pas d'annulation par geste, via CompositeCommand).
#include <QColorDialog>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>

#include <algorithm>
#include <memory>

#include "main_window.hpp"
#include "openstitch/commands/composite_command.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::desktop {

namespace {

QIcon swatch(const std::array<std::uint8_t, 3>& rgb) {
    QPixmap pm(16, 16);
    pm.fill(QColor(rgb[0], rgb[1], rgb[2]));
    return QIcon(pm);
}

bool contains(const std::vector<RegionId>& v, RegionId id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

} // namespace

std::vector<RegionId> MainWindow::selectedRegionIds() const {
    std::vector<RegionId> out = extraRegions_;
    if (selectedRegion_) {
        out.push_back(*selectedRegion_);
    }
    return out;
}

void MainWindow::announceRegionSelection() {
    if (!project_.segmentation) {
        return;
    }
    const std::vector<RegionId> ids = selectedRegionIds();
    if (ids.empty()) {
        return;
    }
    std::size_t pixels = 0;
    for (const RegionId id : ids) {
        if (const auto* region = project_.segmentation->find(id)) {
            pixels += region->pixel_count;
        }
    }
    const double mm2 =
        static_cast<double>(pixels) * project_.mm_per_px.value * project_.mm_per_px.value;
    if (ids.size() == 1) {
        const auto* region = project_.segmentation->find(ids.front());
        statusBar()->showMessage(tr("Région %1 — %2 px (%3 mm²) — RGB(%4, %5, %6)")
                                     .arg(region->id.value)
                                     .arg(region->pixel_count)
                                     .arg(mm2, 0, 'f', 1)
                                     .arg(region->rgb[0])
                                     .arg(region->rgb[1])
                                     .arg(region->rgb[2]));
    } else {
        statusBar()->showMessage(tr("%1 régions — %2 px (%3 mm²) — la dernière cliquée est active")
                                     .arg(ids.size())
                                     .arg(pixels)
                                     .arg(mm2, 0, 'f', 1));
    }
}

void MainWindow::selectRegions(const std::vector<RegionId>& ids, SelectMode mode) {
    if (!project_.segmentation) {
        return;
    }
    std::vector<RegionId> current = selectedRegionIds();
    const auto remove = [&current](RegionId id) {
        current.erase(std::remove(current.begin(), current.end(), id), current.end());
    };
    if (mode == SelectMode::Replace) {
        current.clear();
    }
    for (const RegionId id : ids) {
        if (project_.segmentation->find(id) == nullptr) {
            continue;
        }
        if (mode == SelectMode::Toggle && contains(current, id)) {
            remove(id);
        } else {
            remove(id); // déjà présente : elle redevient l'active (placée en dernier)
            current.push_back(id);
        }
    }
    std::optional<RegionId> active;
    std::vector<RegionId> others;
    if (!current.empty()) {
        active = current.back();
        others.assign(current.begin(), current.end() - 1);
    }
    setSelection(
        {.region = active, .embroidery = std::nullopt, .objects = {}, .extraRegions = others});
    announceRegionSelection();
    displayImage(processed_);
    updateActions();
}

void MainWindow::selectAllRegions() {
    if (!project_.segmentation) {
        return;
    }
    selectRegions(segmentation::all_regions(*project_.segmentation), SelectMode::Replace);
}

void MainWindow::selectRegionsWithSameColor() {
    if (!project_.segmentation || !selectedRegion_) {
        return;
    }
    const auto* active = project_.segmentation->find(*selectedRegion_);
    if (active == nullptr) {
        return;
    }
    std::vector<RegionId> same =
        segmentation::regions_with_color(*project_.segmentation, active->rgb);
    // L'active reste l'active : on la place en dernier.
    same.erase(std::remove(same.begin(), same.end(), *selectedRegion_), same.end());
    same.push_back(*selectedRegion_);
    selectRegions(same, SelectMode::Replace);
}

void MainWindow::selectNeighbourRegions() {
    if (!project_.segmentation || !selectedRegion_) {
        return;
    }
    std::vector<RegionId> add;
    for (const RegionId id : selectedRegionIds()) {
        for (const RegionId n : segmentation::neighbors_of(*project_.segmentation, id)) {
            if (!contains(add, n)) {
                add.push_back(n);
            }
        }
    }
    const std::vector<RegionId> current = selectedRegionIds();
    add.erase(std::remove_if(add.begin(), add.end(),
                             [&current](RegionId r) { return contains(current, r); }),
              add.end());
    if (add.empty()) {
        statusBar()->showMessage(tr("Aucune région voisine à ajouter."), 3000);
        return;
    }
    // Les voisines s'ajoutent SOUS l'active : elle garde son rôle.
    std::vector<RegionId> ordered = add;
    ordered.push_back(*selectedRegion_);
    selectRegions(ordered, SelectMode::Add);
}

void MainWindow::mergeSelectedRegions() {
    if (!project_.segmentation) {
        return;
    }
    const std::vector<RegionId> ids = selectedRegionIds();
    if (ids.size() < 2) {
        statusBar()->showMessage(
            tr("Sélectionnez au moins deux régions (Ctrl+clic ou Maj+clic) pour les fusionner."),
            4000);
        return;
    }
    const RegionId keep = ids.back();
    auto group = std::make_unique<commands::CompositeCommand>(
        tr("Fusionner %1 régions").arg(ids.size()).toStdString());
    for (const RegionId id : ids) {
        if (id != keep) {
            group->add(std::make_unique<commands::MergeRegionsCommand>(keep, id));
        }
    }
    undoStack_.execute(std::move(group), project_);
    setSelection({.region = keep, .embroidery = std::nullopt, .objects = {}, .extraRegions = {}});
    statusBar()->showMessage(
        tr("%1 régions fusionnées dans la région %2.").arg(ids.size()).arg(keep.value));
    refreshImage();
    updateActions();
}

void MainWindow::absorbSelectedRegionIntoNeighbour() {
    if (!project_.segmentation || !selectedRegion_ || !extraRegions_.empty()) {
        return;
    }
    const RegionId id = *selectedRegion_;
    const auto neighbours = segmentation::neighbors_of(*project_.segmentation, id);
    if (neighbours.empty()) {
        statusBar()->showMessage(tr("Cette région n'a aucune voisine : rien à fusionner."), 4000);
        return;
    }
    const RegionId target = neighbours.front(); // la plus longue frontière commune
    undoStack_.execute(std::make_unique<commands::MergeRegionsCommand>(target, id), project_);
    setSelection({.region = target, .embroidery = std::nullopt, .objects = {}, .extraRegions = {}});
    statusBar()->showMessage(tr("Région %1 fusionnée dans sa voisine principale (région %2).")
                                 .arg(id.value)
                                 .arg(target.value));
    refreshImage();
    updateActions();
}

void MainWindow::recolorRegions(const std::vector<RegionId>& ids, std::array<std::uint8_t, 3> rgb) {
    if (!project_.segmentation || ids.empty()) {
        return;
    }
    std::vector<RegionId> todo;
    for (const RegionId id : ids) {
        const auto* region = project_.segmentation->find(id);
        if (region != nullptr && region->rgb != rgb) {
            todo.push_back(id);
        }
    }
    if (todo.empty()) {
        return;
    }
    if (todo.size() == 1) {
        undoStack_.execute(std::make_unique<commands::RecolorRegionCommand>(todo.front(), rgb),
                           project_);
    } else {
        auto group = std::make_unique<commands::CompositeCommand>(
            tr("Recolorer %1 régions").arg(todo.size()).toStdString());
        for (const RegionId id : todo) {
            group->add(std::make_unique<commands::RecolorRegionCommand>(id, rgb));
        }
        undoStack_.execute(std::move(group), project_);
    }
    refreshImage();
    updateActions();
}

void MainWindow::restoreSelectedRegionColors() {
    if (!project_.segmentation || !selectedRegion_) {
        return;
    }
    const std::vector<RegionId> ids = selectedRegionIds();
    auto group = std::make_unique<commands::CompositeCommand>(
        tr("Rétablir la couleur d'origine").toStdString());
    std::size_t changed = 0;
    for (const RegionId id : ids) {
        const auto mean = segmentation::region_mean_color(*project_.segmentation, processed_, id);
        if (!mean) {
            statusBar()->showMessage(
                tr("Couleur d'origine indisponible : l'image n'est plus celle qui a été "
                   "segmentée."),
                5000);
            return;
        }
        const auto* region = project_.segmentation->find(id);
        if (region != nullptr && region->rgb != *mean) {
            group->add(std::make_unique<commands::RecolorRegionCommand>(id, *mean));
            ++changed;
        }
    }
    if (changed == 0) {
        statusBar()->showMessage(tr("Les couleurs sont déjà celles de l'image."), 3000);
        return;
    }
    undoStack_.execute(std::move(group), project_);
    refreshImage();
    updateActions();
}

void MainWindow::deleteSelectedRegions() {
    if (!project_.segmentation || !selectedRegion_) {
        return;
    }
    const std::vector<RegionId> ids = selectedRegionIds();
    if (ids.size() == 1) {
        undoStack_.execute(std::make_unique<commands::RemoveRegionCommand>(ids.front()), project_);
    } else {
        auto group = std::make_unique<commands::CompositeCommand>(
            tr("Supprimer %1 régions").arg(ids.size()).toStdString());
        for (const RegionId id : ids) {
            group->add(std::make_unique<commands::RemoveRegionCommand>(id));
        }
        undoStack_.execute(std::move(group), project_);
    }
    setSelection(
        {.region = std::nullopt, .embroidery = std::nullopt, .objects = {}, .extraRegions = {}});
    refreshImage();
    updateActions();
}

void MainWindow::showRegionContextMenu(RegionId clicked, QPoint globalPos) {
    if (!project_.segmentation) {
        return;
    }
    // Clic droit hors de la sélection : elle devient la région cliquée. Sur un membre de la
    // sélection : on la garde (le menu vaut pour tout l'ensemble).
    if (!contains(selectedRegionIds(), clicked)) {
        selectRegions({clicked}, SelectMode::Replace);
    }
    const std::vector<RegionId> ids = selectedRegionIds();
    QMenu menu(this);
    auto* title = menu.addAction(ids.size() == 1 ? tr("Région %1").arg(ids.front().value)
                                                 : tr("%1 régions").arg(ids.size()));
    title->setEnabled(false);
    menu.addSeparator();

    if (ids.size() >= 2) {
        menu.addAction(mergeSelectionAct_);
    } else if (ids.size() == 1) {
        const auto neighbours = segmentation::neighbors_of(*project_.segmentation, ids.front());
        if (!neighbours.empty()) {
            auto* into = menu.addMenu(tr("Fusionner &dans…"));
            // Du voisin le plus proche (frontière la plus longue) au plus lointain.
            for (const RegionId n : neighbours) {
                const auto* region = project_.segmentation->find(n);
                if (region == nullptr) {
                    continue;
                }
                auto* act =
                    into->addAction(swatch(region->rgb),
                                    tr("Région %1 (%2 px)").arg(n.value).arg(region->pixel_count));
                const RegionId source = ids.front();
                connect(act, &QAction::triggered, this, [this, n, source] {
                    undoStack_.execute(std::make_unique<commands::MergeRegionsCommand>(n, source),
                                       project_);
                    setSelection({.region = n,
                                  .embroidery = std::nullopt,
                                  .objects = {},
                                  .extraRegions = {}});
                    refreshImage();
                    updateActions();
                });
            }
        }
    }
    auto* recolor = menu.addAction(tr("&Recolorer…"));
    connect(recolor, &QAction::triggered, this, &MainWindow::recolorSelectedRegion);
    menu.addAction(restoreColorAct_);
    menu.addSeparator();
    menu.addAction(selectSameColorAct_);
    menu.addAction(selectNeighboursAct_);
    menu.addAction(selectAllRegionsAct_);
    menu.addSeparator();
    menu.addAction(vectorizeRegionAct_);
    auto* del = menu.addAction(tr("&Supprimer"));
    connect(del, &QAction::triggered, this, &MainWindow::deleteSelectedRegions);
    menu.exec(globalPos);
}

} // namespace openstitch::desktop
