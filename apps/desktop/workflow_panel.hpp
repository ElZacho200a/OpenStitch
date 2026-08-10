// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include <array>

class QLabel;
class QToolButton;

namespace openstitch::desktop {

// Indicateur d'étapes du pipeline (repère pédagogique, non contraignant). L'état
// de chaque étape est DÉDUIT du document par MainWindow ; ce widget ne fait que
// l'afficher (pastille + libellé + mot d'état — jamais la couleur seule) et
// signaler un clic pour mettre en avant les outils correspondants.
class WorkflowPanel : public QWidget {
    Q_OBJECT

public:
    static constexpr int kStepCount = 6;  // Image, Régions, Vecteurs, Broderie, Vérif, Export
    enum class State { NotStarted, Available, InProgress, Done, Attention };

    explicit WorkflowPanel(QWidget* parent = nullptr);

    void setStates(const std::array<State, kStepCount>& states);
    [[nodiscard]] State currentState(int index) const { return current_[index]; }
    void applyTheme();  // recolore les pastilles au changement de thème

signals:
    void stepClicked(int index);

private:
    std::array<QToolButton*, kStepCount> buttons_{};
    std::array<QLabel*, kStepCount> dots_{};
    std::array<QLabel*, kStepCount> states_{};
    std::array<State, kStepCount> current_{};
};

}  // namespace openstitch::desktop
