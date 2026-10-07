// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::stitch {

// AD-02 ter : bloc de couleur du design machine -- couleur RGB, identité de
// fil FACULTATIVE (ThreadKey d'AD-01/AI-01, cf. `thread_palette::ThreadKey` --
// jamais un second type d'identité), et les bornes du bloc dans la séquence,
// en indices `[start, end)` (demi-ouvert) de `StitchSequence::commands`.
// UN SEUL type de bloc de couleur dans tout le projet : partagé par le
// design machine des codecs (AI-02, `libs/formats`), la table des blocs du
// design (AI-03a, `libs/stitch_analysis`) et le design importé (AI-04,
// `libs/document`). `thread_key` reste vide tant qu'aucune correspondance
// fil n'est connue (S4, AD-02 bis, renseigne ce champ sans changer le type).
struct ColorBlock {
    std::array<std::uint8_t, 3> rgb{};
    std::optional<thread_palette::ThreadKey> thread_key;
    std::size_t start{0}; // index dans StitchSequence::commands, inclus
    std::size_t end{0};   // exclu

    bool operator==(const ColorBlock&) const = default;
};

} // namespace openstitch::stitch
