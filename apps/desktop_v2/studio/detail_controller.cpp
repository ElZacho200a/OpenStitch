// SPDX-License-Identifier: Apache-2.0
#include "detail_controller.hpp"

#include <QBuffer>
#include <QImage>
#include <cmath>

#include "openstitch/commands/replace_project_command.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
QString pngUrl(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}
QString encodeMask(const segmentation::DetailMask& mask) {
    if (mask.empty()) {
        return {};
    }
    QImage image(mask.width, mask.height, QImage::Format_RGBA8888);
    for (int y = 0; y < mask.height; ++y) {
        auto* row = image.scanLine(y);
        for (int x = 0; x < mask.width; ++x) {
            const auto weight = mask.weights[static_cast<std::size_t>(y) * mask.width + x];
            row[4 * x] = 234;
            row[4 * x + 1] = 62;
            row[4 * x + 2] = 126;
            row[4 * x + 3] = static_cast<uchar>(weight / 2);
        }
    }
    return pngUrl(image);
}
struct ClickOutput {
    std::shared_ptr<const document::Project> project;
    std::uint64_t id{0};
};
} // namespace

DetailController::DetailController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    pump_.setInterval(16);
    connect(&pump_, &QTimer::timeout, this, [this] {
        tasks_.pump();
        if (clickTask_ && tasks_.idle()) {
            clickTask_ = 0;
            emit changed();
        }
    });
    pump_.start();
    refreshTimer_.setSingleShot(true);
    connect(&refreshTimer_, &QTimer::timeout, this, &DetailController::refresh);
    token_ = studio_.session().subscribe([this](ChangeKind) {
        cancel();
        processed_.reset();
        imageUrl_.clear();
        showCommittedMask();
        refreshTimer_.start(0);
    });
    refreshTimer_.start(0);
}
DetailController::~DetailController() {
    pump_.stop();
    tasks_.cancelAll();
    studio_.session().unsubscribe(token_);
}
QSizeF DetailController::imageSizeMm() const {
    const double mm = studio_.session().project().mm_per_px.value;
    return ready() ? QSizeF(processed_->width * mm, processed_->height * mm) : QSizeF();
}
void DetailController::inform(QString text) {
    message_ = std::move(text);
    emit changed();
}
void DetailController::setRadiusMm(double value) {
    if (!std::isfinite(value) || value < 0.2 || value > 50.0) {
        inform(QStringLiteral("Rayon : entre 0,2 et 50 mm."));
        return;
    }
    cancel();
    radius_ = value;
    emit changed();
}
void DetailController::setTolerance(int value) {
    if (value < 0 || value > 100) {
        inform(QStringLiteral("Tolérance : entre 0 et 100."));
        return;
    }
    tolerance_ = value;
    emit changed();
}
void DetailController::setStitchKind(int value) {
    if (value < 0 || value > 3) {
        inform(QStringLiteral("Type de point inconnu."));
        return;
    }
    kind_ = value;
    emit changed();
}
void DetailController::setReplaceSelected(bool value) {
    pending_ = false;
    replace_ = value;
    emit changed();
}
void DetailController::setShowMask(bool value) {
    showMask_ = value;
    emit changed();
}
void DetailController::refresh() {
    tasks_.cancel(refreshTask_);
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    if (snapshot->original.empty()) {
        inform(QStringLiteral("Importez une image source pour utiliser ces outils."));
        return;
    }
    refreshTask_ = tasks_.submit<image::Image>(
        "Image source des outils détail",
        [snapshot](const TaskContext& task) -> Result<image::Image> {
            if (task.cancelled()) {
                return cancelled_error();
            }
            return processedImage(*snapshot);
        },
        [this, revision](Result<image::Image>&& result) {
            if (revision != studio_.session().revision()) {
                return;
            }
            if (!result) {
                inform(QString::fromStdString(result.error().message));
                return;
            }
            processed_ = std::make_shared<const image::Image>(std::move(*result));
            const auto& img = *processed_;
            imageUrl_ = pngUrl(QImage(img.rgba.data(), img.width, img.height, img.width * 4,
                                      QImage::Format_RGBA8888));
            showCommittedMask();
        });
}
void DetailController::showCommittedMask() {
    maskUrl_ = encodeMask(studio_.session().project().detail_mask);
    emit changed();
}
bool DetailController::press(const QString& tool, double x, double y) {
    cancel();
    if (!ready()) {
        inform(QStringLiteral("Image source absente ou en cours de préparation."));
        return false;
    }
    clickStroke_ = tool == QLatin1String("click-to-stitch");
    if (clickStroke_) {
        pendingX_ = x;
        pendingY_ = y;
        stroke_ = {{x, y}};
    } else {
        erase_ = tool == QLatin1String("detail-erase");
        stroke_ = {{x, y}};
        preview();
    }
    emit changed();
    return true;
}
void DetailController::move(double x, double y) {
    if (!stroke_.empty() && !clickStroke_) {
        const auto [lastX, lastY] = stroke_.back();
        const double step = std::max(0.05, radius_ / 3.0);
        const int steps = static_cast<int>(std::ceil(std::hypot(x - lastX, y - lastY) / step));
        for (int i = 1; i <= steps; ++i) {
            stroke_.emplace_back(lastX + (x - lastX) * i / steps, lastY + (y - lastY) * i / steps);
        }
        preview();
    }
}
void DetailController::preview() {
    tasks_.cancel(previewTask_);
    const auto serial = ++previewSerial_;
    const auto snapshot = studio_.session().snapshot();
    const auto image = processed_;
    const auto points = stroke_;
    const auto radius = radius_;
    const bool erase = erase_;
    previewTask_ = tasks_.submit<segmentation::DetailMask>(
        "Aperçu du masque de détail",
        [snapshot, image, points, radius,
         erase](const TaskContext& task) -> Result<segmentation::DetailMask> {
            if (task.cancelled()) {
                return cancelled_error();
            }
            return previewDetail(*snapshot, *image, points, radius, erase);
        },
        [this, serial](Result<segmentation::DetailMask>&& result) {
            if (serial == previewSerial_ && result && gestureActive()) {
                maskUrl_ = encodeMask(*result);
                emit changed();
            }
        });
}
void DetailController::release(double x, double y) {
    if (!gestureActive() || !ready()) {
        return;
    }
    if (clickStroke_) {
        stroke_.clear();
        click(pendingX_, pendingY_);
        return;
    }
    move(x, y);
    const auto points = std::move(stroke_);
    stroke_.clear();
    tasks_.cancel(previewTask_);
    ++previewSerial_;
    const auto result = paintDetail(studio_.session(), *processed_, points, radius_, erase_);
    inform(result.ok ? QStringLiteral("Masque de détail modifié (Ctrl+Z pour annuler).")
                     : QString::fromStdString(result.message));
    showCommittedMask();
}
void DetailController::click(double x, double y, bool confirmed) {
    if (!ready() || busy()) {
        return;
    }
    std::optional<std::uint64_t> replaceId;
    if (replace_) {
        const auto& targets = studio_.session().selection().targets();
        if (targets.size() != 1 || targets.front().kind != TargetKind::EmbroideryObject) {
            inform(QStringLiteral("Sélectionnez un seul objet à régénérer."));
            return;
        }
        replaceId = targets.front().id;
        const auto* obj = studio_.session().project().findEmbroidery(ObjectId{*replaceId});
        if (obj && !obj->overrides.empty() && !confirmed) {
            pending_ = true;
            pendingX_ = x;
            pendingY_ = y;
            pendingRevision_ = studio_.session().revision();
            pendingObject_ = *replaceId;
            inform(QStringLiteral("La régénération supprimera les retouches de cet objet."));
            return;
        }
    }
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    const auto image = processed_;
    const int tolerance = tolerance_;
    const auto kind = static_cast<ClickKind>(kind_);
    clickTask_ = tasks_.submit<ClickOutput>(
        "Clic-pour-broder",
        [snapshot, image, x, y, tolerance, kind, replaceId,
         confirmed](const TaskContext& task) -> Result<ClickOutput> {
            if (task.cancelled()) {
                return cancelled_error();
            }
            DocumentSession draft;
            draft.replaceProject(*snapshot, true);
            const auto result =
                clickToStitch(draft, *image, x, y, tolerance, kind, replaceId, confirmed);
            if (!result.ok) {
                return fail(ErrorCategory::OperationImpossible, result.message);
            }
            if (task.cancelled()) {
                return cancelled_error();
            }
            return ClickOutput{draft.snapshot(), result.objectId};
        },
        [this, snapshot, revision](Result<ClickOutput>&& result) {
            clickTask_ = 0;
            if (revision != studio_.session().revision()) {
                inform(QStringLiteral("Document modifié : résultat ignoré."));
                return;
            }
            if (!result) {
                inform(QString::fromStdString(result.error().message));
                return;
            }
            studio_.session().execute(std::make_unique<commands::ReplaceProjectCommand>(
                "Clic-pour-broder", snapshot, result->project));
            studio_.selectObject(QString::number(result->id), false);
            inform(QStringLiteral("Objet brodé créé (Ctrl+Z pour annuler)."));
        });
    emit changed();
}
bool DetailController::cancel() {
    const bool had = gestureActive() || pending_ || busy();
    stroke_.clear();
    pending_ = false;
    tasks_.cancel(previewTask_);
    tasks_.cancel(clickTask_);
    clickTask_ = 0;
    ++previewSerial_;
    if (had) {
        showCommittedMask();
    }
    return had;
}
void DetailController::clearMask() {
    cancel();
    const auto result = clearDetail(studio_.session());
    inform(result.ok ? QStringLiteral("Zones de détail effacées.")
                     : QString::fromStdString(result.message));
}
void DetailController::confirmReplacement(bool discardRetouches) {
    if (!pending_) {
        return;
    }
    pending_ = false;
    const auto& targets = studio_.session().selection().targets();
    if (discardRetouches && pendingRevision_ == studio_.session().revision() &&
        targets.size() == 1 &&
        targets.front() == Target{TargetKind::EmbroideryObject, pendingObject_}) {
        click(pendingX_, pendingY_, true);
    } else {
        inform(QStringLiteral("Régénération annulée."));
    }
    emit changed();
}

} // namespace openstitch::desktop_v2::studio
