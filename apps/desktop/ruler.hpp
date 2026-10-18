// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QFont>
#include <QWidget>

namespace openstitch::desktop {

class CanvasView;

// Règle graduée en millimètres, synchronisée avec le zoom et le défilement
// de la vue. La règle verticale affiche le repère physique (Y vers le haut).
// HP-UX-043 : police, épaisseur et boîtes d'étiquettes dérivent de la police de l'application
// (taille de police agrandie, DPI élevé) au lieu de valeurs fixes en pixels.
class Ruler : public QWidget {
    Q_OBJECT

public:
    Ruler(Qt::Orientation orientation, CanvasView* view, QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    // Police des graduations (un peu plus petite que celle de l'interface) et épaisseur de la
    // règle.
    [[nodiscard]] QFont rulerFont() const;
    [[nodiscard]] int thickness() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    Qt::Orientation orientation_;
    CanvasView* view_;
};

} // namespace openstitch::desktop
