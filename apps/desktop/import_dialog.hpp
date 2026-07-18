// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDialog>

#include <optional>

#include "openstitch/document/image_placement.hpp"

class QDoubleSpinBox;
class QCheckBox;

namespace openstitch::desktop {

// Dialogue d'import : l'utilisateur fixe la taille physique de l'image.
// Le calcul du placement est délégué à libs/document (aucune logique ici,
// seulement le couplage largeur/hauteur quand « conserver le ratio » est coché).
class ImportDialog : public QDialog {
    Q_OBJECT

public:
    ImportDialog(int widthPx, int heightPx, QWidget* parent = nullptr);

    // Placement choisi, ou nullopt si le dialogue a été annulé.
    [[nodiscard]] std::optional<document::ImagePlacement> placement() const;

private:
    void syncFromWidth();
    void syncFromHeight();

    int widthPx_;
    int heightPx_;
    QDoubleSpinBox* widthMm_{nullptr};
    QDoubleSpinBox* heightMm_{nullptr};
    QCheckBox* keepRatio_{nullptr};
    bool syncing_{false};
};

}  // namespace openstitch::desktop
