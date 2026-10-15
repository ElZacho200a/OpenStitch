// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/project_io/project_io.hpp"

namespace openstitch::project_ops {

// Projet chargé + informations de lecture (version de schéma du fichier, migration).
struct OpenedProject {
    document::Project project;
    project_io::LoadInfo info;
};

// Lecture d'un .osp par le MÊME chemin que le bureau (`project_io::load_project`) : un fichier
// ancien est migré en mémoire, jamais modifié sur disque par cet appel.
[[nodiscard]] Result<OpenedProject> open_project(const std::filesystem::path& path);

// Écriture atomique (fichier temporaire puis renommage, `project_io::save_project`) ; si
// `source` est le fichier ouvert au schéma plus ancien (`migrated`) et qu'on l'écrase, la copie
// de sécurité `<nom>.v<N>.osp.bak` est faite AVANT (comme le bureau). Refuse d'écraser
// `target` si `no_clobber` et que le fichier existe. Ne crée pas de dossier parent.
[[nodiscard]] Result<void> write_project(const std::filesystem::path& target,
                                         const document::Project& project,
                                         const project_io::LoadInfo* source = nullptr,
                                         const std::filesystem::path& sourcePath = {},
                                         bool no_clobber = false);

// Intégrité structurelle d'un projet chargé (validation automatisée, initiative) :
// identifiants uniques, `source_vector` existant, `color_blend` partenaire existant, région
// source présente dans la segmentation, géométrie non vide pour les objets visibles. Liste
// vide = intègre. Ne génère aucun point.
[[nodiscard]] std::vector<std::string> check_integrity(const document::Project& project);

// Sélection d'objets de broderie (initiative) : « color=#RRGGBB », « type=tatami », « visible=true
// », « locked=false », « name=texte » (sous-chaîne), « id=1,2,3 », « area_gt=mm2 », « area_lt=mm2
// », « within=x0,y0,x1,y1 » (mm, objet entièrement dans le rectangle). Critères combinés en ET.
// Résultat : identifiants d'objets de broderie dans l'ordre de couture. Un critère inconnu
// ou mal formé est une erreur nommée (jamais ignoré).
[[nodiscard]] Result<std::vector<ObjectId>>
select_objects(const document::Project& project, const std::vector<std::string>& criteria);

} // namespace openstitch::project_ops
