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
#include "openstitch/thread_palette/thread_library.hpp"

namespace openstitch::thread_palette {

// Identifiant du nuancier personnel de l'utilisateur (fils ajoutés à la main, hors cartes
// de fabricants). Les identités `ThreadKey{kPersonalChartId, code}` du stock désignent
// ces fils.
inline constexpr const char* kPersonalChartId = "personal";

// « Mes fils » (HP-THR-007) : le stock de bobines possédées par l'utilisateur. Deux parties :
//   - des références `ThreadKey` vers les nuanciers de la bibliothèque (cochées « possédé ») ;
//   - un nuancier personnel éditable : fils définis par l'utilisateur (chart_id "personal").
// Type valeur, sans E/S ni dépendance au document. Déterministe : les références sont
// toujours triées (chart_id, code), les fils personnels par code.
class ThreadStock {
public:
    // Marque un fil d'un nuancier comme possédé. Faux s'il l'était déjà ou si la clé est
    // vide / pointe vers le nuancier personnel (utiliser add_personal).
    bool own(const ThreadKey& key);
    // Retire un fil possédé (référence ou fil personnel). Faux s'il n'y était pas.
    bool disown(const ThreadKey& key);
    [[nodiscard]] bool owns(const ThreadKey& key) const;

    // Ajoute (ou remplace, à code identique) un fil personnel. Refuse un code vide ; le
    // chart_id du fil est toujours imposé à kPersonalChartId.
    [[nodiscard]] Result<void> add_personal(Thread thread);

    [[nodiscard]] std::span<const ThreadKey> owned_keys() const noexcept { return owned_; }
    [[nodiscard]] std::span<const Thread> personal_threads() const noexcept { return personal_; }
    [[nodiscard]] std::size_t size() const noexcept { return owned_.size() + personal_.size(); }
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    // Fils du stock résolus : références retrouvées dans `library` (les références
    // inconnues, par ex. un nuancier utilisateur retiré, sont ignorées) puis fils
    // personnels. Ordre déterministe (références triées, puis personnels triés).
    [[nodiscard]] std::vector<Thread> resolve(const ThreadLibrary& library) const;

    // Fils du stock les plus proches de `rgb` (CIEDE2000), triés par distance croissante,
    // égalités départagées par l'ordre de resolve(). Restreint au stock : un fil du
    // catalogue non possédé n'apparaît jamais. Vide si le stock ne résout aucun fil.
    [[nodiscard]] std::vector<ThreadMatch>
    nearest(const ThreadLibrary& library, std::array<std::uint8_t, 3> rgb, std::size_t top_n) const;

    friend bool operator==(const ThreadStock&, const ThreadStock&) = default;

private:
    std::vector<ThreadKey> owned_; // trié, sans doublon
    std::vector<Thread> personal_; // trié par code, sans doublon
};

// Sérialisation JSON déterministe (mêmes données -> mêmes octets), indentée, UTF-8 :
//   {"version":1,"owned":[{"chart":"...","code":"..."}],
//    "personal":[{"code":"...","name":"...","brand":"...","range":"...","rgb":[r,g,b]}]}
[[nodiscard]] std::string stock_to_json(const ThreadStock& stock);

// Relecture stricte. Erreurs (InvalidFile) : JSON illisible, champ manquant ou de mauvais
// type, rgb hors 0..255, code vide, doublon ; UnsupportedFormat si `version` est plus
// récente que celle connue. Ne lève jamais d'exception.
[[nodiscard]] Result<ThreadStock> stock_from_json(std::string_view text);

} // namespace openstitch::thread_palette
