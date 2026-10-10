// SPDX-License-Identifier: Apache-2.0
// Tables de fils intégrées aux formats machine : palette PEC (Brother, 64 entrées) et palette
// JEF (Janome, 78 entrées). Usage INTERNE à libs/formats (jamais exposé hors de la lib).
// Sources et licences : docs/source/formats-pes-jef-exp.md et THIRD_PARTY_LICENSES.md.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace openstitch::formats::detail {

using Rgb = std::array<std::uint8_t, 3>;

inline constexpr int kPecPaletteSize = 64; // indices 1..64 (0 = inconnu)
inline constexpr int kJefPaletteSize = 78; // indices 1..78 (0 = arrêt machine)

// RGB de l'entrée `index` (1..N) ; absent hors plage.
[[nodiscard]] std::optional<Rgb> pec_rgb(int index) noexcept;
[[nodiscard]] std::optional<Rgb> jef_rgb(int index) noexcept;

// Entrée la plus proche (CIEDE2000, égalités -> plus petit indice). Si `avoid` est donné et
// que c'est l'entrée la plus proche, retourne la seconde (évite que deux fils différents
// consécutifs aient le même indice, ce que les machines/relecteurs lisent comme un arrêt).
[[nodiscard]] int nearest_pec_index(const Rgb& rgb, int avoid = 0);
[[nodiscard]] int nearest_jef_index(const Rgb& rgb, int avoid = 0);

} // namespace openstitch::formats::detail
