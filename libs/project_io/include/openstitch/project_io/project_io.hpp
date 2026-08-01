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
// Lecture rétrocompatible : un fichier v1 ou v2 se charge (retouches absentes
// -> overrides vides, état Clean).
inline constexpr int kSchemaVersion = 3;

// Enregistre le projet dans une archive .osp (ZIP : project.json + image
// originale PNG + carte de segmentation binaire). Écriture atomique :
// fichier temporaire puis renommage — un crash pendant l'écriture ne
// corrompt jamais le fichier existant.
[[nodiscard]] Result<void> save_project(const std::filesystem::path& path,
                                        const document::Project& project);

[[nodiscard]] Result<document::Project> load_project(const std::filesystem::path& path);

}  // namespace openstitch::project_io
