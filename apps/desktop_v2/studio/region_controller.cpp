// SPDX-License-Identifier: Apache-2.0
#include "region_controller.hpp"

#include <QColor>
#include <QImage>
#include <algorithm>
#include <cmath>

#include "source_image_tools.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString errorText(const Error& error) {
    QString text = fromUtf8(error.message);
    if (!error.detail.empty()) {
        text += QStringLiteral(" ") + fromUtf8(error.detail);
    }
    return text;
}

QString hexOf(const std::array<std::uint8_t, 3>& rgb) {
    return QColor(rgb[0], rgb[1], rgb[2]).name();
}

std::uint64_t idOf(double value) {
    return value < 0.0 ? 0 : static_cast<std::uint64_t>(value);
}

double numberOr(const QVariantMap& map, const char* key, double fallback) {
    const auto found = map.find(QLatin1String(key));
    if (found == map.end()) {
        return fallback;
    }
    bool ok = false;
    const double value = found->toDouble(&ok);
    return ok ? value : fallback;
}

} // namespace

RegionController::RegionController(StudioController& studio, ImageController& images,
                                   QObject* parent)
    : QObject(parent), studio_(studio), images_(images) {
    pump_.setInterval(16);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(0);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });
    token_ = studio_.session().subscribe([this](ChangeKind kind) {
        const auto& session = studio_.session();
        const auto revision = session.revision();
        if (kind == ChangeKind::Replaced) {
            selected_.clear();
            setStale(false);
        }
        if (revision == observedDocumentRevision_) {
            return; // sauvegarde ou signal de statut sans mutation
        }
        observedDocumentRevision_ = revision;
        if (kind == ChangeKind::Replaced || session.lastChangeAffectsSegmentation()) {
            scheduleRefresh();
        }
    });
    connect(&images_, &ImageController::prepApplied, this,
            [this] { setStale(true, QStringLiteral("l'image a été retraitée")); });
    connect(&images_, &ImageController::segmentationApplied, this,
            [this](int) { setStale(true, QStringLiteral("l'image a été re-segmentée")); });
    scheduleRefresh();
}

RegionController::~RegionController() {
    pump_.stop();
    tasks_.cancelAll();
    studio_.session().unsubscribe(token_);
}

int RegionController::mapWidth() const {
    const auto& seg = studio_.session().project().segmentation;
    return seg ? seg->width : 0;
}

int RegionController::mapHeight() const {
    const auto& seg = studio_.session().project().segmentation;
    return seg ? seg->height : 0;
}

bool RegionController::hasSegmentation() const {
    return studio_.session().project().segmentation.has_value();
}

QVariantList RegionController::selected() const {
    QVariantList out;
    for (const auto id : selected_) {
        out.append(static_cast<qulonglong>(id));
    }
    return out;
}

QVariantMap RegionController::handEdits() const {
    const auto edits = countHandEdits(studio_.session().project());
    return {{QStringLiteral("retouched"), static_cast<int>(edits.retouchedObjects)},
            {QStringLiteral("handDrawn"), static_cast<int>(edits.handDrawnObjects)}};
}

void RegionController::setMessage(const QString& text, bool error) {
    message_ = text;
    messageIsError_ = error;
    emit messageChanged();
}

void RegionController::setStale(bool stale, const QString& reason) {
    if (stale_ == stale && staleReason_ == reason) {
        return;
    }
    stale_ = stale;
    staleReason_ = stale ? reason : QString();
    emit staleChanged();
}

void RegionController::setTool(const QString& tool) {
    if ((tool != QLatin1String("select") && tool != QLatin1String("brush") &&
         tool != QLatin1String("erase")) ||
        tool == tool_) {
        return;
    }
    tool_ = tool;
    emit toolChanged();
}

void RegionController::setBrushRadius(double radiusPx) {
    const double clamped = std::clamp(radiusPx, 1.0, 200.0);
    if (clamped == brushRadius_) {
        return;
    }
    brushRadius_ = clamped;
    emit toolChanged();
}

void RegionController::scheduleRefresh() {
    refreshTimer_.start();
}

