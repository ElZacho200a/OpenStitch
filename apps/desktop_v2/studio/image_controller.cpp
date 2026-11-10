// SPDX-License-Identifier: Apache-2.0
#include "image_controller.hpp"

#include "task_pump.hpp"

#include <set>

#include <algorithm>
#include <cmath>

#include "openstitch/ai_segmentation/backend_registry.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "segmentation_preview.hpp"
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

double numberOr(const QVariantMap& map, const char* key, double fallback) {
    const auto found = map.find(QLatin1String(key));
    if (found == map.end()) {
        return fallback;
    }
    bool ok = false;
    const double value = found->toDouble(&ok);
    return ok ? value : fallback;
}

QImage toQImage(const image::Image& img) {
    if (img.empty()) {
        return {};
    }
    return QImage(img.rgba.data(), img.width, img.height, img.width * 4, QImage::Format_RGBA8888)
        .copy();
}

std::uint64_t originalKey(const document::Project& project) {
    // Empreinte légère de l'image source : taille + quelques octets échantillonnés.
    const auto& o = project.original;
    std::uint64_t key = (static_cast<std::uint64_t>(o.width) << 32) ^
                        static_cast<std::uint64_t>(o.height) ^ o.rgba.size();
    const std::size_t step = std::max<std::size_t>(1, o.rgba.size() / 61);
    for (std::size_t i = 0; i < o.rgba.size(); i += step) {
        key = key * 1099511628211ULL + o.rgba[i];
    }
    return key;
}

ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}

bool inRange(double v, double lo, double hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}

// Exécute le traitement demandé sur `session` (UNE commande). Source unique du chemin « appliquer »
// et du chemin « aperçu » (qui l'exécute sur une copie jetable du projet).
ServiceResult runPrep(DocumentSession& session, const QString& kind, const QVariantMap& p) {
    PrepRequest request;
    if (kind == QLatin1String("brightness_contrast")) {
        request.kind = PrepKind::BrightnessContrast;
        request.a = numberOr(p, "brightness", 0.0);
        request.b = numberOr(p, "contrast", 0.0);
    } else if (kind == QLatin1String("grayscale")) {
        request.kind = PrepKind::Grayscale;
    } else if (kind == QLatin1String("sharpen")) {
        request.kind = PrepKind::Sharpen;
        request.a = numberOr(p, "amount", 1.0);
    } else if (kind == QLatin1String("clahe")) {
        request.kind = PrepKind::Clahe;
        request.a = numberOr(p, "clip", 2.0);
    } else if (kind == QLatin1String("flip_h")) {
        request.kind = PrepKind::FlipHorizontal;
    } else if (kind == QLatin1String("flip_v")) {
        request.kind = PrepKind::FlipVertical;
    } else if (kind == QLatin1String("rotate_right")) {
        request.kind = PrepKind::RotateRight;
    } else if (kind == QLatin1String("rotate_left")) {
        request.kind = PrepKind::RotateLeft;
    } else if (kind == QLatin1String("rotate_180")) {
        request.kind = PrepKind::Rotate180;
    } else if (kind == QLatin1String("crop")) {
        request.kind = PrepKind::Crop;
        request.x = static_cast<int>(numberOr(p, "x", 0));
        request.y = static_cast<int>(numberOr(p, "y", 0));
        request.w = static_cast<int>(numberOr(p, "w", 0));
        request.h = static_cast<int>(numberOr(p, "h", 0));
    } else if (kind == QLatin1String("median") || kind == QLatin1String("bilateral") ||
               kind == QLatin1String("flatten")) {
        if (!session.project().hasImage()) {
            return refuse("Le projet n'a pas d'image source.");
        }
        const double raw = numberOr(p, "strength", 1.0);
        const int maxStrength = kind == QLatin1String("median") ? 2 : 3;
        if (!inRange(raw, 1, maxStrength)) {
            return refuse("La force doit être comprise entre 1 et " + std::to_string(maxStrength) +
                          ".");
        }
        const int strength = static_cast<int>(std::lround(raw));
        image::ImageOp op = kind == QLatin1String("median")
                                ? image::ImageOp{image::MedianDenoiseOp{strength}}
                            : kind == QLatin1String("bilateral")
                                ? image::ImageOp{image::BilateralDenoiseOp{strength}}
                                : image::ImageOp{image::FlattenOp{strength}};
        session.execute(std::make_unique<commands::AppendImageOpCommand>(std::move(op)));
        return {true, {}};
    } else if (kind == QLatin1String("quantize")) {
        if (!session.project().hasImage()) {
            return refuse("Le projet n'a pas d'image source.");
        }
        const double raw = numberOr(p, "colors", 8.0);
        if (!inRange(raw, 2, 64)) {
            return refuse("Le nombre de couleurs doit être compris entre 2 et 64.");
        }
        session.execute(std::make_unique<commands::AppendImageOpCommand>(
            image::QuantizeOp{static_cast<int>(std::lround(raw))}));
        return {true, {}};
    } else {
        return refuse("Traitement d'image inconnu : " + kind.toStdString());
    }
    return applyImagePrep(session, request);
}

