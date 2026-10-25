// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "openstitch/core/ids.hpp"
#include "openstitch/stitch_analysis/thread_usage.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::stitch_analysis {

// Nouvelle couleur/fil d'un objet de broderie.
struct ThreadReassignment {
    ObjectId object;
    std::optional<thread_palette::ThreadKey> thread;
    std::array<std::uint8_t, 3> rgb{};
};

// Plan de fusion de fils (HP-THR-011), calculé SANS rien modifier : l'aperçu de l'interface le
// montre, puis la commande annulable l'applique en un seul pas.
struct ThreadMergePlan {
    // `target[i]` = indice (dans `usage`) du fil qui remplace le fil `i` ; `i` lui-même si le fil
    // est conservé tel quel. Même taille que `usage`.
    std::vector<std::size_t> target;
    std::vector<ThreadReassignment> reassignments; // un par objet à recolorier, ordre du document
    std::size_t before{0};                         // nombre de fils avant
    std::size_t after{0};                          // nombre de fils après

    [[nodiscard]] bool empty() const { return reassignments.empty(); }
};

// « Réduire à N fils » : regroupement agglomératif CIEDE2000 (`thread_palette::reduce_colors`),
// pondéré par le nombre de points ; le fil le plus cousu de chaque groupe est conservé. Plan vide
// si le design utilise déjà `max_threads` fils ou moins. `max_threads == 0` : plan vide.
[[nodiscard]] ThreadMergePlan plan_reduce_threads(const std::vector<ThreadUsage>& usage,
                                                  std::size_t max_threads);

// Fusion manuelle de deux fils choisis : tous les objets du fil `from` prennent le fil `to`.
// Plan vide si un indice est invalide ou si `from == to`.
[[nodiscard]] ThreadMergePlan plan_merge_threads(const std::vector<ThreadUsage>& usage,
                                                 std::size_t from, std::size_t to);

} // namespace openstitch::stitch_analysis
