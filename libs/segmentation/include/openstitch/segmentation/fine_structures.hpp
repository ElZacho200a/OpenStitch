// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/image/image.hpp"

namespace openstitch::segmentation {

// SEG-FINE-01 -- détection raster INDÉPENDANTE des structures fines (traits, nervures, cordages,
// petits lettrages), lue sur l'image NON quantifiée : un trait anti-crénelé et peu contrasté est
// fragmenté ou englouti par la quantification k-means, alors qu'il reste continu dans l'image.
//
// Pipeline : chapeaux haut-de-forme noir ET blanc (L*, a*, b*) à plusieurs diamètres de noyau ;
// validation « crête » (un trait est différent de ses deux côtés, une simple frontière ne l'est que
// d'un) ; hystérésis en 8-connexité (germes au seuil haut, propagation au seuil bas) ; rejet des
// composantes trop courtes. Aucune géométrie vectorielle n'est produite : uniquement des masques
// raster. Déterministe (parallélisme par lignes, aucune dépendance à l'ordre des fils).
struct FineStructureOptions {
    // Largeur maximale (pixels) d'un trait : les diamètres de noyau sont 3, 5, 9, 17... jusqu'au
    // premier diamètre >= max_width_px + 2. 1..32.
    int max_width_px{8};
    // Contraste local minimal (unités CIELAB, ΔE76) d'un pixel candidat (seuil bas, propagation) et
    // d'un pixel germe (seuil haut). 0 < low <= high.
    double low_contrast{10.0};
    double high_contrast{18.0};
    // Une composante (8-connexe) dont la plus grande dimension de boîte englobante est inférieure à
    // cette longueur (pixels) n'est pas un trait (poussière, grain JPEG).
    int min_length_px{6};
    // Pour qu'un pixel soit une crête en couleur : écart du centre aux deux côtés >= contraste/2
    // et écart des deux côtés entre eux <= `ridge_side_ratio` x (somme des deux écarts au centre).
    // Une rampe de dégradé (écart côtés = somme des écarts) est ainsi écartée.
    double ridge_side_ratio{0.75};
    // Couleurs supplémentaires (au plus) que `segment()` peut réserver aux traits dont la couleur
    // n'est proche d'aucun centre de la palette (couleur rare). 0 = aucune.
    int max_extra_colors{3};
    // Écart CIELAB (ΔE76) au centre le plus proche à partir duquel la couleur d'un pixel protégé
    // est jugée « rare » (absente de la palette).
    double rare_color_delta_e{14.0};
};

// Résultat de la détection : tous les tableaux ont width*height éléments, ligne par ligne.
struct FineStructureMap {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> mask;       // 1 = pixel de trait fin
    std::vector<std::uint8_t> confidence; // 0..255 : contraste / (2 x seuil haut), saturé
    // Borne haute de la largeur du trait : diamètre du plus petit noyau qui répond, moins 1 ;
    // 0 = hors masque.
    std::vector<std::uint8_t> width_px;
    std::size_t pixel_count{0};     // pixels du masque
    std::size_t seed_count{0};      // germes (seuil haut) retenus
    std::size_t component_count{0}; // composantes 8-connexes retenues
    std::size_t rejected_short{0};  // composantes rejetées pour longueur insuffisante

    [[nodiscard]] bool empty() const { return pixel_count == 0; }
};

// Détecte les structures fines de `img`. Refuse (UserInput) une image vide ou des options hors
// bornes. Les pixels d'alpha < 128 ne sont jamais des traits et ne servent pas de côté de crête.
[[nodiscard]] Result<FineStructureMap> detect_fine_structures(const image::Image& img,
                                                              const FineStructureOptions& options);

} // namespace openstitch::segmentation
