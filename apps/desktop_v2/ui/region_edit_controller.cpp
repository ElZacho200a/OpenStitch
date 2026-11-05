// SPDX-License-Identifier: Apache-2.0
#include "region_edit_controller.hpp"

#include <QColorDialog>
#include <QImage>
#include <QRectF>

#include <algorithm>
#include <cstring>

#include "preview_canvas.hpp"
#include "region_panel.hpp"

namespace openstitch::desktop_v2 {

namespace {
QString fromUtf8(const std::string& s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
} // namespace

RegionEditController::RegionEditController(Host host, RegionPanel* panel, QObject* parent)
    : QObject(parent), host_(std::move(host)), panel_(panel) {
    connect(panel_, &RegionPanel::regionChosen, this, [this](std::uint64_t id) {
        if (id != selected_) {
            selectRegion(id);
        }
    });
    connect(panel_, &RegionPanel::deleteRequested, this, [this] { (void)deleteSelected(); });
    connect(panel_, &RegionPanel::mergeRequested, this,
            [this](std::uint64_t target) { (void)mergeSelectedInto(target); });
    connect(panel_, &RegionPanel::cleanRequested, this, [this](int px) { (void)cleanIslands(px); });
    connect(panel_, &RegionPanel::recolorRequested, this, [this] {
        if (selected_ == 0) {
            return;
        }
        const auto regions = listRegions(host_.session.project());
        const auto it = std::find_if(regions.begin(), regions.end(),
                                     [this](const RegionInfo& r) { return r.id == selected_; });
        if (it == regions.end()) {
            return;
        }
        const QColor initial(it->rgb[0], it->rgb[1], it->rgb[2]);
        const auto chosen = QColorDialog::getColor(initial, panel_, tr("Couleur de la région"));
        if (chosen.isValid()) {
            (void)recolorSelected({static_cast<std::uint8_t>(chosen.red()),
                                   static_cast<std::uint8_t>(chosen.green()),
                                   static_cast<std::uint8_t>(chosen.blue())});
        }
    });
    connect(panel_, &RegionPanel::mergeColorsRequested, this,
            [this](std::uint64_t target) { (void)mergeSelectedColorInto(target); });
    connect(panel_, &RegionPanel::fillHolesRequested, this,
            [this](int px) { (void)fillHoles(px); });
    connect(panel_, &RegionPanel::regenerateRequested, this, [this] { regenerate(); });
}

bool RegionEditController::active() const {
    return host_.activeTool() == QStringLiteral("region.edit");
}

void RegionEditController::refresh() {
    const auto& project = host_.session.project();
    const auto regions = listRegions(project);
    if (regions.empty()) {
        selected_ = 0;
        panel_->setRegions({}, 0);
        panel_->setNeighbours({});
        panel_->setColorTargets({});
        panel_->setMessage(QObject::tr("Ce projet n'a pas de segmentation : numérisez une image "
                                       "pour corriger ses régions."));
        host_.canvas.setRegionOverlay(QImage(), QRectF());
        return;
    }
    if (std::none_of(regions.begin(), regions.end(),
                     [&](const RegionInfo& r) { return r.id == selected_; })) {
        selected_ = 0; // la région choisie a disparu (fusionnée, supprimée, annulation…)
    }
    panel_->setRegions(regions, selected_);
    std::vector<RegionInfo> neighbours;
    if (selected_ != 0) {
        for (const auto id : neighbourRegions(project, selected_)) {
            const auto it = std::find_if(regions.begin(), regions.end(),
                                         [&](const RegionInfo& r) { return r.id == id; });
            if (it != regions.end()) {
                neighbours.push_back(*it);
            }
        }
    }
    panel_->setNeighbours(neighbours);
    std::vector<RegionInfo> otherColors;
    if (selected_ != 0) {
        const auto isSelected = [this](const RegionInfo& r) { return r.id == selected_; };
        const auto selected = std::find_if(regions.begin(), regions.end(), isSelected);
        if (selected != regions.end()) {
            std::vector<std::array<std::uint8_t, 3>> seen;
            for (const auto& r : regions) {
                if (r.rgb != selected->rgb &&
                    std::find(seen.begin(), seen.end(), r.rgb) == seen.end()) {
                    seen.push_back(r.rgb);
                    otherColors.push_back(r);
                }
            }
        }
    }
    panel_->setColorTargets(otherColors);
    panel_->setDirty(dirty_);
    if (active()) {
        panel_->setMessage(selected_ == 0
                               ? QObject::tr("%n région(s). Cliquez une région pour la corriger.",
                                             "", static_cast<int>(regions.size()))
                               : QObject::tr("Région %1 choisie : supprimez-la, fusionnez-la dans "
                                             "une voisine, ou nettoyez les îlots.")
                                     .arg(selected_));
    }
    showOverlay();
}

void RegionEditController::showOverlay() {
    if (!active() || host_.frame.px_per_mm <= 0.0) {
        host_.canvas.setRegionOverlay(QImage(), QRectF());
        return;
    }
    const auto map =
        renderRegionMap(host_.session.project(),
                        selected_ != 0 ? std::optional<std::uint64_t>(selected_) : std::nullopt);
    if (map.width <= 0 || map.height <= 0) {
        host_.canvas.setRegionOverlay(QImage(), QRectF());
        return;
    }
    QImage image(map.width, map.height, QImage::Format_RGBA8888);
    for (int y = 0; y < map.height; ++y) {
        std::memcpy(image.scanLine(y),
                    map.rgba.data() + static_cast<std::size_t>(y) * map.width * 4,
                    static_cast<std::size_t>(map.width) * 4);
    }
    // Carte pleine (couleurs à plat) : en mode « Régions » elle remplace visuellement les fils,
    // dont les couleurs sont les mêmes ; la région choisie est éclaircie par `renderRegionMap`.
    // Même emplacement que l'image source : centrée sur l'origine, à l'échelle du projet.
    const double mmPerPx = host_.session.project().mm_per_px.value;
    const double wMm = map.width * mmPerPx;
    const double hMm = map.height * mmPerPx;
    const QRectF rect(QPointF(host_.frame.mmToPxX(-wMm / 2.0), host_.frame.mmToPxY(-hMm / 2.0)),
                      QSizeF(wMm * host_.frame.px_per_mm, hMm * host_.frame.px_per_mm));
    host_.canvas.setRegionOverlay(image, rect);
}

bool RegionEditController::onCanvasClick(QPointF scenePx) {
    if (!active() || host_.frame.px_per_mm <= 0.0) {
        return false;
    }
    const auto& project = host_.session.project();
    if (!project.segmentation) {
        host_.notice(QObject::tr("Ce projet n'a pas de segmentation."), true);
        return true;
    }
    const double mmPerPx = host_.session.project().mm_per_px.value;
    const double wMm = project.segmentation->width * mmPerPx;
    const double hMm = project.segmentation->height * mmPerPx;
    const int ix = static_cast<int>((host_.frame.pxToMmX(scenePx.x()) + wMm / 2.0) / mmPerPx);
    const int iy = static_cast<int>((host_.frame.pxToMmY(scenePx.y()) + hMm / 2.0) / mmPerPx);
    const auto id = regionAtPixel(project, ix, iy);
    selectRegion(id.value_or(0));
    return true;
}

void RegionEditController::onDocumentReplaced() {
    selected_ = 0;
    dirty_ = false;
    refresh();
}

void RegionEditController::selectRegion(std::uint64_t id) {
    selected_ = id;
    refresh();
}

bool RegionEditController::deleteSelected() {
    if (selected_ == 0) {
        host_.notice(QObject::tr("Choisissez d'abord une région."), false);
        return false;
    }
    const auto r = deleteRegion(host_.session, selected_);
    if (!r.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    dirty_ = true;
    selected_ = 0;
    refresh();
    host_.notice(QObject::tr("Région supprimée (absorbée par sa voisine). Régénérez les objets "
                             "pour appliquer."),
                 false);
    return true;
}

bool RegionEditController::mergeSelectedInto(std::uint64_t target) {
    if (selected_ == 0) {
        host_.notice(QObject::tr("Choisissez d'abord une région."), false);
        return false;
    }
    // La région choisie est ABSORBÉE par la voisine : la voisine garde sa couleur.
    const auto r = mergeRegionInto(host_.session, target, selected_);
    if (!r.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    dirty_ = true;
    selected_ = target;
    refresh();
    host_.notice(QObject::tr("Régions fusionnées. Régénérez les objets pour appliquer."), false);
    return true;
}

bool RegionEditController::recolorSelected(std::array<std::uint8_t, 3> color) {
    if (selected_ == 0) {
        host_.notice(QObject::tr("Choisissez d'abord une région."), false);
        return false;
    }
    const auto result = recolorRegion(host_.session, selected_, color);
    if (!result.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(result.message)), true);
        return false;
    }
    dirty_ = true;
    refresh();
    host_.notice(QObject::tr("Région recolorée. Régénérez les objets pour appliquer."), false);
    return true;
}

bool RegionEditController::mergeSelectedColorInto(std::uint64_t target) {
    if (selected_ == 0) {
        host_.notice(QObject::tr("Choisissez d'abord une région."), false);
        return false;
    }
    const auto result = mergeRegionColors(host_.session, selected_, target);
    if (!result.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(result.message)), true);
        return false;
    }
    dirty_ = true;
    selected_ = target;
    refresh();
    host_.notice(QObject::tr("Couleurs fusionnées dans toutes les régions concernées. "
                             "Régénérez les objets pour appliquer."),
                 false);
    return true;
}

