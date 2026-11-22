// SPDX-License-Identifier: Apache-2.0
#pragma once

// Étape « Vecteurs » du flux Studio : recalcule les formes vectorielles depuis la segmentation de
// l'image active. Partagé par l'espace Vecteurs de Studio et l'opération `vectorize_segmentation`
// (agent, CLI) : même entrée, mêmes formes, octet pour octet.

#include <optional>
#include <utility>
#include <vector>

#include "openstitch/commands/composite_command.hpp"
#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

// Bornes uniques des réglages (VEC-ADAPT-01, lot A) : Studio, opération et CLI/MCP valident
// contre les mêmes valeurs. Sous un pixel, l'ajustement suit les escaliers du raster plutôt que
// d'ajouter du détail : la borne basse permet de le mesurer, le défaut reste 1,5 px.
// Tolérance de simplification 0 : contour brut des pixels, aucun nœud retiré.
inline constexpr double kPrecisionMinMm = 0.0;
inline constexpr double kPrecisionMaxMm = 2.0;
inline constexpr double kCurveToleranceMinPx = 0.1;
inline constexpr double kCurveToleranceMaxPx = 10.0;

struct RevectorizeOptions {
    double precision_mm{0.2};       // tolérance de simplification des contours
    bool curves{false};             // ajuster des courbes de Bézier
    double curve_tolerance_px{1.5}; // tolérance de l'ajustement, en pixels de l'image
    // Calque image à revectoriser ; absent = l'image active (identifiant invalide = l'original).
    std::optional<ObjectId> image_source{};
};

// Plan de mise à jour, à appliquer par des commandes annulables.
struct RevectorizePlan {
    std::vector<std::pair<ObjectId, std::vector<geometry::PathSet>>> replace; // formes changées
    std::vector<document::VectorObject> add; // nouvelles formes, id à réserver à l'exécution
    int keep{0};                             // formes déjà à jour
    int orphan{0}; // formes dont la région n'existe plus (laissées telles quelles)
    // « Préserver les traits fins » : formes dont la région, présente dans la segmentation, a été
    // entièrement absorbée par les aplats complétés (c'était un morceau de trait). Retirées avec
    // leurs objets de broderie par `append_absorbed_removals` ; une forme verrouillée (ou dont un
    // objet l'est) n'y figure jamais.
    std::vector<ObjectId> absorbed;
};

// Ajoute à `group` le retrait des formes `plan.absorbed` et de leurs objets de broderie (objets
// d'abord). Rien si la liste est vide.
void append_absorbed_removals(const document::Project& project, const RevectorizePlan& plan,
                              commands::CompositeCommand& group);

// Plan vide sans segmentation. Ne modifie pas le projet.
[[nodiscard]] RevectorizePlan revectorize(const document::Project& project,
                                          const RevectorizeOptions& options);

} // namespace openstitch::project_ops
