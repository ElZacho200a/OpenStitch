// SPDX-License-Identifier: Apache-2.0
#include "analysis_controller.hpp"

#include "task_pump.hpp"

#include <QByteArray>
#include <QImage>
#include <algorithm>
#include <cmath>

#include "action_catalog.hpp"
#include "openstitch/document/hoop_library.hpp"
#include "openstitch/document/machine_profile.hpp"
#include "openstitch/project_ops/analysis.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "preferences.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
QString q(const std::string& s) {
    return QString::fromStdString(s);
}
QString q(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

OverlayImage toOverlay(const std::vector<std::uint8_t>& rgba, int columns, int rows, double minX,
                       double minY, double widthMm, double heightMm) {
    OverlayImage layer;
    const QImage view(rgba.data(), columns, rows, columns * 4, QImage::Format_RGBA8888);
    layer.image = view.convertToFormat(QImage::Format_RGBA8888_Premultiplied); // owns its pixels
    layer.x0 = minX;
    layer.y0 = -(minY + heightMm); // model Y up -> scene Y down
    layer.widthMm = widthMm;
    layer.heightMm = heightMm;
    return layer;
}

UserHoopShape shapeOf(document::HoopShape shape) {
    switch (shape) {
    case document::HoopShape::Ellipse:
        return UserHoopShape::Ellipse;
    case document::HoopShape::RoundedRectangle:
        return UserHoopShape::RoundedRectangle;
    default:
        return UserHoopShape::Rectangle;
    }
}

QString shapeName(UserHoopShape shape) {
    switch (shape) {
    case UserHoopShape::Ellipse:
        return QStringLiteral("ellipse");
    case UserHoopShape::RoundedRectangle:
        return QStringLiteral("rounded");
    default:
        return QStringLiteral("rectangle");
    }
}

QVariantMap hoopMap(const UserHoop& h) {
    return {{QStringLiteral("name"), q(h.name)},
            {QStringLiteral("shape"), shapeName(h.shape)},
            {QStringLiteral("widthMm"), h.widthMm},
            {QStringLiteral("heightMm"), h.heightMm},
            {QStringLiteral("cornerRadiusMm"), h.cornerRadiusMm},
            {QStringLiteral("marginMm"), h.marginMm}};
}
} // namespace

AnalysisController::AnalysisController(StudioController& studio, ViewController& view,
                                       std::filesystem::path configDir, QObject* parent)
    : QObject(parent), studio_(studio), view_(view) {
    if (!configDir.empty()) {
        hoopFile_ = configDir / "hoops.txt";
        prefsFile_ = configDir / "preferences.txt";
        userHoops_ = loadUserHoops(hoopFile_);
        render_ = loadPreferences(prefsFile_).render;
    }
    lastRevision_ = studio_.session().revision();
    view_.setRealisticStyle(render_);
    // Bascule « Rendu réaliste » : sur GPU le canevas fait tout (rien à calculer ici) ; en repli
    // logiciel, la bascule déclenche / retire l'image de référence CPU.
    connect(&view_, &ViewController::realisticChanged, this, [this] {
        if (!view_.realistic()) {
            if (realisticReady()) {
                clearRealistic();
            }
        } else if (!view_.realisticOnGpu() && !realisticReady()) {
            computeRealistic();
        }
    });
    connect(&view_, &ViewController::realisticRefreshRequested, this, [this] {
        if (view_.realistic() && !view_.realisticOnGpu()) {
            computeRealistic();
        }
    });
    connect(&studio_, &StudioController::changed, this, &AnalysisController::onDocumentChanged);
    connect(&studio_, &StudioController::sceneChanged, this,
            &AnalysisController::statisticsChanged);
    attachTaskPump(tasks_, this);
}

AnalysisController::~AnalysisController() {
    tasks_.cancelAll();
}

void AnalysisController::say(bool ok, const QString& text) {
    ok_ = ok;
    message_ = text;
    emit messageChanged();
}

void AnalysisController::begin(const QString& label) {
    ++running_;
    busyLabel_ = label;
    emit busyChanged();
}

