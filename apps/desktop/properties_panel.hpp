// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include <optional>

#include "openstitch/document/project.hpp"

class QFormLayout;
class QLabel;
class QVBoxLayout;
class QDoubleSpinBox;

namespace openstitch::desktop {

// Inspecteur : affiche et édite les propriétés de l'élément sélectionné. Pour un
// objet de broderie, expose ses paramètres de couture réellement disponibles et
// émet `paramsEdited` à chaque changement — MainWindow applique alors une
// commande (undo exact). Aucun paramètre n'est modifié directement ici : le
// panneau ne détient PAS de vérité métier, il ne fait que présenter et signaler.
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    // Objet de broderie sélectionné (ses paramètres deviennent éditables).
    void showEmbroidery(const document::EmbroideryObject& object);
    // Informations en lecture seule (région, objet vectoriel) ou état vide.
    void showInfo(const QString& title, const QString& details);

signals:
    void paramsEdited(ObjectId id, document::StitchParams params);

private:
    void clearBody();
    [[nodiscard]] QDoubleSpinBox* mmSpin(double valueMm, double maxMm);

    QVBoxLayout* root_{nullptr};
    QLabel* header_{nullptr};
    QWidget* body_{nullptr};
    std::optional<ObjectId> currentId_;
    bool building_{false};  // évite d'émettre pendant le peuplement
};

}  // namespace openstitch::desktop
