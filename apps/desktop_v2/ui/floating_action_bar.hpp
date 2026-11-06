// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- barre flottante au-dessus du canevas : les gestes fréquents du contexte courant
// (objet, texte, satin, nœuds). Sans logique métier : le shell lui donne des entrées déjà filtrées
// (CommandEntry) et exécute l'identifiant choisi. Chaque bouton est atteignable au clavier (Tab) et
// les mêmes commandes restent au clic droit et dans la palette.

#include <QFrame>
#include <vector>

#include "command_catalog.hpp"

class QHBoxLayout;

namespace openstitch::desktop_v2 {

class FloatingActionBar : public QFrame {
    Q_OBJECT
public:
    explicit FloatingActionBar(QWidget* parent = nullptr);

    // Remplace les boutons ; liste vide : la barre se cache.
    void setEntries(const std::vector<CommandEntry>& entries);
    [[nodiscard]] int buttonCount() const;
    // Se place en haut et au centre de son parent (appelé au redimensionnement).
    void reposition();

signals:
    void chosen(const QString& commandId);

private:
    QHBoxLayout* row_;
};

} // namespace openstitch::desktop_v2