void AnalysisController::end() {
    running_ = std::max(0, running_ - 1);
    if (running_ == 0) {
        busyLabel_.clear();
    }
    emit busyChanged();
}

void AnalysisController::cancel() {
    tasks_.cancelAll();
    running_ = 0;
    busyLabel_.clear();
    emit busyChanged();
    say(true, QStringLiteral("Calcul annulé"));
}

void AnalysisController::onDocumentChanged() {
    const auto revision = studio_.session().revision();
    if (revision == lastRevision_) {
        emit hoopsChanged(); // dirty flag only; cheap
        return;
    }
    lastRevision_ = revision;
    if (fidelityRevision_) {
        emit fidelityChanged(); // la mesure est périmée : le panneau le signale
    }
    // Maps describe one revision: never leave them over a changed document.
    if (density_) {
        density_.reset();
        view_.setAnalysisImage(QStringLiteral("density"), std::nullopt);
        emit densityChanged();
    }
    if (risk_) {
        risk_.reset();
        view_.setAnalysisImage(QStringLiteral("risk"), std::nullopt);
        emit riskChanged();
    }
    if (realisticRevision_) {
        realisticRevision_.reset();
        view_.setAnalysisImage(QStringLiteral("realistic"), std::nullopt);
        emit renderChanged();
    }
    view_.setMarkers({});
    emit diagnosticsChanged(); // stale flag
    emit proposalChanged();
    emit hoopsChanged();
}

// --- Diagnostics -------------------------------------------------------------------------------

void AnalysisController::runDiagnostics() {
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    begin(QStringLiteral("Diagnostic de brodabilité…"));
    tasks_.submit<DiagnosticsResult>(
        "studio.diagnostics",
        [snapshot, revision](const TaskContext& task) {
            return run_diagnostics(snapshot, revision, task);
        },
        [this](Result<DiagnosticsResult>&& result) {
            end();
            if (!result) {
                say(false, q(result.error().message));
                return;
            }
            diagnostics_ = std::move(*result);
            emit diagnosticsChanged();
            say(true, diagnosticsSummary());
        });
}

bool AnalysisController::diagnosticsStale() const {
    return diagnostics_ && diagnostics_->revision != studio_.session().revision();
}

QString AnalysisController::diagnosticsSummary() const {
    if (!diagnostics_) {
        return QStringLiteral("Aucun diagnostic calculé");
    }
    const auto& d = *diagnostics_;
    QString text = QStringLiteral("%1 erreur(s), %2 avertissement(s), %3 info(s) · %4 × %5 mm · "
                                  "%6 points, %7 sauts, %8 changements de fil")
                       .arg(d.errors)
                       .arg(d.warnings)
                       .arg(d.infos)
                       .arg(d.motifWidthMm, 0, 'f', 1)
                       .arg(d.motifHeightMm, 0, 'f', 1)
                       .arg(d.stitches)
                       .arg(d.jumps)
                       .arg(d.colorChanges);
    if (diagnosticsStale()) {
        text += QStringLiteral(" (périmé : le motif a changé depuis)");
    }
    return text;
}

QVariantList AnalysisController::findings() const {
    QVariantList list;
    if (!diagnostics_) {
        return list;
    }
    int index = 0;
    for (const auto& f : diagnostics_->findings) {
        QVariantList objects;
        for (const auto id : f.objects) {
            objects.push_back(QString::number(id));
        }
        list.push_back(QVariantMap{{QStringLiteral("index"), index++},
                                   {QStringLiteral("severity"), q(f.severity)},
                                   {QStringLiteral("code"), q(f.code)},
                                   {QStringLiteral("message"), q(f.message)},
                                   {QStringLiteral("suggestion"), q(f.suggestion)},
                                   {QStringLiteral("objects"), objects},
                                   {QStringLiteral("hasPosition"), f.at_mm.has_value()},
                                   {QStringLiteral("xMm"), f.at_mm ? (*f.at_mm)[0] : 0.0},
                                   {QStringLiteral("yMm"), f.at_mm ? (*f.at_mm)[1] : 0.0}});
    }
    return list;
}

