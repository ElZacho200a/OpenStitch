// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- dialogue généré depuis une `ActionDef` : un éditeur par champ (nombre, entier,
// case, texte, liste, fichier) initialisé aux valeurs par défaut. Il ne valide rien et ne construit
// aucun lot : `ShellWindow` passe `values()` à `buildAction`, qui refuse toute valeur invalide.

#include <QDialog>

#include "action_catalog.hpp"

class QWidget;

namespace openstitch::desktop_v2 {

class ActionDialog : public QDialog {
    Q_OBJECT
public:
    ActionDialog(const ActionDef& action, const Values& initial, QWidget* parent = nullptr);

    [[nodiscard]] Values values() const;

private:
    const ActionDef& action_;
    std::map<std::string, QWidget*> editors_;
};

} // namespace openstitch::desktop_v2
