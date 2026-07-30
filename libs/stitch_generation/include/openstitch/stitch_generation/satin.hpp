// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Barreau : segment transversal reliant un point du rail A à un point du rail B.
// Il définit une correspondance EXACTE entre les deux rails à cette station.
using SatinRungSeg = std::pair<Vec2um, Vec2um>;

// Paramètres d'une colonne satin (§5.3).
struct SatinConfig {
    Micrometers density{400};            // écart entre pénétrations le long d'un rail (0,4 mm)
    Micrometers pull_compensation{0};    // élargit la colonne pour compenser la traction du fil
    bool center_underlay{false};         // sous-couche : point droit sur l'axe central
    Micrometers underlay_spacing{2'000}; // longueur de point de la sous-couche centrale
};

// Résultat d'une génération satin.
struct SatinResult {
    std::vector<Vec2um> underlay;  // vide si pas de sous-couche
    std::vector<Vec2um> satin;     // points du zigzag principal
    double max_width_um{0.0};      // largeur maximale (pour l'avertissement)
};

// Génère les points d'une colonne satin à partir de deux rails (polylignes
// ouvertes, orientées dans le même sens). Les deux rails sont ré-échantillonnés
// par fraction d'abscisse curviligne : à chaque pas, un point sur chaque rail,
// et le fil zigzague d'un bord à l'autre. Densité = pas le long de la colonne.
[[nodiscard]] SatinResult fill_satin(const geometry::Path& rail_a, const geometry::Path& rail_b,
                                     const SatinConfig& config);

// Génère une colonne satin en respectant des BARREAUX (correspondance par
// sections, cf. auto-satin). Chaque paire de barreaux consécutifs découpe les
// rails en intervalles correspondants, interpolés selon LEUR propre abscisse
// curviligne (rails de longueurs/courbures différentes autorisés). L'espacement
// est mesuré PERPENDICULAIREMENT (avance de la ligne médiane), pas le long d'un
// rail. Les barreaux sont traversés exactement. Séquence L0,R0,L1,R1,…
// déterministe. Si `rungs` a moins de 2 éléments, retombe sur `fill_satin`.
[[nodiscard]] SatinResult fill_satin_columns(const geometry::Path& rail_a,
                                             const geometry::Path& rail_b,
                                             const std::vector<SatinRungSeg>& rungs,
                                             const SatinConfig& config);

// Découpe un contour fermé en deux rails, coupé aux deux sommets les plus
// éloignés (les « bouts » de la colonne). Convient aux formes allongées.
// Renvoie nullopt si le contour est trop petit.
[[nodiscard]] std::optional<std::pair<geometry::Path, geometry::Path>> rails_from_contour(
    const geometry::Path& contour);

}  // namespace openstitch::stitch_generation
