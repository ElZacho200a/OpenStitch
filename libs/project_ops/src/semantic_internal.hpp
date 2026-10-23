// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::project_ops::detail {

struct SemanticRegion {
    std::string semantic_id;
    std::string family;
    std::string role;
    std::string level{"technical"};
    std::string priority;
    std::string decision{"keep"};
    std::optional<std::string> stitch_intent; // tatami | auto_satin | running | …
    std::optional<std::string> parent_id;
    std::optional<std::string> thread_rgb;
    std::optional<std::string> mask_path; // relatif au dossier du manifeste
    std::vector<std::string> under;
    std::vector<std::string> over;
};

struct SemanticManifest {
    int width_px{0};
    int height_px{0};
    double width_mm{0.0};
    double height_mm{0.0};
    std::filesystem::path base_dir;
    std::size_t raw_region_count{0}; // longueur de la liste JSON (y compris entrées invalides)
    std::vector<SemanticRegion> regions;
};

struct SemanticProblem {
    std::string code;
    std::string path;
    std::string message;
};

// Lit et valide un manifeste. `Result` échoue seulement si le fichier est illisible ou n'est
// pas du JSON ; sinon `problems` liste les défauts (vide = valide) et le manifeste porte ce qui
// a pu être lu.
[[nodiscard]] Result<SemanticManifest>
read_semantic_manifest(const std::filesystem::path& file, bool skip_files,
                       std::vector<SemanticProblem>& problems);

// Ordre de création (occlusion : le dessous avant le dessus ; ex æquo par identifiant), le même
// que le pont Python. Suppose un manifeste valide (graphe sans cycle).
[[nodiscard]] std::vector<const SemanticRegion*> creation_order(const SemanticManifest& manifest);

// Nom d'objet portant l'identité sémantique : `sem:<id> <n>` (n >= 1).
[[nodiscard]] std::string semantic_object_name(std::string_view semantic_id, std::size_t n);
// Identifiant sémantique lu dans un nom d'objet (`sem:a.b 2` -> `a.b`), sinon rien.
[[nodiscard]] std::optional<std::string> semantic_id_of_name(std::string_view name);
// Objets de broderie portant cet identifiant (nom de l'objet OU de sa forme source), dans
// l'ordre de couture.
[[nodiscard]] std::vector<const document::EmbroideryObject*>
semantic_objects(const document::Project& project, std::string_view semantic_id);

// Résumé textuel des premiers défauts (message de refus d'une opération).
[[nodiscard]] std::string problems_text(const std::vector<SemanticProblem>& problems,
                                        std::size_t limit = 6);

// Masque PNG (même dimensions que l'image source) -> contours avec trous, en µm, origine au
// centre de l'image, Y vers le haut. Mêmes seuils que le pont Python (alpha ou gris > 127) ;
// vectorisation du moteur (`vectorization::vectorize_region`).
[[nodiscard]] Result<std::vector<geometry::PathSet>>
mask_to_paths(const std::filesystem::path& mask, int width_px, int height_px, double mm_per_px);

} // namespace openstitch::project_ops::detail