bool AnalysisController::centerOnFinding(int index) {
    if (!diagnostics_ || index < 0 ||
        static_cast<std::size_t>(index) >= diagnostics_->findings.size()) {
        say(false, QStringLiteral("Constat introuvable"));
        return false;
    }
    if (diagnosticsStale()) {
        say(false, QStringLiteral("Diagnostic périmé : relancez-le avant de centrer la vue"));
        return false;
    }
    const auto& f = diagnostics_->findings[static_cast<std::size_t>(index)];
    if (f.at_mm) {
        view_.centerOnModel((*f.at_mm)[0], (*f.at_mm)[1]);
        view_.setMarkers({OverlayMarker{(*f.at_mm)[0], -(*f.at_mm)[1], QColor(229, 99, 137)}});
        say(true, QStringLiteral("Vue centrée sur le problème"));
        return true;
    }
    if (!f.objects.empty()) {
        return selectFindingObjects(index) && view_.zoomSelection();
    }
    say(false, QStringLiteral("Ce constat concerne tout le motif : rien à centrer"));
    return false;
}

bool AnalysisController::selectFindingObjects(int index) {
    if (!diagnostics_ || index < 0 ||
        static_cast<std::size_t>(index) >= diagnostics_->findings.size()) {
        return false;
    }
    std::vector<Target> targets;
    for (const auto id : diagnostics_->findings[static_cast<std::size_t>(index)].objects) {
        const Target target{TargetKind::EmbroideryObject, id};
        if (studio_.session().exists(target)) {
            targets.push_back(target);
        }
    }
    if (targets.empty()) {
        say(false, QStringLiteral("Les objets de ce constat n'existent plus"));
        return false;
    }
    studio_.session().selection().replace(std::move(targets));
    return true;
}

// --- Maps --------------------------------------------------------------------------------------

void AnalysisController::computeDensity() {
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    begin(QStringLiteral("Carte de densité…"));
    tasks_.submit<DensityOverlay>(
        "studio.density",
        [snapshot, revision](const TaskContext& task) {
            return run_density(snapshot, revision, task);
        },
        [this](Result<DensityOverlay>&& result) {
            end();
            if (!result) {
                say(false, q(result.error().message));
                return;
            }
            if (result->revision != studio_.session().revision()) {
                say(false,
                    QStringLiteral("Le motif a changé pendant le calcul : relancez la carte"));
                return;
            }
            density_ = std::move(*result);
            if (density_->empty()) {
                say(false, QStringLiteral("Rien à analyser : aucun point cousu"));
                return;
            }
            view_.setAnalysisImage(QStringLiteral("density"),
                                   toOverlay(density_->rgba, density_->columns, density_->rows,
                                             density_->minXmm, density_->minYmm, density_->widthMm,
                                             density_->heightMm));
            emit densityChanged();
            say(true, densitySummary());
        });
}

void AnalysisController::clearDensity() {
    density_.reset();
    view_.setAnalysisImage(QStringLiteral("density"), std::nullopt);
    emit densityChanged();
}

QString AnalysisController::densitySummary() const {
    if (!density_) {
        return QStringLiteral("Carte de densité non calculée");
    }
    return QStringLiteral("Maximum %1 piqûres/mm² (seuil %2) · %3 zone(s) trop dense(s)")
        .arg(density_->maxPenetrationsPerMm2, 0, 'f', 1)
        .arg(density_->thresholdPerMm2, 0, 'f', 1)
        .arg(density_->hotspots.size());
}

QVariantList AnalysisController::densityHotspots() const {
    QVariantList list;
    if (density_) {
        for (const auto& h : density_->hotspots) {
            list.push_back(QVariantMap{{QStringLiteral("xMm"), h.centerXmm},
                                       {QStringLiteral("yMm"), h.centerYmm},
                                       {QStringLiteral("maxPerMm2"), h.maxPenetrationsPerMm2},
                                       {QStringLiteral("areaMm2"), h.areaMm2},
                                       {QStringLiteral("object"), QString::number(h.object)}});
        }
    }
    return list;
}

