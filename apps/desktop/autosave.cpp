// SPDX-License-Identifier: Apache-2.0
#include "autosave.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <filesystem>

#include "openstitch/project_io/project_io.hpp"

namespace openstitch::desktop {

namespace {

// Dossier `AppDataLocation/autosave` -- premier usage d'`AppDataLocation`
// dans `apps/desktop` (le seul précédent, `ai_segmentation_dialog.cpp`,
// n'utilise que `TempLocation`) ; voir `docs/source/module-reference.md`.
QString autosaveDirPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/autosave");
}

} // namespace

QString autosaveSlug(const QString& currentProjectPath) {
    if (currentProjectPath.isEmpty()) {
        // ASM-S11-02 : plusieurs processus de l'application peuvent tourner
        // en parallèle sur deux documents jamais enregistrés -- le PID,
        // stable pour la durée du processus, évite qu'ils se marchent dessus
        // tout en réécrivant le même créneau à chaque tick.
        return QStringLiteral("untitled-%1").arg(QCoreApplication::applicationPid());
    }
    // Chemin canonique pour que rouvrir le même projet retrouve le même
    // créneau (même principe que `canonicalOrSelf` de `recent_files.cpp`) ;
    // repli sur le chemin brut si la canonicalisation échoue.
    const QString canonical = QFileInfo(currentProjectPath).canonicalFilePath();
    const QString forHash = canonical.isEmpty() ? currentProjectPath : canonical;
    const QByteArray hash = QCryptographicHash::hash(forHash.toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(hash.toHex());
}

AutosaveSlot slotFor(const QString& currentProjectPath) {
    const QString slug = autosaveSlug(currentProjectPath);
    const QString dir = autosaveDirPath();
    return AutosaveSlot{dir + QStringLiteral("/") + slug + QStringLiteral(".osp"),
                        dir + QStringLiteral("/") + slug + QStringLiteral(".json")};
}

Result<void> writeAutosave(const AutosaveSlot& slot, const document::Project& project,
                           const QString& originalPathForDisplay) {
    // Le dossier autosave n'existe pas forcément encore (premier tick de la
    // première exécution de l'application) -- slotFor() reste pur, c'est ici
    // qu'on le crée, au moment d'écrire.
    QDir().mkpath(QFileInfo(slot.osp_path).absolutePath());

    const auto written =
        project_io::save_project(std::filesystem::path(slot.osp_path.toStdWString()), project);
    if (!written) {
        return written;
    }

    // Sidecar best-effort : écrit séparément (pas de lien atomique avec le
    // .osp), un échec ici n'est jamais signalé à l'appelant -- au pire un
    // chemin d'origine périmé au prochain démarrage, jamais une perte de
    // données (cf. autosave.hpp).
    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("original_path"), originalPathForDisplay);
    sidecar.insert(QStringLiteral("saved_at"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QSaveFile sidecarFile(slot.sidecar_path);
    if (sidecarFile.open(QIODevice::WriteOnly)) {
        sidecarFile.write(QJsonDocument(sidecar).toJson(QJsonDocument::Compact));
        sidecarFile.commit();
    }

    return written;
}

void discardAutosave(const AutosaveSlot& slot) {
    // QFile::remove() renvoie simplement `false` si le fichier n'existe
    // pas -- no-op silencieux, jamais une erreur à faire remonter.
    QFile::remove(slot.osp_path);
    QFile::remove(slot.sidecar_path);
}

std::vector<AutosaveCandidate> scanForRecoverableAutosaves() {
    std::vector<AutosaveCandidate> result;
    QDir dir(autosaveDirPath());
    if (!dir.exists()) {
        return result;
    }
    const QStringList ospFiles = dir.entryList(QStringList{QStringLiteral("*.osp")}, QDir::Files);
    for (const QString& name : ospFiles) {
        AutosaveCandidate candidate;
        candidate.slot.osp_path = dir.filePath(name);
        candidate.slot.sidecar_path =
            dir.filePath(QFileInfo(name).completeBaseName() + QStringLiteral(".json"));

        QFile sidecarFile(candidate.slot.sidecar_path);
        if (sidecarFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(sidecarFile.readAll());
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                candidate.original_path = obj.value(QStringLiteral("original_path")).toString();
                candidate.saved_at = QDateTime::fromString(
                    obj.value(QStringLiteral("saved_at")).toString(), Qt::ISODate);
            }
        }
        // Sidecar absent/corrompu : original_path reste vide ("Projet sans
        // nom"), saved_at reste invalide -- comportement documenté
        // (autosave.hpp), jamais une entrée ignorée pour autant (le .osp
        // reste récupérable).
        result.push_back(std::move(candidate));
    }
    // Ordre non spécifié par QDir::entryList sans tri explicite : le plus
    // récent en tête est la lecture la plus utile dans le dialogue de
    // récupération (un candidat sans horodatage valide est trié en dernier
    // plutôt que de fausser l'ordre).
    std::sort(result.begin(), result.end(),
              [](const AutosaveCandidate& a, const AutosaveCandidate& b) {
                  if (a.saved_at.isValid() != b.saved_at.isValid()) {
                      return a.saved_at.isValid();
                  }
                  return a.saved_at > b.saved_at;
              });
    return result;
}

} // namespace openstitch::desktop
