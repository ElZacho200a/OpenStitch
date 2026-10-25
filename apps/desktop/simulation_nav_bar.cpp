// SPDX-License-Identifier: Apache-2.0
#include "simulation_nav_bar.hpp"

#include <QAction>
#include <QLabel>

#include <cmath>

namespace openstitch::desktop {

namespace {
QString formatMinutes(double minutes) {
    const int totalSeconds = static_cast<int>(std::lround(minutes * 60.0));
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

SimulationNavBar::SimulationNavBar(QWidget* parent)
    : QToolBar(tr("Navigation simulation"), parent) {
    setObjectName(QStringLiteral("simNavBar"));
    setMovable(false);

    prevObjectAct_ = addAction(tr("⏮ Objet"));
    prevObjectAct_->setObjectName(QStringLiteral("action_simPrevObject"));
    prevObjectAct_->setToolTip(tr("Saute au début de l'objet courant, puis du précédent."));
    connect(prevObjectAct_, &QAction::triggered, this,
            [this] { request(stitch_render::SimulationUnit::Object, -1); });

    stepBackAct_ = addAction(tr("◀ Point"));
    stepBackAct_->setObjectName(QStringLiteral("action_simStepBack"));
    stepBackAct_->setToolTip(tr("Recule d'un point."));
    connect(stepBackAct_, &QAction::triggered, this,
            [this] { request(stitch_render::SimulationUnit::Stitch, -1); });

    stepForwardAct_ = addAction(tr("Point ▶"));
    stepForwardAct_->setObjectName(QStringLiteral("action_simStepForward"));
    stepForwardAct_->setToolTip(tr("Avance d'un point."));
    connect(stepForwardAct_, &QAction::triggered, this,
            [this] { request(stitch_render::SimulationUnit::Stitch, +1); });

    nextObjectAct_ = addAction(tr("Objet ⏭"));
    nextObjectAct_->setObjectName(QStringLiteral("action_simNextObject"));
    nextObjectAct_->setToolTip(tr("Saute au début de l'objet suivant."));
    connect(nextObjectAct_, &QAction::triggered, this,
            [this] { request(stitch_render::SimulationUnit::Object, +1); });

    timeLabel_ = new QLabel(this);
    timeLabel_->setObjectName(QStringLiteral("simTimeLabel"));
    timeLabel_->setAccessibleName(tr("Temps de couture écoulé et total"));
    timeLabel_->setToolTip(tr("Temps de couture estimé (700 points/min, 30 s par changement de "
                              "fil, 2 s par coupe)."));
    addWidget(timeLabel_);
    setSequence(nullptr);
}

void SimulationNavBar::setSequence(const stitch::StitchSequence* sequence) {
    sequence_ = sequence;
    index_ = sequence_ != nullptr ? stitch_render::build_simulation_index(*sequence_)
                                  : stitch_render::SimulationIndex{};
    const bool enabled = sequence_ != nullptr && !sequence_->commands.empty();
    for (QAction* a : {stepBackAct_, stepForwardAct_, prevObjectAct_, nextObjectAct_}) {
        a->setEnabled(enabled);
    }
    step_ = -1;
    refreshTime();
}

void SimulationNavBar::setStep(int step) {
    step_ = step;
    refreshTime();
}

QString SimulationNavBar::timeText() const {
    return timeLabel_->text();
}

void SimulationNavBar::refreshTime() {
    if (sequence_ == nullptr || sequence_->commands.empty()) {
        timeLabel_->setText(tr("— / —"));
        return;
    }
    const int last = static_cast<int>(sequence_->commands.size()) - 1;
    const double total = stitch_render::simulation_elapsed_minutes(*sequence_, last, timing_);
    const double elapsed =
        stitch_render::simulation_elapsed_minutes(*sequence_, step_ < 0 ? last : step_, timing_);
    timeLabel_->setText(tr("%1 / %2").arg(formatMinutes(elapsed), formatMinutes(total)));
}

void SimulationNavBar::request(stitch_render::SimulationUnit unit, int direction) {
    if (sequence_ == nullptr || sequence_->commands.empty()) {
        return;
    }
    const int last = static_cast<int>(sequence_->commands.size()) - 1;
    const int current = step_ < 0 ? last : step_;
    emit stepRequested(stitch_render::simulation_step_target(index_, sequence_->commands.size(),
                                                             current, unit, direction));
}

} // namespace openstitch::desktop
