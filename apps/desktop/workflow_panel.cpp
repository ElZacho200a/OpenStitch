// SPDX-License-Identifier: Apache-2.0
#include "workflow_panel.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>

#include "app_theme.hpp"

namespace openstitch::desktop {

namespace {

const char* step_name(int i) {
    switch (i) {
    case 0: return "Image";
    case 1: return "Régions";
    case 2: return "Vecteurs";
    case 3: return "Broderie";
    case 4: return "Vérification";
    default: return "Export";
    }
}

QString state_word(WorkflowPanel::State s) {
    switch (s) {
    case WorkflowPanel::State::NotStarted: return QObject::tr("à faire");
    case WorkflowPanel::State::Available: return QObject::tr("disponible");
    case WorkflowPanel::State::InProgress: return QObject::tr("en cours");
    case WorkflowPanel::State::Done: return QObject::tr("terminé");
    case WorkflowPanel::State::Attention: return QObject::tr("attention");
    }
    return {};
}

QColor state_color(WorkflowPanel::State s, const Tokens& t) {
    switch (s) {
    case WorkflowPanel::State::NotStarted: return t.border;
    case WorkflowPanel::State::Available: return t.info;
    case WorkflowPanel::State::InProgress: return t.accent;
    case WorkflowPanel::State::Done: return t.success;
    case WorkflowPanel::State::Attention: return t.warning;
    }
    return t.border;
}

QPixmap dot(const QColor& color) {
    QPixmap pm(10, 10);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(0, 0, 10, 10);
    p.end();
    return pm;
}

}  // namespace

WorkflowPanel::WorkflowPanel(QWidget* parent) : QWidget(parent) {
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);

    for (int i = 0; i < kStepCount; ++i) {
        current_[i] = State::NotStarted;
        dots_[i] = new QLabel(this);
        dots_[i]->setPixmap(dot(palette().color(QPalette::Mid)));
        buttons_[i] = new QToolButton(this);
        buttons_[i]->setText(tr(step_name(i)));
        buttons_[i]->setAutoRaise(true);
        buttons_[i]->setToolButtonStyle(Qt::ToolButtonTextOnly);
        buttons_[i]->setCursor(Qt::PointingHandCursor);
        connect(buttons_[i], &QToolButton::clicked, this, [this, i] { emit stepClicked(i); });
        states_[i] = new QLabel(this);
        states_[i]->setEnabled(false);

        grid->addWidget(dots_[i], i, 0);
        grid->addWidget(buttons_[i], i, 1, Qt::AlignLeft);
        grid->addWidget(states_[i], i, 2, Qt::AlignRight);
    }
    grid->setColumnStretch(1, 1);
    applyTheme();
}

void WorkflowPanel::setStates(const std::array<State, kStepCount>& states) {
    current_ = states;
    applyTheme();
}

void WorkflowPanel::applyTheme() {
    const Tokens& t = AppTheme::instance().tokens();
    for (int i = 0; i < kStepCount; ++i) {
        dots_[i]->setPixmap(dot(state_color(current_[i], t)));
        states_[i]->setText(state_word(current_[i]));
    }
}

}  // namespace openstitch::desktop
