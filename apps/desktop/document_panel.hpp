// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QPoint>
#include <QWidget>

#include <utility>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

class QAction;
class QComboBox;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace openstitch::desktop {

class CalquePanel;

// Vue structurée du document : onglets Objets (broderie) et Régions
// (segmentation). Sélectionner une ligne émet un signal ; MainWindow route la
// sélection (source de vérité) et rappelle `syncSelection` pour refléter en
// retour la sélection faite au canevas — synchronisation bidirectionnelle.
// Aucune vérité métier ici : simple présentation + signaux.
class DocumentPanel : public QWidget {
    Q_OBJECT

public:
    enum class Kind { None, Region, Vector, Embroidery };

    explicit DocumentPanel(QWidget* parent = nullptr);

    // Pile des calques (HP-OBJ-024), affichée au-dessus des onglets ; MainWindow s'y connecte.
    [[nodiscard]] CalquePanel* calquePanel() const { return calquePanel_; }

    // Reconstruit les listes depuis le document. `editStates` (Lot 8.2) :
    // état Clean/ManuallyEdited/Dirty des objets retouchés (absents = Clean,
    // cf. `stitch_generation::classify_all_edit_states`) — affiché en suffixe
    // + infobulle sur la ligne correspondante. Vide par défaut (rétrocompatible
    // avec les appels existants qui ne connaissent pas cet état).
    void refresh(const document::Project& project,
                 const std::vector<std::pair<ObjectId, stitch_generation::ObjectEditState>>&
                     editStates = {});
    // Sélectionne la ligne correspondant à la sélection courante (sans réémettre).
    void syncSelection(Kind kind, std::uint64_t id);
    // Sélection multiple de régions : coche toutes les lignes de `ids` (l'active devient la
    // ligne courante), sans réémettre de signal.
    void syncRegions(const std::vector<std::uint64_t>& ids, std::uint64_t active);
    // Sélection multiple d'objets vectoriels (Ctrl/Maj + clic au canevas ou dans la liste) : coche
    // les lignes de broderie dont la forme source est dans `vectorIds`, et les lignes de formes non
    // cousues. Sans effet si la liste reflète déjà exactement cet ensemble (conserve l'ancre de
    // sélection de l'utilisateur) ; ne réémet aucun signal.
    void syncObjects(const std::vector<std::uint64_t>& vectorIds);

    // Menu contextuel de la ligne sous `viewportPos` (coordonnées du viewport de la liste Objets) :
    // sélectionne la ligne si elle ne l'est pas, puis propose Masquer/Afficher, Figer/Libérer
    // l'ordre et Supprimer. Appartient à l'appelant (nullptr hors ligne). Les actions ne font
    // qu'émettre les signaux `...Requested` : MainWindow exécute les commandes annulables.
    [[nodiscard]] QMenu* createContextMenu(const QPoint& viewportPos);

    // Dépôt des lignes sélectionnées à la position `viewportPos` du viewport de la liste Objets
    // (appelé par le glisser-déposer ; public pour les tests headless, qui ne peuvent pas piloter
    // une vraie session de glissement). Ne déplace aucune ligne : émet `orderChangeRequested`.
    void handleDrop(const QPoint& viewportPos);

    // Ordre de couture obtenu en déposant les objets `moved` avant (ou après) `target` ; `target`
    // invalide = à la fin. Les objets déplacés gardent leur ordre relatif. Fonction pure.
    [[nodiscard]] static std::vector<ObjectId>
    reorderedAfterDrop(const std::vector<ObjectId>& order, const std::vector<ObjectId>& moved,
                       ObjectId target, bool after);

