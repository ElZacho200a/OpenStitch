// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "shape_batch.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"
#include "warp_brush.hpp"
#include <QObject>
#include <QTimer>

namespace openstitch::desktop_v2::studio {
class WarpTool : public QObject {
    Q_OBJECT
    Q_PROPERTY(double radiusMm READ radiusMm WRITE setRadiusMm NOTIFY optionsChanged)
    Q_PROPERTY(double strength READ strength WRITE setStrength NOTIFY optionsChanged)
public:
    explicit WarpTool(StudioController& studio, QObject* parent = nullptr);
    ~WarpTool() override;
    double radiusMm() const { return options_.radiusMm; }
    double strength() const { return options_.strength; }
    void setRadiusMm(double value);
    void setStrength(double value);
    bool begin(PointMm point);
    void move(PointMm point, bool smooth);
    void end(PointMm point, bool smooth);
    bool cancel();
    bool active() const { return active_; }
    const std::vector<geometry::PathSet>& preview() const { return preview_; }
signals:
    void changed();
    void optionsChanged();
    void message(const QString& text);

private:
    void calculate();
    StudioController& studio_;
    WarpOptions options_;
    WarpOptions strokeOptions_;
    std::shared_ptr<const document::Project> snapshot_;
    ObjectId object_{};
    std::uint64_t revision_{};
    std::uint64_t request_{};
    bool active_{false};
    bool running_{false};
    bool finishing_{false};
    std::vector<WarpTouch> touches_;
    std::vector<geometry::PathSet> preview_;
    QTimer timer_;
    TaskManager tasks_;
};
} // namespace openstitch::desktop_v2::studio
