// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

namespace openstitch::desktop {

class CanvasView;

// Règle graduée en millimètres, synchronisée avec le zoom et le défilement
// de la vue. La règle verticale affiche le repère physique (Y vers le haut).
class Ruler : public QWidget {
    Q_OBJECT

public:
    Ruler(Qt::Orientation orientation, CanvasView* view, QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Qt::Orientation orientation_;
    CanvasView* view_;
};

}  // namespace openstitch::desktop
