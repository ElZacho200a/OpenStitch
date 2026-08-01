// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include <utility>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

class QListWidget;
class QTabWidget;

namespace openstitch::desktop {

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

signals:
    void embroiderySelected(ObjectId id);
    void regionSelected(RegionId id);

private:
    QTabWidget* tabs_{nullptr};
    QListWidget* objectsList_{nullptr};
    QListWidget* regionsList_{nullptr};
    bool syncing_{false};  // évite la boucle sélection -> signal -> sélection
};

}  // namespace openstitch::desktop
