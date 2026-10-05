// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Découpe une région selon une ligne de coupe (deux points quelconques,
// prolongés très au-delà de la région pour garantir une traversée complète
// quels que soient les points fournis) -- façon "cut line" d'Ink/Stitch pour
// guider manuellement la décomposition d'une forme en colonnes satin, en
// complément (pas en remplacement) de la détection automatique de jonctions
// (`auto_satin::build_satin_columns`). Retire une fine bande (`cut_width`,
// quelques dizaines de µm par défaut : négligeable une fois cousu, mais
// garantit une vraie séparation TOPOLOGIQUE via une booléenne Clipper2,
// robuste au maillage discret -- une ligne infiniment fine laisserait parfois
// deux morceaux qui se retouchent en un point, toujours une seule composante
// connexe).
//
// Renvoie un morceau par composante connexe résultante : 1 seul morceau
// (la région inchangée, aux quelques µm de la bande près) si la ligne ne
// traverse pas réellement la région ou est dégénérée (A == B) -- à
// l'appelant de vérifier `size() >= 2` pour savoir si la coupe a réellement
// séparé quelque chose.
[[nodiscard]] Result<std::vector<PathSet>> cut_path_set(const PathSet& region, Vec2um a, Vec2um b,
                                                        Micrometers cut_width = Micrometers{20});

// Variante BORNEE de `cut_path_set` (2026-08-22) : retire la meme bande de
// largeur `cut_width`, perpendiculaire a [a,b] et centree sur son milieu,
// mais limitee a `reach_um` de part et d'autre du milieu au lieu de
// s'etendre jusqu'a la boite englobante de la region. Necessaire quand
// plusieurs branches paralleles d'une meme region occupent la MEME plage
// perpendiculaire a la coupe (ex. les dents d'un peigne, toutes a la meme
// hauteur) -- une coupe non bornee y tranche aussi les branches VOISINES,
// jamais visees par cet appelant precis (defaut reel trouve sur la fixture
// "comb" : aucune distance de coupe testee ne produisait jamais exactement
// 2 morceaux, la ligne infinie traversant systematiquement les 6 dents a la
// fois). `reach_um` doit rester borne par l'appelant (ex. rayon local de la
// branche + marge) : trop court laisserait la coupe ne pas traverser
// entierement la branche visee (comportement identique a une ligne
// degeneree -- 1 seul morceau renvoye), trop long retrouve le probleme que
// cette variante existe pour eviter.
[[nodiscard]] Result<std::vector<PathSet>>
cut_path_set_bounded(const PathSet& region, Vec2um a, Vec2um b, double reach_um,
                     Micrometers cut_width = Micrometers{20});

} // namespace openstitch::geometry
