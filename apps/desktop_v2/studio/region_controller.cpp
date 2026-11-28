// SPDX-License-Identifier: Apache-2.0
#include "region_controller.hpp"

#include "perf_log.hpp"
#include "segmentation_settings.hpp"
#include "task_pump.hpp"

#include <QColor>
#include <QImage>
#include <QRegularExpression>
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

} // namespace

RegionController::RegionController(StudioController& studio, ImageController& images,
                                   QObject* parent)
    : QObject(parent), studio_(studio), images_(images) {
    attachTaskPump(tasks_, this);
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(0);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });
    // Un autre satin demandé : les objets déjà générés ne le suivent plus (« Régénérer » mis en
    // avant), comme après une retouche de région.
    connect(&studio_, &StudioController::satinOptionsChanged, this, [this] {
        emit satinOptionsChanged();
        // Des régions déjà retouchées gardent leur raison (plus forte : formes à refaire aussi).
        if (!hasSegmentation() || segmentationStale() ||
            studio_.session().project().embroidery_objects.empty()) {
            return;
        }
        // Comparé au satin de la dernière génération : revenir à ce réglage n'appelle plus de
        // régénération.
        const auto wanted = objectDigitizeSettingsFrom(studio_.satinOptions());
        const auto& made = generatedSatin_;
        const bool same = wanted.satin_bands == made.satin_bands &&
                          wanted.satin_compare == made.satin_compare &&
                          wanted.satin_min_coverage == made.satin_min_coverage &&
                          wanted.satin_compact_max_mm == made.satin_compact_max_mm &&
                          wanted.satin_compare_max_stitch_mm == made.satin_compare_max_stitch_mm;
        if (!same) {
            setStale(true, satinReason());
        } else if (objectsOnlyStale()) {
            setStale(false);
        }
    });
    token_ = studio_.session().subscribe([this](ChangeKind kind) {
        const auto& session = studio_.session();
        const auto revision = session.revision();
        const auto source = session.project().imageSourceId();
        if (kind == ChangeKind::Replaced) {
            staleSources_.clear();
            // Document ouvert : réglages de sa génération inconnus, supposés ceux du moteur.
            generatedSatin_ = project_ops::DigitizeSettings{};
        }
        if (kind == ChangeKind::Replaced || source != observedImageSource_) {
            observedImageSource_ = source;
            // Une confirmation en attente a été calculée pour l'ancien document ou l'ancienne
            // source : l'exécuter ici remplacerait des retouches jamais comptées.
            cancelConfirmation();
            setMergeTargetMode(false);
            selected_.clear();
            const auto stale = staleSources_.find(source);
            setStale(stale != staleSources_.end(),
                     stale != staleSources_.end() ? stale->second : QString{});
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
    connect(&images_, &ImageController::segmentationAppliedToLayer, this, [this](ObjectId source) {
        const auto reason = tr("l'image a été re-segmentée");
        if (source == observedImageSource_) {
            setStale(true, reason);
        } else {
            staleSources_.insert_or_assign(source, reason);
        }
    });
    scheduleRefresh();
}

RegionController::~RegionController() {
    tasks_.cancelAll();
    studio_.session().unsubscribe(token_);
}

int RegionController::mapWidth() const {
    const auto& seg = studio_.session().project().imageSource().segmentation;
    return seg ? seg->width : 0;
}

int RegionController::mapHeight() const {
    const auto& seg = studio_.session().project().imageSource().segmentation;
    return seg ? seg->height : 0;
}

bool RegionController::hasSegmentation() const {
    return studio_.session().project().imageSource().segmentation.has_value();
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

void RegionController::setWarnings(const std::vector<std::string>& warnings) {
    if (warnings.empty() && warnings_.isEmpty()) {
        return;
    }
    // « région N écartée … » : l'identifiant cité permet de sélectionner la région d'un clic.
    static const QRegularExpression cited(QStringLiteral("^région (\\d+) "));
    warnings_.clear();
    for (const auto& warning : warnings) {
        const QString text = fromUtf8(warning);
        const auto match = cited.match(text);
        warnings_.push_back(QVariantMap{
            {QStringLiteral("text"), text},
            {QStringLiteral("region"), match.hasMatch() ? match.captured(1).toDouble() : -1.0}});
    }
    emit warningsChanged();
}

QString RegionController::warningSummary() const {
    int discarded = 0;
    for (const auto& item : warnings_) {
        if (item.toMap().value(QStringLiteral("region")).toDouble() >= 0.0) {
            ++discarded;
        }
    }
    const int others = static_cast<int>(warnings_.size()) - discarded;
    QStringList parts;
    if (discarded > 0) {
        parts << (discarded == 1 ? QStringLiteral("1 région écartée")
                                 : QStringLiteral("%1 régions écartées").arg(discarded));
    }
    if (others > 0) {
        parts << (others == 1 ? QStringLiteral("1 avertissement")
                              : QStringLiteral("%1 avertissements").arg(others));
    }
    return parts.join(QStringLiteral(", "));
}

void RegionController::setStale(bool stale, const QString& reason) {
    if (stale) {
        staleSources_.insert_or_assign(observedImageSource_, reason);
    } else {
        staleSources_.erase(observedImageSource_);
    }
    if (stale_ == stale && staleReason_ == reason) {
        return;
    }
    stale_ = stale;
    staleReason_ = stale ? reason : QString();
    emit staleChanged();
}

void RegionController::setTool(const QString& tool) {
    if ((tool != QLatin1String("select") && tool != QLatin1String("lasso") &&
         tool != QLatin1String("brush") && tool != QLatin1String("erase")) ||
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
    const PerfScope perf("region.refresh");
    const auto& project = studio_.session().project();
    regions_.clear();
    for (const auto& info : listRegions(project)) {
        regions_.append(
            QVariantMap{{QStringLiteral("id"), static_cast<qulonglong>(info.id)},
                        {QStringLiteral("color"), hexOf(info.rgb)},
                        {QStringLiteral("pixels"), static_cast<qulonglong>(info.pixels)},
                        {QStringLiteral("locked"), locked_.find(info.id) != locked_.end()}});
    }
    // Élague la sélection : une région fusionnée ou supprimée n'existe plus.
    const auto before = selected_.size();
    std::erase_if(selected_, [&](std::uint64_t id) {
        return !project.imageSource().segmentation ||
               !project.imageSource().segmentation->find(RegionId{id});
    });
    // Idem pour le verrouillage : état de session, pas de raison de le garder pour une région
    // qui n'existe plus (fusionnée, supprimée).
    std::erase_if(locked_, [&](std::uint64_t id) {
        return !project.imageSource().segmentation ||
               !project.imageSource().segmentation->find(RegionId{id});
    });
    if (project.imageSource().segmentation && project.imageSource().segmentation->width > 0 &&
        project.imageSource().segmentation->height > 0) {
        const auto& seg = *project.imageSource().segmentation;
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
    if (mergeTargetMode_ && selected_.size() != 1) {
        setMergeTargetMode(false);
    }
}

// --- Verrouillage (état de session) --------------------------------------------------------

void RegionController::applyLockFlags() {
    for (int i = 0; i < regions_.size(); ++i) {
        auto row = regions_[i].toMap();
        const auto id = idOf(row.value(QStringLiteral("id")).toDouble());
        row[QStringLiteral("locked")] = locked_.find(id) != locked_.end();
        regions_[i] = row;
    }
}

bool RegionController::isRegionLocked(double id) const {
    return locked_.find(idOf(id)) != locked_.end();
}

void RegionController::toggleLockRegion(double id) {
    const auto region = idOf(id);
    if (region == 0) {
        return;
    }
    if (locked_.erase(region) == 0) {
        locked_.insert(region);
        // Verrouiller retire la région de la sélection : on ne retouche pas une région verrouillée.
        const auto found = std::find(selected_.begin(), selected_.end(), region);
        if (found != selected_.end()) {
            selected_.erase(found);
            if (mergeTargetMode_) {
                setMergeTargetMode(false);
            }
            emit selectionChanged();
        }
    }
    applyLockFlags();
    emit regionsChanged();
}

// --- Sélection ----------------------------------------------------------------------------

void RegionController::selectRegion(double id, const QString& mode) {
    const PerfScope perf("region.select");
    const auto region = idOf(id);
    const auto& seg = studio_.session().project().imageSource().segmentation;
    if (!seg || region == 0 || !seg->find(RegionId{region})) {
        return;
    }
    if (locked_.find(region) != locked_.end()) {
        setMessage(QStringLiteral("Région verrouillée : déverrouillez-la pour la sélectionner."),
                   true);
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
    const PerfScope perf("region.selectAtPixel");
    const auto id = regionAtPixel(studio_.session().project(), x, y);
    if (mergeTargetMode_) {
        // Le clic désigne la cible : il ne doit jamais remplacer la région source.
        if (id) {
            (void)mergeSelectedInto(static_cast<double>(*id));
        }
        return;
    }
    if (!id) {
        if (!additive) {
            clearSelection();
        }
        return;
    }
    selectRegion(static_cast<double>(*id),
                 additive ? QStringLiteral("toggle") : QStringLiteral("replace"));
}

int RegionController::selectInZone(const QVariantList& polygonPx, bool additive, bool touching) {
    const PerfScope perf("region.selectInZone");
    if (mergeTargetMode_) {
        return 0; // le choix de cible attend UNE région cliquée, pas une zone
    }
    std::vector<PixelPointF> polygon;
    polygon.reserve(static_cast<std::size_t>(polygonPx.size()));
    for (const auto& value : polygonPx) {
        const auto point = value.toMap();
        polygon.push_back({point.value(QStringLiteral("x")).toDouble(),
                           point.value(QStringLiteral("y")).toDouble()});
    }
    auto found = regionsInZone(studio_.session().project(), polygon,
                               touching ? ZoneSelectMode::Touching : ZoneSelectMode::Enclosed);
    std::erase_if(found, [this](std::uint64_t id) { return locked_.find(id) != locked_.end(); });
    const int count = static_cast<int>(found.size());
    auto next = additive ? selected_ : std::vector<std::uint64_t>{};
    for (const auto id : found) {
        if (std::find(next.begin(), next.end(), id) == next.end()) {
            next.push_back(id);
        }
    }
    if (count == 0) {
        setMessage(touching ? QStringLiteral("Aucune région dans la zone.")
                            : QStringLiteral("Aucune région entièrement dans la zone : maintenez "
                                             "Alt pour prendre aussi les régions touchées."));
    } else {
        setMessage(count == 1 ? QStringLiteral("1 région sélectionnée par la zone.")
                              : QStringLiteral("%1 régions sélectionnées par la zone.").arg(count));
    }
    if (next != selected_) {
        selected_ = std::move(next);
        emit selectionChanged();
    }
    return count;
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
    std::erase_if(all, [this](std::uint64_t id) { return locked_.find(id) != locked_.end(); });
    selected_ = std::move(all);
    emit selectionChanged();
}

void RegionController::selectNeighbours() {
    if (selected_.empty()) {
        return;
    }
    auto neighbours = neighboursOfSet(studio_.session().project(), selected_);
    std::erase_if(neighbours,
                  [this](std::uint64_t id) { return locked_.find(id) != locked_.end(); });
    selected_ = std::move(neighbours);
    emit selectionChanged();
}

bool RegionController::stepRegion(int direction) {
    const int count = static_cast<int>(regions_.size());
    if (count == 0) {
        return false;
    }
    int index =
        direction > 0 ? -1 : count; // pas de sélection unique : on part du début / de la fin
    if (selected_.size() == 1) {
        for (int i = 0; i < count; ++i) {
            if (idOf(regions_[i].toMap().value(QStringLiteral("id")).toDouble()) ==
                selected_.front()) {
                index = i;
                break;
            }
        }
    }
    // Saute les régions verrouillées ; si elles le sont toutes, rien à sélectionner.
    for (int step = 0; step < count; ++step) {
        index = ((index + direction) % count + count) % count;
        const auto candidate = idOf(regions_[index].toMap().value(QStringLiteral("id")).toDouble());
        if (locked_.find(candidate) == locked_.end()) {
            selectRegion(static_cast<double>(candidate));
            return true;
        }
    }
    return false;
}

bool RegionController::selectNextRegion() {
    return stepRegion(1);
}

bool RegionController::selectPreviousRegion() {
    return stepRegion(-1);
}

void RegionController::stepBrushRadius(int steps) {
    setBrushRadius(brushRadius_ + steps);
}

void RegionController::invertSelection() {
    auto inverted = invertRegionSet(studio_.session().project(), selected_);
    std::erase_if(inverted, [this](std::uint64_t id) { return locked_.find(id) != locked_.end(); });
    selected_ = std::move(inverted);
    emit selectionChanged();
}

void RegionController::selectAll() {
    auto all = allRegionIds(studio_.session().project());
    std::erase_if(all, [this](std::uint64_t id) { return locked_.find(id) != locked_.end(); });
    selected_ = std::move(all);
    emit selectionChanged();
}

void RegionController::clearSelection() {
    if (selected_.empty())
        return;
    setMergeTargetMode(false); // plus de région source : le choix de cible n'a plus d'objet
    selected_.clear();
    emit selectionChanged();
}

bool RegionController::adoptSessionSelection() {
    const auto& seg = studio_.session().project().imageSource().segmentation;
    const auto& shared = studio_.session().selection();
    if (!seg || adoptedSelectionRevision_ == shared.revision()) {
        return false;
    }
    adoptedSelectionRevision_ = shared.revision();
    std::vector<std::uint64_t> wanted;
    for (const auto& target : shared.targets()) {
        if (target.kind == TargetKind::Region && seg->find(RegionId{target.id}) &&
            std::find(wanted.begin(), wanted.end(), target.id) == wanted.end()) {
            wanted.push_back(target.id);
        }
    }
    if (wanted.empty()) {
        return false;
    }
    // Une autre région devient la source : un choix de cible resté ouvert la fusionnerait au
    // prochain clic.
    setMergeTargetMode(false);
    if (wanted != selected_) {
        selected_ = std::move(wanted);
        emit selectionChanged();
    }
    return true;
}

void RegionController::setMergeTargetMode(bool on) {
    if (on && selected_.size() != 1) {
        setMessage(QStringLiteral("Sélectionnez une seule région source."), true);
        on = false;
    }
    if (on == mergeTargetMode_) {
        return;
    }
    mergeTargetMode_ = on;
    emit mergeTargetModeChanged();
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

QString RegionController::mergeSelectedInto(double target) {
    if (selected_.size() != 1) {
        const auto text = QStringLiteral("Sélectionnez une seule région source.");
        setMessage(text, true);
        return text;
    }
    if (isRegionLocked(target)) {
        const auto text = QStringLiteral("Région cible verrouillée : déverrouillez-la d'abord.");
        setMessage(text, true);
        return text;
    }
    // The target keeps its region ID and colour; the source is absorbed in
    // one undoable document command, rather than merging into an arbitrary
    // first neighbour.
    auto refusal = report(mergeRegionInto(studio_.session(), idOf(target), selected_.front()),
                          QStringLiteral("Région fusionnée dans la région cible."));
    if (refusal.isEmpty()) {
        setMergeTargetMode(false);
    }
    return refusal;
}

QString RegionController::mergeWithNeighbour(double id) {
    const auto region = idOf(id);
    if (locked_.find(region) != locked_.end()) {
        const auto text = QStringLiteral("Région verrouillée : déverrouillez-la d'abord.");
        setMessage(text, true);
        return text;
    }
    const auto all = neighbourRegions(studio_.session().project(), region);
    std::vector<std::uint64_t> neighbours;
    std::ranges::copy_if(all, std::back_inserter(neighbours),
                         [this](std::uint64_t n) { return locked_.find(n) == locked_.end(); });
    if (neighbours.empty()) {
        const auto text = QStringLiteral("Cette région n'a pas de voisine (libre).");
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

QString RegionController::splitSelected() {
    if (selected_.size() != 1) {
        const auto text = QStringLiteral("Sélectionnez une seule région.");
        setMessage(text, true);
        return text;
    }
    return report(splitRegion(studio_.session(), selected_.front()),
                  QStringLiteral("Région séparée en plusieurs morceaux."));
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
    const PerfScope perf("region.paintStroke");
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

void RegionController::regenerate(bool resegment, bool confirmed, const QVariantMap& request) {
    // Réglages satin du panneau, sauf s'ils sont déjà dans la demande (reprise après confirmation).
    QVariantMap settings = request;
    const QVariantMap satin = studio_.satinOptions();
    for (auto it = satin.cbegin(); it != satin.cend(); ++it) {
        if (!settings.contains(it.key())) {
            settings.insert(it.key(), it.value());
        }
    }
    if (!confirmed) {
        const auto edits = countHandEdits(studio_.session().project());
        if (edits.retouchedObjects > 0) {
            // La demande est mémorisée ici : quel que soit le panneau qui confirme, c'est bien
            // elle (même mode, mêmes réglages) qui sera exécutée.
            pendingResegment_ = resegment;
            pendingSettings_ = settings;
            confirmText_ =
                QStringLiteral(
                    "%1 objet(s) portent des retouches de points qui seront remplacées "
                    "par la nouvelle génération. Les %2 objet(s) dessinés à la main sont "
                    "conservés. Vous pourrez tout annuler avec Ctrl+Z.")
                    .arg(edits.retouchedObjects)
                    .arg(edits.handDrawnObjects);
            emit confirmChanged();
            emit confirmRequested(confirmText_);
            return;
        }
    }
    if (!confirmText_.isEmpty()) {
        confirmText_.clear();
        emit confirmChanged();
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
    // Mêmes clés et mêmes bornes que l'aperçu (`segmentationSettingsFrom`), plus le satin.
    const project_ops::DigitizeSettings digitize = objectDigitizeSettingsFrom(settings);
    lastDigitizeSettings_ = digitize;
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    const image::Image image = std::move(*working);
    setWarnings({}); // les avertissements décrivent la génération précédente
    busy_ = true;
    emit busyChanged();
    task_ = tasks_.submit<Regeneration>(
        "Régénération des objets",
        [snapshot, revision, image, digitize, resegment](const TaskContext& task) {
            return resegment ? resegmentObjects(snapshot, revision, image, digitize, task)
                             : regenerateObjects(snapshot, revision, image, digitize, task);
        },
        [this](Result<Regeneration>&& r) { finishRegeneration(std::move(r)); });
}

void RegionController::finishRegeneration(Result<Regeneration>&& r) {
    task_ = 0;
    busy_ = false;
    emit busyChanged();
    if (!r) {
        setMessage(QStringLiteral("Régénération impossible : ") + errorText(r.error()), true);
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
        kept += QStringLiteral(" %1 objet(s) dessiné(s) à la main conservé(s).").arg(r->preserved);
    }
    if (r->lostRetouches > 0) {
        kept += QStringLiteral(" %1 objet(s) retouché(s) remplacé(s).").arg(r->lostRetouches);
    }
    if (r->reused > 0) {
        kept += QStringLiteral(" %1 objet(s) inchangé(s) réutilisé(s).").arg(r->reused);
    }
    generatedSatin_ = lastDigitizeSettings_; // référence du satin « à jour »
    setWarnings(r->warnings);
    if (!warnings_.isEmpty()) {
        kept += QStringLiteral(" %1 : voir le détail.").arg(warningSummary());
    }
    setMessage(QStringLiteral("Objets régénérés : %1 → %2. Annulable (Ctrl+Z).")
                       .arg(r->objectsBefore)
                       .arg(r->objectsAfter) +
                   kept,
               false);
    emit regenerated(static_cast<int>(r->objectsBefore), static_cast<int>(r->objectsAfter));
}

void RegionController::confirmRegeneration() {
    if (confirmText_.isEmpty()) {
        return;
    }
    regenerate(pendingResegment_, true, pendingSettings_);
}

void RegionController::cancelConfirmation() {
    if (confirmText_.isEmpty()) {
        return;
    }
    confirmText_.clear();
    pendingSettings_.clear();
    emit confirmChanged();
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