void AnalysisController::computeFabricRisk() {
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    begin(QStringLiteral("Risque de tirage du tissu…"));
    tasks_.submit<FabricRisk>(
        "studio.fabric_risk",
        [snapshot, revision](const TaskContext& task) {
            return runFabricRisk(snapshot, revision, task);
        },
        [this](Result<FabricRisk>&& result) {
            end();
            if (!result) {
                say(false, q(result.error().message));
                return;
            }
            if (result->revision != studio_.session().revision()) {
                say(false,
                    QStringLiteral("Le motif a changé pendant le calcul : relancez l'analyse"));
                return;
            }
            risk_ = std::move(*result);
            if (!risk_->empty()) {
                view_.setAnalysisImage(QStringLiteral("risk"),
                                       toOverlay(risk_->rgba, risk_->columns, risk_->rows,
                                                 risk_->minXmm, risk_->minYmm, risk_->widthMm,
                                                 risk_->heightMm));
            }
            emit riskChanged();
            say(true, riskVerdict());
        });
}

void AnalysisController::clearFabricRisk() {
    risk_.reset();
    view_.setAnalysisImage(QStringLiteral("risk"), std::nullopt);
    emit riskChanged();
}

QString AnalysisController::riskVerdict() const {
    return risk_ ? q(risk_->verdict()) : QStringLiteral("Risque de tirage non calculé");
}

QString AnalysisController::riskLevel() const {
    return risk_ ? q(risk_->level()) : QString();
}

// --- Statistiques et fidélité (V1) ---------------------------------------------------------------

QVariantMap AnalysisController::statistics() const {
    const auto& scene = studio_.scene();
    if (!scene || !scene->effectiveSequence || scene->effectiveSequence->commands.empty()) {
        return {{QStringLiteral("ready"), false},
                {QStringLiteral("text"),
                 QStringLiteral("Statistiques indisponibles : générez d'abord des points.")}};
    }
    const auto stats = stitch::compute_stats(*scene->effectiveSequence);
    const double widthMm = to_millimeters(stats.bounds.max.x - stats.bounds.min.x).value;
    const double heightMm = to_millimeters(stats.bounds.max.y - stats.bounds.min.y).value;
    const double threadM = stats.thread_length_um / 1e9;
    return {{QStringLiteral("ready"), true},
            {QStringLiteral("stitches"), static_cast<qulonglong>(stats.stitches)},
            {QStringLiteral("jumps"), static_cast<qulonglong>(stats.jumps)},
            {QStringLiteral("trims"), static_cast<qulonglong>(stats.trims)},
            {QStringLiteral("colorChanges"), static_cast<qulonglong>(stats.color_changes)},
            {QStringLiteral("widthMm"), widthMm},
            {QStringLiteral("heightMm"), heightMm},
            {QStringLiteral("threadM"), threadM},
            {QStringLiteral("text"),
             QStringLiteral("Points : %1\nSauts : %2\nCoupes : %3\nChangements de couleur : %4\n"
                            "Dimensions : %5 × %6 mm\nFil cousu estimé : %7 m")
                 .arg(stats.stitches)
                 .arg(stats.jumps)
                 .arg(stats.trims)
                 .arg(stats.color_changes)
                 .arg(widthMm, 0, 'f', 1)
                 .arg(heightMm, 0, 'f', 1)
                 .arg(threadM, 0, 'f', 2)}};
}

bool AnalysisController::fidelityStale() const {
    return fidelityRevision_ && *fidelityRevision_ != studio_.session().revision();
}

void AnalysisController::clearFidelity() {
    if (fidelityRevision_ || !fidelityText_.isEmpty()) {
        fidelityRevision_.reset();
        fidelityText_.clear();
        fidelityMapUrl_.clear();
        emit fidelityChanged();
    }
}

