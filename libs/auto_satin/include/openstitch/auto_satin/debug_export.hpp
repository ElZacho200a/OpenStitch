// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <utility>

#include "openstitch/auto_satin/auto_satin.hpp"
#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::auto_satin {

// SVG de diagnostic superposant : contour source (noir), trous (rouge), arêtes
// du squelette (bleu), extrémités (vert) et jonctions (orange), et
// éventuellement les rails « actuels » (`rails_from_contour`, gris pointillé)
// pour comparaison. Coordonnées en millimètres.
[[nodiscard]] std::string to_debug_svg(
    const geometry::PathSet& region, const AutoSatinAnalysis& analysis,
    const std::optional<std::pair<geometry::Path, geometry::Path>>& current_rails = std::nullopt);

// SVG des colonnes construites : contour (noir), trous (rouge), squelette (vert),
// rail A (bleu), rail B (orange), barreaux (gris). Titre = statut / refus.
[[nodiscard]] std::string columns_to_svg(const geometry::PathSet& region,
                                         const SatinColumnsResult& result);

// SVG des objets satin PARAMÉTRIQUES (§ étape 15, mode `Parametric`) :
// contour, squelette, rails Bézier APLATIS (lisses, pas les nœuds de
// contrôle bruts), poignées Bézier, paires structurantes, lignes d'angle,
// recouvrement de jonction, identifiant d'objet, ordre de couture. Statistiques
// (stations brutes, paires retenues, erreur d'ajustement max, recouvrement)
// en commentaire, une ligne par objet, lisibles en ouvrant le SVG en texte.
[[nodiscard]] std::string parametric_to_svg(const geometry::PathSet& region,
                                            const SatinColumnsResult& result);

}  // namespace openstitch::auto_satin