    // Libellé d'un objet de broderie, partagé avec le panneau Ordre de couture : « 3. Satin —
    // Région 12  (masqué)  [ordre figé] ». `order` = rang de couture (à partir de 1).
    [[nodiscard]] static QString itemText(const document::EmbroideryObject& object, int order,
                                          const QString& suffix = {});

signals:
    void embroiderySelected(ObjectId id);
    void regionSelected(RegionId id);
    // Plusieurs régions cochées dans la liste (Ctrl/Maj + clic) : `ids` dans l'ordre de la
    // liste, `active` = la ligne courante (la dernière cliquée).
    void regionsSelected(const std::vector<std::uint64_t>& ids, std::uint64_t active);
    // Clic sur le nœud d'un groupe de sections (même forme source) : toutes sont visées.
    void groupSelected(ObjectId sourceVector, int sectionCount);
    // Une forme vectorielle non cousue (ligne indépendante) est sélectionnée.
    void vectorSelected(ObjectId id);
    // Plusieurs lignes (Ctrl/Maj + clic) : identifiants des objets de broderie et des formes non
    // cousues, dans l'ordre de la liste (les sections d'un groupe sélectionné y figurent).
    void objectsSelected(const std::vector<ObjectId>& embroidery,
                         const std::vector<ObjectId>& vectors);
    // Plus aucune ligne sélectionnée par l'utilisateur (Ctrl+clic sur la dernière, clic dans le
    // vide).
    void selectionCleared();
    // Actions du menu de ligne et glisser-déposer : MainWindow exécute les commandes annulables.
    void visibilityChangeRequested(const std::vector<ObjectId>& ids, bool visible);
    void orderLockChangeRequested(const std::vector<ObjectId>& ids, bool locked);
    void deleteRequested(const std::vector<ObjectId>& embroidery,
                         const std::vector<ObjectId>& vectors);
    // Nouvel ordre de couture complet (tous les objets) issu d'un glisser-déposer.
    void orderChangeRequested(const std::vector<ObjectId>& newOrder);
    // Cases de la ligne (colonnes Visible / Ordre figé) et renommage au double-clic :
    // MainWindow exécute la commande annulable, jamais de mutation ici.
    void visibilityToggled(ObjectId id, bool visible);
    void orderLockToggled(ObjectId id, bool locked);
    void renameRequested(ObjectId id, QString name);
    // Calques de couture (HP-OBJ-023) : « Ajouter une couche » (bouton « + » ou menu de ligne)
    // et « Dupliquer la couche » ; `id` = objet de broderie visé. MainWindow exécute les commandes.
    // `interactive` : choisir le type et la couleur (Maj + clic) au lieu des valeurs par défaut.
    void addLayerRequested(ObjectId id, bool interactive);
    void duplicateLayerRequested(ObjectId id);

private:
    CalquePanel* calquePanel_{nullptr};
    QPushButton* addLayerBtn_{nullptr}; // « + Couche » : actif si une seule broderie est choisie
    // Ligne de broderie visée par « ajouter une couche » : la ligne unique sélectionnée, ou la
    // dernière couche (celle du dessus) d'un groupe sélectionné. nullptr sinon.
    [[nodiscard]] QTreeWidgetItem* layerTargetItem() const;
    void updateAddLayerButton();
    QTabWidget* tabs_{nullptr};
    // Arbre plutôt que liste plate (§21 du plan de refonte satin,
    // 2026-08-14) : les EmbroideryObject partageant le même `source_vector`
    // (les sections d'un même plan satin, cf. `satin_planning::create_satin_plan`)
    // sont regroupées sous un nœud parent -- mais SEULEMENT quand il y en a
    // plus d'une ; un objet seul reste un simple item de premier niveau,
    // visuellement identique à l'ancienne liste plate.
    QTreeWidget* objectsList_{nullptr};
    QLineEdit* filterEdit_{nullptr};
    QComboBox* typeFilter_{nullptr}; // filtre par type de point (HP-UX-042)
    QListWidget* regionsList_{nullptr};
    // Masque les lignes qui ne contiennent pas le texte du champ de recherche.
    void applyFilter();
    void restoreRenamedText();
    // Feuille de style locale + icône de loupe, reconstruites depuis les jetons du thème
    // courant (appelé à la construction et à chaque AppTheme::changed).
    void applyTheme();
    // Identifiants des lignes sélectionnées (les sections d'un groupe sélectionné comptent).
    void collectSelection(std::vector<ObjectId>& embroidery, std::vector<ObjectId>& vectors) const;
    void onObjectSelectionChanged();
    QAction* searchAction_{nullptr};
    std::vector<ObjectId> order_;            // ordre de couture courant (dernier refresh)
    bool syncing_{false};                    // évite la boucle sélection -> signal -> sélection
    QTreeWidgetItem* renamingItem_{nullptr}; // ligne en cours d'édition de nom
};

} // namespace openstitch::desktop
