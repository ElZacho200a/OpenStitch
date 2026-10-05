// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QDateTime>
#include <QString>

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop {

// Les deux fichiers d'un créneau autosave : l'instantané `.osp` lui-même et
// son sidecar JSON (chemin d'origine affiché + horodatage) -- HP-FILE-004.
struct AutosaveSlot {
    QString osp_path;
    QString sidecar_path;
};

// Un créneau orphelin retrouvé au démarrage (laissé par un arrêt anormal),
// proposé au dialogue de récupération.
struct AutosaveCandidate {
    AutosaveSlot slot;
    QString original_path; // vide -> "Projet sans nom" (document jamais enregistré)
    QDateTime saved_at;    // invalide si le sidecar est absent/corrompu
};

// Hachage stable (SHA-1 hex, QCryptographicHash) du chemin CANONIQUE de
// `currentProjectPath`, ou "untitled-<pid>" si `currentProjectPath` est vide
// (AD-S11-1) : un document jamais enregistré n'a pas de chemin à canoniser,
// et deux processus lancés en parallèle sur deux documents sans nom ne
// doivent pas se marcher dessus (le PID est stable pour la durée du
// processus). Pure.
[[nodiscard]] QString autosaveSlug(const QString& currentProjectPath);

// Résout les deux chemins sous AppDataLocation/autosave/<slug>.{osp,json} à
// partir du slug. Pure (ne crée pas le dossier -- writeAutosave le fait au
// moment d'écrire).
[[nodiscard]] AutosaveSlot slotFor(const QString& currentProjectPath);

// project_io::save_project(slot.osp_path, project) puis sidecar JSON
// {"original_path", "saved_at" (ISO 8601 UTC)} via QSaveFile. Le Result<void>
// retourné reflète l'écriture .osp ; un échec du sidecar seul est avalé
// (best-effort -- au pire un chemin d'origine périmé au prochain démarrage,
// jamais une perte de données).
[[nodiscard]] Result<void> writeAutosave(const AutosaveSlot& slot, const document::Project& project,
                                         const QString& originalPathForDisplay);

// Supprime les deux fichiers du créneau s'ils existent ; no-op silencieux
// sinon (best-effort, jamais un échec à signaler à l'appelant).
void discardAutosave(const AutosaveSlot& slot);

// Énumère autosave/*.osp (tous PID, un arrêt anormal précédent a par
// définition un autre PID que le processus courant), lit chaque sidecar si
// présent.
[[nodiscard]] std::vector<AutosaveCandidate> scanForRecoverableAutosaves();

} // namespace openstitch::desktop
