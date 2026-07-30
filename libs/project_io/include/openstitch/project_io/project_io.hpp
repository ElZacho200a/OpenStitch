// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_io {

// Version courante du schéma du format projet .osp (ADR-009).
// v1 -> v2 : le satin porte des barreaux (rungs) et le projet un cadre (canvas).
// Lecture rétrocompatible : un fichier v1 se charge (barreaux/cadre par défaut).
inline constexpr int kSchemaVersion = 2;

// Enregistre le projet dans une archive .osp (ZIP : project.json + image
// originale PNG + carte de segmentation binaire). Écriture atomique :
// fichier temporaire puis renommage — un crash pendant l'écriture ne
// corrompt jamais le fichier existant.
[[nodiscard]] Result<void> save_project(const std::filesystem::path& path,
                                        const document::Project& project);

[[nodiscard]] Result<document::Project> load_project(const std::filesystem::path& path);

}  // namespace openstitch::project_io
