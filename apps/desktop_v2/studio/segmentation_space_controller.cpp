// SPDX-License-Identifier: Apache-2.0
#include "segmentation_space_controller.hpp"

#include "perf_log.hpp"

#include <algorithm>

#include "gpu_canvas.hpp"
#include "layers/region_layer.hpp"

namespace openstitch::desktop_v2::studio {

SegmentationSpaceController::SegmentationSpaceController(StudioController& studio,
                                                         ImageController& images,
                                                         RegionController& regions, QObject* parent)
    : QObject(parent), studio_(studio), images_(images), regions_(regions) {
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(0);
    connect(&refreshTimer_, &QTimer::timeout, this, &SegmentationSpaceController::refresh);
    connect(&studio_, &StudioController::changed, this,
            &SegmentationSpaceController::scheduleRefresh);
    connect(&images_, &ImageController::changed, this,
            &SegmentationSpaceController::scheduleRefresh);
    connect(&images_, &ImageController::busyChanged, this,
            &SegmentationSpaceController::scheduleRefresh);
    connect(&regions_, &RegionController::regionsChanged, this,
            &SegmentationSpaceController::scheduleRefresh);
    connect(&regions_, &RegionController::selectionChanged, this,
            &SegmentationSpaceController::scheduleRefresh);
    connect(&regions_, &RegionController::staleChanged, this,
            &SegmentationSpaceController::scheduleRefresh);
    connect(&regions_, &RegionController::busyChanged, this,
            &SegmentationSpaceController::scheduleRefresh);
    // Une seule demande de confirmation, portée par RegionController : la carte du canevas et
    // l'onglet « Régions » montrent la même question et confirment la même action.
    connect(&regions_, &RegionController::confirmChanged, this,
            &SegmentationSpaceController::confirmChanged);
    refresh();
}

SegmentationSpaceController::~SegmentationSpaceController() = default;

void SegmentationSpaceController::setOpacity(double value) {
    value = std::clamp(value, 0.0, 1.0);
    if (value == opacity_) {
        return;
    }
    opacity_ = value;
    emit displayChanged();
    scheduleRefresh();
}

void SegmentationSpaceController::setContours(bool value) {
    if (value == contours_) {
        return;
    }
    contours_ = value;
    emit displayChanged();
    scheduleRefresh();
}

bool SegmentationSpaceController::hasSegmentation() const {
    return studio_.session().project().imageSource().segmentation.has_value();
}

int SegmentationSpaceController::regionCount() const {
    const auto& seg = studio_.session().project().imageSource().segmentation;
    return seg ? static_cast<int>(seg->region_count()) : 0;
}

bool SegmentationSpaceController::canSegment() const {
    return images_.hasImage() && !images_.busy() && !regions_.busy();
}

bool SegmentationSpaceController::canUpdateShapes() const {
    return hasSegmentation() && !images_.busy() && !regions_.busy();
}

void SegmentationSpaceController::attach(QObject* canvas) {
    canvas_ = canvas;
    // Arrivée depuis « Aller à la région » (espace Vecteurs) : la région désignée dans la
    // sélection partagée devient la sélection de l'espace.
    (void)regions_.adoptSessionSelection();
    publish();
}

void SegmentationSpaceController::detach() {
    // Un choix de cible de fusion laissé ouvert fusionnerait au retour la prochaine région cliquée.
    regions_.setMergeTargetMode(false);
    if (auto* gpu = qobject_cast<GpuCanvas*>(canvas_.data())) {
        gpu->setRegionLayer(nullptr);
    }
    canvas_.clear();
}

bool SegmentationSpaceController::pickAt(double xMm, double yMm, bool additive) {
    int px = 0;
    int py = 0;
    const bool inside = scenePointToPixel(studio_.session().project(), xMm, yMm, px, py);
    if (inside) {
        regions_.selectAtPixel(px, py, additive);
    } else if (!additive) {
        regions_.clearSelection();
    }
    return inside;
}

int SegmentationSpaceController::selectInZone(const QVariantList& scenePointsMm, bool additive,
                                              bool touching) {
    const auto& source = studio_.session().project().imageSource();
    if (!source.segmentation || source.mm_per_px.value <= 0.0) {
        return 0;
    }
    // Même conversion que `scenePointToPixel`, sans troncature ni rejet hors image : un cadre peut
    // commencer à côté du dessin.
    const double mm = source.mm_per_px.value;
    const double halfW = source.segmentation->width / 2.0;
    const double halfH = source.segmentation->height / 2.0;
    QVariantList pixels;
    pixels.reserve(scenePointsMm.size());
    for (const auto& value : scenePointsMm) {
        const auto point = value.toMap();
        pixels.push_back(QVariantMap{
            {QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble() / mm + halfW},
            {QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble() / mm + halfH}});
    }
    return regions_.selectInZone(pixels, additive, touching);
}

QVariantMap SegmentationSpaceController::pixelAt(double xMm, double yMm) const {
    int px = 0;
    int py = 0;
    const bool inside = scenePointToPixel(studio_.session().project(), xMm, yMm, px, py);
    return {
        {QStringLiteral("valid"), inside}, {QStringLiteral("x"), px}, {QStringLiteral("y"), py}};
}

void SegmentationSpaceController::segmentImage(const QVariantMap& options) {
    if (!canSegment()) {
        return;
    }
    images_.applySegmentation(options.isEmpty() ? images_.segmentationDefaults() : options);
}

void SegmentationSpaceController::updateShapes(bool confirmed) {
    if (!canUpdateShapes()) {
        return;
    }
    if (confirmed && !regions_.confirmText().isEmpty()) {
        regions_.confirmRegeneration(); // la demande affichée, pas une autre
        return;
    }
    regions_.regenerate(false, confirmed, images_.lastSegmentationOptions());
}

void SegmentationSpaceController::cancelConfirm() {
    regions_.cancelConfirmation();
}

void SegmentationSpaceController::scheduleRefresh() {
    if (!refreshTimer_.isActive()) {
        refreshTimer_.start();
    }
}

void SegmentationSpaceController::computeStage() {
    QString state;
    QString detail;
    const bool seg = hasSegmentation();
    if (seg) {
        hadSegmentation_ = true;
    }
    if (!images_.hasImage()) {
        state.clear(); // pas d'image : le calcul de base de l'hôte (bloquée) reprend
    } else if (images_.busy() || regions_.busy()) {
        state = QStringLiteral("blocked");
        detail = tr("Calcul en cours");
    } else if (!seg) {
        if (hadSegmentation_) {
            state = QStringLiteral("stale");
            detail = tr("L'image a changé : la segmentation est à refaire");
        } else {
            state = QStringLiteral("empty");
            detail = tr("Pas encore de segmentation");
        }
    } else if (regions_.stale()) {
        state = QStringLiteral("stale");
        detail = tr("Corrections de régions non répercutées sur les formes : %1")
                     .arg(regions_.staleReason());
    } else {
        state = QStringLiteral("upToDate");
        detail = tr("%n région(s)", nullptr, regionCount());
    }
    if (state != stageState_ || detail != stageDetail_) {
        stageState_ = state;
        stageDetail_ = detail;
    }
    emit stageChanged();
}

void SegmentationSpaceController::refresh() {
    const PerfScope perf("segmentation.refresh");
    const auto& session = studio_.session();
    const auto& project = session.project();
    bool fillChanged = false;
    // La revision globale change aussi pour les objets et leurs paramètres.
    // La miniature versionnée suit uniquement les mutations de segmentation.
    const auto revision = static_cast<std::uint64_t>(regions_.mapRevision());
    if (revision != seenRevision_ || contours_ != fillContours_) {
        const auto fp = segmentationFingerprint(project);
        if (fp != fingerprint_ || contours_ != fillContours_) {
            if (fp != fingerprint_) {
                bounds_ = buildRegionBounds(project);
            }
            fingerprint_ = fp;
            fillContours_ = contours_;
            fill_ = fp != 0 ? buildRegionFill(project, contours_) : QImage{};
            ++fillRevision_;
            ++fillRebuilds_;
            fillChanged = true;
        }
        seenRevision_ = revision;
    }
    std::vector<std::uint64_t> selection;
    for (const auto& id : regions_.selected()) {
        selection.push_back(static_cast<std::uint64_t>(id.toDouble()));
    }
    std::sort(selection.begin(), selection.end());
    bool highlightChanged = false;
    if (fillChanged || selection != shownSelection_) {
        shownSelection_ = selection;
        auto highlight = buildRegionHighlight(project, selection, bounds_);
        highlight_ = std::move(highlight.image);
        highlightBoundsPx_ = highlight.boundsPx;
        ++highlightRevision_;
        ++highlightRebuilds_;
        highlightChanged = true;
    }
    const QRectF fillRectMm = regionLayerRect(project);
    QRectF highlightRectMm;
    if (project.imageSource().segmentation && highlightBoundsPx_.isValid()) {
        const auto& seg = *project.imageSource().segmentation;
        const double mm = project.imageSource().mm_per_px.value;
        const double x = (highlightBoundsPx_.x() - seg.width / 2.0) * mm;
        const double y = (highlightBoundsPx_.y() - seg.height / 2.0) * mm;
        const double w = highlightBoundsPx_.width() * mm;
        const double h = highlightBoundsPx_.height() * mm;
        highlightRectMm = QRectF(x, y, w, h);
    }
    if (fillChanged || highlightChanged || !layer_ || layer_->opacity != opacity_ ||
        layer_->rectMm != fillRectMm || layer_->highlightRectMm != highlightRectMm) {
        auto data = std::make_shared<RegionLayerData>();
        data->fill = fill_;
        data->highlight = highlight_;
        data->rectMm = fillRectMm;
        data->highlightRectMm = highlightRectMm;
        data->opacity = opacity_;
        data->fillRevision = fillRevision_;
        data->highlightRevision = highlightRevision_;
        layer_ = std::move(data);
        publish();
        emit layerChanged();
    }
    computeStage();
}

void SegmentationSpaceController::publish() {
    if (auto* gpu = qobject_cast<GpuCanvas*>(canvas_.data())) {
        gpu->setRegionLayer(layer_);
    }
}

} // namespace openstitch::desktop_v2::studio