void RegionController::refresh() {
    const auto& project = studio_.session().project();
    regions_.clear();
    for (const auto& info : listRegions(project)) {
        regions_.append(
            QVariantMap{{QStringLiteral("id"), static_cast<qulonglong>(info.id)},
                        {QStringLiteral("color"), hexOf(info.rgb)},
                        {QStringLiteral("pixels"), static_cast<qulonglong>(info.pixels)}});
    }
    // Élague la sélection : une région fusionnée ou supprimée n'existe plus.
    const auto before = selected_.size();
    std::erase_if(selected_, [&](std::uint64_t id) {
        return !project.segmentation || !project.segmentation->find(RegionId{id});
    });
    if (project.segmentation && project.segmentation->width > 0 &&
        project.segmentation->height > 0) {
        const auto& seg = *project.segmentation;
        // Le panneau n'affiche que 170 px de haut : ne jamais générer une
        // seconde carte RGBA plein format pour sa miniature.
        constexpr int maxSide = 512;
        const auto maxDim = std::max(seg.width, seg.height);
        const double factor = std::min(1.0, static_cast<double>(maxSide) / maxDim);
        const int thumbW = std::max(1, static_cast<int>(std::lround(seg.width * factor)));
        const int thumbH = std::max(1, static_cast<int>(std::lround(seg.height * factor)));
        QImage preview(thumbW, thumbH, QImage::Format_RGBA8888);
        preview.fill(Qt::transparent);
        const auto w = static_cast<std::size_t>(seg.width);
        for (int y = 0; y < thumbH; ++y) {
            const double yRatio = (y + 0.5) / thumbH;
            const int sy = std::min(seg.height - 1, static_cast<int>(yRatio * seg.height));
            auto* line = preview.scanLine(y);
            for (int x = 0; x < thumbW; ++x) {
                const double xRatio = (x + 0.5) / thumbW;
                const int sx = std::min(seg.width - 1, static_cast<int>(xRatio * seg.width));
                const auto idx = static_cast<std::size_t>(sy) * w + sx;
                if (idx >= seg.labels.size()) {
                    continue;
                }
                const auto label = seg.labels[idx];
                if (label == 0 || label > seg.region_slots.size()) {
                    continue;
                }
                if (!seg.region_slots[label - 1]) {
                    continue;
                }
                const auto& rgb = seg.region_slots[label - 1]->rgb;
                line[x * 4] = rgb[0];
                line[x * 4 + 1] = rgb[1];
                line[x * 4 + 2] = rgb[2];
                line[x * 4 + 3] = 255;
            }
        }
        images_.images()->set(QStringLiteral("regions"), std::move(preview));
    } else {
        images_.images()->set(QStringLiteral("regions"), {});
    }
    ++mapRevision_;
    emit regionsChanged();
    if (before != selected_.size()) {
        emit selectionChanged();
    }
}

// --- Sélection ----------------------------------------------------------------------------

void RegionController::selectRegion(double id, const QString& mode) {
    const auto region = idOf(id);
    const auto& seg = studio_.session().project().segmentation;
    if (!seg || region == 0 || !seg->find(RegionId{region})) {
        return;
    }
    const auto found = std::find(selected_.begin(), selected_.end(), region);
    if (mode == QLatin1String("toggle")) {
        if (found != selected_.end()) {
            selected_.erase(found);
        } else {
            selected_.push_back(region);
        }
    } else if (mode == QLatin1String("add")) {
        if (found == selected_.end()) {
            selected_.push_back(region);
        }
    } else {
        if (selected_.size() == 1 && selected_.front() == region)
            return;
        selected_.assign(1, region);
    }
    // La sélection ne change pas les données de segmentation : le canevas se charge du
    // surlignage, sans reconstruire la miniature RGBA ni la liste complète.
    emit selectionChanged();
}

void RegionController::selectAtPixel(int x, int y, bool additive) {
    const auto id = regionAtPixel(studio_.session().project(), x, y);
    if (!id) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    selectRegion(static_cast<double>(*id),
                 additive ? QStringLiteral("toggle") : QStringLiteral("replace"));
}

void RegionController::selectSameColor() {
    if (selected_.empty()) {
        return;
    }
    std::vector<std::uint64_t> all;
    for (const auto id : selected_) {
        for (const auto same : sameColorRegions(studio_.session().project(), id)) {
            if (std::find(all.begin(), all.end(), same) == all.end()) {
                all.push_back(same);
            }
        }
    }
    selected_ = std::move(all);
    emit selectionChanged();
}

void RegionController::selectNeighbours() {
    if (selected_.empty()) {
        return;
    }
    selected_ = neighboursOfSet(studio_.session().project(), selected_);
    emit selectionChanged();
}

