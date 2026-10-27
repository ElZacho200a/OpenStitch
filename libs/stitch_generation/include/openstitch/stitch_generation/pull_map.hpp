// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/pull_field.hpp"

namespace openstitch::stitch_generation {

// Prévisualisation du tirage du tissu (niveau 3) : deux lectures du MÊME champ `PullField`.
//   - `compute_pull_map` : carte de déplacement prédit (où et de combien le tissu bouge) ;
//   - `simulate_fabric_pull` : la séquence « telle que le tissu la restituera » (chaque
//     pénétration décalée du déplacement prédit), pour voir les trous entre couleurs.
// Lecture seule, déterministe, jamais stocké dans le document. Les coefficients sont empiriques
// tant que `FabricResponse::calibrated` est faux : c'est un indicateur, pas une mesure.

// Segments cousus consécutifs d'un même objet (jamais à travers saut, coupe, déplacement caché ni
// verrou) : sources du champ, partagées avec `apply_predicted_pull`.
[[nodiscard]] std::vector<PullSource> collect_pull_sources(const stitch::StitchSequence& sequence);

// Réglages nommés, EMPIRIQUES (non validés sur machine) : « default », « linen » (lin lâche, tirage
// plus fort et plus souple en trame), « stable » (tissu bien stabilisé, tirage réduit). Un nom
// inconnu rend « default ».
[[nodiscard]] FabricResponse fabric_preset(std::string_view name);
[[nodiscard]] const std::vector<std::string_view>& fabric_preset_names();

struct PullMap {
    Vec2um origin{};     // coin bas-gauche de la case (0, 0)
    Micrometers cell{0}; // côté de case
    std::size_t columns{0};
    std::size_t rows{0};
    std::vector<PullSample> samples; // columns * rows, ligne par ligne, Y vers le haut

    [[nodiscard]] bool empty() const { return columns == 0 || rows == 0; }
    [[nodiscard]] const PullSample& at(std::size_t col, std::size_t row) const {
        return samples[row * columns + col];
    }
};

struct PullPeak {
    bool found{false};
    Vec2um position{};
    Vec2um displacement{};
    double magnitude_mm{0.0};
};

// Échantillonne le champ au centre de chaque case, sur l'emprise des points cousus élargie de la
// portée du champ. Le côté de case est augmenté si la grille dépasserait 512 x 512 cases.
[[nodiscard]] PullMap compute_pull_map(const stitch::StitchSequence& sequence,
                                       const FabricResponse& fabric, Micrometers cell);

[[nodiscard]] PullPeak find_pull_peak(const PullMap& map);

// Image RGBA de la carte (une case = un pixel), ligne 0 = haut (Y le plus grand). Palette froide ->
// chaude sur la norme du déplacement, échelle `scale_mm` au sommet ; les cases sans influence sont
// transparentes. `scale_mm` <= 0 : le déplacement maximal de la carte (au moins 0,1 mm).
struct PullRaster {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba;
    double scale_mm{0.1};
};
[[nodiscard]] PullRaster rasterize_pull_map(const PullMap& map, double scale_mm = 0.0);

// Copie de la séquence telle que le tissu la restituera : une pénétration cousue dans un bloc de
// couleur est décalée du déplacement que les blocs cousus AVANT elle ont déjà imposé au tissu (les
// positions d'origine servent de sources). C'est ce défaut de repérage entre couleurs (contour noir
// qui ne retombe plus sur le remplissage) que l'on veut voir : le premier bloc reste en place, les
// suivants suivent le tissu déformé. Sans segment cousu ni changement de couleur, la séquence est
// rendue inchangée.
[[nodiscard]] stitch::StitchSequence simulate_fabric_pull(const stitch::StitchSequence& sequence,
                                                          const FabricResponse& fabric);

} // namespace openstitch::stitch_generation