void AnalysisController::measureFidelity() {
    if (studio_.session().project().embroidery_objects.empty()) {
        say(false, QStringLiteral("Le projet ne contient aucun objet de broderie."));
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    begin(QStringLiteral("Fidélité à l'image…"));
    struct Measured {
        std::uint64_t revision{0};
        project_ops::FidelityReport report;
    };
    tasks_.submit<Measured>(
        "studio.fidelity",
        [snapshot, revision](const TaskContext&) -> Result<Measured> {
            project_ops::FidelityOptions options;
            options.error_map = true;
            auto report = project_ops::measure_fidelity(*snapshot, options);
            if (!report) {
                return std::unexpected(report.error());
            }
            return Measured{revision, std::move(*report)};
        },
        [this](Result<Measured>&& result) {
            end();
            if (!result) {
                say(false, q(result.error().message));
                return;
            }
            fidelityRevision_ = result->revision;
            fidelityText_ = QString::fromStdString(result->report.text);
            const auto& png = result->report.error_map_png;
            fidelityMapUrl_ =
                png.empty()
                    ? QString()
                    : QStringLiteral("data:image/png;base64,") +
                          QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(png.data()),
                                                         static_cast<qsizetype>(png.size()))
                                                  .toBase64());
            emit fidelityChanged();
            say(true, QStringLiteral("Fidélité mesurée."));
        });
}

// --- Order optimisation
// ----------------------------------------------------------------------------

QStringList AnalysisController::strategies() const {
    QStringList list;
    for (const auto s : {PathStrategy::ByColor, PathStrategy::ByProximity,
                         PathStrategy::ColorThenProximity, PathStrategy::Layered}) {
        list.push_back(q(pathStrategyLabel(s)));
    }
    return list;
}

