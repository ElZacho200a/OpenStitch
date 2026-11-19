// SPDX-License-Identifier: Apache-2.0
#pragma once

// Étape « Vecteurs » du flux Studio : recalcule les formes vectorielles depuis la segmentation de
// l'image active. Partagé par l'espace Vecteurs de Studio et l'opération `vectorize_segmentation`
// (agent, CLI) : même entrée, mêmes formes, octet pour octet.

#include <optional>
#include <utility>
#include <vector>

#include "openstitch/document/project.hpp"

namespace openstitch::project_ops {

// Bornes uniques des réglages (VEC-ADAPT-01, lot A) : Studio, opération et CLI/MCP valident
// contre les mêmes valeurs. Sous un pixel, l'ajustement suit les escaliers du raster plutôt que
// d'ajouter du détail : la borne basse permet de le mesurer, le défaut reste 1,5 px.
inline constexpr double kPrecisionMinMm = 0.01;
inline constexpr double kPrecisionMaxMm = 2.0;
inline constexpr double kCurveToleranceMinPx = 0.1;
inline constexpr double kCurveToleranceMaxPx = 10.0;

struct RevectorizeOptions {
    double precision_mm{0.2};       // tolérance de simplification des contours
    bool curves{false};             // ajuster des courbes de Bézier
    double curve_tolerance_px{1.5}; // tolérance de l'ajustement, en pixels de l'image
    // Précision intelligente (VEC-ADAPT-01, lot B) : tolérances réduites sur les petites formes et
    // les formes fines (`vectorization::AdaptiveTolerance`). Désactivée : résultat inchangé.
    bool adaptive{false};
    // Lot C : la tolérance varie aussi le long d'un contour (courbes seulement, requiert
    // `adaptive`) : pointes, pétales et encoches plus fidèles que les bords larges.
    bool adaptive_local{false};
    // Calque image à revectoriser ; absent = l'image active (identifiant invalide = l'original).
    std::optional<ObjectId> image_source{};
};

// Plan de mise à jour, à appliquer par des commandes annulables.
struct RevectorizePlan {
    std::vector<std::pair<ObjectId, std::vector<geometry::PathSet>>> replace; // formes changées
    std::vector<document::VectorObject> add; // nouvelles formes, id à réserver à l'exécution
    int keep{0};                             // formes déjà à jour
    int orphan{0}; // formes dont la région n'existe plus (laissées telles quelles)
};

// Facteur de tolérance d'une surcharge de région (Maximum 1/3, Standard 1, Simplify 2).
[[nodiscard]] double vector_precision_factor(document::VectorPrecision level);

// Tolérance effectivement appliquée à une région par `revectorize` avec ces options : facteur
// (1 = réglage de base), valeur en px (courbes) ou en mm (polygone), et origine du facteur.
struct RegionTolerance {
    double factor{1.0};
    double value{0.0}; // px si `curves`, mm sinon
    bool curves{false};
    bool overridden{false}; // surcharge de l'utilisateur
};
[[nodiscard]] RegionTolerance region_tolerance(const document::Project& project,
                                               const RevectorizeOptions& options, RegionId region);

// Plan vide sans segmentation. Ne modifie pas le projet.
[[nodiscard]] RevectorizePlan revectorize(const document::Project& project,
                                          const RevectorizeOptions& options);

} // namespace openstitch::project_ops