QVariantMap field(const char* key, const QString& label, double lo, double hi, double def,
                  double step) {
    return {{QStringLiteral("key"), QLatin1String(key)},
            {QStringLiteral("label"), label},
            {QStringLiteral("min"), lo},
            {QStringLiteral("max"), hi},
            {QStringLiteral("value"), def},
            {QStringLiteral("step"), step}};
}

QVariantMap entry(const char* kind, const QString& label, const QString& group,
                  QVariantList fields = {}) {
    return {{QStringLiteral("kind"), QLatin1String(kind)},
            {QStringLiteral("label"), label},
            {QStringLiteral("group"), group},
            {QStringLiteral("fields"), fields}};
}

struct ImagePair {
    QImage before;
    QImage after;
};

} // namespace

// --- Stockage et fournisseur d'images ------------------------------------------------------

void PrepareImages::set(const QString& key, const QImage& image) {
    const std::lock_guard lock(mutex_);
    for (auto& [k, v] : images_) {
        if (k == key) {
            v = image;
            return;
        }
    }
    images_.emplace_back(key, image);
}

QImage PrepareImages::get(const QString& key) const {
    const std::lock_guard lock(mutex_);
    for (const auto& [k, v] : images_) {
        if (k == key) {
            return v;
        }
    }
    return {};
}

QImage PrepareImageProvider::requestImage(const QString& id, QSize* size,
                                          const QSize& requestedSize) {
    const QString key = id.left(id.indexOf(QLatin1Char('?')));
    QImage image = images_->get(key);
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
    } else if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0 &&
               (image.width() > requestedSize.width() || image.height() > requestedSize.height())) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

// --- Contrôleur ---------------------------------------------------------------------------

ImageController::ImageController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    attachTaskPump(tasks_, this);
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(0);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { refreshNow(); });
    token_ = studio_.session().subscribe([this](ChangeKind kind) {
        if (kind == ChangeKind::Replaced) {
            lastOriginalKey_ = 0;
            lastOps_.clear();
            prepPreview_ = false;
            segPreview_ = false;
            segInfo_.clear();
        }
        scheduleRefresh();
    });
    scheduleRefresh();
}

ImageController::~ImageController() {
    tasks_.cancelAll();
    studio_.session().unsubscribe(token_);
}

bool ImageController::hasImage() const {
    return studio_.session().project().hasImage();
}

bool ImageController::hasSegmentation() const {
    return studio_.session().project().segmentation.has_value();
}

int ImageController::regionCount() const {
    const auto& seg = studio_.session().project().segmentation;
    return seg ? static_cast<int>(seg->region_count()) : 0;
}

double ImageController::mmPerPx() const {
    return studio_.session().project().mm_per_px.value;
}