void AnalysisController::proposeOrder(int strategy, bool improve) {
    if (strategy < 0 || strategy > 3) {
        say(false, QStringLiteral("Stratégie inconnue"));
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    const auto kind = static_cast<PathStrategy>(strategy);
    begin(QStringLiteral("Optimisation de l'ordre…"));
    tasks_.submit<PathProposal>(
        "studio.optimize_order",
        [snapshot, revision, kind, improve](const TaskContext& task) {
            return desktop_v2::proposeOrder(snapshot, revision, kind, improve, task);
        },
        [this](Result<PathProposal>&& result) {
            end();
            if (!result) {
                say(false, q(result.error().message));
                return;
            }
            proposal_ = std::move(*result);
            emit proposalChanged();
            say(proposal_->changes, proposal_->changes
                                        ? QStringLiteral("Proposition prête : comparez avant/après")
                                        : QStringLiteral("L'ordre actuel est déjà le meilleur "
                                                         "pour cette stratégie"));
        });
}

bool AnalysisController::proposalStale() const {
    return proposal_ && proposal_->revision != studio_.session().revision();
}

QStringList AnalysisController::proposalSummary() const {
    QStringList list;
    if (proposal_) {
        for (const auto& line : proposal_->summary()) {
            list.push_back(q(line));
        }
    }
    return list;
}

bool AnalysisController::applyProposal() {
    if (!proposal_) {
        say(false, QStringLiteral("Aucune proposition à appliquer"));
        return false;
    }
    const auto result = desktop_v2::applyProposal(studio_.session(), *proposal_);
    say(result.ok, result.ok ? QStringLiteral("Ordre de couture optimisé (annulable en une étape)")
                             : q(result.message));
    if (result.ok) {
        proposal_.reset(); // consumed
        emit proposalChanged();
    }
    return result.ok;
}

void AnalysisController::discardProposal() {
    proposal_.reset();
    emit proposalChanged();
}

// --- Hoops and machine
// -------------------------------------------------------------------------------

QVariantList AnalysisController::libraryHoops() const {
    QVariantList list;
    for (const auto& h : document::hoop_library()) {
        list.push_back(QVariantMap{{QStringLiteral("id"), q(h.id)},
                                   {QStringLiteral("family"), q(h.family)},
                                   {QStringLiteral("name"), q(h.name)},
                                   {QStringLiteral("widthMm"), h.width_mm},
                                   {QStringLiteral("heightMm"), h.height_mm}});
    }
    return list;
}

QVariantList AnalysisController::userHoops() const {
    QVariantList list;
    for (const auto& h : userHoops_) {
        list.push_back(hoopMap(h));
    }
    return list;
}

QVariantMap AnalysisController::currentHoop() const {
    return hoopMap(userHoopFromProject(studio_.session().project(), "Cadre actuel"));
}

QString AnalysisController::machineId() const {
    return q(studio_.session().project().machine_id);
}

QVariantList AnalysisController::machines() const {
    QVariantList list;
    for (const auto& m : document::machine_profiles()) {
        list.push_back(QVariantMap{{QStringLiteral("id"), q(m.id)},
                                   {QStringLiteral("name"), q(m.name)},
                                   {QStringLiteral("fieldWidthMm"), m.max_field_width_mm},
                                   {QStringLiteral("fieldHeightMm"), m.max_field_height_mm},
                                   {QStringLiteral("needles"), m.needles},
                                   {QStringLiteral("maxSpm"), m.max_speed_spm}});
    }
    return list;
}

bool AnalysisController::applyLibraryHoop(const QString& id, double marginMm) {
    const auto* spec = document::find_hoop(id.toStdString());
    if (!spec) {
        say(false, QStringLiteral("Cadre inconnu : %1").arg(id));
        return false;
    }
    UserHoop hoop;
    hoop.name = std::string(spec->name);
    hoop.shape = shapeOf(spec->shape);
    hoop.widthMm = spec->width_mm;
    hoop.heightMm = spec->height_mm;
    hoop.cornerRadiusMm = spec->corner_radius_mm;
    hoop.marginMm = marginMm;
    const auto result = desktop_v2::applyUserHoop(studio_.session(), hoop);
    say(result.ok,
        result.ok ? QStringLiteral("Cadre « %1 » appliqué").arg(q(spec->name)) : q(result.message));
    emit hoopsChanged();
    return result.ok;
}

bool AnalysisController::saveUserHoop(const QString& name, double widthMm, double heightMm,
                                      const QString& shape, double cornerRadiusMm,
                                      double marginMm) {
    UserHoop hoop;
    hoop.name = name.toStdString();
    hoop.shape = shape == QStringLiteral("ellipse")   ? UserHoopShape::Ellipse
                 : shape == QStringLiteral("rounded") ? UserHoopShape::RoundedRectangle
                                                      : UserHoopShape::Rectangle;
    hoop.widthMm = widthMm;
    hoop.heightMm = heightMm;
    hoop.cornerRadiusMm = cornerRadiusMm;
    hoop.marginMm = marginMm;
    const auto result = addUserHoop(userHoops_, hoop);
    if (!result.ok) {
        say(false, q(result.message));
        return false;
    }
    saveHoops();
    emit hoopsChanged();
    say(true, QStringLiteral("Cadre « %1 » enregistré dans Mes cadres").arg(name));
    return true;
}

bool AnalysisController::saveCurrentHoopAs(const QString& name) {
    const auto hoop = userHoopFromProject(studio_.session().project(), name.toStdString());
    const auto result = addUserHoop(userHoops_, hoop);
    if (!result.ok) {
        say(false, q(result.message));
        return false;
    }
    saveHoops();
    emit hoopsChanged();
    say(true, QStringLiteral("Cadre actuel enregistré sous « %1 »").arg(name));
    return true;
}

bool AnalysisController::applyUserHoop(const QString& name) {
    const auto* hoop = findUserHoop(userHoops_, name.toStdString());
    if (!hoop) {
        say(false, QStringLiteral("Cadre inconnu : %1").arg(name));
        return false;
    }
    const auto result = desktop_v2::applyUserHoop(studio_.session(), *hoop);
    say(result.ok,
        result.ok ? QStringLiteral("Cadre « %1 » appliqué").arg(name) : q(result.message));
    emit hoopsChanged();
    return result.ok;
}

bool AnalysisController::removeUserHoop(const QString& name) {
    if (!desktop_v2::removeUserHoop(userHoops_, name.toStdString())) {
        say(false, QStringLiteral("Cadre inconnu : %1").arg(name));
        return false;
    }
    saveHoops();
    emit hoopsChanged();
    say(true, QStringLiteral("Cadre « %1 » retiré").arg(name));
    return true;
}

void AnalysisController::saveHoops() {
    if (!hoopFile_.empty() && !saveUserHoops(hoopFile_, userHoops_)) {
        say(false, QStringLiteral("Mes cadres : écriture impossible, la liste ne survivra pas au "
                                  "redémarrage"));
    }
}

QVariantMap AnalysisController::checkHoop() {
    QVariantMap out;
    const auto& scene = studio_.scene();
    if (!scene || scene->objectBounds.empty()) {
        say(false, QStringLiteral("Rien à vérifier : aucun objet cousu"));
        out[QStringLiteral("fits")] = true;
        out[QStringLiteral("checked")] = false;
        return out;
    }
    const auto& project = studio_.session().project();
    const auto hoop = userHoopFromProject(project, "");
    const double halfW = hoop.widthMm / 2.0 - hoop.marginMm;
    const double halfH = hoop.heightMm / 2.0 - hoop.marginMm;
    const bool ellipse = hoop.shape == UserHoopShape::Ellipse;
    const auto inside = [&](double x, double y) {
        if (ellipse) {
            return halfW > 0 && halfH > 0 &&
                   (x * x) / (halfW * halfW) + (y * y) / (halfH * halfH) <= 1.0 + 1e-9;
        }
        return std::abs(x) <= halfW + 1e-9 && std::abs(y) <= halfH + 1e-9;
    };
    std::vector<OverlayMarker> markers;
    QVariantList outside;
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& [id, b] : scene->objectBounds) {
        x0 = std::min(x0, b.x0);
        y0 = std::min(y0, b.y0);
        x1 = std::max(x1, b.x1);
        y1 = std::max(y1, b.y1);
        if (!inside(b.x0, b.y0) || !inside(b.x1, b.y0) || !inside(b.x0, b.y1) ||
            !inside(b.x1, b.y1)) {
            outside.push_back(QString::number(id));
            markers.push_back({(b.x0 + b.x1) / 2.0, (b.y0 + b.y1) / 2.0, QColor(229, 99, 137)});
        }
    }
    const double motifW = x1 - x0;
    const double motifH = y1 - y0;
    out[QStringLiteral("checked")] = true;
    out[QStringLiteral("fits")] = outside.isEmpty();
    out[QStringLiteral("outsideIds")] = outside;
    out[QStringLiteral("motifWidthMm")] = motifW;
    out[QStringLiteral("motifHeightMm")] = motifH;
    QString text = outside.isEmpty()
                       ? QStringLiteral("Le motif (%1 × %2 mm) tient dans le cadre")
                             .arg(motifW, 0, 'f', 1)
                             .arg(motifH, 0, 'f', 1)
                       : QStringLiteral("%1 objet(s) dépassent la zone de broderie du cadre "
                                        "(test de la boîte englobante)")
                             .arg(outside.size());
    bool machineFits = true;
    if (const auto* machine = document::find_machine(project.machine_id)) {
        machineFits = motifW <= machine->max_field_width_mm + 1e-9 &&
                      motifH <= machine->max_field_height_mm + 1e-9;
        if (!machineFits) {
            text += QStringLiteral(" · dépasse le champ de la machine (%1 × %2 mm)")
                        .arg(machine->max_field_width_mm)
                        .arg(machine->max_field_height_mm);
        }
    }
    out[QStringLiteral("machineFits")] = machineFits;
    view_.setMarkers(std::move(markers));
    say(outside.isEmpty() && machineFits, text);
    return out;
}

