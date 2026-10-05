// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::thread_palette {

// Registre en mémoire de tous les nuanciers compilés dans le binaire (C-S1-02
// : aucune E/S disque). Construit une seule fois, dans l'ordre textuel des
// appels aux factories de `catalog.cpp` — cet ordre est l'ordre observable,
// stable entre deux exécutions et entre plateformes (AD-S1-2).
[[nodiscard]] std::span<const ThreadChart> all_charts();

// Résout un nuancier par son identifiant stable. nullptr si inconnu.
[[nodiscard]] const ThreadChart* find_chart(std::string_view chart_id) noexcept;

// Résout un fil par nuancier + code fabricant. Optionnel vide si le nuancier
// ou le code sont inconnus.
[[nodiscard]] std::optional<Thread> find_by_code(std::string_view chart_id,
                                                 std::string_view code) noexcept;

// Recherche par sous-chaîne du nom, insensible à la casse, sur tous les
// nuanciers de `all_charts()` dans l'ordre du registre.
[[nodiscard]] std::vector<Thread> search_by_name(std::string_view needle) noexcept;

} // namespace openstitch::thread_palette
