// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/autodigitize/contour_objects.hpp"
#include "openstitch/autodigitize/contour_options.hpp"
#include "openstitch/image/image.hpp"

namespace openstitch::autodigitize {

// Couche de traits sombres (HP-AUTO-005) : dessin au trait d'une illustration (contours noirs,
// mâts, haubans) cousu PAR-DESSUS les remplissages.
//
// Sans elle, un trait sombre est une région comme une autre : il fusionne avec les aplats sombres
// voisins et devient un amas de blocs. Ici, les parties plus étroites que `max_line_width_mm` de
// l'ensemble des pixels SOMBRES de l'image (lues sur l'image, où le trait reste continu) sont
// retirées de la segmentation des remplissages (rendues aux régions voisines, donc les
// remplissages passent dessous, sans trou) et cousues séparément, en dernier, comme lignes
// médianes (même analyse et mêmes garde-fous que le mode contours), d'une seule couleur : la
// moyenne des pixels de trait.
struct LineLayerOptions {
    bool enabled{false};
    // Une partie de région plus étroite que cette largeur est un trait.
    double max_line_width_mm{1.6};
    // Un pixel est « sombre » si sa clarté CIELAB L* est au plus égale à cette valeur.
    double dark_max_lightness{25.0};
    // Ne coudre que les traits qui sont de VRAIS objets (haubans, mâts, cordages : leur voisinage
    // est une seule zone de remplissage). Un trait dont le voisinage contient au moins deux zones
    // de couleurs différentes est une SÉPARATION (contour entre deux aplats) : il n'est pas cousu,
    // les deux zones se rejoignent. Le contour entre une zone et le fond ignoré reste un trait
    // (contour du motif). Désactivé par défaut (comportement historique : tout trait sombre est
    // cousu).
    bool objects_only{false};
};

struct LineLayerMetrics {
    std::size_t line_pixels{0};       // pixels retirés des remplissages
    std::size_t reassigned_pixels{0}; // dont rendus à une région voisine
    ContourMetrics contours;          // analyse des traits (composantes, segments, longueur)
    std::size_t line_objects{0};
    std::size_t separation_pixels{0}; // pixels de séparation non cousus (`objects_only`)
};

// Segmentation -> objets : remplissages (`auto_digitize`) PUIS traits. Les objets de traits sont
// ajoutés à la suite : ils sont cousus après tous les remplissages. `options.enabled` faux :
// strictement `auto_digitize`. Déterministe.
[[nodiscard]] Result<AutoResult>
auto_digitize_with_lines(const segmentation::Segmentation& seg, const image::Image& source,
                         IdGenerator<ObjectId>& ids, const AutoOptions& options,
                         const LineLayerOptions& lines, const ContourOptions& contour_options,
                         LineLayerMetrics* metrics = nullptr);

} // namespace openstitch::autodigitize
