// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::document {

// Objet vectoriel éditable, issu (en général) d'une région segmentée.
// C'est la matière première des futurs objets de broderie : la géométrie
// reste éditable, les points seront générés à partir d'elle (Phase 6).
struct VectorObject {
    ObjectId id;
    std::string name;
    std::optional<RegionId> source_region;  // lien conservé vers la région d'origine
    std::array<std::uint8_t, 3> rgb{};
    std::vector<geometry::PathSet> paths;   // plusieurs morceaux possibles
    bool visible{true};
};

// Adresse d'un nœud dans un objet : morceau, chemin (0 = extérieur,
// i+1 = trou i), index du nœud.
struct NodeRef {
    std::size_t set{0};
    std::size_t path{0};
    std::size_t node{0};
};

// Accès au chemin désigné par (set, path) ; nullptr si hors bornes.
[[nodiscard]] geometry::Path* path_in(VectorObject& object, std::size_t set, std::size_t path);
[[nodiscard]] const geometry::Path* path_in(const VectorObject& object, std::size_t set,
                                            std::size_t path);

}  // namespace openstitch::document