bool AnalysisController::setMachine(const QString& id) {
    const auto* def = findAction("machine.set");
    if (!def || !document::find_machine(id.toStdString())) {
        say(false, QStringLiteral("Machine inconnue : %1").arg(id));
        return false;
    }
    ActionContext ctx;
    ctx.project = &studio_.session().project();
    std::string error;
    const auto batch = buildAction(*def, {{"machine", id.toStdString()}}, ctx, error);
    if (batch.empty()) {
        say(false, q(error));
        return false;
    }
    const auto result = studio_.session().applyBatch(batch, def->label);
    say(result.committed, result.committed ? QStringLiteral("Profil machine appliqué")
                                           : QStringLiteral("Profil machine refusé"));
    emit hoopsChanged();
    return result.committed;
}

// --- Render settings
// ---------------------------------------------------------------------------------

QVariantMap AnalysisController::renderSettings() const {
    return {{QStringLiteral("pxPerMm"), render_.pxPerMm},
            {QStringLiteral("threadWidthMm"), render_.threadWidthMm},
            {QStringLiteral("relief"), render_.relief},
            {QStringLiteral("sheen"), render_.sheen},
            {QStringLiteral("twist"), render_.twist},
            {QStringLiteral("shadow"), render_.shadow},
            {QStringLiteral("fabricWeave"), render_.fabricWeave},
            {QStringLiteral("fabricRelief"), render_.fabricRelief},
            {QStringLiteral("highQuality"), render_.highQuality},
            {QStringLiteral("fabricColor"),
             QColor(render_.fabricRgb[0], render_.fabricRgb[1], render_.fabricRgb[2])},
            {QStringLiteral("preset"), q(matchingRenderPreset(render_))}};
}

