// SPDX-License-Identifier: Apache-2.0
#include "workflow_panel.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace openstitch::desktop {

namespace {

const char* step_name(int i) {
    switch (i) {
    case 0:
        return "Source";
    case 1:
        return "Régions";
    case 2:
        return "Vecteurs";
    case 3:
        return "Broderie";
    case 4:
        return "Vérification";
    default:
        return "Export";
    }
}

QString state_word(WorkflowPanel::State s) {
    switch (s) {
    case WorkflowPanel::State::NotStarted:
        return QObject::tr("à faire");
    case WorkflowPanel::State::Available:
        return QObject::tr("disponible");
    case WorkflowPanel::State::InProgress:
        return QObject::tr("en cours");
    case WorkflowPanel::State::Done:
        return QObject::tr("terminé");
    case WorkflowPanel::State::Attention:
        return QObject::tr("attention");
    case WorkflowPanel::State::Skipped:
        return QObject::tr("non requis");
    }
    return {};
}

const char* state_key(WorkflowPanel::State s) {
    switch (s) {
    case WorkflowPanel::State::NotStarted:
        return "todo";
    case WorkflowPanel::State::Available:
        return "available";
    case WorkflowPanel::State::InProgress:
        return "progress";
    case WorkflowPanel::State::Done:
        return "done";
    case WorkflowPanel::State::Attention:
        return "attention";
    case WorkflowPanel::State::Skipped:
        return "todo"; // état neutre : étape volontairement sautée
    }
    return "todo";
}

// Applique une propriété dynamique puis force la réévaluation de la feuille de style.
void set_state_property(QWidget* w, const char* key) {
    w->setProperty("state", QString::fromLatin1(key));
    w->style()->unpolish(w);
    w->style()->polish(w);
}

} // namespace

WorkflowPanel::WorkflowPanel(QWidget* parent) : QWidget(parent) {
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(10, 10, 10, 10);
    column->setSpacing(6);

    // Une carte par étape : badge numéroté (✓ quand terminé) · nom cliquable · mot d'état.
    for (int i = 0; i < kStepCount; ++i) {
        current_[i] = State::NotStarted;
        auto* card = new QFrame(this);
        card->setProperty("card", true);
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(8, 4, 8, 4);
        row->setSpacing(8);

        dots_[i] = new QLabel(QString::number(i + 1), card);
        dots_[i]->setProperty("badge", true);
        dots_[i]->setAlignment(Qt::AlignCenter);
        buttons_[i] = new QToolButton(card);
        buttons_[i]->setText(tr(step_name(i)));
        buttons_[i]->setToolButtonStyle(Qt::ToolButtonTextOnly);
        buttons_[i]->setCursor(Qt::PointingHandCursor);
        connect(buttons_[i], &QToolButton::clicked, this, [this, i] { emit stepClicked(i); });
        states_[i] = new QLabel(card);
        states_[i]->setProperty("pill", true);

        row->addWidget(dots_[i]);
        row->addWidget(buttons_[i], 1, Qt::AlignLeft);
        row->addWidget(states_[i]);
        column->addWidget(card);
    }
    column->addStretch(1);
    applyTheme();
}

void WorkflowPanel::setStates(const std::array<State, kStepCount>& states) {
    current_ = states;
    applyTheme();
}

void WorkflowPanel::applyTheme() {
    // Les couleurs viennent de la feuille de style (propriété « state ») : rien à peindre ici.
    for (int i = 0; i < kStepCount; ++i) {
        const char* key = state_key(current_[i]);
        if (current_[i] == State::Done) {
            dots_[i]->setText(QStringLiteral("✓"));
        } else if (current_[i] == State::Skipped) {
            dots_[i]->setText(QStringLiteral("–"));
        } else {
            dots_[i]->setText(QString::number(i + 1));
        }
        states_[i]->setText(state_word(current_[i]));
        buttons_[i]->setToolTip(current_[i] == State::Skipped
                                    ? tr("Cette étape n’est pas nécessaire pour cette source.")
                                    : QString());
        set_state_property(dots_[i], key);
        set_state_property(states_[i], key);
    }
}

} // namespace openstitch::desktop
