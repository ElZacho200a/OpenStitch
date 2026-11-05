// SPDX-License-Identifier: Apache-2.0
#pragma once

// A tiny, non-blocking confirmation of an *already committed* action.
// Never influences project state, selection, preview rendering or the undo stack.
#include <QElapsedTimer>
#include <QLabel>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;

namespace openstitch::desktop_v2 {

class FeedbackCue : public QLabel {
    Q_OBJECT
public:
    explicit FeedbackCue(QWidget* parent = nullptr);

    void pulse();
    void setReducedMotion(bool reduced);
    [[nodiscard]] bool reducedMotion() const { return reducedMotion_; }
    [[nodiscard]] bool animating() const;

private:
    QGraphicsOpacityEffect* opacity_;
    QPropertyAnimation* fade_;
    QTimer* clear_;
    QElapsedTimer lastPulse_;
    bool reducedMotion_{false};
};

} // namespace openstitch::desktop_v2
