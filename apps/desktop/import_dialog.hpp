// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>
#include <QImage>
#include <QSizeF>

#include <optional>

#include "openstitch/document/image_placement.hpp"

class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;

namespace openstitch::desktop {

// Dialogue d'import : l'utilisateur fixe la taille physique de l'image. Affiche
// un aperçu, la résolution (mm/pixel et dpi) résultante, une alerte si l'image dépasse
// le cadre et si ses proportions sont modifiées. La taille proposée d'emblée tient dans le
// cadre ; « Ajuster au cadre » y ramène l'image en un clic. Le calcul du placement est délégué
// à libs/document (aucune logique métier ici, seulement le couplage largeur/hauteur et
// l'affichage).
class ImportDialog : public QDialog {
    Q_OBJECT

public:
    ImportDialog(int widthPx, int heightPx, const QImage& preview, QSizeF hoopMm,
                 QWidget* parent = nullptr);

    // Placement choisi, ou nullopt si le dialogue a été annulé.
    [[nodiscard]] std::optional<document::ImagePlacement> placement() const;

private:
    void syncFromWidth();
    void syncFromHeight();
    void recompute(); // met à jour mm/pixel, dpi et les alertes (cadre, proportions)
    void fitToHoop(); // plus grande taille, ratio conservé, qui tient dans le cadre
    // Bornes des deux champs : avec « conserver les proportions », la largeur est bornée pour que
    // la hauteur déduite reste dans [1, 1000] mm (et réciproquement) au lieu d'être écrêtée.
    void applyRatioRanges();

    int widthPx_;
    int heightPx_;
    QSizeF hoopMm_;
    QDoubleSpinBox* widthMm_{nullptr};
    QDoubleSpinBox* heightMm_{nullptr};
    QCheckBox* keepRatio_{nullptr};
    QPushButton* fitButton_{nullptr};
    QLabel* resolutionLabel_{nullptr};
    QLabel* warningLabel_{nullptr};
    QLabel* ratioLabel_{nullptr};
    bool syncing_{false};
};

} // namespace openstitch::desktop
