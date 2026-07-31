// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/document/embroidery_object.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::stitch_generation {

// Un point de remplissage : sa position et s'il est atteint par un SAUT
// (aiguille levée, sans couture — un « jump » machine) ou par un point COUSU.
// Chaque trajet cousu est validé géométriquement contre la région et ses trous ;
// à défaut de trajet intérieur valide, la liaison est un saut.
//
// Terminologie : ce drapeau distingue Stitch (cousu) de Jump (aiguille levée).
// Le vrai « travel stitch » de broderie — un déplacement COUSU caché sous la
// couche supérieure (underpath) — n'est PAS implémenté : les liaisons non
// cousables sont donc des sauts, pas des sous-chemins cachés.
struct FillStitch {
    Vec2um pos{};
    bool jump{false};    // true = saut (aiguille levée) ; false = point cousu
    bool travel{false};  // true = déplacement COUSU caché (underpath, §15) ; jump alors false

    bool operator==(const FillStitch&) const = default;
};

// Remplissage tatami d'une région (contour extérieur + trous), par balayage
// de lignes parallèles (§5.4) :
// - rangées espacées de `row_spacing`, orientées selon `angle` ;
// - intersection des lignes avec le polygone (règle pair-impair, trous gérés) ;
// - couture en serpentin (alternance du sens d'une rangée à l'autre) ;
// - subdivision de chaque rangée en points <= `stitch_length` ;
// - décalage régulier des pénétrations d'aiguille selon `stagger`.
// Une liaison entre deux segments (même rangée, ou d'une rangée à l'autre) qui
// sortirait de la région ou traverserait un trou est classée en DÉPLACEMENT,
// jamais en point cousu (corrige le débordement hors région).
// Le retrait de bord (`inset`) est appliqué par l'appelant en amont.
[[nodiscard]] std::vector<FillStitch> fill_tatami(const geometry::PathSet& region,
                                                  const document::TatamiParams& params);

// Sous-couches de remplissage (§15), à coudre AVANT la couche supérieure :
// - contour rentré (`underlay_edge`) : running le long du bord EXTÉRIEUR
//   uniquement (retrait `underlay_inset`) — stabilise les bords. Les bords de
//   TROUS ne reçoivent jamais de sous-couche de contour (l'inset d'un trou le
//   rétrécit vers son intérieur ; le longer ferait passer la sous-couche
//   au-dessus du trou). Politique sûre si le retrait échoue ou fait disparaître
//   la forme (pièce trop petite) : AUCUNE sous-couche de contour n'est émise
//   pour cette forme — jamais de repli silencieux sur le bord brut (sans
//   marge). Un retrait explicitement nul (`underlay_inset` == 0) est une
//   intention distincte : le bord brut est alors bien celui voulu.
// - rangées parallèles espacées (`underlay_parallel`) : balayage perpendiculaire
//   aux rangées supérieures, pas `underlay_spacing` — évite l'affaissement.
// Chaque passe est une polyligne cousue. Vide si aucune sous-couche activée.
[[nodiscard]] std::vector<std::vector<Vec2um>> tatami_underlay(
    const geometry::PathSet& region, const document::TatamiParams& params);

// Le segment [a,b] reste-t-il ENTIÈREMENT dans la région (extérieur moins les
// trous) ? Indépendant de l'orientation du segment ; un suivi de frontière
// (segment colinéaire à un bord) est autorisé, mais toute portion tombant
// dans un trou ou hors de l'extérieur est rejetée — y compris lorsque le
// segment ne fait que TOUCHER un sommet du trou sans le « croiser »
// franchement. C'est la validation utilisée par `fill_tatami` pour décider
// liaison cousue vs saut ; exposée pour test.
[[nodiscard]] bool segment_stays_in_region(const geometry::PathSet& region, Vec2um a, Vec2um b);

}  // namespace openstitch::stitch_generation
