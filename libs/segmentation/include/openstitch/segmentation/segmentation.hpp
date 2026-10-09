// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/ids.hpp"
#include "openstitch/image/image.hpp"

namespace openstitch::segmentation {

// Une région connexe issue de la quantification. Deux régions distinctes
// restent distinctes même si elles partagent la même couleur (§4.3).
struct Region {
    RegionId id;
    std::array<std::uint8_t, 3> rgb{}; // couleur représentative
    std::size_t pixel_count{0};
};

// Carte de régions d'une image.
// - labels[y*width+x] : 0 = fond (pixels transparents), sinon slot+1 ;
// - region_slots[slot] : la région, ou nullopt si elle a été fusionnée/supprimée.
// Les region_slots ne sont JAMAIS réutilisés : RegionId (= slot+1) est stable
// pendant toute la vie de la segmentation.
struct Segmentation {
    int width{0};
    int height{0};
    std::vector<std::uint32_t> labels;
    std::vector<std::optional<Region>> region_slots;

    [[nodiscard]] const Region* find(RegionId id) const;
    [[nodiscard]] Region* find(RegionId id);
    [[nodiscard]] std::size_t region_count() const;
};

struct SegmentationOptions {
    int max_colors{8};     // 2..64
    int min_region_px{16}; // régions plus petites absorbées par leur voisine
    // Lissage optionnel des frontières : après l'affectation initiale
    // pixel-à-pixel (au plus proche centre Lab, intrinsèquement bruitée —
    // effet « poivre et sel » sur les photos/dégradés/artefacts JPEG),
    // chaque pixel opaque est réaffecté à la couleur majoritaire dans son
    // voisinage (vote local, fenêtre de côté 2*rayon+1). 0 = désactivé,
    // comportement strictement identique à avant (déterminisme des tests,
    // pipelines existants). Un rayon plus grand donne des formes plus
    // lisses mais efface les détails plus fins que lui.
    int smoothing_radius_px{0};
};

// Quantifie l'image en CIELAB (k-means déterministe) puis extrait les
// composantes connexes (4-connexité). Les pixels d'alpha < 128 forment le fond.
[[nodiscard]] Result<Segmentation> segment(const image::Image& img,
                                           const SegmentationOptions& options);

[[nodiscard]] std::optional<RegionId> region_at(const Segmentation& seg, int x, int y);

// Fusionne `absorb` dans `keep` (la couleur de `keep` est conservée).
// Renvoie les indices de pixels réétiquetés — nécessaires à l'annulation.
[[nodiscard]] Result<std::vector<std::uint32_t>> merge_regions(Segmentation& seg, RegionId keep,
                                                               RegionId absorb);

// Supprime une région en l'absorbant dans sa voisine majoritaire (choix
// déterministe), ou dans le fond si elle n'a aucune voisine.
// Renvoie {absorbeur (invalide = fond), indices réétiquetés}.
[[nodiscard]] Result<std::pair<RegionId, std::vector<std::uint32_t>>>
remove_region(Segmentation& seg, RegionId id);

// Change la couleur représentative. Renvoie l'ancienne couleur.
[[nodiscard]] Result<std::array<std::uint8_t, 3>> recolor_region(Segmentation& seg, RegionId id,
                                                                 std::array<std::uint8_t, 3> rgb);

// Clarté CIELAB L* (0 = noir, 100 = blanc, illuminant D65) d'une couleur
// sRGB 8 bits. Calcul analytique, sans OpenCV : réutilisable par tout
// appelant qui doit juger si une couleur est « claire ».
[[nodiscard]] double cielab_lightness(std::array<std::uint8_t, 3> rgb);

// Frontière partagée entre deux régions : `length` = nombre d'arêtes de
// pixels communes (4-connexité), donc une longueur en pixels. `a < b`.
struct RegionBorder {
    RegionId a;
    RegionId b;
    std::size_t length{0};

