// SPDX-License-Identifier: Apache-2.0
#include "warp_tool.hpp"

#include "task_pump.hpp"
#include <cmath>

namespace openstitch::desktop_v2::studio {
namespace {
std::optional<Vec2um> position(PointMm p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || std::abs(p.x) > 1000 || std::abs(p.y) > 1000)
        return {};
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x * 1000))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y * 1000))}};
}
} // namespace
WarpTool::WarpTool(StudioController& studio, QObject* parent) : QObject(parent), studio_(studio) {
    attachTaskPump(tasks_, this);
}
WarpTool::~WarpTool() {
    tasks_.cancelAll();
}
void WarpTool::setRadiusMm(double value) {
    const auto valid = validateWarpOptions({value, options_.strength});
    if (!valid) {
        emit message(QString::fromStdString(valid.error().message));
        return;
    }
    cancel();
    options_.radiusMm = value;
    emit optionsChanged();
}
void WarpTool::setStrength(double value) {
    const auto valid = validateWarpOptions({options_.radiusMm, value});
    if (!valid) {
        emit message(QString::fromStdString(valid.error().message));
        return;
    }
    cancel();
    options_.strength = value;
    emit optionsChanged();
}
bool WarpTool::begin(PointMm point) {
    cancel();
    const auto p = position(point);
    const auto& selection = studio_.session().selection().targets();
    if (!p || selection.size() != 1 || selection.front().kind != TargetKind::EmbroideryObject) {
        emit message(tr("Sélectionnez d'abord UNE forme vectorielle"));
        return false;
    }
    snapshot_ = studio_.session().snapshot();
    object_ = ObjectId{selection.front().id};
    revision_ = studio_.session().revision();
    strokeOptions_ = options_;
    touches_ = {{*p, false}};
    active_ = true;
    calculate();
    return true;
}
void WarpTool::move(PointMm point, bool smooth) {
    if (!active_ || finishing_)
        return;
    const auto p = position(point);
    if (!p || touches_.size() >= 4096) {
        cancel();
        emit message(tr("Geste hors limites ou trop long"));
        return;
    }
    if (touches_.back().point == *p && touches_.back().smooth == smooth)
        return;
    touches_.push_back({*p, smooth});
    if (!running_)
        calculate();
}
void WarpTool::end(PointMm point, bool smooth) {
    move(point, smooth);
    if (!active_)
        return;
    finishing_ = true;
    if (!running_)
        calculate();
}
bool WarpTool::cancel() {
    const bool had = active_;
    ++request_;
    tasks_.cancelAll();
    active_ = running_ = finishing_ = false;
    touches_.clear();
    preview_.clear();
    snapshot_.reset();
    emit changed();
    return had;
}
void WarpTool::calculate() {
    running_ = true;
    const auto request = request_;
    const auto count = touches_.size();
    const auto finish = finishing_;
    tasks_.submit<WarpPreview>(
        "studio.warp-preview",
        [snapshot = snapshot_, object = object_, options = strokeOptions_, touches = touches_,
         finish](const TaskContext& task) {
            return previewWarp(*snapshot, object, options, touches, finish, task);
        },
        [this, request, count, finish](Result<WarpPreview>&& result) {
            if (request != request_)
                return;
            running_ = false;
            if (revision_ != studio_.session().revision()) {
                cancel();
                emit message(tr("Le document a changé ; geste annulé"));
                return;
            }
            if (!result) {
                const auto text = QString::fromStdString(result.error().message);
                cancel();
                emit message(text);
                return;
            }
            preview_ = std::move(result->paths);
            emit changed();
            if (count != touches_.size() || finish != finishing_) {
                calculate();
                return;
            }
            if (finish) {
                auto command = std::move(result->command);
                cancel();
                studio_.session().execute(std::move(command));
                emit message(tr("Contour déformé — une étape d'annulation"));
            }
        });
}
} // namespace openstitch::desktop_v2::studio
