// SPDX-License-Identifier: Apache-2.0
#include "region_edit_controller.hpp"

#include <QColorDialog>
#include <QImage>
#include <QRectF>

#include <algorithm>
#include <cstring>

#include "document_session.hpp"

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
    connect(panel_, &RegionPanel::regionsChosen, this,
            [this](const std::vector<std::uint64_t>& ids) {
                if (ids != sel_) {
                    selectRegions(ids);
                }
            });
    connect(panel_, &RegionPanel::selectAllRequested, this, [this] { selectAll(); });
    connect(panel_, &RegionPanel::selectSameColorRequested, this, [this] { selectSameColor(); });
    connect(panel_, &RegionPanel::selectNeighboursRequested, this, [this] { selectNeighbours(); });
    connect(panel_, &RegionPanel::invertRequested, this, [this] { invertSelection(); });
    connect(panel_, &RegionPanel::clearRequested, this, [this] { clearSelection(); });
    connect(panel_, &RegionPanel::selectObjectsRequested, this,
            [this] { (void)selectObjectsOfSelection(); });
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
    connect(panel_, &RegionPanel::reduceRequested, this,
            [this](int colors, double distance, bool merge) {
                (void)reducePalette(colors, distance, merge);
            });
    connect(panel_, &RegionPanel::reduceParamsChanged, this,
            [this](int colors, double distance, bool merge) {
                updatePaletteReducePreview(colors, distance, merge);
            });
    // Sélection PARTAGÉE : une sélection de régions faite ailleurs (modèle commun) est reprise ici,
    // et celle d'objets la remplace (la sélection est unique).
    selectionToken_ = host_.session.selection().subscribe([this](const SelectionModel& model) {
        if (publishing_) {
            return;
        }
        std::vector<std::uint64_t> ids;
        for (const auto& t : model.targets()) {
            if (t.kind == TargetKind::Region) {
                ids.push_back(t.id);
            }
        }
        if (ids != sel_) {
            adoptSelection(std::move(ids), false);
        }
    });
}

RegionEditController::~RegionEditController() {
    host_.session.selection().unsubscribe(selectionToken_);
}

void RegionEditController::adoptSelection(std::vector<std::uint64_t> ids, bool publish) {
    // Identifiants vivants et uniques, dans l'ordre reçu.
    const auto live = allRegionIds(host_.session.project());
    std::vector<std::uint64_t> clean;
    for (const auto id : ids) {
        if (std::find(live.begin(), live.end(), id) != live.end() &&
            std::find(clean.begin(), clean.end(), id) == clean.end()) {
            clean.push_back(id);
        }
    }
    sel_ = std::move(clean);
    selected_ = sel_.empty() ? 0 : sel_.back();
    if (publish) {
        std::vector<Target> targets;
        targets.reserve(sel_.size());
        for (const auto id : sel_) {
            targets.push_back({TargetKind::Region, id});
        }
        publishing_ = true;
        host_.session.selection().replace(std::move(targets));
        publishing_ = false;
    }
    refresh();
}

void RegionEditController::selectRegions(std::vector<std::uint64_t> ids) {
    adoptSelection(std::move(ids), true);
}

void RegionEditController::selectAll() {
    selectRegions(allRegionIds(host_.session.project()));
}

void RegionEditController::selectSameColor() {
    if (selected_ == 0) {
        host_.notice(QObject::tr("Choisissez d'abord une région."), false);
        return;
    }
    auto ids = sameColorRegions(host_.session.project(), selected_);
    std::erase(ids, selected_);
    ids.push_back(selected_); // la principale reste la dernière
    selectRegions(std::move(ids));
}

void RegionEditController::selectNeighbours() {
    const auto ids = neighboursOfSet(host_.session.project(), sel_);
    if (ids.empty()) {
        host_.notice(QObject::tr("Aucune région voisine."), false);
        return;
    }
    selectRegions(ids);
}

void RegionEditController::invertSelection() {
    selectRegions(invertRegionSet(host_.session.project(), sel_));
}

void RegionEditController::clearSelection() {
    selectRegions({});
}

