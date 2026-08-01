// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include <optional>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

class QFormLayout;
class QLabel;
class QVBoxLayout;
class QDoubleSpinBox;
class QPushButton;

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
    // Indicateur Clean/ManuallyEdited/Dirty (Lot 8.2) : mis à jour à CHAQUE
    // rafraîchissement, y compris quand la sélection elle-même n'a pas changé
    // (une retouche/undo/redo peut faire changer l'état sans changer la
    // sélection). Volontairement séparé de `showEmbroidery` (qui ne se
    // reconstruit, lui, que sur un vrai changement de sélection) pour ne
    // jamais interrompre une édition de paramètre en cours dans le formulaire.
    // `id` doit correspondre à l'objet actuellement montré par `showEmbroidery`
    // (nullopt si aucune broderie n'est inspectée) : un id différent est
    // ignoré (retard d'affichage évité plutôt qu'un mauvais bouton affiché).
    void setEditState(std::optional<ObjectId> id, stitch_generation::ObjectEditState state);

signals:
    void paramsEdited(ObjectId id, document::StitchParams params);
    // Émis par le bouton « Abandonner les retouches » (état ManuallyEdited ou
    // Dirty) : MainWindow demande confirmation puis exécute
    // DiscardOverridesCommand (annulable), jamais de mutation directe ici.
    void discardOverridesRequested(ObjectId id);

private:
    void clearBody();
    [[nodiscard]] QDoubleSpinBox* mmSpin(double valueMm, double maxMm);

    QVBoxLayout* root_{nullptr};
    QLabel* header_{nullptr};
    QLabel* editStateLabel_{nullptr};
    QPushButton* discardButton_{nullptr};
    QWidget* body_{nullptr};
    std::optional<ObjectId> currentId_;
    std::optional<ObjectId> editStateId_;
    bool building_{false};  // évite d'émettre pendant le peuplement
};

}  // namespace openstitch::desktop
