// SPDX-License-Identifier: Apache-2.0
#include "bucket_tool.hpp"
#include <cmath>
namespace openstitch::desktop_v2::studio {
BucketTool::BucketTool(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    timer_.setInterval(8);
    connect(&timer_, &QTimer::timeout, this, [this] { tasks_.pump(); });
    timer_.start();
}
BucketTool::~BucketTool() {
    timer_.stop();
    tasks_.cancelAll();
}
bool BucketTool::begin(PointMm point) {
    cancel();
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 1000 ||
        std::abs(point.y) > 1000) {
        emit message(tr("Position du pot hors limites"));
        return false;
    }
    const Vec2um seed{Micrometers{static_cast<std::int32_t>(std::lround(point.x * 1000))},
                      Micrometers{static_cast<std::int32_t>(std::lround(point.y * 1000))}};
    revision_ = studio_.session().revision();
    const auto request = request_;
    active_ = true;
    tasks_.submit<geometry::PathSet>(
        "studio.bucket-preview",
        [snapshot = studio_.session().snapshot(), seed](const TaskContext& task) {
            return previewBucket(*snapshot, seed, Micrometers{100}, task);
        },
        [this, request](Result<geometry::PathSet>&& result) {
            if (request != request_)
                return;
            if (revision_ != studio_.session().revision()) {
                cancel();
                emit message(tr("Le document a changé ; remplissage annulé"));
                return;
            }
            if (!result) {
                const auto text = QString::fromStdString(result.error().message);
                cancel();
                emit message(text);
                return;
            }
            preview_ = {std::move(*result)};
            emit changed();
            if (finishing_)
                commit();
        });
    emit changed();
    return true;
}
void BucketTool::end() {
    finishing_ = true;
    if (active_ && !preview_.empty())
        commit();
}
bool BucketTool::cancel() {
    const bool had = active_;
    ++request_;
    tasks_.cancelAll();
    active_ = finishing_ = false;
    preview_.clear();
    emit changed();
    return had;
}
void BucketTool::commit() {
    auto result = commitBucket(studio_.session(), revision_, std::move(preview_.front()));
    cancel();
    emit message(result ? tr("Vide rempli — forme vectorielle créée")
                        : QString::fromStdString(result.error().message));
}
} // namespace openstitch::desktop_v2::studio
