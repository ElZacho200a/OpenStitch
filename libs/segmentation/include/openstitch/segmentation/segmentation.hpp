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

} // namespace openstitch::segmentation
