// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace openstitch::thread_palette {

// Identité stable d'un fil : nuancier + référence fabricant dans ce nuancier.
// Type valeur léger, sans pointeur ni référence vers le nuancier — destiné à
// être embarqué par un consommateur (stitch/document/formats/stitch_analysis,
// hors périmètre S1) sans dépendre du cycle de vie du registre.
struct ThreadKey {
    std::string chart_id; // ex. "madeira_polyneon"
    std::string code;     // référence fabricant, ex. "1919"

    constexpr auto operator<=>(const ThreadKey&) const = default;
    constexpr bool operator==(const ThreadKey&) const = default;
};

// Une entrée d'un nuancier : identité, métadonnées descriptives, approximation
// RGB publiée par le fabricant.
struct Thread {
    ThreadKey key;
    std::string brand;
    std::string range;
    std::string name;
    std::array<std::uint8_t, 3> rgb{};
};

// Un nuancier de fabricant chargé en mémoire (données compilées, voir
// libs/thread_palette/data/).
struct ThreadChart {
    std::string chart_id;
    std::string display_name;
    std::string source_note; // fabricant, gamme, URL source, date de consultation (S1-POLICY-2)
    std::vector<Thread> threads;
};

} // namespace openstitch::thread_palette
