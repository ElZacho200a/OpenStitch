// SPDX-License-Identifier: Apache-2.0
#include "compare_controller.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "source_image_tools.hpp"
#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2::studio {
namespace {
OverlayImage placed(QImage image, double mmPerPx) {
    OverlayImage result;
    result.image = image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    result.widthMm = image.width() * mmPerPx;
    result.heightMm = image.height() * mmPerPx;
    result.x0 = -result.widthMm / 2;
    result.y0 = -result.heightMm / 2;
    return result;
}
void appendPath(std::vector<OverlayJump>& lines, const geometry::Path& path) {
    const auto flat = geometry::flatten(path, Micrometers{40});
    const auto& pts = flat.points;
    const auto line = [&lines](Vec2um a, Vec2um b) {
        lines.push_back(
            {a.x.value / 1000.0, -a.y.value / 1000.0, b.x.value / 1000.0, -b.y.value / 1000.0});
    };
    for (std::size_t i = 1; i < pts.size(); ++i)
        line(pts[i - 1], pts[i]);
    if (flat.closed && pts.size() > 2)
        line(pts.back(), pts.front());
}
} // namespace
CompareController::CompareController(StudioController& studio, ViewController& view,
                                     ImageController& images, QObject* parent)
    : QObject(parent), studio_(studio), view_(view), images_(images) {
    pump_.setInterval(8);
    connect(&pump_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    pump_.start();
    refresh_.setSingleShot(true);
    refresh_.setInterval(0);
    connect(&refresh_, &QTimer::timeout, this, &CompareController::rebuild);
    connect(&studio_, &StudioController::sceneChanged, this, [this] {
        // Discard stale resources immediately; never show an earlier document's source.
        layers_.reset();
        publish();
        refresh_.start();
    });
    connect(&images_, &ImageController::imagesChanged, this, [this] { refresh_.start(); });
    connect(&images_, &ImageController::previewChanged, this, [this] { refresh_.start(); });
    refresh_.start();
}
CompareController::~CompareController() {
    pump_.stop();
    tasks_.cancelAll();
}
void CompareController::publish() {
    view_.setComparison(layers_, display_);
    emit changed();
}
#define OS_COMPARE_FLAG(Name, Field)                                                               \
    void CompareController::set##Name(bool value) {                                                \
        if (display_.Field == value)                                                               \
            return;                                                                                \
        display_.Field = value;                                                                    \
        publish();                                                                                 \
    }
OS_COMPARE_FLAG(SourceVisible, source)
OS_COMPARE_FLAG(SegmentationVisible, segmentation)
OS_COMPARE_FLAG(VectorsVisible, vectors)
OS_COMPARE_FLAG(OrderVisible, order)
OS_COMPARE_FLAG(Curtain, curtain)
#undef OS_COMPARE_FLAG
void CompareController::setOpacity(double value) {
    if (!std::isfinite(value))
        return;
    value = std::clamp(value, 0.0, 1.0);
    if (display_.opacity == value)
        return;
    display_.opacity = value;
    publish();
}
void CompareController::setPosition(double value) {
    if (!std::isfinite(value))
        return;
    value = std::clamp(value, 0.0, 1.0);
    if (display_.position == value)
        return;
    display_.position = value;
    publish();
}
void CompareController::rebuild() {
    const auto scene = studio_.scene();
    if (!scene || scene->revision != studio_.session().revision())
        return;
    const int preview = images_.segPreviewActive() ? images_.imageRevision() : 0;
    if (requestedDocument_ == scene->revision && requestedPreview_ == preview)
        return;
    requestedDocument_ = scene->revision;
    requestedPreview_ = preview;
    const auto request = ++request_;
    tasks_.cancelAll();
    // QImages are immutable, implicitly shared values captured before entering the worker.
    auto segmentation = images_.segPreviewActive()
                            ? images_.images()->get(QStringLiteral("segmentation"))
                            : QImage{};
    const double mmPerPx = scene->project->mm_per_px.value;
    tasks_.submit<std::shared_ptr<const ComparisonLayers>>(
        "studio.comparison",
        [scene, segmentation,
         mmPerPx](const TaskContext& task) -> Result<std::shared_ptr<const ComparisonLayers>> {
            auto data = std::make_shared<ComparisonLayers>();
            const auto image = processedImage(*scene->project);
            if (image && !image->empty()) {
                data->source = placed(QImage(image->rgba.data(), image->width, image->height,
                                             image->width * 4, QImage::Format_RGBA8888)
                                          .copy(),
                                      mmPerPx);
            }
            if (!segmentation.isNull())
                data->segmentation = placed(segmentation, mmPerPx);
            else if (scene->project->segmentation) {
                const auto map = segmentation::render_map(*scene->project->segmentation);
                data->segmentation = placed(QImage(map.rgba.data(), map.width, map.height,
                                                   map.width * 4, QImage::Format_RGBA8888)
                                                .copy(),
                                            mmPerPx);
            }
            for (const auto& shape : scene->project->vector_objects) {
                if (!shape.visible)
                    continue;
                if (task.cancelled())
                    return std::shared_ptr<const ComparisonLayers>{};
                for (const auto& set : shape.paths) {
                    appendPath(data->vectors, set.outer);
                    for (const auto& hole : set.holes)
                        appendPath(data->vectors, hole);
                }
            }
            // Connect object transitions in the actual effective sewing sequence.
            for (std::size_t i = 1; i < scene->segments.size(); ++i) {
                if (scene->segments[i - 1].objectId != scene->segments[i].objectId) {
                    const auto& a = scene->segments[i - 1].thread;
                    const auto& b = scene->segments[i].thread;
                    data->order.push_back({a.bx, a.by, b.ax, b.ay});
                    const double dx = b.ax - a.bx, dy = b.ay - a.by;
                    const double length = std::hypot(dx, dy);
                    if (length > 0.2) {
                        const double size = std::min(1.0, length / 3);
                        const double ux = dx / length, uy = dy / length;
                        data->order.push_back({b.ax, b.ay, b.ax - size * ux + size * uy / 2,
                                               b.ay - size * uy - size * ux / 2});
                        data->order.push_back({b.ax, b.ay, b.ax - size * ux - size * uy / 2,
                                               b.ay - size * uy + size * ux / 2});
                    }
                }
            }
            return std::shared_ptr<const ComparisonLayers>(std::move(data));
        },
        [this, request,
         revision = scene->revision](Result<std::shared_ptr<const ComparisonLayers>>&& result) {
            if (request != request_ || revision != studio_.session().revision() || !result ||
                !*result)
                return;
            layers_ = std::move(*result);
            ++builds_;
            publish();
        });
}
} // namespace openstitch::desktop_v2::studio
