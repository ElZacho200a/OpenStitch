// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/core/units.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/stipple/tour.hpp"

namespace openstitch::stipple {

// Carte de densité : 0 = rien à coudre, 1 = le plus dense. Grille régulière,
// éventuellement sous-échantillonnée par rapport à l'image d'origine.
struct DensityMap {
    int width{0};
    int height{0};
    double scale{1.0};         // pixels d'image par pixel de carte
    std::vector<float> values; // width * height, ligne par ligne

    [[nodiscard]] float at(int x, int y) const {
        return values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }
};

struct DensityOptions {
    // Dimension maximale de la carte (le calcul de Voronoï est en O(pixels)).
    int max_dimension{600};
    // Exposant appliqué à (1 - luminance) : > 1 accentue le contraste.
    double gamma{1.0};
    // Vrai : les zones CLAIRES reçoivent le plus de points (fond sombre, fil clair).
    bool invert{false};
};

// Luminance (Rec. 601) -> densité = (1 - L)^gamma. Les pixels d'alpha < 128
// valent 0. Moyenne par blocs pour le sous-échantillonnage. Déterministe.
[[nodiscard]] Result<DensityMap> density_from_image(const image::Image& img,
                                                    const DensityOptions& options = {});

struct StippleOptions {
    int point_count{2000}; // 1..200000
    int iterations{30};    // relaxations de Lloyd pondérées (0..200)
    std::uint32_t seed{1}; // graine du tirage initial (générateur interne portable)
    TourOptions tour{};
};

struct StippleResult {
    std::vector<PointD> points; // en pixels de la CARTE, origine en haut à gauche
    int lloyd_iterations{0};    // relaxations effectivement faites (arrêt anticipé possible)
    double last_move{0.0};      // déplacement moyen au dernier tour (px de carte)
};

// Points de stippling de Secord (« Weighted Voronoi Stippling », NPAR 2002) :
// tirage initial par rejet selon la densité, puis relaxations de Lloyd où
// chaque point va au centroïde, PONDÉRÉ par la densité, de sa cellule de
// Voronoï (calculée exactement sur la grille). Le résultat est un semis dense
// là où la carte est sombre, régulier (bruit bleu) partout. Déterministe pour
// une graine donnée, indépendamment de la plateforme.
[[nodiscard]] Result<StippleResult> stipple_points(const DensityMap& density,
                                                   const StippleOptions& options);

// Chemin de couture continu : points du semis ordonnés par un tour TSP, coupé à
// l'arête la plus longue (le tour fermé devient un chemin ouvert) puis aux liaisons
// plus longues que `max_link` (aucun trait traversant un espace vide).
struct StitchPath {
    std::vector<std::vector<Vec2um>> polylines; // µm, Y vers le haut, origine au centre
    double tour_length_px{0.0};
    double initial_tour_length_px{0.0};
};

struct PathOptions {
    Millimeters mm_per_px{25.4 / 96.0}; // de l'image d'origine
    Micrometers max_link{8'000};        // 0 : ne coupe jamais
};

// Tour + conversion en coordonnées du modèle. `image_width/height` sont ceux de
// l'image d'origine (le centre de l'image est l'origine).
[[nodiscard]] StitchPath build_stitch_path(const StippleResult& stipple, const DensityMap& density,
                                           int image_width, int image_height,
                                           const PathOptions& path, const TourOptions& tour);

} // namespace openstitch::stipple
