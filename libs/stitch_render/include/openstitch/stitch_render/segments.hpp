// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::stitch_render {

// Un brin de fil entre deux pénétrations d'aiguille consécutives. Repère
// « écran » en millimètres : Y vers le BAS (la conversion depuis le repère du
// modèle, Y vers le haut, est faite ici une fois pour toutes).
struct ThreadSegment {
    float ax{0};
    float ay{0};
    float bx{0};
    float by{0};
    std::array<std::uint8_t, 3> rgb{};

    bool operator==(const ThreadSegment&) const = default;
};

// Couleur de fil d'un objet source, ou nullopt si l'objet est masqué (filtre
// d'affichage) : ses points ne sont alors pas dessinés.
using ThreadColorFn = std::function<std::optional<std::array<std::uint8_t, 3>>(ObjectId)>;

// Brins cousus de `sequence` (donc de la séquence EFFECTIVE du projet, jamais
// la séquence brute), dans l'ordre de couture -- l'ordre est celui de
// l'empilement du rendu. Seules les commandes d'indice <= `last_index`
// comptent (simulation) ; passer `sequence.commands.size()` pour tout prendre.
// Deux points ne sont reliés que s'ils appartiennent au même objet ; un
// changement de couleur, une coupe, un arrêt ou la fin rompent la continuité.
[[nodiscard]] std::vector<ThreadSegment>
build_thread_segments(const stitch::StitchSequence& sequence, std::size_t last_index,
                      const ThreadColorFn& color_of);

struct RectMm {
    double x0{0};
    double y0{0};
    double x1{0};
    double y1{0};

    [[nodiscard]] double width() const { return x1 - x0; }
    [[nodiscard]] double height() const { return y1 - y0; }
    [[nodiscard]] bool empty() const { return !(x1 > x0) || !(y1 > y0); }
    [[nodiscard]] bool contains(const RectMm& o) const {
        return o.x0 >= x0 && o.y0 >= y0 && o.x1 <= x1 && o.y1 <= y1;
    }

    bool operator==(const RectMm&) const = default;
};

// Boîte englobante des brins (rect vide si aucun brin).
[[nodiscard]] RectMm segments_bounds(const std::vector<ThreadSegment>& segments);

// Condensat stable (FNV-1a) des brins : clé de cache du rendu.
[[nodiscard]] std::uint64_t hash_segments(const std::vector<ThreadSegment>& segments);

} // namespace openstitch::stitch_render
