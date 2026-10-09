// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_io {

// Version courante du schéma du format projet .osp (ADR-009).
// v1 -> v2 : le satin porte des barreaux (rungs) et le projet un cadre (canvas).
// v2 -> v3 : un objet de broderie peut porter des retouches manuelles
// (`overrides`/`editedFingerprint`/`editedPointCount`, ADR-014, Lot 8.1) --
// changement de nature du fichier (les points ne sont plus purement dérivés
// pour un objet retouché), pas seulement un nouveau champ de finition.
// v3 -> v4 : les satins séparent `maxWidth` (seuil doux/avertissement) de
// `maxWidthHard` (plafond géométrique du corridor). Lecture tolérante :
// champ absent -> 48 mm.
// Lecture rétrocompatible : un fichier v1/v2/v3 se charge (champs absents
// remplacés par leurs valeurs par défaut).
inline constexpr int kSchemaVersion = 5;

// Enregistre le projet dans une archive .osp (ZIP : project.json + image
// originale PNG + carte de segmentation binaire). Écriture atomique :
// fichier temporaire puis renommage — un crash pendant l'écriture ne
// corrompt jamais le fichier existant.
[[nodiscard]] Result<void> save_project(const std::filesystem::path& path,
                                        const document::Project& project);

// Informations de chargement : version du schéma du fichier lu. Un fichier
// plus ancien que kSchemaVersion est migré en mémoire (migrated == true).
struct LoadInfo {
    int fileVersion = 0;
    bool migrated = false;
};

// `info` (optionnel) reçoit la version lue et l'indicateur de migration.
// Ne lève jamais d'exception : un JSON valide mais de structure invalide
// ressort en Result d'erreur (InvalidFile).
[[nodiscard]] Result<document::Project> load_project(const std::filesystem::path& path,
                                                     LoadInfo* info = nullptr);

// Chemin de la copie de sécurité d'un fichier migré : `<nom>.v<N>.osp.bak`.
[[nodiscard]] std::filesystem::path migration_backup_path(const std::filesystem::path& path,
                                                          int fromVersion);

// Copie `path` vers migration_backup_path(path, fromVersion) si la copie
// n'existe pas encore (à appeler avant d'écraser un fichier migré).
[[nodiscard]] Result<void> backup_before_migrated_save(const std::filesystem::path& path,
                                                       int fromVersion);

} // namespace openstitch::project_io
