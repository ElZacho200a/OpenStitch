// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

#include <QListWidget>
#include <QWidget>

#include "openstitch/core/units.hpp"

class QCheckBox;
class QDoubleSpinBox;
class QDropEvent;

namespace openstitch::desktop {

// HP-SEQ-002 -- liste de l'ordre de couture réordonnable par glisser-déposer. Le widget ne
// modifie JAMAIS ses lignes : un dépôt émet `moveRequested(from, to)` (indices de lignes, `to` =
// ligne finale) et la fenêtre principale exécute une `ICommand` puis reconstruit la liste.
class OrderListWidget final : public QListWidget {
    Q_OBJECT
public:
    explicit OrderListWidget(QWidget* parent = nullptr);

signals:
    void moveRequested(int from, int to);

protected:
    void dropEvent(QDropEvent* event) override;
};

// HP-SEQ-008 / HP-SEQ-001 -- réglages de l'optimisation de l'ordre : point de départ et
// d'arrivée du design (mm, repère document) et amélioration locale 2-opt / Or-opt.
class OrderOptionsWidget final : public QWidget {
    Q_OBJECT
public:
    explicit OrderOptionsWidget(QWidget* parent = nullptr);

    // Affiche les valeurs du projet sans émettre `endpointsChanged`.
    void setEndpoints(const std::optional<Vec2um>& start, const std::optional<Vec2um>& end);
    [[nodiscard]] std::optional<Vec2um> start() const;
    [[nodiscard]] std::optional<Vec2um> end() const;
    [[nodiscard]] bool improveEnabled() const;

signals:
    // L'appelant relit `start()` / `end()`.
    void endpointsChanged();

private:
    void emitChanged();

    QCheckBox* startCheck_{nullptr};
    QDoubleSpinBox* startX_{nullptr};
    QDoubleSpinBox* startY_{nullptr};
    QCheckBox* endCheck_{nullptr};
    QDoubleSpinBox* endX_{nullptr};
    QDoubleSpinBox* endY_{nullptr};
    QCheckBox* improveCheck_{nullptr};
};

} // namespace openstitch::desktop
