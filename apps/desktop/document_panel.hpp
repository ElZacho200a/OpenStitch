// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include "openstitch/document/project.hpp"

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

    // Reconstruit les listes depuis le document.
    void refresh(const document::Project& project);
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