void RegionController::invertSelection() {
    selected_ = invertRegionSet(studio_.session().project(), selected_);
    emit selectionChanged();
}

void RegionController::selectAll() {
    selected_ = allRegionIds(studio_.session().project());
    emit selectionChanged();
}

void RegionController::clearSelection() {
    if (selected_.empty())
        return;
    selected_.clear();
    emit selectionChanged();
}

QVariantList RegionController::objectsOfSelection() const {
    QVariantList out;
    for (const auto id : objectsOfRegions(studio_.session().project(), selected_)) {
        out.append(static_cast<qulonglong>(id));
    }
    return out;
}

// --- Retouches ----------------------------------------------------------------------------

QString RegionController::report(const ServiceResult& result, const QString& done) {
    if (!result.ok) {
        const auto text = fromUtf8(result.message);
        setMessage(text, true);
        return text;
    }
    setStale(true, QStringLiteral("des régions ont été retouchées"));
    QString text = done + QStringLiteral(" Régénérez les objets pour appliquer (Ctrl+Z annule).");
    if (!result.message.empty()) {
        text += QStringLiteral(" ") + fromUtf8(result.message);
    }
    setMessage(text, false);
    return {};
}

QString RegionController::recolorSelected(const QString& hexColor) {
    const QColor color(hexColor);
    if (!color.isValid()) {
        const auto text = QStringLiteral("Couleur invalide.");
        setMessage(text, true);
        return text;
    }
    if (selected_.empty()) {
        const auto text = QStringLiteral("Sélectionnez au moins une région.");
        setMessage(text, true);
        return text;
    }
    return report(recolorRegionSet(studio_.session(), selected_,
                                   {static_cast<std::uint8_t>(color.red()),
                                    static_cast<std::uint8_t>(color.green()),
                                    static_cast<std::uint8_t>(color.blue())}),
                  QStringLiteral("Région(s) recolorée(s)."));
}

QString RegionController::mergeSelected() {
    if (selected_.size() < 2) {
        const auto text = QStringLiteral("Sélectionnez au moins deux régions à fusionner.");
        setMessage(text, true);
        return text;
    }
    return report(mergeRegionSet(studio_.session(), selected_),
                  QStringLiteral("Régions fusionnées."));
}

QString RegionController::mergeWithNeighbour(double id) {
    const auto region = idOf(id);
    const auto neighbours = neighbourRegions(studio_.session().project(), region);
    if (neighbours.empty()) {
        const auto text = QStringLiteral("Cette région n'a pas de voisine.");
        setMessage(text, true);
        return text;
    }
    return report(mergeRegionInto(studio_.session(), neighbours.front(), region),
                  QStringLiteral("Région fusionnée dans sa voisine."));
}

QString RegionController::mergeColorInto(double source, double target) {
    return report(mergeRegionColors(studio_.session(), idOf(source), idOf(target)),
                  QStringLiteral("Teinte fusionnée."));
}

QString RegionController::deleteSelected() {
    if (selected_.empty()) {
        const auto text = QStringLiteral("Sélectionnez au moins une région.");
        setMessage(text, true);
        return text;
    }
    return report(deleteRegionSet(studio_.session(), selected_),
                  QStringLiteral("Région(s) supprimée(s)."));
}

QString RegionController::cleanIslands(int minPixels) {
    return report(
        cleanSmallIslands(studio_.session(), static_cast<std::size_t>(std::max(1, minPixels))),
        QStringLiteral("Îlots nettoyés."));
}

QString RegionController::fillHoles(int maxPixels) {
    return report(
        fillSmallHoles(studio_.session(), static_cast<std::size_t>(std::max(1, maxPixels))),
        QStringLiteral("Trous rebouchés."));
}

QVariantMap RegionController::previewReduction(int maxColors, double maxDistance,
                                               bool mergeAdjacent) const {
    const auto p = previewColorReduction(studio_.session().project(),
                                         static_cast<std::size_t>(std::max(0, maxColors)),
                                         maxDistance, mergeAdjacent);
    return {{QStringLiteral("before"), static_cast<int>(p.colorsBefore)},
            {QStringLiteral("after"), static_cast<int>(p.colorsAfter)},
            {QStringLiteral("recolors"), static_cast<int>(p.recolors)},
            {QStringLiteral("merges"), static_cast<int>(p.merges)}};
}

