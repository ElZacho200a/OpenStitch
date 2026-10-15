// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

// Schémas JSON (champ `schema` de chaque document) : « openstitch.<nom>/<majeure> ».
// La majeure ne change que pour une rupture ; des champs peuvent être AJOUTÉS sans
// changer la majeure (les lecteurs ignorent les champs inconnus). Documenté dans
// docs/source/cli-project.md (conventions de stabilité).
inline constexpr const char* kInspectSchema = "openstitch.project-inspect/1";
inline constexpr const char* kObjectSchema = "openstitch.object-inspect/1";

struct InspectOptions {
    // Génère les points (effective_sequence) pour renseigner points, dimensions et position
    // dans la séquence ; faux = inspection légère (aucune génération).
    bool with_stitches{true};
    // Détail complet des paramètres de chaque objet (sinon : résumé type + champs clés).
    bool full_params{true};
    // Inclut les contours (nœuds en mm) des objets vectoriels.
    bool with_geometry{false};
};

// Document JSON d'un projet : métadonnées (version de schéma .osp, cadre, résolution,
// finitions), image, régions de segmentation, objets vectoriels, objets de broderie
// (identifiants stables, couleur, technique, paramètres, rang dans l'ordre de couture,
// source vectorielle, dimensions mm, nombre de points), totaux de séquence et
// avertissements (`stitch_analysis::check_stitch_limits`). Déterministe : même projet,
// même texte (clés triées par construction, aucun horodatage).
[[nodiscard]] Result<std::string> inspect_project_json(const document::Project& project,
                                                       const InspectOptions& options = {});

// Résumé lisible (texte) du même contenu : cadre, totaux, une ligne par objet, avertissements.
[[nodiscard]] Result<std::string> inspect_project_text(const document::Project& project,
                                                       const InspectOptions& options = {});

// Un objet de broderie (id d'objet de broderie) : paramètres complets, géométrie source,
// statistiques de ses points, avertissements qui le concernent. Erreur
// `OperationImpossible` (message « objet introuvable ») si l'id n'existe pas.
[[nodiscard]] Result<std::string> inspect_object_json(const document::Project& project,
                                                      ObjectId embroidery_id,
                                                      const InspectOptions& options = {});

// Nom stable (« running », « tatami », « satin », « directional », « auto_satin ») du type de
// points d'un objet : vocabulaire commun à l'inspection, aux opérations et aux sélecteurs.
[[nodiscard]] const char* stitch_type_name(const document::EmbroideryObject& object);

// Couleur « #RRGGBB » en majuscules, et lecture inverse (« #RRGGBB » ou « RRGGBB »).
[[nodiscard]] std::string rgb_to_hex(const std::array<std::uint8_t, 3>& rgb);
[[nodiscard]] std::optional<std::array<std::uint8_t, 3>> rgb_from_hex(std::string_view text);

} // namespace openstitch::project_ops