bool RegionEditController::selectObjectsOfSelection() {
    const auto objects = objectsOfRegions(host_.session.project(), sel_);
    if (objects.empty()) {
        host_.notice(QObject::tr("Aucun objet ne vient de ces régions (régénérez les objets ?)."),
                     true);
        return false;
    }
    std::vector<Target> targets;
    for (const auto id : objects) {
        targets.push_back({TargetKind::EmbroideryObject, id});
    }
    host_.session.selection().replace(std::move(targets)); // quitte la sélection de régions
    return true;
}

bool RegionEditController::mergeSelection() {
    if (sel_.size() < 2) {
        host_.notice(QObject::tr("Choisissez au moins deux régions à fusionner."), false);
        return false;
    }
    const auto count = sel_.size();
    const auto r = mergeRegionSet(host_.session, sel_);
    if (!r.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    dirty_ = true;
    refresh();
    host_.notice(
        QObject::tr("%1 régions fusionnées. Régénérez les objets pour appliquer.").arg(count),
        false);
    return true;
}

void RegionEditController::requestRecolor() {
    emit panel_->recolorRequested();
}

bool RegionEditController::selectRegionsOfSelectedObjects() {
    std::vector<std::uint64_t> objectIds;
    for (const auto& t : host_.session.selection().targets()) {
        if (t.kind == TargetKind::EmbroideryObject) {
            objectIds.push_back(t.id);
        }
    }
    const auto regions = regionsOfObjects(host_.session.project(), objectIds);
    if (regions.empty()) {
        host_.notice(QObject::tr("Les objets choisis ne viennent d'aucune région."), true);
        return false;
    }
    selectRegions(regions);
    return true;
}

bool RegionEditController::active() const {
    const QString tool = host_.activeTool();
    return tool == QStringLiteral("region.edit") || tool == QStringLiteral("region.brush");
}

void RegionEditController::refresh() {
    const auto& project = host_.session.project();
    const auto regions = listRegions(project);
    if (regions.empty()) {
        selected_ = 0;
        sel_.clear();
        panel_->setRegions({}, 0);
        panel_->setNeighbours({});
        panel_->setColorTargets({});
        panel_->setMessage(QObject::tr("Ce projet n'a pas de segmentation : numérisez une image "
                                       "pour corriger ses régions."));
        host_.canvas.setRegionOverlay(QImage(), QRectF());
        return;
    }
    // Régions disparues (fusionnées, supprimées, annulation…) : retirées de l'ensemble choisi.
    std::erase_if(sel_, [&](std::uint64_t id) {
        return std::none_of(regions.begin(), regions.end(),
                            [id](const RegionInfo& r) { return r.id == id; });
    });
    selected_ = sel_.empty() ? 0 : sel_.back();
    panel_->setRegions(regions, sel_);
    std::vector<RegionInfo> neighbours;
    if (selected_ != 0) {
        for (const auto id : neighboursOfSet(project, sel_)) {
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
    panel_->requestReducePreview();
    if (active()) {
        panel_->setMessage(
            selected_ == 0 ? QObject::tr("%n région(s). Cliquez une région pour la corriger.", "",
                                         static_cast<int>(regions.size()))
            : sel_.size() > 1 ? QObject::tr("%1 régions choisies : fusionnez-les, "
                                            "supprimez-les ou recolorez-les ensemble.")
                                    .arg(sel_.size())
                              : QObject::tr("Région %1 choisie : supprimez-la, fusionnez-la "
                                            "dans une voisine, ou nettoyez les îlots.")
                                    .arg(selected_));
    }
    showOverlay();
    if (host_.stateChanged) {
        host_.stateChanged();
    }
}

void RegionEditController::showOverlay() {
    if (!active() || host_.frame.px_per_mm <= 0.0) {
        host_.canvas.setRegionOverlay(QImage(), QRectF());
        return;
    }
    const auto map = renderRegionMap(host_.session.project(), sel_);
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

bool RegionEditController::onCanvasClick(QPointF scenePx, Qt::KeyboardModifiers modifiers) {
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
    const bool toggle = (modifiers & Qt::ControlModifier) != 0;
    const bool add = (modifiers & Qt::ShiftModifier) != 0;
    if (!id) {
        if (!toggle && !add) {
            selectRegion(0); // clic sur le fond : on désélectionne
        }
        return true;
    }
    if (toggle) {
        auto next = sel_;
        if (const auto it = std::find(next.begin(), next.end(), *id); it != next.end()) {
            next.erase(it);
        } else {
            next.push_back(*id);
        }
        selectRegions(std::move(next));
    } else if (add) {
        auto next = sel_;
        std::erase(next, *id);
        next.push_back(*id);
        selectRegions(std::move(next));
    } else {
        selectRegion(*id);
    }
    return true;
}

void RegionEditController::onDocumentReplaced() {
    selected_ = 0;
    sel_.clear();
    dirty_ = false;
    refresh();
}

void RegionEditController::selectRegion(std::uint64_t id) {
    selectRegions(id == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{id});
}

bool RegionEditController::deleteSelected() {
    if (selected_ == 0) {
        host_.notice(QObject::tr("Choisissez d'abord une région."), false);
        return false;
    }
    const auto count = sel_.size();
    const auto r = deleteRegionSet(host_.session, sel_);
    if (!r.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    dirty_ = true;
    selectRegions({});
    host_.notice(count > 1 ? QObject::tr("%1 régions supprimées (absorbées par leurs voisines). "
                                         "Régénérez les objets pour appliquer.")
                                 .arg(count)
                           : QObject::tr("Région supprimée (absorbée par sa voisine). Régénérez "
                                         "les objets pour appliquer."),
                 false);
    return true;
}

bool RegionEditController::mergeSelectedInto(std::uint64_t target) {
    if (selected_ == 0) {
        host_.notice(QObject::tr("Choisissez d'abord une région."), false);
        return false;
    }
    // Les régions choisies sont ABSORBÉES par la cible : elle garde sa couleur.
    const auto r = mergeRegionSet(host_.session, sel_, target);
    if (!r.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    dirty_ = true;
    selectRegion(target);
    host_.notice(QObject::tr("Régions fusionnées. Régénérez les objets pour appliquer."), false);
    return true;
}

bool RegionEditController::recolorSelected(std::array<std::uint8_t, 3> color) {
    if (selected_ == 0) {
        host_.notice(QObject::tr("Choisissez d'abord une région."), false);
        return false;
    }
    const auto result = recolorRegionSet(host_.session, sel_, color);
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
    selectRegion(target);
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

bool RegionEditController::reducePalette(int maxColors, double maxDistance, bool mergeAdjacent) {
    const auto before = listRegions(host_.session.project()).size();
    const auto r = reduceColors(host_.session, static_cast<std::size_t>(std::max(0, maxColors)),
                                maxDistance, mergeAdjacent);
    if (!r.ok) {
        host_.notice(QObject::tr("Palette : %1").arg(fromUtf8(r.message)), true);
        return false;
    }
    dirty_ = true;
    refresh();
    host_.notice(QObject::tr("Palette réduite (%1 → %2 régions). Régénérez les objets pour "
                             "appliquer.")
                     .arg(before)
                     .arg(listRegions(host_.session.project()).size()),
                 false);
    return true;
}

void RegionEditController::updatePaletteReducePreview(int maxColors, double maxDistance,
                                                      bool mergeAdjacent) {
    const auto p = previewColorReduction(host_.session.project(),
                                         static_cast<std::size_t>(std::max(0, maxColors)),
                                         maxDistance, mergeAdjacent);
    panel_->setReducePreview(static_cast<int>(p.colorsBefore), static_cast<int>(p.colorsAfter));
}

bool RegionEditController::fillHoles(int maxPixels) {
    const auto limit = static_cast<std::size_t>(std::max(1, maxPixels));
    const auto result = fillSmallHoles(host_.session, limit);
    if (!result.ok) {
        host_.notice(QObject::tr("Régions : %1").arg(fromUtf8(result.message)), true);
        return false;
    }
    dirty_ = true;
    refresh();
    host_.notice(QObject::tr("Trous rebouchés. Régénérez les objets pour appliquer."), false);
    return true;
}

bool RegionEditController::paintStroke(const QVector<QPointF>& scenePx, bool erase) {
    if (host_.activeTool() != QStringLiteral("region.brush") || host_.frame.px_per_mm <= 0.0) {
        return false;
    }
    const auto& project = host_.session.project();
    if (!project.segmentation) {
        host_.notice(QObject::tr("Ce projet n'a pas de segmentation."), true);
        return false;
    }
    if (selected_ == 0) {
        host_.notice(
            QObject::tr("Pinceau : choisissez d'abord une région avec l'outil Régions (X)."), true);
        return false;
    }
    const double mmPerPx = project.mm_per_px.value;
    const double wMm = project.segmentation->width * mmPerPx;
    const double hMm = project.segmentation->height * mmPerPx;
    std::vector<std::pair<double, double>> centres;
    centres.reserve(static_cast<std::size_t>(scenePx.size()));
    for (const QPointF& p : scenePx) {
        centres.emplace_back((host_.frame.pxToMmX(p.x()) + wMm / 2.0) / mmPerPx,
                             (host_.frame.pxToMmY(p.y()) + hMm / 2.0) / mmPerPx);
    }
    const auto result =
        paintRegionStroke(host_.session, selected_, centres, panel_->brushRadius(), erase);
    if (!result.ok) {
        host_.notice(QObject::tr("Pinceau : %1").arg(fromUtf8(result.message)), true);
        return false;
    }
    dirty_ = true;
    refresh();
    QString note = erase ? QObject::tr("Région effacée au pinceau. Régénérez les objets pour "
                                       "appliquer.")
                         : QObject::tr("Frontière retouchée. Régénérez les objets pour appliquer.");
    if (!result.message.empty()) {
        note += QStringLiteral(" ") + fromUtf8(result.message);
    }
    host_.notice(note, false);
    return true;
}

void RegionEditController::regenerate(bool resegmentFirst) {
    // Jamais d'écrasement silencieux : si des retouches de points vont être remplacées, on le dit
    // avant, avec la solution réversible (Ctrl+Z).
    if (const auto edits = countHandEdits(host_.session.project());
        edits.retouchedObjects > 0 && host_.confirmReplace) {
        const QString question =
            QObject::tr("%n objet(s) portent des retouches de points qui seront remplacées par la "
                        "nouvelle génération. Les %1 objet(s) dessinés à la main sont conservés. "
                        "Vous pourrez tout annuler avec Ctrl+Z.",
                        "", static_cast<int>(edits.retouchedObjects))
                .arg(edits.handDrawnObjects);
        if (!host_.confirmReplace(question)) {
            host_.notice(QObject::tr("Régénération annulée : vos retouches sont intactes."), false);
            return;
        }
    }
    regenerateConfirmed(resegmentFirst);
}

void RegionEditController::regenerateConfirmed(bool resegmentFirst) {
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
        [snapshot, revision, image, settings, resegmentFirst](const TaskContext& ctx) {
            return resegmentFirst ? resegmentObjects(snapshot, revision, image, settings, ctx)
                                  : regenerateObjects(snapshot, revision, image, settings, ctx);
        },
        [this, resegmentFirst](Result<Regeneration>&& r) {
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
            QString kept;
            if (r->preserved > 0) {
                kept +=
                    QObject::tr(" %1 objet(s) dessiné(s) à la main conservé(s).").arg(r->preserved);
            }
            if (r->lostRetouches > 0) {
                kept += QObject::tr(" %1 objet(s) retouché(s) remplacé(s).").arg(r->lostRetouches);
            }
            host_.notice(
                (resegmentFirst
                     ? QObject::tr(
                           "Image re-segmentée, objets régénérés : %1 → %2. Annulable (Ctrl+Z).")
                     : QObject::tr("Objets régénérés : %1 → %2. Annulable (Ctrl+Z)."))
                        .arg(r->objectsBefore)
                        .arg(r->objectsAfter) +
                    kept,
                false);
        });
}

} // namespace openstitch::desktop_v2
