// SPDX-License-Identifier: Apache-2.0
#include "view_controller.hpp"
#include "change_gate.hpp"

#include "inspector_model.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_render/segments.hpp"

namespace openstitch::desktop_v2::studio {

struct ViewController::SimData {
    std::shared_ptr<const document::Project> project;
    std::shared_ptr<const stitch::StitchSequence> sequence;
    SimulationMarks marks;
    std::set<std::uint64_t> visible;
    bool imported{false};
    std::vector<OverlayJump> jumps;
    std::vector<std::uint32_t> stitchesBefore; // size = commands + 1
    std::vector<std::uint32_t> segmentsBefore; // visible segments prefix, size = commands + 1
    std::uint64_t revision{0};
};

namespace {
constexpr std::size_t kMaxJumpLines = 20'000;

std::string valueOf(const std::string& text, const std::string& key) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind(key + "=", 0) == 0) {
            return line.substr(key.size() + 1);
        }
    }
    return {};
}

bool flag(const std::string& text, const std::string& key, bool fallback) {
    const auto v = valueOf(text, key);
    return v == "1" ? true : v == "0" ? false : fallback;
}

double number(const std::string& text, const std::string& key, double fallback, double lo,
              double hi) {
    const auto v = valueOf(text, key);
    if (v.empty()) {
        return fallback;
    }
    try {
        const double parsed = std::stod(v);
        return std::isfinite(parsed) && parsed >= lo && parsed <= hi ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

QString clock(double seconds) {
    const int total = static_cast<int>(std::lround(std::max(0.0, seconds)));
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

ViewController::ViewController(StudioController& studio, std::filesystem::path configDir,
                               QObject* parent)
    : QObject(parent), studio_(studio) {
    if (!configDir.empty()) {
        configFile_ = configDir / "studio-view.txt";
        load();
    }
    gateStamp_ = documentStamp(studio_);
    connect(&studio_, &StudioController::changed, this, [this] {
        const auto stamp = documentStamp(studio_);
        if (stamp != gateStamp_) { // filtres et compteurs : pas de recalcul pour un statut
            gateStamp_ = stamp;
            emit changed();
        }
    });
    connect(&studio_, &StudioController::sceneChanged, this, &ViewController::onSceneChanged);
    pump_.setInterval(8);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
    playTimer_.setInterval(33);
    connect(&playTimer_, &QTimer::timeout, this, [this] { advance(33); });
    if (studio_.scene()) {
        rebuildSimData();
    }
}

ViewController::~ViewController() {
    pump_.stop();
    playTimer_.stop();
    tasks_.cancelAll();
}

void ViewController::load() {
    std::ifstream in(configFile_);
    if (!in) {
        return;
    }
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    grid_ = flag(text, "grid", grid_);
    gridStep_ = number(text, "grid_step_mm", gridStep_, 0.5, 500.0);
    guidesVisible_ = flag(text, "guides", guidesVisible_);
    rulers_ = flag(text, "rulers", rulers_);
    layerStitches_ = flag(text, "layer_stitches", layerStitches_);
    layerJumps_ = flag(text, "layer_jumps", layerJumps_);
    layerHoop_ = flag(text, "layer_hoop", layerHoop_);
    layerDensity_ = flag(text, "layer_density", layerDensity_);
    layerRisk_ = flag(text, "layer_risk", layerRisk_);
    pxPerMm100_ = number(text, "px_per_mm_100", pxPerMm100_, 1.0, 40.0);
    spm_ = static_cast<int>(number(text, "machine_spm", spm_, 100, 1500));
    speed_ = number(text, "sim_speed", speed_, 0.25, 256.0);
}

void ViewController::save() const {
    if (configFile_.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(configFile_.parent_path(), ec);
    const auto temp = configFile_.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            return;
        }
        out << "grid=" << grid_ << "\ngrid_step_mm=" << gridStep_ << "\nguides=" << guidesVisible_
            << "\nrulers=" << rulers_ << "\nlayer_stitches=" << layerStitches_
            << "\nlayer_jumps=" << layerJumps_ << "\nlayer_hoop=" << layerHoop_
            << "\nlayer_density=" << layerDensity_ << "\nlayer_risk=" << layerRisk_
            << "\npx_per_mm_100=" << pxPerMm100_ << "\nmachine_spm=" << spm_
            << "\nsim_speed=" << speed_ << "\n";
    }
    std::filesystem::rename(temp, configFile_, ec);
}

void ViewController::attachCanvas(QObject* canvas) {
    auto* next = qobject_cast<GpuCanvas*>(canvas);
    if (canvas_ == next) {
        return;
    }
    if (canvas_) {
        disconnect(canvas_, nullptr, this, nullptr);
    }
    canvas_ = next;
    if (canvas_) {
        connect(canvas_, &GpuCanvas::destroyed, this, [this] { canvas_.clear(); });
        connect(canvas_, &GpuCanvas::diagnosticsChanged, this, [this] {
            // Le backend n'est connu qu'après le premier rendu : republier si le chemin bascule.
            if (realistic_ && realisticOnGpu() != lastRealisticGpu_) {
                lastRealisticGpu_ = realisticOnGpu();
                publish();
            }
            emit realisticModeChanged();
        });
    }
    publish();
}

void ViewController::setRealistic(bool value) {
    if (realistic_ == value) {
        return;
    }
    realistic_ = value;
    publish(); // GPU : le calque est masqué / affiché, rien n'est recalculé si la scène est la même
    emit realisticChanged();
    emit realisticModeChanged();
}

QString ViewController::realisticMode() const {
    if (!realistic_) {
        return {};
    }
    return realisticOnGpu() ? QStringLiteral("Réaliste · GPU")
                            : QStringLiteral("Réaliste · CPU (repli)");
}

void ViewController::setRealisticStyle(const RenderSettings& settings) {
    const auto style = realisticStyleFrom(settings);
    if (style == realisticStyle_) {
        return;
    }
    realisticStyle_ = style;
    if (realistic_) {
        publish();
    }
}

void ViewController::say(const QString& text) {
    message_ = text;
    emit messageChanged();
}

int ViewController::hiddenCount() const {
    return static_cast<int>(hiddenObjectCount(studio_.session().project()));
}

// --- Filters -----------------------------------------------------------------------------------

bool ViewController::showOnlyColour(const QString& hex) {
    const QColor colour(hex);
    if (!colour.isValid()) {
        say(QStringLiteral("Couleur invalide : %1").arg(hex));
        return false;
    }
    const auto result =
        desktop_v2::showOnlyColour(studio_.session(), {static_cast<std::uint8_t>(colour.red()),
                                                       static_cast<std::uint8_t>(colour.green()),
                                                       static_cast<std::uint8_t>(colour.blue())});
    say(result.ok ? QStringLiteral("Seule la couleur %1 est affichée").arg(colour.name())
                  : QString::fromStdString(result.message));
    return result.ok;
}

bool ViewController::showOnlyType(const QString& typeId) {
    const auto result = desktop_v2::showOnlyType(studio_.session(), typeId.toStdString());
    say(result.ok ? QStringLiteral("Seuls les points « %1 » sont affichés").arg(typeId)
                  : QString::fromStdString(result.message));
    return result.ok;
}

namespace {
QVariantMap groupMap(const FilterGroup& g, const QString& label) {
    return {{QStringLiteral("id"), QString::fromStdString(g.key)},
            {QStringLiteral("label"), label},
            {QStringLiteral("total"), g.total},
            {QStringLiteral("visible"), g.visible},
            {QStringLiteral("checked"), g.visible > 0},
            {QStringLiteral("partial"), g.visible > 0 && g.visible < g.total}};
}

QString typeLabelOf(const std::string& typeId) {
    const std::string settable = settableIdOf(typeId);
    for (const auto& t : settableStitchTypes()) {
        if (t.id == settable) {
            return QString::fromUtf8(t.label.data(), static_cast<qsizetype>(t.label.size()));
        }
    }
    return typeId == "satin" ? QStringLiteral("Satin (colonnes)") : QString::fromStdString(typeId);
}
} // namespace

QVariantList ViewController::filterTypes() const {
    QVariantList out;
    for (const auto& g : typeGroups(studio_.session().project())) {
        out.push_back(groupMap(g, typeLabelOf(g.key)));
    }
    return out;
}

QVariantList ViewController::filterColours() const {
    QVariantList out;
    for (const auto& g : colourGroups(studio_.session().project())) {
        out.push_back(groupMap(g, QString::fromStdString(g.key)));
    }
    return out;
}

bool ViewController::setTypeVisible(const QString& typeId, bool visible) {
    const auto result =
        desktop_v2::setTypeVisible(studio_.session(), typeId.toStdString(), visible);
    say(result.ok ? (visible ? QStringLiteral("Type de points affiché")
                             : QStringLiteral("Type de points masqué"))
                  : QString::fromStdString(result.message));
    return result.ok;
}

bool ViewController::setColourVisible(const QString& hex, bool visible) {
    const QColor colour(hex);
    if (!colour.isValid()) {
        say(QStringLiteral("Couleur invalide : %1").arg(hex));
        return false;
    }
    const auto result = desktop_v2::setColourVisible(studio_.session(),
                                                     {static_cast<std::uint8_t>(colour.red()),
                                                      static_cast<std::uint8_t>(colour.green()),
                                                      static_cast<std::uint8_t>(colour.blue())},
                                                     visible);
    say(result.ok
            ? (visible ? QStringLiteral("Couleur affichée") : QStringLiteral("Couleur masquée"))
            : QString::fromStdString(result.message));
    return result.ok;
}

bool ViewController::setAllColours(const QString& mode) {
    const auto result = desktop_v2::setAllColours(studio_.session(), mode.toStdString());
    say(result.ok ? QStringLiteral("Couleurs mises à jour")
                  : QString::fromStdString(result.message));
    return result.ok;
}

bool ViewController::hideSmallerThan(double minAreaMm2) {
    const auto result = desktop_v2::hideSmallerThan(studio_.session(), minAreaMm2);
    say(result.ok ? QStringLiteral("Petites zones masquées")
                  : QString::fromStdString(result.message));
    return result.ok;
}

bool ViewController::showAll() {
    const auto result = desktop_v2::showAllObjects(studio_.session());
    say(result.ok ? QStringLiteral("Tous les objets sont affichés")
                  : QString::fromStdString(result.message));
    return result.ok;
}

// --- Grid, guides, layers ------------------------------------------------------------------------

void ViewController::setGridVisible(bool value) {
    if (value != grid_) {
        grid_ = value;
        save();
        publish();
        emit displayChanged();
    }
}

void ViewController::setGridStepMm(double value) {
    if (!std::isfinite(value) || value < 0.5 || value > 500.0) {
        say(QStringLiteral("Pas de grille refusé : entre 0,5 et 500 mm"));
        return;
    }
    if (value != gridStep_) {
        gridStep_ = value;
        save();
        publish();
        emit displayChanged();
    }
}

void ViewController::setGuidesVisible(bool value) {
    if (value != guidesVisible_) {
        guidesVisible_ = value;
        save();
        publish();
        emit displayChanged();
    }
}

void ViewController::setRulersVisible(bool value) {
    if (value != rulers_) {
        rulers_ = value;
        save();
        emit displayChanged();
    }
}

QVariantList ViewController::guides() const {
    QVariantList list;
    for (const double x : guidesX_) {
        list.push_back(
            QVariantMap{{QStringLiteral("vertical"), true}, {QStringLiteral("positionMm"), x}});
    }
    for (const double y : guidesY_) {
        list.push_back(
            QVariantMap{{QStringLiteral("vertical"), false}, {QStringLiteral("positionMm"), y}});
    }
    return list;
}

bool ViewController::addGuide(bool vertical, double positionMm) {
    if (!std::isfinite(positionMm) || std::abs(positionMm) > 5000.0) {
        say(QStringLiteral("Position de guide invalide"));
        return false;
    }
    auto& list = vertical ? guidesX_ : guidesY_;
    if (std::find(list.begin(), list.end(), positionMm) != list.end()) {
        say(QStringLiteral("Un guide existe déjà à cette position"));
        return false;
    }
    list.push_back(positionMm);
    std::sort(list.begin(), list.end());
    guidesVisible_ = true;
    publish();
    emit displayChanged();
    return true;
}

bool ViewController::removeGuide(int index) {
    if (index < 0 || index >= static_cast<int>(guidesX_.size() + guidesY_.size())) {
        return false;
    }
    if (index < static_cast<int>(guidesX_.size())) {
        guidesX_.erase(guidesX_.begin() + index);
    } else {
        guidesY_.erase(guidesY_.begin() + (index - static_cast<int>(guidesX_.size())));
    }
    publish();
    emit displayChanged();
    return true;
}

void ViewController::clearGuides() {
    if (guidesX_.empty() && guidesY_.empty()) {
        say(QStringLiteral("Aucun guide à effacer"));
        return;
    }
    guidesX_.clear();
    guidesY_.clear();
    publish();
    emit displayChanged();
}

void ViewController::setGuideColor(const QColor& color) {
    if (color.isValid() && color != guideColor_) {
        guideColor_ = color;
        publish();
        emit displayChanged();
    }
}

#define OS_LAYER_SETTER(Name, member)                                                              \
    void ViewController::set##Name(bool value) {                                                   \
        if (value != member) {                                                                     \
            member = value;                                                                        \
            save();                                                                                \
            publish();                                                                             \
            emit displayChanged();                                                                 \
        }                                                                                          \
    }
OS_LAYER_SETTER(LayerStitches, layerStitches_)
OS_LAYER_SETTER(LayerJumps, layerJumps_)
OS_LAYER_SETTER(LayerHoop, layerHoop_)
OS_LAYER_SETTER(LayerDensity, layerDensity_)
OS_LAYER_SETTER(LayerRisk, layerRisk_)
#undef OS_LAYER_SETTER

void ViewController::setAnalysisImage(const QString& key, std::optional<OverlayImage> image) {
    if (image) {
        images_[key] = std::move(*image);
    } else {
        images_.erase(key);
    }
    publish();
    emit displayChanged();
}

void ViewController::setMarkers(std::vector<OverlayMarker> markers) {
    markers_ = std::move(markers);
    publish();
}

// --- Zoom ------------------------------------------------------------------------------------

void ViewController::zoomRectangle() {
    if (!canvas_) {
        say(QStringLiteral("Aucun canevas pour zoomer"));
        return;
    }
    canvas_->setZoomRectMode(true);
    say(QStringLiteral("Tracez un rectangle pour zoomer"));
}

bool ViewController::zoomSelection() {
    const bool ok = canvas_ && canvas_->zoomToSelection();
    if (!ok) {
        say(QStringLiteral("Sélectionnez d'abord un objet à cadrer"));
    }
    return ok;
}

void ViewController::zoomActualSize() {
    if (canvas_) {
        canvas_->zoomToScale(pxPerMm100_);
    }
}

bool ViewController::zoomPreviousView() {
    const bool ok = canvas_ && canvas_->zoomPrevious();
    if (!ok) {
        say(QStringLiteral("Pas de vue précédente"));
    }
    return ok;
}

bool ViewController::calibrateZoom100(double measuredMm, double referenceMm) {
    if (!canvas_ || !std::isfinite(measuredMm) || !std::isfinite(referenceMm) || measuredMm < 1.0 ||
        referenceMm < 1.0) {
        say(QStringLiteral("Mesure invalide : indiquez la longueur réelle en mm"));
        return false;
    }
    const double ppm = canvas_->zoom() * referenceMm / measuredMm;
    if (ppm < 1.0 || ppm > 40.0) {
        say(QStringLiteral("Mesure incohérente avec l'écran (1 à 40 px/mm attendus)"));
        return false;
    }
    pxPerMm100_ = ppm;
    save();
    emit displayChanged();
    say(QStringLiteral("Zoom 100 % étalonné : %1 px/mm").arg(ppm, 0, 'f', 2));
    return true;
}

void ViewController::centerOnModel(double xMm, double yMm) {
    if (canvas_) {
        canvas_->centerOnScene(xMm, -yMm);
    }
}

// --- Simulation ----------------------------------------------------------------------------------

void ViewController::onSceneChanged() {
    const auto& scene = studio_.scene();
    if (!scene) {
        sim_.reset();
        tasks_.cancelAll();
        if (playing_) {
            pause();
        }
        emit simChanged();
        return;
    }
    rebuildSimData();
    if (realistic_ && !realisticOnGpu()) {
        emit realisticRefreshRequested();
    }
}

void ViewController::rebuildSimData() {
    const auto scene = studio_.scene();
    if (!scene) {
        return;
    }
    tasks_.cancelAll();
    const auto revision = scene->revision;
    const auto project = scene->project;
    tasks_.submit<std::shared_ptr<const SimData>>(
        "studio.simulation",
        [project, revision, cachedSequence = scene->effectiveSequence](
            const TaskContext& task) -> Result<std::shared_ptr<const SimData>> {
            // Reuse the sequence used to build the scene. Empty scenes without stitches
            // retain the original fallback behaviour.
            auto sequence = cachedSequence;
            if (!sequence) {
                auto generated = stitch_generation::effective_sequence(*project, task);
                if (!generated) {
                    return std::unexpected(generated.error());
                }
                sequence = std::make_shared<const stitch::StitchSequence>(std::move(*generated));
            }
            auto data = std::make_shared<SimData>();
            data->project = project;
            data->revision = revision;
            data->imported = project->imported_design.has_value();
            for (const auto& object : project->embroidery_objects) {
                if (object.visible && project->calqueVisible(object)) {
                    data->visible.insert(object.id.value);
                }
            }
            data->sequence = std::move(sequence);
            data->marks = simulationMarks(*data->sequence);
            const auto& commands = data->sequence->commands;
            data->stitchesBefore.reserve(commands.size() + 1);
            data->segmentsBefore.reserve(commands.size() + 1);
            std::uint32_t stitches = 0;
            std::uint32_t segments = 0;
            bool hasPos = false;
            float lastX = 0.0F;
            float lastY = 0.0F;
            std::uint64_t lastSource = 0;
            for (std::size_t i = 0; i < commands.size(); ++i) {
                data->stitchesBefore.push_back(stitches);
                data->segmentsBefore.push_back(segments);
                const auto& cmd = commands[i];
                const bool sews = cmd.type == stitch::CommandType::Stitch;
                const bool jumps = cmd.type == stitch::CommandType::Jump;
                if (sews) {
                    ++stitches;
                }
                if (!sews && !jumps) {
                    hasPos = false;
                } else {
                    // Reproduit exactement les règles de build_thread_segments : même
                    // source, mêmes conversions float mm, saut/coupe/couleur qui rompt
                    // la continuité. Le coût est payé UNE fois sur le worker.
                    const float x = static_cast<float>(cmd.pos.x.value / 1000.0);
                    const float y = static_cast<float>(-cmd.pos.y.value / 1000.0);
                    const auto source = cmd.source.value;
                    const bool moved = x != lastX || y != lastY;
                    const bool visible =
                        data->visible.contains(source) || (source == 0 && data->imported);
                    if (sews && hasPos && lastSource == source && moved && visible) {
                        ++segments;
                    }
                    lastX = x;
                    lastY = y;
                    lastSource = source;
                    hasPos = true;
                }
                if (commands[i].type == stitch::CommandType::Jump && i > 0 &&
                    data->jumps.size() < kMaxJumpLines) {
                    const auto& a = commands[i - 1].pos;
                    const auto& b = commands[i].pos;
                    data->jumps.push_back({a.x.value / 1000.0, -a.y.value / 1000.0,
                                           b.x.value / 1000.0, -b.y.value / 1000.0});
                }
                if ((i % 8192) == 0 && task.cancelled()) {
                    return cancelled_error();
                }
            }
            data->stitchesBefore.push_back(stitches);
            data->segmentsBefore.push_back(segments);
            return std::shared_ptr<const SimData>(std::move(data));
        },
        [this, revision](Result<std::shared_ptr<const SimData>>&& result) {
            const auto scene = studio_.scene();
            if (!result || !scene || scene->revision != revision) {
                return;
            }
            sim_ = std::move(*result);
            position_ = std::min<std::size_t>(position_, sim_->sequence->commands.size());
            publish();
            emit simChanged();
        });
}

int ViewController::simTotal() const {
    return sim_ ? static_cast<int>(sim_->sequence->commands.size()) : 0;
}

std::size_t ViewController::segmentsAt(std::size_t position) const {
    if (!sim_ || position >= sim_->sequence->commands.size()) {
        return CanvasOverlays::all;
    }
    // O(1) pendant la lecture et le scrub ; auparavant O(position) et allocation
    // d'une liste complète de segments à chaque frame de 33 ms.
    return sim_->segmentsBefore[position];
}

void ViewController::goTo(std::size_t position) {
    if (!sim_) {
        return;
    }
    position = std::min(position, sim_->sequence->commands.size());
    if (position == position_ && simActive_) {
        return;
    }
    position_ = position;
    simActive_ = true;
    publish();
    emit simChanged();
}

void ViewController::startSimulation() {
    if (!sim_) {
        say(studio_.busy() ? QStringLiteral("La simulation se prépare…")
                           : QStringLiteral("Rien à simuler : ouvrez une broderie"));
        return;
    }
    simActive_ = true;
    position_ = 0;
    publish();
    emit simChanged();
}

void ViewController::stopSimulation() {
    pause();
    simActive_ = false;
    position_ = sim_ ? sim_->sequence->commands.size() : 0;
    publish();
    emit simChanged();
}

void ViewController::play() {
    if (!sim_) {
        return;
    }
    if (!simActive_ || position_ >= sim_->sequence->commands.size()) {
        position_ = 0;
        simActive_ = true;
    }
    playing_ = true;
    carry_ = 0;
    playTimer_.start();
    publish();
    emit simChanged();
}

void ViewController::pause() {
    if (playing_) {
        playing_ = false;
        playTimer_.stop();
        emit simChanged();
    }
}

void ViewController::advance(int milliseconds) {
    if (!sim_ || !playing_ || milliseconds <= 0) {
        return;
    }
    carry_ += spm_ / 60.0 * speed_ * milliseconds / 1000.0;
    const auto whole = static_cast<std::size_t>(carry_);
    carry_ -= static_cast<double>(whole);
    const auto total = sim_->sequence->commands.size();
    const auto next = std::min(total, position_ + std::max<std::size_t>(1, whole));
    position_ = next;
    if (next >= total) {
        playing_ = false;
        playTimer_.stop();
    }
    publish();
    emit simChanged();
}

void ViewController::step(int delta) {
    if (!sim_) {
        return;
    }
    pause();
    const auto current =
        static_cast<std::int64_t>(simActive_ ? position_ : sim_->sequence->commands.size());
    const auto target = std::clamp<std::int64_t>(
        current + delta, 0, static_cast<std::int64_t>(sim_->sequence->commands.size()));
    simActive_ = false; // force goTo to publish even at the same position
    goTo(static_cast<std::size_t>(target));
}

void ViewController::setSimPosition(int value) {
    if (value < 0) {
        return;
    }
    simActive_ = false;
    goTo(static_cast<std::size_t>(value));
}

void ViewController::nextObject() {
    if (sim_) {
        pause();
        simActive_ = false;
        goTo(nextMark(sim_->marks.objects, position_, sim_->marks.total));
    }
}

void ViewController::previousObject() {
    if (sim_) {
        pause();
        simActive_ = false;
        goTo(previousMark(sim_->marks.objects, position_));
    }
}

void ViewController::nextColour() {
    if (sim_) {
        pause();
        simActive_ = false;
        goTo(nextMark(sim_->marks.colours, position_, sim_->marks.total));
    }
}

void ViewController::previousColour() {
    if (sim_) {
        pause();
        simActive_ = false;
        goTo(previousMark(sim_->marks.colours, position_));
    }
}

void ViewController::toEnd() {
    if (sim_) {
        pause();
        simActive_ = false;
        goTo(sim_->marks.total);
    }
}

void ViewController::toStart() {
    if (sim_) {
        pause();
        simActive_ = false;
        goTo(0);
    }
}

void ViewController::setSimSpeed(double value) {
    if (!std::isfinite(value) || value < 0.25 || value > 256.0) {
        say(QStringLiteral("Vitesse refusée : entre ×0,25 et ×256"));
        return;
    }
    speed_ = value;
    save();
    emit simChanged();
}

void ViewController::setMachineSpm(int value) {
    if (value < 100 || value > 1500) {
        say(QStringLiteral("Vitesse machine refusée : 100 à 1500 points/min"));
        return;
    }
    spm_ = value;
    save();
    emit simChanged();
}

QString ViewController::simTimeText() const {
    if (!sim_) {
        return QStringLiteral("–");
    }
    const auto at = simActive_ ? position_ : sim_->sequence->commands.size();
    return QStringLiteral("%1 / %2 points")
        .arg(sim_->stitchesBefore[at])
        .arg(sim_->stitchesBefore.back());
}

QString ViewController::simEstimate() const {
    if (!sim_) {
        return {};
    }
    const auto at = simActive_ ? position_ : sim_->sequence->commands.size();
    const double perStitch = 60.0 / spm_;
    return QStringLiteral("%1 / %2 à %3 points/min (hors changements de fil)")
        .arg(clock(sim_->stitchesBefore[at] * perStitch))
        .arg(clock(sim_->stitchesBefore.back() * perStitch))
        .arg(spm_);
}

void ViewController::publish() {
    if (!canvas_) {
        return;
    }
    auto overlays = std::make_shared<CanvasOverlays>();
    // The realistic raster replaces the flat stitches while it is on; the canvas keeps its
    // geometry untouched so switching back is instantaneous.
    const auto realistic = images_.find(QStringLiteral("realistic"));
    overlays->showStitches = layerStitches_ && realistic == images_.end();
    if (realistic != images_.end()) {
        overlays->images.push_back(realistic->second);
    }
    overlays->showHoop = layerHoop_;
    overlays->grid = grid_;
    overlays->gridStepMm = gridStep_;
    overlays->guideColor = guideColor_;
    if (guidesVisible_) {
        overlays->guidesX = guidesX_;
        for (const double y : guidesY_) {
            overlays->guidesY.push_back(-y); // model Y up -> scene Y down
        }
    }
    if (layerDensity_) {
        if (const auto found = images_.find(QStringLiteral("density")); found != images_.end()) {
            overlays->images.push_back(found->second);
        }
    }
    if (layerRisk_) {
        if (const auto found = images_.find(QStringLiteral("risk")); found != images_.end()) {
            overlays->images.push_back(found->second);
        }
    }
    overlays->markers = markers_;
    if (layerJumps_ && sim_) {
        overlays->jumps = sim_->jumps;
    }
    overlays->visibleSegments = simActive_ ? segmentsAt(position_) : CanvasOverlays::all;
    overlays->realistic = realistic_ && realisticOnGpu();
    overlays->realisticStyle = realisticStyle_;
    canvas_->setOverlays(std::move(overlays));
}

} // namespace openstitch::desktop_v2::studio
