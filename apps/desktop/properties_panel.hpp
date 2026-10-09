// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QPointer>
#include <QWidget>

#include <optional>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

class QCheckBox;
class QFormLayout;
class QLabel;
class QListWidget;
class QSpinBox;
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
    // Régions sélectionnées (segmentation) : résumé, pastille de couleur de la région active
    // (un clic ouvre le sélecteur de couleur) et boutons d'édition. `canMerge` : au moins deux
    // régions ; `canAbsorb` : exactement une région.
    struct RegionSelectionInfo {
        QString title;
        QString summary;
        QColor activeColor;
        bool canMerge{false};
        bool canAbsorb{false};
    };
    void showRegions(const RegionSelectionInfo& info);
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
    // Auto-satin (spec specs/plans/satin-squelette-traversees.md) : met à jour la
    // liste des guides et le résumé de diagnostic de l'objet inspecté. Comme
    // `setEditState`, appelé à CHAQUE rafraîchissement sans reconstruire le
    // formulaire (les guides se posent aussi depuis le canevas). `id` doit
    // correspondre à l'objet montré ; sinon ignoré.
    void setAutoSatinState(std::optional<ObjectId> id, const document::AutoSatinParams* params,
                           const QString& summary);

signals:
    // Bouton de l'inspecteur des régions : `actionName` = objectName de la QAction de MainWindow à
    // déclencher (une seule source d'état, de raccourci et de grisage).
    void regionActionRequested(const QString& actionName);
    void paramsEdited(ObjectId id, document::StitchParams params);
    // Émis par le bouton « Abandonner les retouches » (état ManuallyEdited ou
    // Dirty) : MainWindow demande confirmation puis exécute
    // DiscardOverridesCommand (annulable), jamais de mutation directe ici.
    void discardOverridesRequested(ObjectId id);
    // Bouton « Convertir en remplissage directionnel » d'un tatami : MainWindow
    // construit les paramètres équivalents (cœur) et exécute la commande.
    void convertToDirectionalRequested(ObjectId id);
    // Bouton « Éditer les guides » d'un remplissage directionnel : active
    // l'outil de guides du canevas sur cet objet.
    void editDirectionGuidesRequested(ObjectId id);
    // Auto-satin : bouton « Placer un guide » (active l'outil de guides du canevas),
    // modification de l'angle d'un guide et suppression d'un guide. MainWindow
    // construit la commande annulable à partir des guides ACTUELS du document.
    void editSatinGuidesRequested(ObjectId id);
    void satinGuideChangeRequested(ObjectId id, int index, double angleDeg, bool absolute);
    void satinGuideRemoveRequested(ObjectId id, int index);

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
    bool building_{false}; // évite d'émettre pendant le peuplement
    // Auto-satin : widgets mis à jour hors reconstruction (cf. setAutoSatinState).
    QPointer<QListWidget> satinGuideList_;
    QPointer<QLabel> satinSummary_;
    QPointer<QSpinBox> satinGuideAngle_;
    QPointer<QCheckBox> satinGuideAbsolute_;
    QPointer<QPushButton> satinGuideRemove_;
};

} // namespace openstitch::desktop
