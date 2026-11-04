// SPDX-License-Identifier: Apache-2.0
// Ticket GitHub prérempli pour un rapport de plantage (HP-QA-002). Aucun envoi : l'URL est
// seulement ouverte dans le navigateur, l'utilisateur relit et valide lui-même.
#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

class QMainWindow;
class QMenu;
class QWidget;

namespace openstitch::desktop {

// Remplace tout chemin de fichier (Windows ou POSIX) par « <chemin> » : le texte du ticket ne
// doit révéler aucun nom de projet ni de dossier utilisateur.
[[nodiscard]] QString redactPaths(const QString& text);

// Corps du ticket : version, système, dernières lignes du journal (chemins masqués).
[[nodiscard]] QString crashReportBody(const QStringList& logTail, bool hasDump);

// URL « nouveau ticket » du dépôt, titre et corps encodés, bornée en longueur.
[[nodiscard]] QUrl crashReportUrl(const QStringList& logTail, bool hasDump);

// Ajoute « Envoyer un rapport de plantage… » (objectName `action_crash_report`) au menu.
void installCrashReportAction(QMainWindow* window, QMenu* helpMenu);

// Au démarrage : si un dump récent non encore proposé existe, propose le rapport (une fois).
void proposeCrashReportIfRecent(QWidget* parent);

} // namespace openstitch::desktop
