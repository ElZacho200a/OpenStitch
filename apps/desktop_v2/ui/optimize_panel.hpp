// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include "path_optimizer.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace openstitch::desktop_v2 {

// Panneau « Optimisation » : l'utilisateur choisit une stratégie, VOIT les mesures avant / après
// (changements de fil, sauts, coupes, déplacements, points) puis accepte ou refuse. Rien n'est
// modifié avant « Accepter » ; une proposition devenue périmée (document modifié) ne peut plus être
// acceptée. Aucune logique métier : le calcul vit dans `core/path_optimizer`.
class OptimizePanel final : public QWidget {
    Q_OBJECT

public:
    explicit OptimizePanel(QWidget* parent = nullptr);

    [[nodiscard]] PathStrategy strategy() const;
    [[nodiscard]] bool improve() const;

    void setBusy(bool busy);
    void setEnabledForDocument(bool hasEnoughObjects);
    void showProposal(const PathProposal& proposal);
    void showMessage(const QString& message);
    void setStale(bool stale);
    void clearProposal();

    [[nodiscard]] bool hasProposal() const { return hasProposal_; }
    [[nodiscard]] bool canAccept() const;
    [[nodiscard]] int metricCount() const;
    [[nodiscard]] QString metricText(int row) const;
    [[nodiscard]] QString verdict() const;
    void chooseStrategy(PathStrategy strategy);

signals:
    void proposeRequested(PathStrategy strategy, bool improve);
    void acceptRequested();
    void refuseRequested();

private:
    void updateButtons();

    QComboBox* strategy_{nullptr};
    QCheckBox* improve_{nullptr};
    QPushButton* propose_{nullptr};
    QPushButton* accept_{nullptr};
    QPushButton* refuse_{nullptr};
    QLabel* verdict_{nullptr};
    QListWidget* metrics_{nullptr};
    bool hasProposal_{false};
    bool changes_{false};
    bool stale_{false};
    bool busy_{false};
    bool enabled_{false};
};

} // namespace openstitch::desktop_v2
