// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

inline constexpr const char* kSemanticSchema = "openstitch.semantic/1";
inline constexpr const char* kSemanticValidationSchema = "openstitch.semantic-validation/1";
inline constexpr const char* kSemanticMapSchema = "openstitch.semantic-map/1";

// Validation d'un manifeste sémantique `openstitch.semantic/1` (format : docs/agent-semantic-
// format.md) : schéma, identifiants stables, hiérarchie et occlusions sans cycle, masques PNG
// présents, dans le dossier du manifeste, de mêmes dimensions que l'image source. Mêmes codes
// d'erreur (`E_SCHEMA`, `E_MASK_SIZE`, `E_OCCLUSION_CYCLE`…) que le validateur Python
// `scripts/openstitch_semantic.py`. `skip_files` valide la structure sans lire les masques.
// Retourne toujours le document JSON (`openstitch.semantic-validation/1`, `ok`,
// `errors[{code,path,message}]`), y compris pour un fichier illisible (`E_IO`). Ne modifie rien.
[[nodiscard]] std::string validate_semantic_manifest(const std::filesystem::path& manifest,
                                                     bool skip_files = false);

// Provenance sémantique d'un projet : identifiant sémantique -> objets de broderie, formes
// sources et types de points (`openstitch.semantic-map/1`). L'identité est portée par le NOM de
// l'objet (`sem:<semantic_id> <n>`, convention du pont Python), donc sauvegardée dans le .osp
// sans changement de format ; une couleur de fil identique n'en fait jamais le même objet.
[[nodiscard]] std::string semantic_map_json(const document::Project& project);

} // namespace openstitch::project_ops