int ImageController::colorCount() const {
    const auto& seg = studio_.session().project().segmentation;
    if (!seg) {
        return 0;
    }
    std::set<std::uint32_t> colours;
    for (const auto& slot : seg->region_slots) {
        if (slot) {
            colours.insert((static_cast<std::uint32_t>(slot->rgb[0]) << 16) |
                           (static_cast<std::uint32_t>(slot->rgb[1]) << 8) | slot->rgb[2]);
        }
    }
    return static_cast<int>(colours.size());
}

QVariantList ImageController::prepCatalog() const {
    const QString tone = QStringLiteral("Tons");
    const QString clean = QStringLiteral("Nettoyage");
    const QString geom = QStringLiteral("Géométrie");
    QVariantList out;
    out.append(entry("brightness_contrast", QStringLiteral("Luminosité / contraste"), tone,
                     {field("brightness", QStringLiteral("Luminosité"), -100, 100, 0, 1),
                      field("contrast", QStringLiteral("Contraste"), -100, 100, 0, 1)}));
    out.append(entry("grayscale", QStringLiteral("Niveaux de gris"), tone));
    out.append(entry("clahe", QStringLiteral("Contraste local (CLAHE)"), tone,
                     {field("clip", QStringLiteral("Limite"), 1, 8, 2, 0.5)}));
    out.append(entry("sharpen", QStringLiteral("Netteté"), tone,
                     {field("amount", QStringLiteral("Force"), 0.05, 3, 1, 0.05)}));
    out.append(entry("median", QStringLiteral("Débruitage médian"), clean,
                     {field("strength", QStringLiteral("Force"), 1, 2, 1, 1)}));
    out.append(entry("bilateral", QStringLiteral("Lissage bilatéral"), clean,
                     {field("strength", QStringLiteral("Force"), 1, 3, 2, 1)}));
    out.append(entry("flatten", QStringLiteral("Aplatir les zones"), clean,
                     {field("strength", QStringLiteral("Force"), 1, 3, 2, 1)}));
    out.append(entry("quantize", QStringLiteral("Réduire les couleurs"), clean,
                     {field("colors", QStringLiteral("Couleurs"), 2, 64, 8, 1)}));
    out.append(entry("rotate_right", QStringLiteral("Pivoter à droite"), geom));
    out.append(entry("rotate_left", QStringLiteral("Pivoter à gauche"), geom));
    out.append(entry("rotate_180", QStringLiteral("Pivoter de 180°"), geom));
    out.append(entry("flip_h", QStringLiteral("Retourner horizontalement"), geom));
    out.append(entry("flip_v", QStringLiteral("Retourner verticalement"), geom));
    out.append(entry("crop", QStringLiteral("Recadrer"), geom,
                     {field("x", QStringLiteral("X (px)"), 0, 100000, 0, 1),
                      field("y", QStringLiteral("Y (px)"), 0, 100000, 0, 1),
                      field("w", QStringLiteral("Largeur (px)"), 8, 100000, 8, 1),
                      field("h", QStringLiteral("Hauteur (px)"), 8, 100000, 8, 1)}));
    return out;
}

QVariantMap ImageController::segmentationDefaults() const {
    const segmentation::SegmentationOptions defaults;
    return {{QStringLiteral("max_colors"), defaults.max_colors},
            {QStringLiteral("min_region_px"), defaults.min_region_px},
            {QStringLiteral("smoothing_radius_px"), 3}};
}

void ImageController::setMessage(const QString& text, bool error) {
    message_ = text;
    messageIsError_ = error;
    emit messageChanged();
}

void ImageController::setBusy(bool busy, const QString& label) {
    if (busy_ == busy && busyLabel_ == label) {
        return;
    }
    busy_ = busy;
    busyLabel_ = busy ? label : QString();
    emit busyChanged();
}

void ImageController::scheduleRefresh() {
    refreshTimer_.start();
}

void ImageController::trackOps() {
    const auto& project = studio_.session().project();
    ops_.clear();
    for (const auto& name : imagePrepNames(project)) {
        ops_.append(fromUtf8(name));
    }
}

