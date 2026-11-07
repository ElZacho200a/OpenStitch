// SPDX-License-Identifier: Apache-2.0
#include "file_controller.hpp"

#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

#include "action_controller.hpp"
#include "digitize_pipeline.hpp"
#include "export_settings.hpp"
#include "file_io.hpp"
#include "openstitch/formats/svg.hpp"
#include "openstitch/stitch_analysis/placement_template.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "production_sheet.hpp"
#include "view_tools.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string toUtf8(const QString& text) {
    const auto bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::filesystem::path toPath(const QString& text) {
    return std::filesystem::path(text.toStdWString());
}

QString fromPath(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

QString lowerSuffix(const QString& path) {
    return QFileInfo(path).suffix().toLower();
}

QString errorText(const Error& error) {
    QString text = fromUtf8(error.message);
    if (!error.detail.empty()) {
        text += QStringLiteral(" ") + fromUtf8(error.detail);
    }
    return text;
}

bool isImageSuffix(const QString& suffix) {
    static const std::set<QString> known{QStringLiteral("png"),  QStringLiteral("jpg"),
                                         QStringLiteral("jpeg"), QStringLiteral("bmp"),
                                         QStringLiteral("tif"),  QStringLiteral("tiff"),
                                         QStringLiteral("webp"), QStringLiteral("gif")};
    return known.contains(suffix);
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

// Écriture atomique : l'ancien fichier reste intact si l'écriture échoue.
bool writeFileAtomic(const std::filesystem::path& path, const std::string& bytes) {
    auto temp = path;
    temp += ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

std::string lockId(LockKind kind) {
    switch (kind) {
    case LockKind::None:
        return "none";
    case LockKind::BackAndForth:
        return "backforth";
    case LockKind::Triangle:
        return "triangle";
    case LockKind::MicroZigzag:
        return "microzigzag";
    }
    return "none";
}

std::optional<LockKind> parseLock(const QString& id) {
    for (const auto kind :
         {LockKind::None, LockKind::BackAndForth, LockKind::Triangle, LockKind::MicroZigzag}) {
        if (fromUtf8(lockId(kind)) == id) {
            return kind;
        }
    }
    return std::nullopt;
}

struct MachineOutcome {
    stitch::StitchStats stats;
    std::string warning; // vide : le motif tient dans le cadre / le champ / le format
};

struct SheetJob {
    ProductionSheet sheet;
    std::filesystem::path png;
};

void layoutProductionPdf(const QString& pdfPath, const ProductionSheet& s, const QImage& preview,
                         const QString& title) {
    QPdfWriter writer(pdfPath);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(120);
    writer.setTitle(title);
    QPainter p(&writer);
    const int margin = 60;
    int y = margin;
    QFont f = p.font();
    f.setPointSize(16);
    f.setBold(true);
    p.setFont(f);
    p.drawText(margin, y + 24, title);
    y += 60;
    f.setPointSize(10);
    f.setBold(false);
    p.setFont(f);
    const auto line = [&](const QString& text) {
        p.drawText(margin, y, text);
        y += 22;
    };
    line(QObject::tr("Motif : %1 × %2 mm — cadre : %3 × %4 mm%5")
             .arg(s.designWidthMm, 0, 'f', 1)
             .arg(s.designHeightMm, 0, 'f', 1)
             .arg(s.hoopWidthMm, 0, 'f', 0)
             .arg(s.hoopHeightMm, 0, 'f', 0)
             .arg(s.fitsHoop ? QString() : QObject::tr("  ⚠ dépasse la zone brodable")));
    line(QObject::tr("%1 objets · %2 points · %3 sauts · %4 coupes · %5 changements de fil")
             .arg(s.objects)
             .arg(s.stitches)
             .arg(s.jumps)
             .arg(s.trims)
             .arg(s.colorChanges));
    line(QObject::tr("Fil : %1 m · durée estimée : %2 min (800 points/min)")
             .arg(s.threadMetres, 0, 'f', 1)
             .arg(s.minutesAt800spm, 0, 'f', 0));
    y += 10;
    if (!preview.isNull()) {
        const int maxW = writer.width() - 2 * margin;
        const QImage scaled =
            preview.scaled(maxW, 520, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawImage(margin, y, scaled);
        y += scaled.height() + 20;
    }
    f.setBold(true);
    p.setFont(f);
    line(QObject::tr("Plan de fils (ordre de couture)"));
    f.setBold(false);
    p.setFont(f);
    int n = 1;
    for (const auto& b : s.blocks) {
        if (y > writer.height() - margin) {
            writer.newPage();
            y = margin;
        }
        p.fillRect(margin, y - 14, 18, 14, QColor(b.rgb[0], b.rgb[1], b.rgb[2]));
        p.drawRect(margin, y - 14, 18, 14);
        p.drawText(margin + 28, y,
                   QObject::tr("%1. #%2%3%4 — %5 points, %6 mm — %7")
                       .arg(n++)
                       .arg(b.rgb[0], 2, 16, QLatin1Char('0'))
                       .arg(b.rgb[1], 2, 16, QLatin1Char('0'))
                       .arg(b.rgb[2], 2, 16, QLatin1Char('0'))
                       .arg(b.stitches)
                       .arg(b.lengthMm, 0, 'f', 0)
                       .arg(fromUtf8(b.objects)));
        y += 22;
    }
    p.end();
}

} // namespace

FileController::FileController(StudioController& studio, ActionController* actions,
                               const QString& stateDir, QObject* parent)
    : QObject(parent), studio_(studio), actions_(actions) {
    if (!stateDir.isEmpty()) {
        stateDir_ = toPath(stateDir);
        std::error_code ec;
        std::filesystem::create_directories(stateDir_, ec);
        recent_ = pruneMissingRecent(loadRecentList(stateDir_ / "recent.txt"));
        userHoops_ = loadUserHoops(stateDir_ / "hoops.txt");
        autosave_ = std::make_unique<AutosaveStore>(stateDir_ / "autosave");
        recoveryPending_ = autosave_->exists();
    }
    wasDirty_ = studio_.dirty();
    connect(&studio_, &StudioController::changed, this, [this] {
        noteRecentFromStudio();
        const bool dirty = studio_.dirty();
        if (wasDirty_ && !dirty) {
            clearAutosaveFile(); // enregistré : plus rien à récupérer
        }
        wasDirty_ = dirty;
    });
    connect(&studio_, &StudioController::documentReplaced, this, [this] {
        if (!studio_.dirty()) {
            clearAutosaveFile();
        }
    });
    connect(&studio_, &StudioController::loadFinished, this, [this](bool ok) {
        if (!ok) {
            setMessage(studio_.status(), true);
        }
    });
    pump_.setInterval(16);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
    autosaveTimer_.setInterval(60'000);
    connect(&autosaveTimer_, &QTimer::timeout, this, [this] { (void)autosaveNow(); });
    if (autosave_) {
        autosaveTimer_.start();
    }
}

FileController::~FileController() {
    pump_.stop();
    autosaveTimer_.stop();
    tasks_.cancelAll();
}

void FileController::setAutosaveIntervalMs(int ms) {
    autosaveTimer_.setInterval(std::max(10, ms));
    if (autosave_) {
        autosaveTimer_.start();
    }
}

// --- État commun -------------------------------------------------------------------------------

void FileController::setMessage(const QString& text, bool error) {
    message_ = text;
    messageIsError_ = error;
    emit messageChanged();
}

void FileController::setBusy(bool busy, const QString& label) {
    busy_ = busy;
    busyLabel_ = busy ? label : QString();
    emit busyChanged();
}

bool FileController::ensureCanStartTask() {
    if (busy_) {
        setMessage(QStringLiteral("Une opération est déjà en cours : patientez ou annulez-la."),
                   true);
        return false;
    }
    return true;
}

std::filesystem::path FileController::pathOf(const QUrl& url, const char* defaultSuffix) const {
    if (!url.isLocalFile()) {
        return {};
    }
    auto path = toPath(url.toLocalFile());
    if (defaultSuffix != nullptr && !path.empty() && !path.has_extension()) {
        path += std::string(".") + defaultSuffix;
    }
    return path;
}

void FileController::cancelTask() {
    if (!busy_) {
        return;
    }
    tasks_.cancelAll();
    ++loadGeneration_;
    currentTask_ = 0;
    setBusy(false);
    setMessage(QStringLiteral("Opération annulée."));
}

// --- Contrôle des modifications non enregistrées ------------------------------------------------

void FileController::guarded(const QString& reason, std::function<void()> action) {
    if (!studio_.dirty()) {
        action();
        return;
    }
    pending_ = std::move(action);
    pendingReason_ = reason;
    pendingSaveAs_ = false;
    emit guardChanged();
}

void FileController::finishGuard(bool proceed) {
    auto action = std::move(pending_);
    pending_ = nullptr;
    pendingReason_.clear();
    pendingSaveAs_ = false;
    emit guardChanged();
    if (proceed && action) {
        action();
    }
}

void FileController::resolveGuard(const QString& choice) {
    if (!pending_) {
        return;
    }
    if (choice == QLatin1String("discard")) {
        finishGuard(true);
    } else if (choice == QLatin1String("save")) {
        if (studio_.hasPath()) {
            if (studio_.saveProject()) {
                finishGuard(true);
            } else {
                setMessage(studio_.status(), true);
                finishGuard(false); // l'enregistrement a échoué : on ne perd rien, on s'arrête
            }
        } else {
            pendingSaveAs_ = true;
            emit saveAsRequested();
        }
    } else {
        finishGuard(false);
    }
}

void FileController::guardSaveAs(const QUrl& url) {
    if (!pending_ || !pendingSaveAs_) {
        return;
    }
    if (studio_.saveProjectAs(url)) {
        finishGuard(true);
    } else {
        setMessage(studio_.status(), true);
        finishGuard(false);
    }
}

void FileController::requestNew() {
    guarded(QStringLiteral("nouveau"), [this] { doNew(); });
}

void FileController::doNew() {
    studio_.newProject();
    setMessage(QStringLiteral("Nouveau projet."));
}

bool FileController::requestQuit() {
    if (!studio_.dirty()) {
        clearAutosaveFile();
        return true;
    }
    guarded(QStringLiteral("quitter"), [this] {
        clearAutosaveFile();
        emit quitApproved();
    });
    return false;
}

// --- Ouvrir : récents, glisser-déposer, ligne de commande ---------------------------------------

QVariantList FileController::recent() const {
    QVariantList list;
    for (const auto& entry : recent_) {
        const QString path = fromUtf8(entry);
        const QFileInfo info(path);
        list.push_back(QVariantMap{{QStringLiteral("path"), path},
                                   {QStringLiteral("name"), info.fileName()},
                                   {QStringLiteral("folder"), info.absolutePath()}});
    }
    return list;
}

void FileController::persistRecent() {
    if (!stateDir_.empty()) {
        (void)saveRecentList(stateDir_ / "recent.txt", recent_);
    }
    emit recentChanged();
}

void FileController::noteRecentFromStudio() {
    const std::string path = toUtf8(studio_.path());
    if (path == lastNotedPath_) {
        return;
    }
    lastNotedPath_ = path;
    if (path.empty()) {
        return;
    }
    recent_ = pushRecent(std::move(recent_), path);
    persistRecent();
}

void FileController::clearRecent() {
    recent_.clear();
    persistRecent();
}

void FileController::requestOpenRecent(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= recent_.size()) {
        return;
    }
    const QString path = fromUtf8(recent_[static_cast<std::size_t>(index)]);
    if (!QFileInfo::exists(path)) {
        recent_ = pruneMissingRecent(recent_);
        persistRecent();
        setMessage(QStringLiteral("Ce fichier n'existe plus : %1").arg(path), true);
        return;
    }
    openPath(path);
}

QString FileController::currentFolder() const {
    return studio_.hasPath() ? QFileInfo(studio_.path()).absolutePath() : QString();
}

void FileController::revealFolder() {
    const QString folder = currentFolder();
    if (folder.isEmpty()) {
        setMessage(QStringLiteral("Enregistrez d'abord le projet pour ouvrir son dossier."), true);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void FileController::openUrl(const QUrl& url) {
    if (!url.isLocalFile()) {
        setMessage(QStringLiteral("Choisissez un fichier local."), true);
        return;
    }
    openPath(url.toLocalFile());
}

void FileController::openDropped(const QVariantList& urls) {
    for (const auto& value : urls) {
        const QUrl url = value.toUrl();
        if (url.isLocalFile()) {
            openPath(url.toLocalFile());
            return;
        }
    }
    setMessage(QStringLiteral("Seuls les fichiers locaux peuvent être déposés."), true);
}

void FileController::openPath(const QString& path) {
    if (!QFileInfo::exists(path)) {
        setMessage(QStringLiteral("Fichier introuvable : %1").arg(path), true);
        return;
    }
    const QString suffix = lowerSuffix(path);
    if (suffix == QLatin1String("osp")) {
        guarded(QStringLiteral("ouvrir"), [this, path] { doOpenOsp(path); });
    } else if (isMachineDesignFile(toPath(path))) {
        guarded(QStringLiteral("ouvrir"), [this, path] { doOpenMachine(path); });
    } else if (isImageSuffix(suffix)) {
        emit imageImportRequested(path);
    } else if (suffix == QLatin1String("svg") || suffix == QLatin1String("dxf")) {
        importVector(path);
    } else {
        setMessage(QStringLiteral("Format non pris en charge : .%1 (OSP, DST, PES, JEF, EXP, "
                                  "image, SVG ou DXF attendus)")
                       .arg(suffix),
                   true);
    }
}

void FileController::doOpenOsp(const QString& path) {
    setMessage(QStringLiteral("Ouverture de %1…").arg(QFileInfo(path).fileName()));
    studio_.openProject(QUrl::fromLocalFile(path));
}

template <typename T>
void FileController::runJob(const QString& label, std::function<Result<T>(const TaskContext&)> work,
                            std::function<void(T&&)> onOk, const QString& errorPrefix) {
    if (!ensureCanStartTask()) {
        return;
    }
    const auto generation = ++loadGeneration_;
    setBusy(true, label);
    currentTask_ = tasks_.submit<T>(
        toUtf8(label), std::move(work),
        [this, generation, onOk = std::move(onOk), errorPrefix](Result<T>&& result) {
            if (generation != loadGeneration_) {
                return;
            }
            currentTask_ = 0;
            setBusy(false);
            if (!result) {
                setMessage(errorPrefix + errorText(result.error()), true);
                return;
            }
            onOk(std::move(*result));
        });
}

void FileController::doOpenMachine(const QString& path) {
    const auto file = toPath(path);
    const QString name = QFileInfo(path).fileName();
    const auto revision = studio_.session().revision();
    setMessage(QStringLiteral("Import de %1…").arg(name));
    runJob<document::Project>(
        QStringLiteral("Import de %1").arg(name),
        [file](const TaskContext&) { return loadMachineProject(file); },
        [this, name, revision](document::Project&& project) {
            if (revision != studio_.session().revision()) {
                setMessage(QStringLiteral("Document modifié pendant l'import : résultat ignoré."),
                           true);
                return;
            }
            studio_.session().selection().clear();
            // Un design importé n'a pas encore de projet enregistré.
            studio_.adoptProject(std::move(project), {}, name, true);
            setMessage(QStringLiteral("%1 importé comme broderie machine : à numériser ou à "
                                      "exporter.")
                           .arg(name));
        },
        QStringLiteral("Import impossible : "));
}

// --- Imports -----------------------------------------------------------------------------------

QVariantMap FileController::imageImportDefaults() const {
    const project_ops::DigitizeSettings defaults;
    return {{QStringLiteral("heightMm"), 100.0},
            {QStringLiteral("maxColors"), defaults.max_colors},
            {QStringLiteral("minRegionPx"), defaults.min_region_px},
            {QStringLiteral("smoothingPx"), defaults.smoothing_px},
            {QStringLiteral("skipBackground"), defaults.skip_background},
            {QStringLiteral("mode"), fromUtf8(defaults.mode)}};
}

void FileController::importImage(const QString& path, const QVariantMap& options) {
    if (!QFileInfo::exists(path)) {
        setMessage(QStringLiteral("Image introuvable : %1").arg(path), true);
        return;
    }
    const auto defaults = imageImportDefaults();
    DigitizeRequest request;
    request.image = toPath(path);
    request.target_height_mm = numberOr(options, "heightMm", defaults.value("heightMm").toDouble());
    if (request.target_height_mm < 5.0 || request.target_height_mm > 1000.0) {
        setMessage(QStringLiteral("Hauteur du motif : entre 5 et 1000 mm."), true);
        return;
    }
    auto& s = request.settings;
    s.max_colors = static_cast<int>(numberOr(options, "maxColors", s.max_colors));
    s.min_region_px = static_cast<int>(numberOr(options, "minRegionPx", s.min_region_px));
    s.smoothing_px = static_cast<int>(numberOr(options, "smoothingPx", s.smoothing_px));
    s.skip_background = static_cast<int>(numberOr(options, "skipBackground", s.skip_background));
    if (const auto mode = options.value(QStringLiteral("mode")).toString(); !mode.isEmpty()) {
        if (mode != QLatin1String("shapes") && mode != QLatin1String("contours") &&
            mode != QLatin1String("stipple")) {
            setMessage(QStringLiteral("Mode d'import inconnu : %1").arg(mode), true);
            return;
        }
        s.mode = toUtf8(mode);
    }
    if (s.max_colors < 2 || s.max_colors > 32) {
        setMessage(QStringLiteral("Nombre de couleurs : entre 2 et 32."), true);
        return;
    }
    const QString name = QFileInfo(path).fileName();
    guarded(QStringLiteral("importer"), [this, request, name] {
        const auto revision = studio_.session().revision();
        setMessage(QStringLiteral("Numérisation de %1…").arg(name));
        runJob<DigitizeOutput>(
            QStringLiteral("Numérisation de %1").arg(name),
            [request](const TaskContext& task) { return run_digitize(request, task); },
            [this, name, revision](DigitizeOutput&& out) {
                if (revision != studio_.session().revision()) {
                    setMessage(QStringLiteral("Document modifié pendant l'import : résultat "
                                              "ignoré."),
                               true);
                    return;
                }
                const auto objects = out.report.embroidery_objects;
                const auto stitches = out.stats.stitches;
                studio_.session().selection().clear();
                studio_.adoptProject(std::move(out.project), {}, name, false);
                setMessage(QStringLiteral("%1 objets, %2 points, %3 × %4 mm")
                               .arg(objects)
                               .arg(stitches)
                               .arg(out.width_mm, 0, 'f', 1)
                               .arg(out.height_mm, 0, 'f', 1));
            },
            QStringLiteral("Numérisation impossible : "));
    });
}

void FileController::importVector(const QString& path, const QString& stitchType) {
    if (actions_ == nullptr) {
        setMessage(QStringLiteral("Import vectoriel indisponible."), true);
        return;
    }
    if (!QFileInfo::exists(path)) {
        setMessage(QStringLiteral("Fichier introuvable : %1").arg(path), true);
        return;
    }
    const QString suffix = lowerSuffix(path);
    QString refusal;
    if (suffix == QLatin1String("svg")) {
        refusal = actions_->run(QStringLiteral("file.import_svg"),
                                {{QStringLiteral("file"), path},
                                 {QStringLiteral("stitch_type"),
                                  stitchType.isEmpty() ? QStringLiteral("auto") : stitchType}});
    } else if (suffix == QLatin1String("dxf")) {
        refusal =
            actions_->run(QStringLiteral("file.import_dxf"), {{QStringLiteral("file"), path}});
    } else {
        refusal = QStringLiteral("Seuls les fichiers SVG et DXF s'importent comme tracés.");
    }
    if (!refusal.isEmpty()) {
        setMessage(QStringLiteral("Import impossible : %1").arg(refusal), true);
        return;
    }
    setMessage(QStringLiteral("%1 importé : tracés ajoutés au projet (annulable).")
                   .arg(QFileInfo(path).fileName()));
}

// --- Exports -----------------------------------------------------------------------------------

QVariantList FileController::machineFormats() const {
    QVariantList list;
    for (const auto& format : exportFormats()) {
        list.push_back(QVariantMap{{QStringLiteral("extension"), fromUtf8(format.extension)},
                                   {QStringLiteral("label"), fromUtf8(format.label)},
                                   {QStringLiteral("carriesColors"), format.carriesColors}});
    }
    return list;
}

QVariantList FileController::readableFilters() const {
    QVariantList list;
    for (const auto& filter : readableMachineFormats()) {
        QStringList patterns;
        for (const auto& pattern : filter.patterns) {
            patterns << fromUtf8(pattern);
        }
        list.push_back(QVariantMap{{QStringLiteral("label"), fromUtf8(filter.label)},
                                   {QStringLiteral("filter"),
                                    QStringLiteral("%1 (%2)").arg(
                                        fromUtf8(filter.label), patterns.join(QLatin1Char(' ')))}});
    }
    return list;
}

QString FileController::lastExportName() const {
    return lastExport_.empty() ? QString() : fromPath(lastExport_.filename());
}

QVariantMap FileController::exportOptions() const {
    const auto o = exportOptionsOf(studio_.session().project());
    return {{QStringLiteral("finishingEnabled"), o.finishingEnabled},
            {QStringLiteral("trimThresholdMm"), o.trimThresholdMm},
            {QStringLiteral("trimBeforeColorChange"), o.trimBeforeColorChange},
            {QStringLiteral("lockKind"), fromUtf8(lockId(o.lockKind))},
            {QStringLiteral("lockLengthMm"), o.lockLengthMm},
            {QStringLiteral("lockPasses"), o.lockPasses},
            {QStringLiteral("filterShortStitches"), o.filterShortStitches},
            {QStringLiteral("minStitchMm"), o.minStitchMm},
            {QStringLiteral("splitLongStitches"), o.splitLongStitches},
            {QStringLiteral("maxStitchMm"), o.maxStitchMm},
            {QStringLiteral("autoJoin"), o.autoJoin}};
}

QString FileController::setExportOptions(const QVariantMap& v) {
    auto o = exportOptionsOf(studio_.session().project());
    const auto flag = [&](const char* key, bool& target) {
        if (v.contains(QLatin1String(key))) {
            target = v.value(QLatin1String(key)).toBool();
        }
    };
    flag("finishingEnabled", o.finishingEnabled);
    flag("trimBeforeColorChange", o.trimBeforeColorChange);
    flag("filterShortStitches", o.filterShortStitches);
    flag("splitLongStitches", o.splitLongStitches);
    flag("autoJoin", o.autoJoin);
    o.trimThresholdMm = numberOr(v, "trimThresholdMm", o.trimThresholdMm);
    o.lockLengthMm = numberOr(v, "lockLengthMm", o.lockLengthMm);
    o.lockPasses = static_cast<int>(numberOr(v, "lockPasses", o.lockPasses));
    o.minStitchMm = numberOr(v, "minStitchMm", o.minStitchMm);
    o.maxStitchMm = numberOr(v, "maxStitchMm", o.maxStitchMm);
    if (v.contains(QStringLiteral("lockKind"))) {
        const auto kind = parseLock(v.value(QStringLiteral("lockKind")).toString());
        if (!kind) {
            return QStringLiteral("Type de point d'arrêt inconnu.");
        }
        o.lockKind = *kind;
    }
    const auto result = desktop_v2::setExportOptions(studio_.session(), o);
    return result.ok ? QString() : fromUtf8(result.message);
}

QString FileController::exportCheck(const QString& formatExtension) const {
    return fromUtf8(
        checkExport(studio_.session().project(), toUtf8(formatExtension.toLower())).summary());
}

void FileController::exportMachine(const QUrl& url, const QVariantMap& options) {
    auto path = pathOf(url);
    if (path.empty()) {
        setMessage(QStringLiteral("Choisissez un fichier local."), true);
        return;
    }
    if (!options.isEmpty()) {
        QVariantMap settings = options;
        settings.remove(QStringLiteral("format"));
        if (!settings.isEmpty()) {
            const auto refusal = setExportOptions(settings);
            if (!refusal.isEmpty() && !refusal.startsWith(QLatin1String("Ces options sont déjà"))) {
                setMessage(QStringLiteral("Réglages d'export refusés : %1").arg(refusal), true);
                return;
            }
        }
    }
    QString extension = QString::fromStdWString(path.extension().wstring()).mid(1).toLower();
    if (extension.isEmpty()) {
        extension = options.value(QStringLiteral("format"), QStringLiteral("dst")).toString();
        path += "." + extension.toStdString();
    }
    const auto known = exportFormats();
    if (std::ranges::none_of(
            known, [&](const ExportFormat& f) { return f.extension == toUtf8(extension); })) {
        QStringList names;
        for (const auto& f : known) {
            names << fromUtf8(f.extension);
        }
        setMessage(QStringLiteral("Format d'export inconnu : .%1 (disponibles : %2)")
                       .arg(extension, names.join(QStringLiteral(", "))),
                   true);
        return;
    }
    runMachineExport(path);
}

void FileController::runMachineExport(const std::filesystem::path& path) {
    const auto snapshot = studio_.session().snapshot();
    const std::string extension = [&] {
        auto e = path.extension().string();
        if (!e.empty() && e.front() == '.') {
            e.erase(0, 1);
        }
        std::ranges::transform(e, e.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return e;
    }();
    const QString name = fromPath(path.filename());
    setMessage(QStringLiteral("Export de %1 en cours…").arg(name));
    runJob<MachineOutcome>(
        QStringLiteral("Export %1").arg(name),
        [snapshot, path, extension](const TaskContext& task) -> Result<MachineOutcome> {
            auto stats = export_project_file(snapshot, path, task);
            if (!stats) {
                return std::unexpected(stats.error());
            }
            MachineOutcome out{*stats, {}};
            const auto check = checkExport(*snapshot, extension);
            if (check.ok && (!check.fitsHoop || !check.fitsMachineField || !check.formatAccepted)) {
                out.warning = check.summary();
            }
            return out;
        },
        [this, path, name](MachineOutcome&& out) {
            lastExport_ = path;
            emit exportChanged();
            QString text =
                QStringLiteral("%1 écrit (%2 points).").arg(name).arg(out.stats.stitches);
            if (!out.warning.empty()) {
                text += QStringLiteral(" Attention : ") + fromUtf8(out.warning);
            }
            setMessage(text);
        },
        QStringLiteral("Export impossible : "));
}

void FileController::exportAgain() {
    if (lastExport_.empty()) {
        setMessage(QStringLiteral("Aucun export précédent : utilisez « Exporter pour la "
                                  "machine »."),
                   true);
        return;
    }
    runMachineExport(lastExport_);
}

void FileController::exportThreadList(const QUrl& url) {
    const auto path = pathOf(url, "csv");
    if (path.empty()) {
        setMessage(QStringLiteral("Choisissez un fichier local."), true);
        return;
    }
    std::size_t rows = 0;
    const auto result = exportThreadListCsv(studio_.session().project(), path, &rows);
    if (!result.ok) {
        setMessage(fromUtf8(result.message), true);
        return;
    }
    setMessage(QStringLiteral("Liste des fils exportée (%1 fils).").arg(rows));
}

void FileController::exportDxf(const QUrl& url) {
    const auto path = pathOf(url, "dxf");
    if (path.empty()) {
        setMessage(QStringLiteral("Choisissez un fichier local."), true);
        return;
    }
    std::size_t count = 0;
    const auto result = exportVectorsToDxf(studio_.session().project(), path, &count);
    if (!result.ok) {
        setMessage(QStringLiteral("Export DXF impossible : %1").arg(fromUtf8(result.message)),
                   true);
        return;
    }
    setMessage(QStringLiteral("%1 tracé(s) exporté(s) en DXF.").arg(count));
}

void FileController::exportSvg(const QUrl& url) {
    const auto path = pathOf(url, "svg");
    if (path.empty()) {
        setMessage(QStringLiteral("Choisissez un fichier local."), true);
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    runJob<int>(
        QStringLiteral("Export SVG"),
        [snapshot, path](const TaskContext& task) -> Result<int> {
            auto sequence = stitch_generation::effective_sequence(*snapshot, task);
            if (!sequence) {
                return std::unexpected(sequence.error());
            }
            if (sequence->commands.empty()) {
                return fail(ErrorCategory::OperationImpossible, "Aucun point à exporter.");
            }
            if (auto written = formats::write_svg_file(path, *sequence); !written) {
                return std::unexpected(written.error());
            }
            return 0;
        },
        [this, path](int&&) {
            setMessage(QStringLiteral("SVG écrit : %1").arg(fromPath(path.filename())));
        },
        QStringLiteral("Export SVG impossible : "));
}

void FileController::exportImage(const QUrl& url, double pxPerMm, int jpegQuality) {
    const auto path = pathOf(url, "png");
    if (path.empty()) {
        setMessage(QStringLiteral("Choisissez un fichier local."), true);
        return;
    }
    if (pxPerMm < 1.0 || pxPerMm > 20.0) {
        setMessage(QStringLiteral("Résolution : entre 1 et 20 pixels par mm."), true);
        return;
    }
    const QString suffix = QString::fromStdWString(path.extension().wstring()).mid(1).toLower();
    const bool jpeg = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg");
    if (!jpeg && suffix != QLatin1String("png")) {
        setMessage(QStringLiteral("Format d'image : .png ou .jpg."), true);
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    // Le rendu s'écrit toujours en PNG ; le JPEG est converti à l'arrivée.
    auto png = path;
    if (jpeg) {
        png = std::filesystem::temp_directory_path() /
              ("openstitch_studio_export_" + std::to_string(snapshot->embroidery_objects.size()) +
               "_" + std::to_string(studio_.session().revision()) + ".png");
    }
    runJob<int>(
        QStringLiteral("Export de l'image"),
        [snapshot, png, pxPerMm](const TaskContext& task) -> Result<int> {
            if (auto r = export_preview_png(snapshot, png, pxPerMm, task); !r) {
                return std::unexpected(r.error());
            }
            return 0;
        },
        [this, path, png, jpeg, jpegQuality](int&&) {
            if (jpeg) {
                QImage image(fromPath(png));
                std::error_code ec;
                std::filesystem::remove(png, ec);
                // Un JPEG n'a pas de transparence : fond blanc.
                QImage flat(image.size(), QImage::Format_RGB32);
                flat.fill(Qt::white);
                QPainter painter(&flat);
                painter.drawImage(0, 0, image);
                painter.end();
                if (image.isNull() ||
                    !flat.save(fromPath(path), "JPEG", std::clamp(jpegQuality, 1, 100))) {
                    setMessage(QStringLiteral("Écriture du JPEG impossible."), true);
                    return;
                }
            }
            setMessage(QStringLiteral("Image écrite : %1").arg(fromPath(path.filename())));
        },
        QStringLiteral("Export impossible : "));
}

void FileController::exportProductionSheet(const QUrl& url) {
    const auto pdf = pathOf(url, "pdf");
    if (pdf.empty()) {
        setMessage(QStringLiteral("Choisissez un fichier local."), true);
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    const auto png =
        std::filesystem::temp_directory_path() /
        ("openstitch_studio_sheet_" + std::to_string(studio_.session().revision()) + ".png");
    runJob<SheetJob>(
        QStringLiteral("Fiche de production"),
        [snapshot, png](const TaskContext& task) -> Result<SheetJob> {
            auto sheet = buildProductionSheet(*snapshot, task);
            if (!sheet) {
                return std::unexpected(sheet.error());
            }
            SheetJob job{std::move(*sheet), {}};
            if (job.sheet.stitches > 0 && export_preview_png(snapshot, png, 8.0, task)) {
                job.png = png;
            }
            return job;
        },
        [this, pdf](SheetJob&& job) {
            QImage preview;
            if (!job.png.empty()) {
                preview.load(fromPath(job.png));
                std::error_code ec;
                std::filesystem::remove(job.png, ec);
            }
            layoutProductionPdf(fromPath(pdf), job.sheet, preview,
                                QStringLiteral("Fiche de production — OpenStitch"));
            setMessage(QStringLiteral("Fiche écrite : %1").arg(fromPath(pdf.filename())));
        },
        QStringLiteral("Fiche impossible : "));
}

void FileController::exportPlacementTemplate(const QUrl& url, const QVariantMap& options) {
    const auto pdf = pathOf(url, "pdf");
    if (pdf.empty()) {
        setMessage(QStringLiteral("Choisissez un fichier local."), true);
        return;
    }
    stitch_analysis::PlacementTemplateOptions tpl;
    const QString paper = options.value(QStringLiteral("paper"), QStringLiteral("A4")).toString();
    if (paper == QLatin1String("A3")) {
        tpl.paper_width = Micrometers{297'000};
        tpl.paper_height = Micrometers{420'000};
    } else if (paper == QLatin1String("Letter")) {
        tpl.paper_width = Micrometers{215'900};
        tpl.paper_height = Micrometers{279'400};
    } else if (paper != QLatin1String("A4")) {
        setMessage(QStringLiteral("Papier inconnu : %1 (A4, A3 ou Letter).").arg(paper), true);
        return;
    }
    tpl.landscape = options.value(QStringLiteral("landscape")).toBool();
    const QString title = studio_.title();
    tpl.title = toUtf8(title);
    const auto snapshot = studio_.session().snapshot();
    runJob<int>(
        QStringLiteral("Gabarit de placement"),
        [snapshot, pdf, tpl](const TaskContext& task) -> Result<int> {
            auto sequence = stitch_generation::effective_sequence(*snapshot, task);
            if (!sequence) {
                return std::unexpected(sequence.error());
            }
            if (sequence->commands.empty()) {
                return fail(ErrorCategory::OperationImpossible,
                            "Générez d'abord des points de broderie.");
            }
            const auto layout = stitch_analysis::make_placement_template(*sequence, tpl);
            if (!writeFileAtomic(pdf, stitch_analysis::placement_template_to_pdf(layout))) {
                return fail(ErrorCategory::Internal, "Le fichier PDF n'a pas pu être écrit.");
            }
            return static_cast<int>(layout.pages.size());
        },
        [this, pdf](int&& pages) {
            setMessage(QStringLiteral("Gabarit écrit : %1 (%2 page(s) à l'échelle 1:1).")
                           .arg(fromPath(pdf.filename()))
                           .arg(pages));
        },
        QStringLiteral("Gabarit impossible : "));
}

// --- Cadre et machine --------------------------------------------------------------------------

QVariantList FileController::hoops() const {
    QVariantList list;
    for (const auto& h : hoopChoices()) {
        list.push_back(QVariantMap{{QStringLiteral("id"), fromUtf8(h.id)},
                                   {QStringLiteral("family"), fromUtf8(h.family)},
                                   {QStringLiteral("name"), fromUtf8(h.name)},
                                   {QStringLiteral("widthMm"), h.widthMm},
                                   {QStringLiteral("heightMm"), h.heightMm}});
    }
    return list;
}

QVariantList FileController::machines() const {
    QVariantList list;
    for (const auto& m : machineChoices()) {
        list.push_back(QVariantMap{{QStringLiteral("id"), fromUtf8(m.id)},
                                   {QStringLiteral("name"), fromUtf8(m.name)},
                                   {QStringLiteral("preferredFormat"), fromUtf8(m.preferredFormat)},
                                   {QStringLiteral("fieldWidthMm"), m.fieldWidthMm},
                                   {QStringLiteral("fieldHeightMm"), m.fieldHeightMm}});
    }
    return list;
}

QVariantList FileController::userHoops() const {
    QVariantList list;
    for (const auto& h : userHoops_) {
        list.push_back(QVariantMap{{QStringLiteral("name"), fromUtf8(h.name)},
                                   {QStringLiteral("widthMm"), h.widthMm},
                                   {QStringLiteral("heightMm"), h.heightMm},
                                   {QStringLiteral("marginMm"), h.marginMm}});
    }
    return list;
}

QVariantMap FileController::currentHoop() const {
    const auto& project = studio_.session().project();
    return {{QStringLiteral("id"), fromUtf8(currentHoopId(project))},
            {QStringLiteral("marginMm"), currentMarginMm(project)}};
}

namespace {
QString refusalOf(const ServiceResult& result) {
    return result.ok ? QString() : fromUtf8(result.message);
}
} // namespace

QString FileController::applyHoop(const QString& hoopId, double marginMm) {
    const auto refusal =
        refusalOf(desktop_v2::applyHoop(studio_.session(), toUtf8(hoopId), marginMm));
    setMessage(refusal.isEmpty() ? QStringLiteral("Cadre appliqué.") : refusal, !refusal.isEmpty());
    return refusal;
}

QString FileController::applyCustomHoop(double widthMm, double heightMm, double marginMm) {
    const auto refusal =
        refusalOf(desktop_v2::applyCustomHoop(studio_.session(), widthMm, heightMm, marginMm));
    setMessage(refusal.isEmpty() ? QStringLiteral("Cadre personnalisé appliqué.") : refusal,
               !refusal.isEmpty());
    return refusal;
}

QString FileController::applyMachine(const QString& machineId) {
    const auto refusal = refusalOf(desktop_v2::applyMachine(studio_.session(), toUtf8(machineId)));
    setMessage(refusal.isEmpty() ? QStringLiteral("Machine appliquée.") : refusal,
               !refusal.isEmpty());
    return refusal;
}

QString FileController::saveCurrentHoopAs(const QString& name) {
    auto list = userHoops_;
    const auto hoop = userHoopFromProject(studio_.session().project(), toUtf8(name.trimmed()));
    const auto result = addUserHoop(list, hoop);
    if (!result.ok) {
        setMessage(fromUtf8(result.message), true);
        return fromUtf8(result.message);
    }
    userHoops_ = std::move(list);
    if (!stateDir_.empty() && !saveUserHoops(stateDir_ / "hoops.txt", userHoops_)) {
        setMessage(QStringLiteral("Cadre appliqué mais « Mes cadres » n'a pas pu être écrit."),
                   true);
    } else {
        setMessage(QStringLiteral("Cadre « %1 » ajouté à Mes cadres.").arg(name.trimmed()));
    }
    emit userHoopsChanged();
    return {};
}

QString FileController::applyUserHoop(const QString& name) {
    const auto* hoop = findUserHoop(userHoops_, toUtf8(name));
    if (hoop == nullptr) {
        setMessage(QStringLiteral("Cadre inconnu : %1").arg(name), true);
        return QStringLiteral("Cadre inconnu");
    }
    const auto refusal = refusalOf(desktop_v2::applyUserHoop(studio_.session(), *hoop));
    setMessage(refusal.isEmpty() ? QStringLiteral("Cadre « %1 » appliqué.").arg(name) : refusal,
               !refusal.isEmpty());
    return refusal;
}

QString FileController::removeUserHoop(const QString& name) {
    if (!desktop_v2::removeUserHoop(userHoops_, toUtf8(name))) {
        return QStringLiteral("Cadre inconnu");
    }
    if (!stateDir_.empty()) {
        (void)saveUserHoops(stateDir_ / "hoops.txt", userHoops_);
    }
    emit userHoopsChanged();
    return {};
}

// --- Sauvegarde automatique --------------------------------------------------------------------

bool FileController::autosaveNow() {
    if (!autosave_ || !studio_.dirty()) {
        return false;
    }
    if (recoveryPending_) {
        return false; // ne jamais écraser une sauvegarde que l'utilisateur n'a pas tranchée
    }
    return autosave_->write(studio_.session().project()).has_value();
}

void FileController::clearAutosaveFile() {
    if (autosave_ && !recoveryPending_) {
        autosave_->clear();
    }
}

void FileController::recoverAutosave() {
    if (!recoveryPending_ || !autosave_) {
        return;
    }
    guarded(QStringLiteral("récupérer"), [this] {
        auto loaded = autosave_->load();
        if (!loaded) {
            setMessage(
                QStringLiteral("Récupération impossible : %1").arg(errorText(loaded.error())),
                true);
            return;
        }
        recoveryPending_ = false;
        studio_.session().selection().clear();
        // Non enregistré : le document reste « modifié » jusqu'au premier Enregistrer.
        studio_.adoptProject(std::move(*loaded), {}, QStringLiteral("Document récupéré"), false);
        emit recoveryChanged();
        setMessage(QStringLiteral("Document récupéré : pensez à l'enregistrer."));
    });
}

void FileController::discardRecovery() {
    if (!autosave_) {
        return;
    }
    autosave_->clear();
    recoveryPending_ = false;
    emit recoveryChanged();
}

} // namespace openstitch::desktop_v2::studio
