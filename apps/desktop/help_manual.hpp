// SPDX-License-Identifier: Apache-2.0
#pragma once

// Manuel utilisateur consultable dans l'application (HP-HELP-001) et parcours guidé
// (HP-HELP-002). Le manuel est docs/source/user-guide.md embarqué en ressource Qt et rendu
// par QTextBrowser (Markdown natif de Qt) : aucune dépendance supplémentaire. Aucune logique
// métier : correspondance outil/panneau -> titre de section, rendu et défilement.

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>

#include "tools.hpp"

class QLineEdit;
class QTextBrowser;
class QWidget;

namespace openstitch::desktop {

// Contenu Markdown du manuel (ressource embarquée ; vide si la ressource manque).
[[nodiscard]] QString manualMarkdown();

// Titre (texte exact d'un titre `##`/`###` de user-guide.md) de la section qui décrit l'outil.
[[nodiscard]] QString manualHeadingForTool(Tool tool);

// Titres des sections associées aux panneaux ancrés : exposé pour la garde de synchronisation
// doc/IHM.
[[nodiscard]] QStringList manualPanelHeadings();
// `panel` : « properties », « document », « workflow », « analysis », « order », « filters »,
// « threads », « colors », « history ». Vide si inconnu.
[[nodiscard]] QString manualHeadingForPanel(const QString& panel);

// Titres (niveaux 2 et 3) présents dans un texte Markdown, dans l'ordre.
[[nodiscard]] QStringList markdownHeadings(const QString& markdown);

// Image d'exemple du parcours guidé : formes pleines de trois couleurs, générée par code
// (déterministe, sans fichier ni droit d'auteur).
[[nodiscard]] QImage tutorialSampleImage();

// « Manuel d'utilisation » : texte du guide, recherche, ouverture à une section. Non modal.
class ManualDialog : public QDialog {
    Q_OBJECT

public:
    explicit ManualDialog(QWidget* parent = nullptr);

    // Fait défiler jusqu'au titre de texte exact `heading` ; false s'il n'existe pas.
    bool showTopic(const QString& heading);
    [[nodiscard]] QString currentTopic() const { return currentTopic_; }
    [[nodiscard]] QTextBrowser* browser() const { return browser_; }
    [[nodiscard]] QLineEdit* searchField() const { return search_; }

public slots:
    // Cherche `text` dans le manuel à partir du début ; false si absent.
    bool search(const QString& text);

private:
    QTextBrowser* browser_{nullptr};
    QLineEdit* search_{nullptr};
    QString currentTopic_;
};

} // namespace openstitch::desktop
