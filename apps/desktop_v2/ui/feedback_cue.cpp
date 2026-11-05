// SPDX-License-Identifier: Apache-2.0
#include "feedback_cue.hpp"

#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QTimer>

namespace openstitch::desktop_v2 {

FeedbackCue::FeedbackCue(QWidget* parent)
    : QLabel(parent), opacity_(new QGraphicsOpacityEffect(this)),
      fade_(new QPropertyAnimation(opacity_, "opacity", this)), clear_(new QTimer(this)) {
    setObjectName(QStringLiteral("feedbackCue"));
    setAccessibleName(tr("Confirmation de modification"));
    setToolTip(tr("La dernière action a été appliquée au document."));
    setMinimumWidth(16);
    setAlignment(Qt::AlignCenter);
    setText(QString());
    opacity_->setOpacity(0.0);
    setGraphicsEffect(opacity_);

    fade_->setDuration(140);
    fade_->setEasingCurve(QEasingCurve::OutCubic);
    clear_->setSingleShot(true);
    clear_->setInterval(1000);
    connect(clear_, &QTimer::timeout, this, [this] {
        fade_->stop();
        opacity_->setOpacity(0.0);
        setText(QString());
    });
}

void FeedbackCue::pulse() {
    // A rapid sequence of edits should not create a distracting flashing status line.
    if (lastPulse_.isValid() && lastPulse_.elapsed() < 180) {
        return;
    }
    lastPulse_.restart();
    clear_->stop();
    fade_->stop();
    setText(QStringLiteral("✓"));
    if (reducedMotion_) {
        opacity_->setOpacity(1.0);
    } else {
        fade_->setStartValue(0.20);
        fade_->setEndValue(1.0);
        fade_->start();
    }
    clear_->start();
}

void FeedbackCue::setReducedMotion(bool reduced) {
    reducedMotion_ = reduced;
    if (reducedMotion_ && fade_->state() == QAbstractAnimation::Running) {
        fade_->stop();
        opacity_->setOpacity(1.0);
    }
}

bool FeedbackCue::animating() const {
    return fade_->state() == QAbstractAnimation::Running;
}

} // namespace openstitch::desktop_v2
