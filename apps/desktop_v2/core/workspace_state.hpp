// SPDX-License-Identifier: Apache-2.0
#pragma once

// Desktop V2 -- état d'espace de travail hors document : liste des fichiers récents et
// sauvegarde automatique avec récupération. Sans Qt ; l'interface ne fait que persister la liste
// (QSettings) et proposer la récupération.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::desktop_v2 {

constexpr std::size_t kMaxRecentFiles = 10;

// Place `path` en tête (sans doublon, comparaison exacte), tronque à `maxCount`. Chemin vide :
// liste inchangée.
[[nodiscard]] std::vector<std::string> pushRecent(std::vector<std::string> list,
                                                  const std::string& path,
                                                  std::size_t maxCount = kMaxRecentFiles);
// Retire les fichiers qui n'existent plus.
[[nodiscard]] std::vector<std::string> pruneMissingRecent(const std::vector<std::string>& list);

// Liste persistée : un chemin UTF-8 par ligne. Fichier absent / illisible : liste vide.
[[nodiscard]] std::vector<std::string> loadRecentList(const std::filesystem::path& file);
[[nodiscard]] bool saveRecentList(const std::filesystem::path& file,
                                  const std::vector<std::string>& list);

// Fichier de sauvegarde automatique unique dans `dir` (créé au besoin). L'écriture est atomique
// (fichier temporaire puis renommage) : une coupure ne laisse jamais un .osp tronqué.
class AutosaveStore {
public:
    explicit AutosaveStore(std::filesystem::path dir);

    [[nodiscard]] const std::filesystem::path& file() const { return file_; }
    [[nodiscard]] bool exists() const;
    [[nodiscard]] Result<void> write(const document::Project& project) const;
    [[nodiscard]] Result<document::Project> load() const;
    // Supprime la sauvegarde (document enregistré ou récupération refusée). Absente : sans effet.
    void clear() const;

private:
    std::filesystem::path file_;
};

} // namespace openstitch::desktop_v2
