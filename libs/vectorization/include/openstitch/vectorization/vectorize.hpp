// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/segmentation/segmentation.hpp"

namespace openstitch::vectorization {

struct VectorizeOptions {
    Millimeters mm_per_px{25.4 / 96.0};  // résolution de travail de l'image
    Micrometers simplify_tolerance{200}; // 0,2 mm : sous le pas machine (0,1 mm) x2
    // > 0 : les contours deviennent des COURBES de Bézier (ajustement de
    // Schneider avec détection des coins, `geometry::fit_curves`) au lieu de
    // polygones Douglas-Peucker ; `simplify_tolerance` est alors ignorée. L'écart
    // maximal au contour de pixels est `curve_tolerance` (bornée à périmètre/16
    // pour les petits trous). Prendre ~1,5 px ou plus : en dessous, l'escalier de
    // la trame empêche de réduire le nombre de nœuds. 0 (défaut) : polygones.
    Micrometers curve_tolerance{0};
};

// Transforme une région segmentée en contours vectoriels propres :
// extraction des contours et des trous (Suzuki-Abe via OpenCV), conversion
// en coordonnées physiques (µm, origine au centre de l'image, Y vers le
// haut), simplification Douglas-Peucker puis nettoyage Clipper2.
// Une région en plusieurs morceaux produit plusieurs PathSet.
[[nodiscard]] Result<std::vector<geometry::PathSet>>
vectorize_region(const segmentation::Segmentation& seg, RegionId id,
                 const VectorizeOptions& options);

// Variante coopérative : aucune géométrie partielle n’est retournée après annulation.
// Le contexte par défaut de l’API historique conserve exactement les mêmes résultats.
[[nodiscard]] Result<std::vector<geometry::PathSet>>
vectorize_region(const segmentation::Segmentation& seg, RegionId id,
                 const VectorizeOptions& options, const TaskContext& task);

// Vectorise plusieurs régions EN PARALLÈLE (`parallel_for`) : `out[i]` est le résultat de
// `vectorize_region(seg, ids[i], options)`, exactement. Chaque région est indépendante et la
// segmentation n'est que lue : le résultat ne dépend ni du nombre de fils ni de leur
// ordonnancement. À préférer à une boucle sur `vectorize_region` dès qu'il y a plusieurs régions.
[[nodiscard]] std::vector<Result<std::vector<geometry::PathSet>>>
vectorize_regions(const segmentation::Segmentation& seg, const std::vector<RegionId>& ids,
                  const VectorizeOptions& options);

// Annulation et progression par région terminée ; out[i] conserve l’ordre de ids.
[[nodiscard]] std::vector<Result<std::vector<geometry::PathSet>>>
vectorize_regions(const segmentation::Segmentation& seg, const std::vector<RegionId>& ids,
                  const VectorizeOptions& options, const TaskContext& task);

// Le contour `current` d'un objet n'est-il que le contour brut `raw` de sa région, épaissi par le
// recouvrement des tatamis voisins ? Vrai seulement si `current` est plus grand que `raw` (de
// plus de 3 %) ET si l'écart entre les deux tient dans une bande fine (`max_band` de large sur le
// périmètre de `raw`), dans les deux sens. Une fusion, une union ou une retouche de forme
// ajoute/retire une surface bien plus grande qu'une bande : le contour est alors voulu par
// l'utilisateur et ne doit JAMAIS être remplacé par celui de la région d'origine (défaut :
// « après fusion de deux zones, le satin revient à la forme première »).
[[nodiscard]] bool is_overlap_grown_contour(const std::vector<geometry::PathSet>& current,
                                            const std::vector<geometry::PathSet>& raw,
                                            Micrometers max_band = Micrometers{1250});

} // namespace openstitch::vectorization
