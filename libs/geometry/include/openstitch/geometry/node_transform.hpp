// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "openstitch/core/units.hpp"
#include "openstitch/geometry/path.hpp"

namespace openstitch::geometry {

// Transformation groupée de nœuds (HP-VEC-017). Fonctions pures, Qt-free, en micromètres
// entiers, déterministes.

// Similitude autour de `pivot` (mise à l'échelle uniforme puis rotation), suivie d'une
// translation : p' = pivot + scale·R(rotation)·(p − pivot) + translation.
struct NodeTransform {
    Vec2um pivot{};
    double scale{1.0};
    double rotation_rad{0.0};
    Vec2um translation{};

    [[nodiscard]] bool is_identity() const {
        return scale == 1.0 && rotation_rad == 0.0 && translation == Vec2um{};
    }
};

// Applique `transform` aux nœuds `selected` (indices dans `path.nodes`, doublons et indices
// hors bornes ignorés) : position et tangentes (la partie échelle/rotation seulement)
// suivent, les relations géométriques entre nœuds sélectionnés sont conservées. Nombre de
// nœuds, types et tracé ouvert/fermé inchangés.
//
// `soft_radius_um` > 0 : transition adoucie. Chaque nœud NON sélectionné situé à moins de ce
// rayon d'un nœud sélectionné suit le déplacement de son plus proche nœud sélectionné, pondéré
// par un lissage cubique (1 au contact, 0 au rayon). Ses tangentes ne changent pas.
// Sélection vide ou transformation identité : chemin inchangé.
[[nodiscard]] Path transform_nodes(const Path& path, const std::vector<std::size_t>& selected,
                                   const NodeTransform& transform, double soft_radius_um = 0.0);

// Même opération sur le chemin `path_index` (0 = extérieur, i+1 = trou i) d'une région. `nullopt`
// si l'index est invalide ou si le résultat INTRODUIT un croisement que l'original n'avait pas.
[[nodiscard]] std::optional<PathSet>
transform_path_set_nodes(const PathSet& set, std::size_t path_index,
                         const std::vector<std::size_t>& selected, const NodeTransform& transform,
                         double soft_radius_um, Micrometers crossing_tolerance);

// Indices des nœuds de `path` dont la position est dans le rectangle [min, max] (inclus).
[[nodiscard]] std::vector<std::size_t> nodes_in_rect(const Path& path, Vec2um min, Vec2um max);

} // namespace openstitch::geometry