bool RegionEditController::cleanIslands(int minPixels) {
    const auto r =
        cleanSmallIslands(host_.session, static_cast<std::size_t>(std::max(1, minPixels)));
    if (!r.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    dirty_ = true;
    refresh();
    host_.notice(QObject::tr("Îlots nettoyés. Régénérez les objets pour appliquer."), false);
    return true;
}

bool RegionEditController::fillHoles(int maxPixels) {
    const auto result = fillSmallHoles(host_.session, static_cast<std::size_t>(std::max(1, maxPixels)));
    if (!result.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(result.message)), true);
        return false;
    }
    dirty_ = true;
    refresh();
    host_.notice(QObject::tr("Petits trous rebouchés. Régénérez les objets pour appliquer."), false);
    return true;
}

void RegionEditController::regenerate() {
    const image::Image* working = host_.workingImage();
    if (working == nullptr || working->empty()) {
        host_.notice(QObject::tr("Image de travail introuvable : impossible de régénérer."), true);
        return;
    }
    if (task_ != 0) {
        (void)host_.tasks.cancel(task_);
    }
    panel_->setBusy(true);
    const auto snapshot = host_.session.snapshot();
    const auto revision = host_.session.revision();
    const image::Image image = *working; // copie : la tâche ne lit jamais l'état de l'interface
    const auto settings = host_.settings();
    task_ = host_.tasks.submit<Regeneration>(
        QObject::tr("Régénération des objets").toStdString(),
        [snapshot, revision, image, settings](const TaskContext& ctx) {
            return regenerateObjects(snapshot, revision, image, settings, ctx);
        },
        [this](Result<Regeneration>&& r) {
            task_ = 0;
            panel_->setBusy(false);
            if (!r) {
                host_.notice(
                    QObject::tr("Régénération impossible : %1").arg(fromUtf8(r.error().message)),
                    true);
                refresh();
                return;
            }
            const auto applied = applyRegeneration(host_.session, *r);
            if (!applied.ok) {
                host_.notice(QObject::tr("Régénération : %1").arg(fromUtf8(applied.message)), true);
                refresh();
                return;
            }
            dirty_ = false;
            refresh();
            host_.notice(QObject::tr("Objets régénérés : %1 → %2. Annulable (Ctrl+Z).")
                             .arg(r->objectsBefore)
                             .arg(r->objectsAfter),
                         false);
        });
}

} // namespace openstitch::desktop_v2
