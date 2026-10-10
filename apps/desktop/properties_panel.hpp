// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QPointer>
#include <QRectF>
#include <QWidget>

#include <optional>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

#include "wheel_guard.hpp"

class QCheckBox;
class QComboBox;
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
    // Objet vectoriel : informations + position (X, Y du coin bas-gauche) et taille (L, H)
    // en mm, dans le repère du document (Y vers le haut, comme l'indicateur de curseur).
    void showVectorObject(ObjectId id, const QString& title, const QString& details, QRectF boxMm);
    // Vrai si le formulaire montre déjà cette boîte (tolérance 0,02 mm) : sinon MainWindow
    // le reconstruit (annulation, déplacement au canevas...).
    [[nodiscard]] bool showsVectorBox(ObjectId id, QRectF boxMm) const;
    // Multi-sélection : bloc « Appliquer à N objets » (type de points, espacement, angle).
    void showMultiSelection(int objectCount, int embroideryCount);
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
    // HP-ENG-010 : resynchronise le choix « Entrée/sortie » après une annulation/un
    // rétablissement (le mode n'est pas dans `StitchParams`). `id` doit correspondre à
    // l'objet montré ; sinon ignoré. Sans reconstruction ni émission.
    void setJoinMode(std::optional<ObjectId> id, document::JoinMode mode);

    // Lettrage : bandeau « Texte « … » » + bouton « Modifier le texte… » en tête de l'inspecteur
    // quand la sélection est une lettre d'un texte (`summary` vide = masqué). Persistant comme
    // l'indicateur d'état : mis à jour à chaque rafraîchissement, sans reconstruire le formulaire.
    void setTextInfo(const QString& summary);
    [[nodiscard]] bool textInfoVisible() const;

    // Paramètres que le formulaire représente actuellement. MainWindow compare
    // `showsParams` au document à chaque rafraîchissement : un écart (annulation,
    // changement de type de points, rotation au canevas...) reconstruit le
    // formulaire, au lieu de laisser un formulaire périmé écraser le document.
    [[nodiscard]] bool showsParams(const document::StitchParams& params) const;
    // Resynchronise la copie interne SANS reconstruire (après qu'une édition du
    // formulaire a été appliquée au document, qui peut l'avoir complétée).
    void adoptParams(ObjectId id, const document::StitchParams& params);

signals:
    // Bouton « Modifier le texte… » du bandeau lettrage (cf. setTextInfo) : MainWindow rouvre
    // le dialogue de texte (même chemin que le double-clic et F2).
    void editTextRequested();
    // Bouton de l'inspecteur des régions : `actionName` = objectName de la QAction de MainWindow à
    // déclencher (une seule source d'état, de raccourci et de grisage).
    void regionActionRequested(const QString& actionName);
    // Nouvelle boîte demandée pour un objet vectoriel (X, Y, L, H en mm, Y vers le haut).
    void vectorBoxEdited(ObjectId id, QRectF boxMm);
    // Bouton « Appliquer à N objets » : `stitchType` -1 = inchangé, 0 = contour cousu,
    // 1 = tatami ; espacement/angle seulement si leur case est cochée.
    void applyToSelectionRequested(int stitchType, bool setSpacing, double spacingMm, bool setAngle,
                                   double angleDeg);
    // `field` : libellé du champ modifié (« Espacement des rangées »), repris dans le
    // nom d'historique et utilisé pour fusionner une rafale en un seul pas d'annulation.
    // Seul ce champ diffère de la copie courante : aucun autre n'est relu ni arrondi.
    void paramsEdited(ObjectId id, document::StitchParams params, QString field);
    // Clic sur un guide d'orientation de la liste : MainWindow le met en évidence.
    void satinGuideSelected(ObjectId id, int index);
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
    // HP-ENG-010 : mode d'entrée/sortie automatiques d'un objet (0 = hérite du projet,
    // 1 = automatique, 2 = désactivé) ; MainWindow exécute SetEmbroideryJoinModeCommand.
    void joinModeEdited(ObjectId id, int mode);
    // HP-STI-004 : bordure satin. `createBorderSatinRequested` : depuis un objet vectoriel,
    // largeur (mm), côté (0 centré, 1 intérieur, 2 extérieur), coins (0 vifs, 1 arrondis).
    // `borderSatinEdited` : changement de ces réglages d'un satin de bordure existant
    // (MainWindow régénère les rails depuis le contour source, commande annulable).
    void createBorderSatinRequested(ObjectId vectorId, double widthMm, int side, int corner);
    void borderSatinEdited(ObjectId id, double widthMm, int side, int corner);

private:
    void clearBody();
    void updateGuideAngleLabel(bool absolute);
    // Champ en mm borné [minMm ; maxMm] ; l'infobulle porte la plage (« Plage : … »).
    [[nodiscard]] QDoubleSpinBox* mmSpin(double valueMm, double maxMm, double minMm = 0.0,
                                         const QString& tip = {});

    QVBoxLayout* root_{nullptr};
    QLabel* header_{nullptr};
    QLabel* editStateLabel_{nullptr};
    QPushButton* discardButton_{nullptr};
    QLabel* textInfoLabel_{nullptr};
    QPushButton* textEditButton_{nullptr};
    QWidget* body_{nullptr};
    std::optional<ObjectId> currentId_;
    std::optional<ObjectId> editStateId_;
    // Copie des paramètres représentés par le formulaire (cf. showsParams).
    document::StitchParams shown_{document::RunningStitchParams{}};
    bool hasShown_{false};
    std::optional<ObjectId> vectorId_;
    QRectF vectorBox_;
    WheelGuard* wheelGuard_{nullptr};
    bool building_{false}; // évite d'émettre pendant le peuplement
    // Auto-satin : widgets mis à jour hors reconstruction (cf. setAutoSatinState).
    QPointer<QListWidget> satinGuideList_;
    QPointer<QLabel> satinSummary_;
    QPointer<QDoubleSpinBox> satinGuideAngle_;
    QPointer<QLabel> satinGuideAngleLabel_;
    QPointer<QCheckBox> satinGuideAbsolute_;
    QPointer<QPushButton> satinGuideRemove_;
    QPointer<QComboBox> joinCombo_;
};

} // namespace openstitch::desktop
