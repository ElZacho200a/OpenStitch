// SPDX-License-Identifier: Apache-2.0
// En-tête interne à libs/auto_satin : traçage de corridor par pieds de bord
// les plus proches (HP-STI-018, Phase B, specs/plans/hp-sti-018-turning-satin.md
// §2.2). Remplace, STATION PAR STATION, la mesure historique de
// `compute_column_stations` (une normale, un rayon, une intersection unique
// via `cross_section`) par une marche le long de l'axe interrogeant
// `nearest_boundary_feet` (medial_field.hpp) à chaque échantillon.
//
// Phase B seulement : câblé derrière un indicateur TEST/DEV UNIQUEMENT
// (`SatinColumnsParameters::use_corridor_tracing_dev_only`, jamais exposé en
// CLI ni IHM -- voir ce champ dans satin_column.hpp) à l'intérieur de
// `compute_column_stations` (satin_column.cpp), qui convertit chaque
// `CorridorStation` en `Station` interne avant de poursuivre le pipeline
// EXISTANT inchangé (nettoyage anti-croisement, `trim_unstable_junction_tail`,
// `extend_tip`, validations de couverture). Ne remplace PAS encore
// `trim_unstable_junction_tail`/`resolve_junction` (ça, c'est la Phase C,
// `find_stable_corridor_end`) : ce fichier ne produit que la mesure dense,
// rien de plus.
#pragma once

#include "geometry_detail.hpp"
#include "medial_field.hpp"
#include "openstitch/auto_satin/satin_column.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openstitch::auto_satin::detail {

// Une station dense du corridor : la contrepartie de `Station` (interne à
// satin_column.cpp) mais mesurée par pieds de bord les plus proches plutôt
// que par ray-cast. `foot_a`/`foot_b` remplacent `rail_a_point`/`rail_b_point`
// d'aujourd'hui -- `foot_a` est le pied du côté +N (gauche du sens de
// parcours, même convention que `Station::railA` : normale = tangente
// tournée de +90°), `foot_b` celui du côté -N (droite). Jamais choisis par un
// test gauche/droite recalculé à chaque station une fois qu'une station
// précédente existe : cf. `trace_corridor`.
struct CorridorStation {
    P2 axis_point;
    BoundaryFoot foot_a;
    BoundaryFoot foot_b;
    P2 tangent;
    double width_um{0.0};
    // Nombre de pieds de bord DISTINCTS (non coïncidents EN POSITION, pas le
    // compte brut du vecteur renvoyé par `nearest_boundary_feet`) à cette
    // station -- un simple sommet ordinaire de polygone peut à lui seul
    // contribuer 2 entrées à égalité sans qu'il s'agisse d'une vraie
    // jonction, cf. l'avertissement de medial_field.hpp ; ce champ applique
    // déjà le filtre de non-coïncidence, ce n'est PAS `feet.size()` brut.
    // 2 = corridor ordinaire ; >= 3 = un emplacement de contour
    // supplémentaire, géométriquement distinct, est à portée (signal de
    // proximité de jonction, exploité en Phase C par
    // `find_stable_corridor_end`).
    int foot_multiplicity{2};
    // true si cette station n'a pas pu être mesurée directement (aucun pied
    // de bord exploitable à ce point d'axe -- polygones vides/dégénérés,
    // cas limite qui ne devrait pas survenir sur une région valide) et a donc
    // été comblée depuis la station précédente plutôt que réellement
    // mesurée. Permet à `trace_corridor` de ne jamais échouer ni produire un
    // vecteur de taille différente de `axis.size()` (cf. ci-dessous),
    // contrairement à `cross_section`/`CrossSectionFailure`.
    bool interpolated{false};
};

