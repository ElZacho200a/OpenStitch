// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Remodelage interactif d'un contour (HP-VEC-016 : « déplacement direct d'un segment »).
// Fonctions pures, Qt-free, en micromètres entiers, déterministes.

// Segment le plus proche d'un point : `segment` est l'index du nœud de départ, `t` le
// paramètre de la courbe (Bézier) ou de la droite, `point` le point du tracé le plus proche.
struct SegmentHit {
    std::size_t segment{0};
    double t{0.0};
    double distance_um{0.0};
    Vec2um point{};
};

// Segment (droit ou cubique) du `path` le plus proche de `p`, à au plus `max_distance_um`.
// `nullopt` si aucun segment n'est assez proche ou si le chemin a moins de 2 nœuds.
[[nodiscard]] std::optional<SegmentHit> nearest_segment(const Path& path, Vec2um p,
                                                        double max_distance_um);

// « Tire » le point d'abscisse `t` du segment `segment` de `delta`, sans toucher aux nœuds
// d'extrémité : seules les poignées de Bézier des deux extrémités du segment bougent
// (pondérées par la base de Bernstein pour que le point saisi suive le curseur, `t` borné à
// [0,15 ; 0,85] pour ne pas exploser près d'une extrémité). Un segment droit devient une
// cubique. Les nœuds Lisse gardent leur tangente continue (la poignée opposée pivote,
// longueur conservée) ; les nœuds Coin gardent leur angle. Le nombre de nœuds ne change pas.
// `segment` hors bornes ou chemin à moins de 2 nœuds : chemin inchangé.
[[nodiscard]] Path reshape_segment(const Path& path, std::size_t segment, double t, Vec2um delta);

// Vrai si le contour (aplati à `tolerance`) se coupe lui-même (croisement transversal de
// deux arêtes non adjacentes).
[[nodiscard]] bool path_self_intersects(const Path& path, Micrometers tolerance);

// Vrai si la région comporte un croisement : auto-intersection d'un contour, ou croisement
// entre le contour extérieur et un trou / entre deux trous.
[[nodiscard]] bool path_set_has_crossings(const PathSet& set, Micrometers tolerance);

// Remodèle le segment `segment` du chemin `path_index` (0 = extérieur, i+1 = trou i) de `set`
// (voir `reshape_segment`). `nullopt` si les index sont invalides ou si le résultat
// INTRODUIT un croisement que la forme d'origine n'avait pas (un contour déjà invalide reste
// éditable). Le reste de la région est inchangé.
[[nodiscard]] std::optional<PathSet> reshape_path_set_segment(const PathSet& set,
                                                              std::size_t path_index,
                                                              std::size_t segment, double t,
                                                              Vec2um delta, Micrometers tolerance);

// Variante « translater l'arête » (Alt) : les DEUX nœuds du segment `segment` se déplacent de
// `delta` avec leurs poignées (tangentes relatives, donc inchangées) ; le reste du contour reste
// en place. Sur un tracé ouvert, les extrémités restent déplaçables. `segment` hors bornes ou
// chemin à moins de 2 nœuds : chemin inchangé.
[[nodiscard]] Path translate_segment(const Path& path, std::size_t segment, Vec2um delta);

// Comme `reshape_path_set_segment` pour `translate_segment` (même refus des croisements nouveaux).
[[nodiscard]] std::optional<PathSet> translate_path_set_segment(const PathSet& set,
                                                                std::size_t path_index,
                                                                std::size_t segment, Vec2um delta,
                                                                Micrometers tolerance);

} // namespace openstitch::geometry