    bool operator==(const RegionBorder&) const = default;
};

// Toutes les frontières entre régions vivantes (le fond transparent, label
// 0, n'est jamais une région), triées par (a, b) -- déterministe. Base des
// règles de voisinage de l'auto-numérisation (angles, chevauchement,
// fusion des fragments).
[[nodiscard]] std::vector<RegionBorder> region_adjacency(const Segmentation& seg);

// Nettoyage des fragments avant auto-numérisation (Lot D, audit marine) --
// sur une COPIE de travail : les régions de l'utilisateur ne sont pas
// modifiées par ces fonctions quand l'appelant travaille sur une copie.
//
// Ouverture morphologique (noyau elliptique de `min_width_px` de diamètre)
// région par région : les isthmes et lamelles plus étroits que ce diamètre
// sont retirés de leur région et rendus, de proche en proche, à la région
// VOISINE majoritaire (4-connexité, égalité -> plus petit label). Une partie
// retirée qui ne touche aucune autre région (lamelle isolée dans le vide)
// reste à sa région d'origine. Une région entièrement retirée disparaît.
// `min_width_px` < 2 : sans effet. Renvoie le nombre de pixels réaffectés.
// Déterministe.
std::size_t remove_thin_parts(Segmentation& seg, int min_width_px);

// Fusionne chaque région de moins de `min_px` pixels avec la voisine qui
// partage la plus longue frontière (égalité -> plus petit identifiant), en
// traitant toujours la plus petite région restante d'abord ; la région
// absorbante garde sa couleur. Une région dont la couleur vaut `excluded`
// (fond ignoré) n'absorbe jamais : un fragment qui n'a pas d'autre voisine
// reste tel quel (isolé au milieu du fond). Renvoie le nombre de fusions.
// Déterministe.
//
// `boundary_weight` : la taille comparée à `min_px` est « pixels −
// boundary_weight × (longueur de frontière avec les autres régions − 2) ». La
// vectorisation trace les contours par les centres des pixels de bord : le
// polygone perd ~½ pixel par arête de frontière, et c'est son aire qui
// décide du type de point. 0,5 aligne donc le seuil sur l'aire vectorisée
// (audit marine : des régions de 3 mm² en pixels donnaient des contours de
// 2,5 mm²).
std::size_t merge_small_regions(Segmentation& seg, std::size_t min_px,
                                std::optional<std::array<std::uint8_t, 3>> excluded = {},
                                double boundary_weight = 0.0);

// Seuils de la recommandation « ignorer le fond » (§ Lot A, audit marine).
struct BackgroundCandidateOptions {
    double min_lightness{90.0}; // L* au-dessus duquel la couleur est jugée quasi blanche
    int min_sides_touched{3};   // nombre minimal de bords de l'image touchés (sur 4)
};

// Fond présumé d'une image opaque : la couleur de la plus grande région
// (même critère que `autodigitize::AutoOptions::skip_largest_region`, qui
// exclut ensuite TOUTES les régions de cette couleur exacte). Les mesures
// portent donc sur l'ensemble des régions de cette couleur, pas seulement
// sur le plus gros morceau.
struct BackgroundCandidate {
    RegionId region; // plus grande région (la candidate)
    std::array<std::uint8_t, 3> rgb{};
    double area_ratio{0.0}; // part de l'image (0..1) couverte par cette couleur
    double lightness{0.0};  // L* CIELAB de la couleur
    int sides_touched{0};   // bords de l'image touchés (0..4)
    // Vrai seulement pour un fond quasi blanc qui encadre le motif : sur une
    // image plein cadre, la plus grande région est un vrai élément du motif
    // (ciel, mer...) et ne doit JAMAIS être ignorée par défaut.
    bool recommended{false};
};

// Candidate au rôle de fond, ou nullopt si la segmentation est vide.
// Déterministe : à pixel_count égal, le plus petit identifiant l'emporte.
[[nodiscard]] std::optional<BackgroundCandidate>
background_candidate(const Segmentation& seg, const BackgroundCandidateOptions& options = {});

// Carte des régions en RGBA (fond transparent) ; la région `highlight`
// est éclaircie pour matérialiser la sélection.
[[nodiscard]] image::Image render_map(const Segmentation& seg,
                                      std::optional<RegionId> highlight = {});

// --- Aides de sélection et d'édition par groupes de régions (ergonomie de la segmentation) ---

// Toutes les régions vivantes, par identifiant croissant.
[[nodiscard]] std::vector<RegionId> all_regions(const Segmentation& seg);

// Régions vivantes dont la couleur représentative vaut exactement `rgb`, par identifiant
// croissant (« sélectionner tout ce qui a cette couleur »).
[[nodiscard]] std::vector<RegionId> regions_with_color(const Segmentation& seg,
                                                       std::array<std::uint8_t, 3> rgb);

// Voisines de `id` (4-connexité), de la frontière commune la plus longue à la plus courte
// (égalité -> plus petit identifiant). Le fond n'est jamais une voisine.
[[nodiscard]] std::vector<RegionId> neighbors_of(const Segmentation& seg, RegionId id);

// Régions ayant au moins un pixel dans le rectangle [x0, x1] × [y0, y1] (bornes incluses, les
// coordonnées sont ramenées à l'image), par identifiant croissant (sélection au cadre).
[[nodiscard]] std::vector<RegionId> regions_in_rect(const Segmentation& seg, int x0, int y0,
                                                    int x1, int y1);

// Couleur moyenne, dans `original` (image de la même taille que la segmentation), des pixels de
// la région : permet de rendre à une région recolorée sa couleur d'origine.
[[nodiscard]] Result<std::array<std::uint8_t, 3>>
region_mean_color(const Segmentation& seg, const image::Image& original, RegionId id);

// Comme `render_map`, avec plusieurs régions éclaircies (sélection multiple).
[[nodiscard]] image::Image render_map_multi(const Segmentation& seg,
                                            const std::vector<RegionId>& highlights);

} // namespace openstitch::segmentation