void ImageController::refreshNow() {
    const auto& project = studio_.session().project();
    const std::uint64_t key = project.hasImage() ? originalKey(project) : 0;
    const bool same = key == lastOriginalKey_ && project.ops == lastOps_;
    trackOps();
    if (same) {
        emit changed();
        return;
    }
    lastOriginalKey_ = key;
    lastOps_ = project.ops;
    if (refreshTask_ != 0) {
        (void)tasks_.cancel(refreshTask_);
        refreshTask_ = 0;
    }
    if (!project.hasImage()) {
        width_ = height_ = 0;
        images_->set(QStringLiteral("before"), {});
        images_->set(QStringLiteral("after"), {});
        ++imageRevision_;
        emit changed();
        emit imagesChanged();
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    refreshTask_ = tasks_.submit<ImagePair>(
        "Image de travail",
        [snapshot](const TaskContext& task) -> Result<ImagePair> {
            auto after = processedImage(*snapshot);
            if (!after) {
                return std::unexpected(after.error());
            }
            if (task.cancelled()) {
                return cancelled_error();
            }
            return ImagePair{toQImage(snapshot->original), toQImage(*after)};
        },
        [this](Result<ImagePair>&& r) {
            refreshTask_ = 0;
            if (!r) {
                setMessage(QStringLiteral("Image de travail illisible : ") + errorText(r.error()),
                           true);
                return;
            }
            images_->set(QStringLiteral("before"), r->before);
            images_->set(QStringLiteral("after"), r->after);
            width_ = r->after.width();
            height_ = r->after.height();
            ++imageRevision_;
            emit changed();
            emit imagesChanged();
        });
    emit changed();
}

QString ImageController::applyPrep(const QString& kind, const QVariantMap& params) {
    clearPrepPreview();
    const auto result = runPrep(studio_.session(), kind, params);
    if (!result.ok) {
        const auto text = fromUtf8(result.message);
        setMessage(text, true);
        return text;
    }
    // Les objets déjà générés viennent de l'ancienne image : ils sont obsolètes.
    setMessage(QStringLiteral("Traitement appliqué (Ctrl+Z pour annuler). La segmentation et les "
                              "objets sont à régénérer."),
               false);
    emit prepApplied(kind);
    return {};
}

void ImageController::previewPrep(const QString& kind, const QVariantMap& params) {
    if (prepTask_ != 0) {
        (void)tasks_.cancel(prepTask_);
        prepTask_ = 0;
    }
    if (!hasImage()) {
        setMessage(QStringLiteral("Le projet n'a pas d'image source."), true);
        return;
    }
    const auto snapshot = studio_.session().snapshot();
    setBusy(true, QStringLiteral("Aperçu du traitement"));
    prepTask_ = tasks_.submit<QImage>(
        "Aperçu du traitement",
        [snapshot, kind, params](const TaskContext& task) -> Result<QImage> {
            // Copie jetable du projet : le document vivant n'est jamais touché.
            DocumentSession scratch;
            scratch.replaceProject(*snapshot, true);
            const auto done = runPrep(scratch, kind, params);
            if (!done.ok) {
                return fail(ErrorCategory::OperationImpossible, done.message);
            }
            if (task.cancelled()) {
                return cancelled_error();
            }
            auto after = processedImage(scratch.project());
            if (!after) {
                return std::unexpected(after.error());
            }
            return toQImage(*after);
        },
        [this](Result<QImage>&& r) {
            prepTask_ = 0;
            setBusy(false);
            if (!r) {
                prepPreview_ = false;
                setMessage(errorText(r.error()), true);
                emit previewChanged();
                return;
            }
            images_->set(QStringLiteral("preview"), *r);
            prepPreview_ = true;
            ++imageRevision_;
            setMessage(QStringLiteral("Aperçu : rien n'est appliqué tant que vous ne validez pas."),
                       false);
            emit previewChanged();
            emit imagesChanged();
        });
}

void ImageController::clearPrepPreview() {
    if (prepTask_ != 0) {
        (void)tasks_.cancel(prepTask_);
        prepTask_ = 0;
        setBusy(false);
    }
    if (prepPreview_) {
        prepPreview_ = false;
        images_->set(QStringLiteral("preview"), {});
        ++imageRevision_;
        emit previewChanged();
        emit imagesChanged();
    }
}

segmentation::SegmentationOptions ImageController::optionsFrom(const QVariantMap& map) const {
    segmentation::SegmentationOptions options;
    options.max_colors = std::clamp(static_cast<int>(numberOr(map, "max_colors", 8)), 2, 64);
    options.min_region_px =
        std::clamp(static_cast<int>(numberOr(map, "min_region_px", 16)), 1, 100000);
    options.smoothing_radius_px =
        std::clamp(static_cast<int>(numberOr(map, "smoothing_radius_px", 3)), 0, 10);
    return options;
}

void ImageController::previewSegmentation(const QVariantMap& optionsMap) {
    if (segTask_ != 0) {
        (void)tasks_.cancel(segTask_);
        segTask_ = 0;
    }
    if (!hasImage()) {
        setMessage(QStringLiteral("Le projet n'a pas d'image source."), true);
        return;
    }
    const auto options = optionsFrom(optionsMap);
    const auto snapshot = studio_.session().snapshot();
    setBusy(true, QStringLiteral("Aperçu de la segmentation"));
    segTask_ = tasks_.submit<SegmentationPreview>(
        "Aperçu de la segmentation",
        [snapshot, options](const TaskContext& task) -> Result<SegmentationPreview> {
            auto working = processedImage(*snapshot);
            if (!working) {
                return std::unexpected(working.error());
            }
            return desktop_v2::previewSegmentation(*working, options, task);
        },
        [this](Result<SegmentationPreview>&& r) {
            segTask_ = 0;
            setBusy(false);
            if (!r) {
                segPreview_ = false;
                setMessage(QStringLiteral("Aperçu impossible : ") + errorText(r.error()), true);
                emit previewChanged();
                return;
            }
            images_->set(
                QStringLiteral("segmentation"),
                QImage(r->rgba.data(), r->width, r->height, r->width * 4, QImage::Format_RGBA8888)
                    .copy());
            segPreview_ = true;
            std::set<std::uint32_t> previewColours;
            for (std::size_t i = 0; i + 3 < r->rgba.size(); i += 4) {
                if (r->rgba[i + 3] != 0) {
                    previewColours.insert((static_cast<std::uint32_t>(r->rgba[i]) << 16) |
                                          (static_cast<std::uint32_t>(r->rgba[i + 1]) << 8) |
                                          r->rgba[i + 2]);
                }
            }
            segInfo_ = QStringLiteral("%1 régions, %2 couleurs, la plus petite %3 px (%4 s)")
                           .arg(r->regions)
                           .arg(previewColours.size())
                           .arg(r->smallestRegionPx)
                           .arg(r->seconds, 0, 'f', 2);
            ++imageRevision_;
            emit previewChanged();
            emit imagesChanged();
        });
}

void ImageController::cancelSegmentationPreview() {
    if (segTask_ != 0) {
        (void)tasks_.cancel(segTask_);
        segTask_ = 0;
        setBusy(false);
    }
    if (segPreview_) {
        segPreview_ = false;
        segInfo_.clear();
        images_->set(QStringLiteral("segmentation"), {});
        ++imageRevision_;
        emit imagesChanged();
    }
    emit previewChanged();
}

void ImageController::applySegmentation(const QVariantMap& optionsMap) {
    if (!hasImage()) {
        setMessage(QStringLiteral("Le projet n'a pas d'image source."), true);
        return;
    }
    if (applyTask_ != 0) {
        (void)tasks_.cancel(applyTask_);
        applyTask_ = 0;
    }
    const auto options = optionsFrom(optionsMap);
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    setBusy(true, QStringLiteral("Segmentation de l'image"));
    applyTask_ = tasks_.submit<segmentation::Segmentation>(
        "Segmentation de l'image",
        [snapshot, options](const TaskContext& task) -> Result<segmentation::Segmentation> {
            auto working = processedImage(*snapshot);
            if (!working) {
                return std::unexpected(working.error());
            }
            auto seg = segmentation::segment(*working, options);
            if (!seg) {
                return std::unexpected(seg.error());
            }
            if (task.cancelled()) {
                return cancelled_error();
            }
            // Couleur de chaque région = moyenne réelle de l'image (comme `segment_image`).
            for (auto& slot : seg->region_slots) {
                if (slot) {
                    if (const auto mean =
                            segmentation::region_mean_color(*seg, *working, slot->id)) {
                        slot->rgb = *mean;
                    }
                }
            }
            return std::move(*seg);
        },
        [this, revision](Result<segmentation::Segmentation>&& r) {
            applyTask_ = 0;
            setBusy(false);
            if (!r) {
                setMessage(QStringLiteral("Segmentation impossible : ") + errorText(r.error()),
                           true);
                return;
            }
            if (studio_.session().revision() != revision) {
                setMessage(QStringLiteral("Le document a changé pendant le calcul : relancez la "
                                          "segmentation."),
                           true);
                return;
            }
            const int regions = static_cast<int>(r->region_count());
            studio_.session().execute(
                std::make_unique<commands::SetSegmentationCommand>(std::move(*r)));
            cancelSegmentationPreview();
            setMessage(QStringLiteral("Image segmentée en %1 régions (Ctrl+Z pour annuler). Les "
                                      "objets sont à régénérer.")
                           .arg(regions),
                       false);
            emit segmentationApplied(regions);
        });
}

void ImageController::cancelTask() {
    for (TaskId* id : {&refreshTask_, &prepTask_, &segTask_, &applyTask_, &aiTask_}) {
        if (*id != 0) {
            (void)tasks_.cancel(*id);
            *id = 0;
        }
    }
    aiBusy_ = false;
    setBusy(false);
    emit aiChanged();
    setMessage(QStringLiteral("Calcul annulé : le document est intact."), false);
}

void ImageController::refreshAiBackends() {
    if (aiTask_ != 0) {
        return;
    }
    aiBusy_ = true;
    emit aiChanged();
    using namespace ai_segmentation;
    struct Report {
        QVariantList backends;
        QString summary;
    };
    aiTask_ = tasks_.submit<Report>(
        "Backends de segmentation IA",
        [](const TaskContext&) -> Result<Report> {
            Report report;
            const auto path = backend_settings_env_path();
            if (path.empty()) {
                report.summary = QStringLiteral(
                    "Aucun backend IA déclaré : segmentation classique par couleurs.");
                return report;
            }
            auto settings = load_backend_settings(std::filesystem::path(path));
            if (!settings) {
                report.summary = QStringLiteral("Réglages IA illisibles : ") +
                                 fromUtf8(settings.error().message) +
                                 QStringLiteral(" Segmentation classique utilisée.");
                return report;
            }
            const auto statuses = discover_backends(*settings, process_transport_factory());
            for (const auto& status : statuses) {
                report.backends.append(QVariantMap{
                    {QStringLiteral("id"), fromUtf8(status.config.id)},
                    {QStringLiteral("name"), fromUtf8(status.config.display_name)},
                    {QStringLiteral("state"),
                     fromUtf8(std::string(backend_state_name(status.state)))},
                    {QStringLiteral("available"), status.state == BackendState::Available},
                    {QStringLiteral("diagnostics"), fromUtf8(status.diagnostics)}});
            }
            const auto resolution = resolve_backend(statuses, settings->preferred_id);
            report.summary = fromUtf8(resolution.reason);
            if (resolution.classical_fallback) {
                report.summary += QStringLiteral(" Repli : segmentation classique.");
            }
            return report;
        },
        [this](Result<Report>&& r) {
            aiTask_ = 0;
            aiBusy_ = false;
            if (r) {
                ai_ = r->backends;
                aiSummary_ = r->summary;
            } else {
                ai_.clear();
                aiSummary_ = QStringLiteral("Sondage IA impossible : ") + errorText(r.error());
            }
            emit aiChanged();
        });
}

} // namespace openstitch::desktop_v2::studio
