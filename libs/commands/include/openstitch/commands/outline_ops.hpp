// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "openstitch/commands/vector_ops.hpp"

namespace openstitch::commands {

// Contour de broderie autour d'un ou plusieurs objets (HP-VEC-006, « Outlines & Offsets »).
enum class OutlineKind {
    Running, // point droit simple
    Triple,  // point triple (3 passes)
    Satin,   // anneau d'épaisseur `satin_width`, rempli en auto-satin
};

struct OutlineOptions {
    Micrometers gap{1'000}; // écart entre la sélection et le contour (>= 0)
    OutlineKind kind{OutlineKind::Running};
    Micrometers satin_width{2'500}; // largeur de l'anneau (Satin seulement)
    bool around_holes{false};       // contourne aussi les trous (avec le même écart)
};

// Crée, en UNE commande annulable, un nouvel objet vectoriel (nom « Contour ») et sa broderie :
// l'union des formes de `ids` décalée vers l'extérieur de `gap`. Les objets d'origine ne sont pas
// modifiés. Commande nulle (avec `error`) si moins d'un objet existe ou si le décalage échoue.
[[nodiscard]] VectorOpResult make_outline_command(document::Project& project,
                                                  const std::vector<ObjectId>& ids,
                                                  const OutlineOptions& options);

} // namespace openstitch::commands
