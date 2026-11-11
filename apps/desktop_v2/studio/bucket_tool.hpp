// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "paint_bucket.hpp"
#include "shape_batch.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"
#include <QObject>
#include <QTimer>

namespace openstitch::desktop_v2::studio {
class BucketTool : public QObject {
    Q_OBJECT
public:
    explicit BucketTool(StudioController& studio, QObject* parent = nullptr);
    ~BucketTool() override;
    bool begin(PointMm point);
    void end();
    bool cancel();
    bool active() const { return active_; }
    const std::vector<geometry::PathSet>& preview() const { return preview_; }
signals:
    void changed();
    void message(const QString& text);

private:
    void commit();
    StudioController& studio_;
    std::uint64_t revision_{};
    std::uint64_t request_{};
    bool active_{false};
    bool finishing_{false};
    std::vector<geometry::PathSet> preview_;
    TaskManager tasks_;
};
} // namespace openstitch::desktop_v2::studio
