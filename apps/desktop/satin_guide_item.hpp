// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QGraphicsLineItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPathStroker>

#include <functional>
#include <utility>

namespace openstitch::desktop {

// Segment de guide sélectionnable. La forme de hit-test est élargie en unités
// de scène pour qu'un guide reste manipulable sans comparaison pixel fragile.
class SatinGuideItem final : public QGraphicsLineItem {
public:
    SatinGuideItem(QLineF line, std::function<void()> onSelected)
        : QGraphicsLineItem(line), onSelected_(std::move(onSelected)) {
        setFlag(ItemIsSelectable);
        setCursor(Qt::PointingHandCursor);
    }

    [[nodiscard]] QPainterPath shape() const override {
        QPainterPath path;
        path.moveTo(line().p1());
        path.lineTo(line().p2());
        QPainterPathStroker stroker;
        stroker.setWidth(1.0);  // 1 mm de tolérance de sélection
        return stroker.createStroke(path);
    }

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsLineItem::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && onSelected_) {
            onSelected_();
        }
    }

private:
    std::function<void()> onSelected_;
};

}  // namespace openstitch::desktop
