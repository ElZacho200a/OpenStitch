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

namespace openstitch::desktop {

// Dialogue d'import : l'utilisateur fixe la taille physique de l'image. Affiche
// un aperçu, la résolution mm/pixel résultante et une alerte si l'image dépasse
// le cadre. Le calcul du placement est délégué à libs/document (aucune logique
// métier ici, seulement le couplage largeur/hauteur et l'affichage).
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
    void recompute();  // met à jour mm/pixel et l'alerte de dépassement

    int widthPx_;
    int heightPx_;
    QSizeF hoopMm_;
    QDoubleSpinBox* widthMm_{nullptr};
    QDoubleSpinBox* heightMm_{nullptr};
    QCheckBox* keepRatio_{nullptr};
    QLabel* resolutionLabel_{nullptr};
    QLabel* warningLabel_{nullptr};
    bool syncing_{false};
};

}  // namespace openstitch::desktop