QVariantList AnalysisController::renderPresets() const {
    QVariantList list;
    for (const char* id : {"draft", "balanced", "high", "photo"}) {
        list.push_back(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(id)},
                                   {QStringLiteral("label"), q(renderPresetLabel(id))}});
    }
    return list;
}

void AnalysisController::savePreferencesFile() {
    if (prefsFile_.empty()) {
        return;
    }
    auto prefs = loadPreferences(prefsFile_);
    prefs.render = render_;
    if (!savePreferences(prefsFile_, prefs)) {
        say(false, QStringLiteral("Préférences : écriture impossible"));
    }
}

bool AnalysisController::setRenderSetting(const QString& key, const QString& value) {
    auto next = render_;
    if (!renderSettingsApply(next, key.toStdString(), value.toStdString())) {
        say(false, QStringLiteral("Réglage de rendu refusé : %1 = %2").arg(key, value));
        return false;
    }
    render_ = clamped(next);
    view_.setRealisticStyle(render_);
    savePreferencesFile();
    emit renderChanged();
    return true;
}

bool AnalysisController::applyRenderPreset(const QString& id) {
    const auto preset = renderPreset(id.toStdString());
    if (!preset) {
        say(false, QStringLiteral("Préréglage inconnu : %1").arg(id));
        return false;
    }
    render_ = *preset;
    view_.setRealisticStyle(render_);
    savePreferencesFile();
    emit renderChanged();
    say(true,
        QStringLiteral("Préréglage « %1 » appliqué").arg(q(renderPresetLabel(id.toStdString()))));
    return true;
}

void AnalysisController::computeRealistic() {
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    const auto settings = render_;
    begin(QStringLiteral("Rendu réaliste…"));
    tasks_.submit<PreviewImage>(
        "studio.realistic",
        [snapshot, revision, settings](const TaskContext& task) {
            return render_preview(snapshot, revision, settings, task);
        },
        [this](Result<PreviewImage>&& result) {
            end();
            if (!result) {
                say(false, q(result.error().message));
                return;
            }
            if (result->revision != studio_.session().revision()) {
                say(false, QStringLiteral("Le motif a changé pendant le rendu : relancez-le"));
                return;
            }
            QImage image;
            if (!image.loadFromData(result->png.data(), static_cast<uint>(result->png.size()),
                                    "PNG")) {
                say(false, QStringLiteral("Rendu illisible"));
                return;
            }
            OverlayImage layer;
            layer.image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            layer.x0 = result->origin_x_mm; // already screen space (Y down)
            layer.y0 = result->origin_y_mm;
            layer.widthMm = result->width / result->px_per_mm;
            layer.heightMm = result->height / result->px_per_mm;
            realisticRevision_ = result->revision;
            view_.setAnalysisImage(QStringLiteral("realistic"), std::move(layer));
            emit renderChanged();
            say(true, QStringLiteral("Rendu réaliste affiché"));
        });
}

void AnalysisController::clearRealistic() {
    realisticRevision_.reset();
    view_.setAnalysisImage(QStringLiteral("realistic"), std::nullopt);
    emit renderChanged();
}

} // namespace openstitch::desktop_v2::studio
