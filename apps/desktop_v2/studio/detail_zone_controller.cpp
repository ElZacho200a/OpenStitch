// SPDX-License-Identifier: Apache-2.0
#include "detail_zone_controller.hpp"

#include <QImage>
#include <algorithm>
#include <cmath>

#include "source_image_tools.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

constexpr std::size_t kMaxStrokePoints = 200'000;

// Incrustation orange translucide : opacité proportionnelle au poids du masque (bord doux compris).
QImage buildOverlay(const segmentation::DetailMask& mask) {
    QImage image(mask.width, mask.height, QImage::Format_ARGB32_Premultiplied);
    constexpr std::uint32_t kMaxAlpha = 140;
    constexpr std::uint32_t kR = 255;
    constexpr std::uint32_t kG = 140;
    for (int y = 0; y < mask.height; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(image.scanLine(y));
        const auto* weights = mask.weights.data() + static_cast<std::size_t>(y) * mask.width;
        for (int x = 0; x < mask.width; ++x) {
            const std::uint32_t a = weights[x] * kMaxAlpha / 255U;
            row[x] = (a << 24) | ((kR * a / 255U) << 16) | ((kG * a / 255U) << 8);
        }
    }
    return image;
}

} // namespace

DetailZoneController::DetailZoneController(StudioController& studio, ImageController& images,
                                           QObject* parent)
    : QObject(parent), studio_(studio), images_(images) {
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(0);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { refresh(); });
    token_ = studio_.session().subscribe([this](ChangeKind) { scheduleRefresh(); });
    // La segmentation vient de profiter du masque : plus rien à relancer.
    connect(&images_, &ImageController::segmentationApplied, this, [this](int) {
        dirty_ = false;
        emit maskChanged();
    });
    connect(&images_, &ImageController::segmentationAppliedToLayer, this, [this](ObjectId) {
        dirty_ = false;
        emit maskChanged();
    });
    scheduleRefresh();
}

DetailZoneController::~DetailZoneController() {
    studio_.session().unsubscribe(token_);
}

void DetailZoneController::setTool(const QString& tool) {
    const QString next =
        tool == QLatin1String("paint") || tool == QLatin1String("erase") ? tool : QString();
    if (next == tool_) {
        return;
    }
    tool_ = next;
    emit toolChanged();
}

void DetailZoneController::setRadiusMm(double radiusMm) {
    const double clamped = std::clamp(radiusMm, kMinDetailRadiusMm, kMaxDetailRadiusMm);
    if (std::abs(clamped - radiusMm_) < 1e-9) {
        return;
    }
    radiusMm_ = clamped;
    emit radiusChanged();
}

void DetailZoneController::stepRadius(int steps) {
    setRadiusMm(radiusMm_ * std::pow(1.2, steps));
}

QString DetailZoneController::paintStroke(const QVariantList& scenePointsMm) {
    const int w = images_.imageWidth();
    const int h = images_.imageHeight();
    const double mmPerPx = images_.mmPerPx();
    if (w <= 0 || h <= 0 || !(mmPerPx > 0.0)) {
        const auto text = QStringLiteral("Ce projet n'a pas d'image source.");
        setMessage(text, true);
        return text;
    }
    // Pixels du trait. Entre deux points consécutifs, des centres intermédiaires tous les
    // demi-rayons : un geste rapide ne laisse pas de trous dans la zone peinte.
    const double stepPx = std::max(1.0, radiusMm_ / mmPerPx / 2.0);
    std::vector<std::pair<int, int>> pixels;
    bool havePrevious = false;
    double px = 0.0;
    double py = 0.0;
    for (const auto& item : scenePointsMm) {
        const auto point = item.toMap();
        const double x = point.value(QStringLiteral("x")).toDouble() / mmPerPx + w / 2.0;
        const double y = point.value(QStringLiteral("y")).toDouble() / mmPerPx + h / 2.0;
        if (havePrevious) {
            const double distance = std::hypot(x - px, y - py);
            const int pieces = std::max(1, static_cast<int>(std::ceil(distance / stepPx)));
            for (int i = 1; i <= pieces && pixels.size() < kMaxStrokePoints; ++i) {
                const double t = static_cast<double>(i) / pieces;
                pixels.emplace_back(static_cast<int>(std::floor(px + (x - px) * t)),
                                    static_cast<int>(std::floor(py + (y - py) * t)));
            }
        } else {
            pixels.emplace_back(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
        }
        havePrevious = true;
        px = x;
        py = y;
    }
    const bool erase = tool_ == QLatin1String("erase");
    const auto result =
        paintDetailPixels(studio_.session(), w, h, mmPerPx, pixels, radiusMm_, erase);
    if (!result.ok) {
        const auto text = QString::fromStdString(result.message);
        setMessage(text, true);
        return text;
    }
    dirty_ = true;
    setMessage(erase ? QStringLiteral("Zone de détail effacée.")
                     : QStringLiteral("Zone de détail peinte. Relancez la segmentation pour "
                                      "l'appliquer."));
    return {};
}

QString DetailZoneController::clearAll() {
    const auto result = clearDetail(studio_.session());
    if (!result.ok) {
        const auto text = QString::fromStdString(result.message);
        setMessage(text, true);
        return text;
    }
    dirty_ = true;
    setMessage(
        QStringLiteral("Zones de détail effacées. Relancez la segmentation pour l'appliquer."));
    return {};
}

void DetailZoneController::setMessage(const QString& text, bool error) {
    message_ = text;
    messageIsError_ = error;
    emit messageChanged();
}

void DetailZoneController::scheduleRefresh() {
    if (!refreshTimer_.isActive()) {
        refreshTimer_.start();
    }
}

void DetailZoneController::refresh() {
    static const segmentation::DetailMask none;
    const auto& project = studio_.session().project();
    const auto& mask = project.hasActiveImage() ? project.imageSource().detail_mask : none;
    if (mask.width == shownWidth_ && mask.height == shownHeight_ && mask.weights == shownWeights_) {
        return;
    }
    shownWidth_ = mask.width;
    shownHeight_ = mask.height;
    shownWeights_ = mask.weights;
    if (mask.any_detail()) {
        std::size_t marked = 0;
        for (const auto weight : mask.weights) {
            marked += weight >= 128 ? 1U : 0U;
        }
        coverage_ = 100.0 * static_cast<double>(marked) / static_cast<double>(mask.weights.size());
        images_.images()->set(QStringLiteral("detail"), buildOverlay(mask));
    } else {
        coverage_ = 0.0;
        dirty_ = false;
        images_.images()->set(QStringLiteral("detail"), {});
    }
    ++overlayRevision_;
    emit maskChanged();
}

} // namespace openstitch::desktop_v2::studio
