// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- écran d'accueil affiché tant qu'aucun projet n'est ouvert : explique le parcours en
// quatre étapes et propose les trois façons de commencer (ouvrir une image, projet vierge, projet
// récent). Aucune logique métier : il émet des signaux que la fenêtre principale traite.

#include <QWidget>

class QListWidget;

namespace openstitch::desktop_v2 {

class WelcomePage : public QWidget {
    Q_OBJECT
public:
    explicit WelcomePage(QWidget* parent = nullptr);

    void setRecent(const QStringList& paths);
    [[nodiscard]] int recentCount() const;
    void activateRecent(int row); // équivaut à un clic sur la ligne (tests)

signals:
    void openRequested();
    void newRequested();
    void recentRequested(const QString& path);

private:
    QListWidget* recent_;
    QWidget* recentBox_;
};

} // namespace openstitch::desktop_v2
