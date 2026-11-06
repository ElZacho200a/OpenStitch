// SPDX-License-Identifier: Apache-2.0
#pragma once

// Logical-pixel camera; device pixel ratio belongs to the graphics backend, never the model.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "studio_scene.hpp"

namespace openstitch::desktop_v2 {

class StudioCamera {
public:
    void setViewport(double width, double height) {
        if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
            (width == width_ && height == height_)) {
            return;
        }
        offset_.x += (width - width_) / 2.0;
        offset_.y += (height - height_) / 2.0;
        width_ = width;
        height_ = height;
        ++revision_;
    }

    void pan(double dx, double dy) {
        if (!std::isfinite(dx) || !std::isfinite(dy) || (dx == 0 && dy == 0)) {
            return;
        }
        offset_.x += dx;
        offset_.y += dy;
        ++revision_;
    }

    void zoomAt(double factor, ScenePoint screen) {
        if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(screen.x) ||
            !std::isfinite(screen.y)) {
            return;
        }
        const double next = std::clamp(scale_ * factor, kMinScale, kMaxScale);
        if (next == scale_) {
            return;
        }
        const ScenePoint anchor = screenToWorld(screen);
        scale_ = next;
        offset_ = {screen.x - anchor.x * scale_, screen.y - anchor.y * scale_};
        ++revision_;
    }

    void fit(const stitch_render::RectMm& bounds, double padding = 40.0) {
        if (!std::isfinite(bounds.x0) || !std::isfinite(bounds.y0) || !std::isfinite(bounds.x1) ||
            !std::isfinite(bounds.y1) || bounds.x1 < bounds.x0 || bounds.y1 < bounds.y0 ||
            !std::isfinite(padding) || width_ <= 0 || height_ <= 0) {
            return;
        }
        padding = std::clamp(padding, 0.0, std::max(0.0, std::min(width_, height_) / 2.0 - 1.0));
        scale_ = std::clamp(std::min((width_ - 2 * padding) / std::max(0.22, bounds.width()),
                                     (height_ - 2 * padding) / std::max(0.22, bounds.height())),
                            kMinScale, kMaxScale);
        offset_ = {width_ / 2.0 - (bounds.x0 + bounds.x1) / 2.0 * scale_,
                   height_ / 2.0 - (bounds.y0 + bounds.y1) / 2.0 * scale_};
        ++revision_;
    }

    [[nodiscard]] ScenePoint worldToScreen(ScenePoint world) const {
        return {world.x * scale_ + offset_.x, world.y * scale_ + offset_.y};
    }
    [[nodiscard]] ScenePoint screenToWorld(ScenePoint screen) const {
        return {(screen.x - offset_.x) / scale_, (screen.y - offset_.y) / scale_};
    }
    [[nodiscard]] double scale() const { return scale_; }
    [[nodiscard]] ScenePoint offset() const { return offset_; }
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
    static constexpr double kMinScale = 0.02;
    static constexpr double kMaxScale = 4000.0;
    double width_{0};
    double height_{0};
    double scale_{1};
    ScenePoint offset_;
    std::uint64_t revision_{0};
};

} // namespace openstitch::desktop_v2
