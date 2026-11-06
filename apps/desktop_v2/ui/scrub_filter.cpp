// SPDX-License-Identifier: Apache-2.0
#include "scrub_filter.hpp"

#include <QEvent>
#include <QMouseEvent>
#include <QWidget>
#include <algorithm>

#include "scrub.hpp"

namespace openstitch::desktop_v2 {

ScrubFilter::ScrubFilter(QWidget* label, Field field) : QObject(label), field_(std::move(field)) {
    label->installEventFilter(this);
    label->setCursor(Qt::SizeHorCursor);
    label->setToolTip(tr("Glisser horizontalement pour changer la valeur "
                         "(Maj : ×10, Ctrl : fin). Le champ reste modifiable au clavier."));
}

bool ScrubFilter::eventFilter(QObject* watched, QEvent* event) {
    (void)watched;
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto* e = static_cast<QMouseEvent*>(event);
        if (e->button() == Qt::LeftButton) {
            dragging_ = true;
            pressX_ = e->position().x();
            // Valeur « mixte » (minimum spécial) : on part du plus petit réglage réel.
            startValue_ = std::max(field_.get(), field_.min);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        if (!dragging_) {
            break;
        }
        const auto* e = static_cast<QMouseEvent*>(event);
        const auto speed = (e->modifiers() & Qt::ShiftModifier)     ? ScrubSpeed::Coarse
                           : (e->modifiers() & Qt::ControlModifier) ? ScrubSpeed::Fine
                                                                    : ScrubSpeed::Normal;
        const double v = scrubbedValue(startValue_, e->position().x() - pressX_, field_.step,
                                       field_.min, field_.max, speed);
        if (v != field_.get()) {
            field_.set(v);
        }
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (dragging_) {
            dragging_ = false;
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

} // namespace openstitch::desktop_v2
