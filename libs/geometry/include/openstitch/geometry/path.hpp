// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"

namespace openstitch::geometry {

enum class NodeType : std::uint8_t {
    Corner,  // point anguleux
    Smooth,  // point lisse (tangentes liées) — exploité quand les courbes arriveront
};

// Nœud éditable d'un chemin. Les tangentes (Bézier) sont optionnelles :
// absentes, le segment vers le nœud suivant est une droite.
struct PathNode {
    Vec2um pos{};
    NodeType type{NodeType::Corner};
    std::optional<Vec2um> tan_in;   // relatif à pos
    std::optional<Vec2um> tan_out;  // relatif à pos

    constexpr bool operator==(const PathNode& o) const {
        return pos == o.pos && type == o.type && tan_in == o.tan_in && tan_out == o.tan_out;
    }
};

// Chemin : polyligne ou courbe fermée/ouverte.
struct Path {
    std::vector<PathNode> nodes;
    bool closed{true};

    bool operator==(const Path&) const = default;
};

// Région vectorielle : un contour extérieur et ses trous.
struct PathSet {
    Path outer;
    std::vector<Path> holes;

    bool operator==(const PathSet&) const = default;
};

// Aire signée (en µm², via double : l'aire déborde int64 pour de grandes
// formes). Positive si le chemin tourne en sens antihoraire dans le repère
// Y vers le haut.
[[nodiscard]] double signed_area_um2(const Path& path);

// Insère un nouveau nœud sur le segment [segment_index, segment_index+1]
// (ou [n-1, 0] si `path.closed`) à l'abscisse curviligne locale t (dans
// [0,1], clampé). Si le segment porte des tangentes (cubique), la
// subdivision est EXACTE (De Casteljau) : les deux moitiés recousent une
// courbe identique au segment d'origine, jamais une approximation — le
// nouveau nœud est alors Lisse (tangente-continu par construction). Segment
// sans tangente : insertion linéaire, nœud Coin. `segment_index` hors bornes
// (ou chemin à moins de 2 nœuds) renvoie `path` inchangé.
[[nodiscard]] Path insert_node_on_segment(const Path& path, std::size_t segment_index, double t);

}  // namespace openstitch::geometry
