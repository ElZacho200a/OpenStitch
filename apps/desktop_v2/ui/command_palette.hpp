// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- palette de commandes (Ctrl+K) : champ de recherche + liste filtrée par
// `filterCommands`. N'exécute rien : elle émet l'identifiant choisi, c'est
// `ShellWindow::runCommand` qui agit (même chemin que les menus et les raccourcis).

#include <QDialog>
#include <vector>

#include "command_catalog.hpp"

class QLineEdit;
class QListWidget;

namespace openstitch::desktop_v2 {

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* parent = nullptr);

    void setEntries(std::vector<CommandEntry> entries);
    void setQuery(const QString& query); // refiltre
    [[nodiscard]] QStringList visibleIds() const;
    // Valide la ligne courante : émet `chosen` si elle est activée, retourne vrai dans ce cas.
    bool activateCurrent();

signals:
    void chosen(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void refill();

    std::vector<CommandEntry> entries_;
    QLineEdit* query_;
    QListWidget* list_;
};

} // namespace openstitch::desktop_v2
