// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Satin de BORDURE à largeur fixe (HP-STI-004) : une colonne satin de largeur constante
// qui suit un contour (fermé ou ouvert). C'est le « trait satin » des logiciels de
// broderie : bordure d'écusson, tige, lettre filaire.
//
// Construction : le contour est aplati, puis pour chaque sommet on calcule les points
// des deux rails décalés de la demi-largeur (ou de la largeur entière d'un côté) le long
// des normales ; chaque sommet donne un BARREAU perpendiculaire (point rail A, point
// rail B). Aux coins :
//   - `Sharp`  : jointure en onglet (le rail extérieur reste anguleux) ; au-delà d'un
//                rapport d'onglet de 3 (angle très aigu) l'onglet est biseauté ;
//   - `Round`  : le rail extérieur décrit un arc (un barreau tous les 12°), le rail
//                intérieur reste au point d'onglet (éventail de barreaux).
// Le côté intérieur du virage est ramené à au plus la moitié de l'arête voisine (rails
// jamais croisés). Les rails et barreaux sont stockés dans `SatinParams` : la génération
// passe par le moteur satin existant (sous-couches, routage, verrous...).
//
// `left_is_inside` : sens du contour. Pour un contour fermé, vrai si la matière (l'intérieur
// de la région) est à GAUCHE du sens de parcours ; `ring_left_is_inside` le déduit de
// l'orientation. Pour un tracé ouvert, `Inside` désigne la gauche.
// Renvoie nullopt si le tracé est dégénéré (moins de 2 points distincts).
[[nodiscard]] std::optional<document::SatinParams>
border_satin_from_path(const geometry::Path& path, const document::BorderSatinSpec& spec,
                       Micrometers density = Micrometers{400}, bool left_is_inside = true);

// Sens de la matière pour un anneau d'une région (extérieur ou trou).
[[nodiscard]] bool ring_left_is_inside(const geometry::Path& ring, bool is_hole);

// Un satin de bordure par anneau de la région (extérieur d'abord, puis les trous), dans
// cet ordre. Les anneaux dégénérés sont ignorés. `path_set_index` est mémorisé dans
// `border` (avec le numéro d'anneau) pour la régénération.
[[nodiscard]] std::vector<document::SatinParams>
border_satin_from_region(const geometry::PathSet& region, const document::BorderSatinSpec& spec,
                         Micrometers density = Micrometers{400}, std::uint32_t path_set_index = 0);

// Idem pour tous les PathSet d'un objet vectoriel, dans l'ordre.
[[nodiscard]] std::vector<document::SatinParams>
border_satin_from_paths(const std::vector<geometry::PathSet>& paths,
                        const document::BorderSatinSpec& spec,
                        Micrometers density = Micrometers{400});

// Régénère les rails/barreaux d'un satin de bordure existant depuis le contour source,
// avec la nouvelle spécification (largeur, côté, coins) ; l'anneau suivi est celui de
// `previous.border`. Tous les autres réglages de `previous` (densité, sous-couches,
// verrous...) sont conservés. nullopt si l'anneau n'existe plus ou est dégénéré.
[[nodiscard]] std::optional<document::SatinParams>
regenerate_border_satin(const std::vector<geometry::PathSet>& paths,
                        const document::SatinParams& previous,
                        const document::BorderSatinSpec& spec);

// Largeur bornée à la plage valide [0,5 ; 20] mm.
[[nodiscard]] Micrometers clamp_border_width(Micrometers width);

} // namespace openstitch::stitch_generation
