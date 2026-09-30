// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QStringList>

namespace openstitch::desktop {

// Liste des projets récents (le plus récent en tête), persistée via
// QSettings comme le reste des préférences UI (cf. `ai_preferences.hpp` pour
// le même gabarit) -- HP-FILE-003. Aucune logique métier : pure préférence
// d'interface, pas de lib cœur en dépendance.

[[nodiscard]] QStringList loadRecentFiles();
void saveRecentFiles(const QStringList& files);

// Insère `path` en tête, déduplique par chemin canonique (une entrée déjà
// présente -- y compris sous une orthographe différente du même fichier --
// est déplacée en tête plutôt que dupliquée), tronque à 10 entrées. Pure :
// ne touche pas QSettings.
[[nodiscard]] QStringList addRecentFile(QStringList current, const QString& path);

// Retire les entrées dont le fichier n'existe plus sur disque
// (QFileInfo::exists()), ordre préservé. Pure : ne touche pas QSettings --
// la purge des entrées disparues se fait à la lecture (AD-S11-2), pas à
// l'écriture, pour qu'un disque amovible temporairement absent ne perde pas
// son entrée avant une reconstruction effective sans lui.
[[nodiscard]] QStringList pruneMissingRecentFiles(QStringList current);

} // namespace openstitch::desktop
