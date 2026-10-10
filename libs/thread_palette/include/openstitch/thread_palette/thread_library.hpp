// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/thread_palette/color_distance.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::thread_palette {

// Nuancier GÉNÉRIQUE sans marque (`chart_id == "generic"`) : une trentaine de
// couleurs usuelles (noms de couleurs courants, valeurs RGB choisies pour ce
// projet — aucune donnée de fabricant, donc libre de droits). Sert de
// nuancier par défaut ; les cartes de marques viennent de l'utilisateur
// (import CSV, voir chart_import.hpp).
[[nodiscard]] ThreadChart generic_basic_chart();

// Vrai pour un nuancier intégré dont les données sont des valeurs fictives de
// développement (source_note commençant par « DONNEES PLACEHOLDER »).
[[nodiscard]] bool is_demo_chart(const ThreadChart& chart) noexcept;

// Bibliothèque de fils active de l'application : nuanciers intégrés (générique
// d'abord, puis les nuanciers compilés de `all_charts()`) suivis des nuanciers
// importés par l'utilisateur, dans leur ordre d'ajout. Type valeur, sans E/S :
// l'application charge les fichiers utilisateur et appelle `add_chart`.
// Déterministe (aucun conteneur non ordonné).
class ThreadLibrary {
public:
    // Nuanciers intégrés uniquement.
    [[nodiscard]] static ThreadLibrary with_builtin();

    // Ajoute un nuancier utilisateur. Refuse un identifiant vide ou déjà
    // présent (jamais de remplacement silencieux d'un nuancier existant), un
    // nuancier sans fil, ou des codes en double.
    [[nodiscard]] Result<void> add_chart(ThreadChart chart);

    // Retire un nuancier UTILISATEUR (les intégrés ne se retirent pas).
    // Retourne faux si l'identifiant est inconnu ou intégré.
    bool remove_chart(std::string_view chart_id);

    [[nodiscard]] std::span<const ThreadChart> charts() const noexcept { return charts_; }
    [[nodiscard]] bool is_builtin(std::string_view chart_id) const noexcept;
    [[nodiscard]] const ThreadChart* find_chart(std::string_view chart_id) const noexcept;
    [[nodiscard]] std::optional<Thread> find(const ThreadKey& key) const;

    // Recherche par sous-chaîne (insensible à la casse, ASCII) dans le code, le
    // nom, la gamme ou la marque. `chart_id` vide = tous les nuanciers.
    // Résultats dans l'ordre de la bibliothèque, tronqués à `limit` (0 = tous).
    [[nodiscard]] std::vector<Thread>
    search(std::string_view needle, std::string_view chart_id = {}, std::size_t limit = 0) const;

    // Fils les plus proches d'une couleur (CIEDE2000), triés par distance
    // croissante, égalités départagées par l'ordre de la bibliothèque (HP-THR-003).
    // `chart_id` vide = tous les nuanciers.
    [[nodiscard]] std::vector<ThreadMatch>
    nearest(std::array<std::uint8_t, 3> rgb, std::string_view chart_id, std::size_t top_n) const;

private:
    std::vector<ThreadChart> charts_;
    std::size_t builtin_count_{0};
};

} // namespace openstitch::thread_palette
