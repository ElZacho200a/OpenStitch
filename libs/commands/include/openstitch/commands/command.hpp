// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "openstitch/document/project.hpp"

namespace openstitch::commands {

// Command pattern (ADR-010) : TOUTE mutation du document passe par une
// commande, dès la première. `apply` puis `revert` doivent ramener le
// document exactement à son état antérieur.
class ICommand {
public:
    virtual ~ICommand() = default;

    virtual void apply(document::Project& project) = 0;
    virtual void revert(document::Project& project) = 0;
    [[nodiscard]] virtual std::string name() const = 0;

    // Fusion d'annulation (coalescence) : une rafale de modifications du même
    // champ du même objet (molette d'un spinbox, flèches du clavier) ne doit
    // former qu'UN pas d'annulation. Deux commandes sont fusionnables si leur
    // clé est non vide et identique (ex. « params:12:row_spacing »). Par défaut
    // une commande n'est jamais fusionnée.
    [[nodiscard]] virtual std::string mergeKey() const { return {}; }

    // Absorbe `newer` (déjà appliquée au document par la pile, de même clé) :
    // après l'appel, `revert` de CETTE commande doit toujours ramener l'état
    // d'avant la première, et `apply` rejouer l'état après `newer`. Renvoie
    // false si la fusion est impossible (la pile empile alors normalement).
    virtual bool mergeWith(const ICommand& newer) {
        (void)newer;
        return false;
    }
};

} // namespace openstitch::commands