// Marche le long de `axis` (polyligne déjà lissée/rééchantillonnée par
// l'appelant -- même préparation que `compute_column_stations` : Chaikin puis
// `resample_arc`, AVANT cet appel, jamais refaite ici) et calcule une
// `CorridorStation` par point d'axe.
//
// À chaque station, interroge `nearest_boundary_feet` DEUX FOIS, avec deux
// `FootQuery` différents -- décision prise en cours de Phase B, après qu'une
// requête unique (le `FootQuery` par défaut, max_feet=3, tolerance_relative=
// 2%) ait fait régresser des formes à branche unique SANS jonction
// (`ribbon`/`s`/`notch` du corpus `shapes.cpp`) : un axe réel (centerline
// issue du squelette aminci, jamais une ligne médiane mathématique exacte)
// n'est quasiment jamais parfaitement équidistant des deux côtés, et le
// filtre de tolérance à 2% (conçu dans medial_field.hpp pour détecter une
// PROXIMITÉ DE JONCTION, pas pour mesurer une largeur) excluait alors le côté
// le plus éloigné même quand il restait le bon pied à mesurer -- plusieurs
// stations consécutives perdaient un côté, et la colonne entière finissait
// refusée. Les deux besoins sont donc découplés :
//   - une requête LARGE (max_feet=6, tolerance_relative=100% -- garde tout
//     candidat jusqu'à 2x la distance minimale) pour la SÉLECTION de
//     `foot_a`/`foot_b` ;
//   - le `FootQuery` par DÉFAUT (max_feet=3, tolerance_relative=2%,
//     justification inchangée : 2 pieds = corridor ordinaire, un 3e qui
//     arrive à portée = contact avec un troisième emplacement de contour)
//     pour `foot_multiplicity` SEULEMENT -- c'est là, et seulement là,
//     qu'un filtre serré a un sens.
//
// Sélection de `foot_a`/`foot_b` parmi les candidats retournés, PAR
// CONTINUITÉ avec la station précédente plutôt que par un test gauche/droite
// recalculé à chaque station (un ray-cast -- ou une sélection naïve par plus
// proche -- peut changer de bord d'une station à l'autre près d'un coin sans
// qu'il s'agisse d'un vrai changement de branche) :
//   - pour `foot_a` (resp. `foot_b`) : cherche, parmi les candidats non
//     encore attribués, celui du MÊME polygone que `previous.foot_a` (resp.
//     `foot_b`) dont l'arête est identique OU immédiatement adjacente
//     (distance circulaire <= 1 sur l'anneau d'arêtes du polygone) -- les
//     candidats étant déjà triés par distance croissante, le premier trouvé
//     est le plus proche remplissant ce critère ;
//   - à défaut (aucun candidat continu -- coin franchi trop vite, ou
//     changement réel de bord), repli sur un partage gauche/droite de la
//     tangente locale (`dot(candidat - axis_point, normale) >= 0` = gauche)
//     parmi les candidats encore libres ;
//   - à défaut encore (aucun candidat du bon côté -- cas dégénéré), prend le
//     candidat libre restant le plus proche, quel que soit son côté, plutôt
//     que de laisser le pied vide.
// PREMIÈRE station d'une branche (pas de précédente) : saute directement au
// partage gauche/droite ci-dessus.
//
// `tangent` : différence centrée le long de `axis`, MÊME convention que
// `compute_column_stations` aujourd'hui (`axis[i+1] - axis[i-1]`, bornes en
// tête/queue à sens unique), normalisée. `width_um` = distance entre
// `foot_a.point` et `foot_b.point`.
//
// Ne peut jamais échouer (contrairement à `cross_section`) : une station sans
// aucun pied exploitable (feet.size() < 2 après sélection) est comblée depuis
// la station précédente et marquée `interpolated` -- `trace_corridor(axis,
// polys, params).size() == axis.size()` est TOUJOURS vrai, invariant exploité
// par l'appelant (§ câblage test-only dans `compute_column_stations`,
// satin_column.cpp) qui n'a donc pas besoin de réconcilier des trous d'indice
// entre `axis` et le résultat.
[[nodiscard]] std::vector<CorridorStation> trace_corridor(const std::vector<P2>& axis,
                                                          const std::vector<Poly>& polys,
                                                          const SatinColumnsParameters& params);

// --- Phase C (HP-STI-018, §2.2/§2.6) : critere de stabilite structurel ------
//
// Remplace `trim_unstable_junction_tail`'s width-drift/plateau heuristic (a
// quantity measured AFTER the fact, on an already-ray-cast station) with a
// single structural criterion read directly off `foot_multiplicity` : walking
// INWARD from a junction-node end of a branch's dense station sequence, the
// StableCorridorEnd is the first station whose `foot_multiplicity == 2` AND
// whose next `junction_stability_margin_stations` consecutive stations
// (further inward still) are ALSO `foot_multiplicity == 2`. A station still
// "contaminated" by a neighbouring branch necessarily shows multiplicity >= 3
// at that radius -- true BY CONSTRUCTION of `nearest_boundary_feet` (a third,
// geometrically distinct contour location is within tolerance), not by
// inference -- so a StableCorridorEnd can never be geometrically inside a
// neighbour's corridor the way a width-stable-but-mislocated `cross_section`
// station could (the exact historical bug on fixture "t",
// docs/source/satin.md:690-703).
//
// `edge_id` is NOT computed here (a plain station sequence carries no
// `SkeletonEdge` identity) -- it is always default (0) in the returned
// `CorridorEnd`; a caller that needs it (e.g. a future `describe_junctions`,
// Phase E) fills it in itself from the branch it already knows it is
// processing.
struct CorridorEnd {
    std::uint32_t edge_id{0};
    bool at_end{false};
    CorridorStation station;
};

// Index-returning form of the criterion above, for a caller that must know
// WHERE to cut `stations` (e.g. `compute_column_stations`, which must decide
// which axis samples to even attempt measuring before any `Station` exists --
// see satin_column.cpp) rather than just the station's own geometry.
// `atEnd == true` walks from `stations.back()` inward (decreasing index),
// `atEnd == false` from `stations.front()` inward (increasing index).
//
// Degenerate fallback (no station satisfies the margin -- an entire branch
// contaminated, or too short to even test `junction_stability_margin_stations`
// stations of margin): returns the END index itself (`stations.size() - 1`
// for `atEnd`, `0` otherwise), i.e. NO trim at all -- same conservative floor
// as `trim_unstable_junction_tail`'s own `st.size() >= 3` guard, never empties
// a branch out from under its caller. Returns 0 if `stations` is empty
// (caller must already guard against an empty branch before reaching here).
[[nodiscard]] std::size_t
find_stable_corridor_end_index(const std::vector<CorridorStation>& stations, bool atEnd,
                               const SatinColumnsParameters& params);

// Value-returning form, for direct callers/tests (§5 of the plan: explicit
// `t`/`trident` fixtures asserting the thin branch's `CorridorEnd` lies
// outside the wide branch's rail-to-rail span). Thin wrapper over
// `find_stable_corridor_end_index`.
[[nodiscard]] CorridorEnd find_stable_corridor_end(const std::vector<CorridorStation>& stations,
                                                   bool atEnd,
                                                   const SatinColumnsParameters& params);

} // namespace openstitch::auto_satin::detail