QString RegionController::reducePalette(int maxColors, double maxDistance, bool mergeAdjacent) {
    return report(reduceColors(studio_.session(), static_cast<std::size_t>(std::max(0, maxColors)),
                               maxDistance, mergeAdjacent),
                  QStringLiteral("Palette réduite."));
}

QString RegionController::paintStroke(const QVariantList& pointsPx, bool erase) {
    if (selected_.size() != 1) {
        const auto text = QStringLiteral("Pinceau : choisissez d'abord UNE région.");
        setMessage(text, true);
        return text;
    }
    std::vector<std::pair<double, double>> centres;
    for (const auto& item : pointsPx) {
        const auto point = item.toMap();
        centres.emplace_back(point.value(QStringLiteral("x")).toDouble(),
                             point.value(QStringLiteral("y")).toDouble());
    }
    return report(
        paintRegionStroke(studio_.session(), selected_.front(), centres, brushRadius_, erase),
        erase ? QStringLiteral("Région effacée au pinceau.")
              : QStringLiteral("Frontière retouchée."));
}

// --- Régénération -------------------------------------------------------------------------

void RegionController::regenerate(bool resegment, bool confirmed, const QVariantMap& settings) {
    if (!confirmed) {
        const auto edits = countHandEdits(studio_.session().project());
        if (edits.retouchedObjects > 0) {
            emit confirmRequested(
                QStringLiteral(
                    "%1 objet(s) portent des retouches de points qui seront remplacées par "
                    "la nouvelle génération. Les %2 objet(s) dessinés à la main sont "
                    "conservés. Vous pourrez tout annuler avec Ctrl+Z.")
                    .arg(edits.retouchedObjects)
                    .arg(edits.handDrawnObjects));
            return;
        }
    }
    auto working = processedImage(studio_.session().project());
    if (!working) {
        setMessage(QStringLiteral("Image de travail introuvable : ") + errorText(working.error()),
                   true);
        return;
    }
    if (task_ != 0) {
        (void)tasks_.cancel(task_);
        task_ = 0;
    }
    project_ops::DigitizeSettings digitize;
    digitize.max_colors = static_cast<int>(numberOr(settings, "max_colors", digitize.max_colors));
    digitize.min_region_px =
        static_cast<int>(numberOr(settings, "min_region_px", digitize.min_region_px));
    digitize.smoothing_px =
        static_cast<int>(numberOr(settings, "smoothing_px", digitize.smoothing_px));
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    const image::Image image = std::move(*working);
    busy_ = true;
    emit busyChanged();
    task_ = tasks_.submit<Regeneration>(
        "Régénération des objets",
        [snapshot, revision, image, digitize, resegment](const TaskContext& task) {
            return resegment ? resegmentObjects(snapshot, revision, image, digitize, task)
                             : regenerateObjects(snapshot, revision, image, digitize, task);
        },
        [this](Result<Regeneration>&& r) {
            task_ = 0;
            busy_ = false;
            emit busyChanged();
            if (!r) {
                setMessage(QStringLiteral("Régénération impossible : ") + errorText(r.error()),
                           true);
                return;
            }
            const auto applied = applyRegeneration(studio_.session(), *r);
            if (!applied.ok) {
                setMessage(QStringLiteral("Régénération : ") + fromUtf8(applied.message), true);
                return;
            }
            setStale(false);
            QString kept;
            if (r->preserved > 0) {
                kept += QStringLiteral(" %1 objet(s) dessiné(s) à la main conservé(s).")
                            .arg(r->preserved);
            }
            if (r->lostRetouches > 0) {
                kept +=
                    QStringLiteral(" %1 objet(s) retouché(s) remplacé(s).").arg(r->lostRetouches);
            }
            setMessage(QStringLiteral("Objets régénérés : %1 → %2. Annulable (Ctrl+Z).")
                               .arg(r->objectsBefore)
                               .arg(r->objectsAfter) +
                           kept,
                       false);
            emit regenerated(static_cast<int>(r->objectsBefore), static_cast<int>(r->objectsAfter));
        });
}

void RegionController::cancelTask() {
    if (task_ != 0) {
        (void)tasks_.cancel(task_);
        task_ = 0;
        busy_ = false;
        emit busyChanged();
        setMessage(QStringLiteral("Régénération annulée : le document est intact."), false);
    }
}

} // namespace openstitch::desktop_v2::studio
