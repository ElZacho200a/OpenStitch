// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QToolBar>

#include "openstitch/stitch_render/simulation.hpp"

class QAction;
class QLabel;

namespace openstitch::desktop {

// HP-SIM-002/004 : barre de navigation de la simulation (pas à pas, objet précédent /
// suivant) et temps de couture écoulé / total. Aucune logique métier : les calculs sont
// ceux de stitch_render/simulation.hpp ; la barre émet `stepRequested` et c'est la fenêtre
// qui déplace le curseur de simulation.
class SimulationNavBar : public QToolBar {
    Q_OBJECT
public:
    explicit SimulationNavBar(QWidget* parent = nullptr);

    // La séquence doit rester valide tant qu'elle est installée (nullptr : barre grisée).
    void setSequence(const stitch::StitchSequence* sequence);
    // `step` < 0 : couture complète affichée (pas de simulation en cours).
    void setStep(int step);

    [[nodiscard]] QString timeText() const;

signals:
    void stepRequested(int step);

private:
    void request(stitch_render::SimulationUnit unit, int direction);
    void refreshTime();

    const stitch::StitchSequence* sequence_{nullptr};
    stitch_render::SimulationIndex index_;
    stitch_render::SimulationTiming timing_;
    int step_{-1};
    QAction* stepBackAct_{nullptr};
    QAction* stepForwardAct_{nullptr};
    QAction* prevObjectAct_{nullptr};
    QAction* nextObjectAct_{nullptr};
    QLabel* timeLabel_{nullptr};
};

} // namespace openstitch::desktop
