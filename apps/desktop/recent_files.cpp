// SPDX-License-Identifier: Apache-2.0
#include "recent_files.hpp"

#include <QFileInfo>
#include <QSettings>

#include <algorithm>

namespace openstitch::desktop {

namespace {
constexpr auto kRecentFiles = "recent/files";
constexpr qsizetype kMaxRecentFiles = 10;

// Chemin canonique pour comparer deux orthographes du même fichier (casse de
// lecteur, séparateurs, `..`) ; si le fichier n'existe plus (canonique
// vide), compare le chemin tel quel plutôt que de ne jamais matcher.
QString canonicalOrSelf(const QString& path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? path : canonical;
}
} // namespace

QStringList loadRecentFiles() {
    QSettings settings;
    return settings.value(QLatin1String(kRecentFiles)).toStringList();
}

void saveRecentFiles(const QStringList& files) {
    QSettings settings;
    settings.setValue(QLatin1String(kRecentFiles), files);
}

QStringList addRecentFile(QStringList current, const QString& path) {
    const QString canonical = canonicalOrSelf(path);
    current.erase(std::remove_if(current.begin(), current.end(),
                                 [&canonical](const QString& existing) {
                                     return canonicalOrSelf(existing) == canonical;
                                 }),
                  current.end());
    current.prepend(path);
    while (current.size() > kMaxRecentFiles) {
        current.removeLast();
    }
    return current;
}

QStringList pruneMissingRecentFiles(QStringList current) {
    current.erase(std::remove_if(current.begin(), current.end(),
                                 [](const QString& path) { return !QFileInfo::exists(path); }),
                  current.end());
    return current;
}

} // namespace openstitch::desktop
