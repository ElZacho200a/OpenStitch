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
QImage buildOverlay(const segmentation::DetailMask& mask, bool linework) {
    QImage image(mask.width, mask.height, QImage::Format_ARGB32_Premultiplied);
    constexpr std::uint32_t kMaxAlpha = 140;
    // Orange pour le détail, cyan pour les traits fins.
    const std::uint32_t kR = linework ? 0U : 255U;
    const std::uint32_t kG = linework ? 190U : 140U;
    const std::uint32_t kB = linework ? 255U : 0U;
    for (int y = 0; y < mask.height; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(image.scanLine(y));
        const auto* weights = mask.weights.data() + static_cast<std::size_t>(y) * mask.width;
        for (int x = 0; x < mask.width; ++x) {
            const std::uint32_t a = weights[x] * kMaxAlpha / 255U;
            row[x] = (a << 24) | ((kR * a / 255U) << 16) | ((kG * a / 255U) << 8) | (kB * a / 255U);
        }
    }
    return image;
}

QString overlayKey(document::MaskKind kind) {
    return kind == document::MaskKind::Linework ? QStringLiteral("linework")
                                                : QStringLiteral("detail");
}

} // namespace

DetailZoneController::DetailZoneController(StudioController& studio, ImageController& images,
                                           document::MaskKind kind, QObject* parent)
    : QObject(parent), studio_(studio), images_(images), kind_(kind) {
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
    // Peindre sur une carte masquée serait aveugle : l'outil la rend visible.
    if (!tool_.isEmpty()) {
        setOverlayVisible(true);
    }
}

void DetailZoneController::setOverlayVisible(bool visible) {
    if (visible == overlayVisible_) {
        return;
    }
    overlayVisible_ = visible;
    emit overlayVisibleChanged();
}

void DetailZoneController::markShapesUpdated() {
    if (kind_ != document::MaskKind::Linework || !dirty_) {
        return;
    }
    dirty_ = false;
    setMessage(QStringLiteral("Formes mises à jour : les zones de traits fins sont appliquées."));
    emit maskChanged();
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
        paintDetailPixels(studio_.session(), w, h, mmPerPx, pixels, radiusMm_, erase, kind_);
    if (!result.ok) {
        const auto text = QString::fromStdString(result.message);
        setMessage(text, true);
        return text;
    }
    dirty_ = true;
    if (kind_ == document::MaskKind::Linework) {
        setMessage(erase ? QStringLiteral("Zone de traits fins effacée. Mettez à jour les formes "
                                          "pour l'appliquer.")
                         : QStringLiteral("Zone de traits fins peinte. Mettez à jour les formes "
                                          "pour l'appliquer."));
        return {};
    }
    setMessage(erase ? QStringLiteral("Zone de détail effacée.")
                     : QStringLiteral("Zone de détail peinte. Relancez la segmentation pour "
                                      "l'appliquer."));
    return {};
}

QString DetailZoneController::clearAll() {
    const auto result = clearDetail(studio_.session(), kind_);
    if (!result.ok) {
        const auto text = QString::fromStdString(result.message);
        setMessage(text, true);
        return text;
    }
    dirty_ = true;
    setMessage(kind_ == document::MaskKind::Linework
                   ? QStringLiteral("Zones de traits fins effacées. Mettez à jour les formes "
                                    "pour l'appliquer.")
                   : QStringLiteral(
                         "Zones de détail effacées. Relancez la segmentation pour l'appliquer."));
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
    const auto& mask = project.hasActiveImage() ? project.imageSource().mask(kind_) : none;
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
        images_.images()->set(overlayKey(kind_),
                              buildOverlay(mask, kind_ == document::MaskKind::Linework));
    } else {
        coverage_ = 0.0;
        // Détail : plus de zone, rien à relancer. Traits fins : l'effacement reste à appliquer.
        dirty_ = dirty_ && kind_ == document::MaskKind::Linework;
        images_.images()->set(overlayKey(kind_), {});
    }
    ++overlayRevision_;
    emit maskChanged();
}

} // namespace openstitch::desktop_v2::studio
