// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "openstitch/core/error.hpp"
#include "openstitch/formats/machine.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::formats {

using EncodeFn = Result<std::vector<std::uint8_t>> (*)(const stitch::StitchSequence&);
using DecodeFn = Result<stitch::StitchSequence> (*)(std::span<const std::uint8_t>);

// AI-02 : UNE entrée de ce registre = UN format machine (identifiant,
// extensions, capacités lecture/écriture, contraintes machine par défaut).
// Seule liste de formats machine du projet -- l'UI (menus Importer/
// Exporter) et la CLI l'interrogent au lieu de coder chaque format en dur.
// `encode`/`decode` sont nuls quand `can_write`/`can_read` sont faux
// (encodeur ou décodeur pas encore écrit, cf. HP-FMT-002..005) : c'est ce
// qui permet à `project_io`'s composition générique (AI-03b) de rester
// inchangée quand un nouveau codec s'inscrit (S2b, S2c) -- seule CETTE table
// grandit, par une ligne.
struct FormatInfo {
    std::string id;                      // "dst", futurs "pes", "jef", "exp"...
    std::string display_name;            // "Tajima DST"
    std::vector<std::string> extensions; // {"dst"}, sans le point, minuscules
    bool can_read{false};
    bool can_write{false};
    MachineConstraints default_constraints{};
    EncodeFn encode{nullptr};
    DecodeFn decode{nullptr};
    // HP-FMT-002..005 : variantes AVEC options machine (coupes, arrêts, couleurs) et couleurs
    // de blocs décodées. Renseignées pour tous les formats ; `encode`/`decode` restent les
    // appels simples (options par défaut) pour la compatibilité.
    EncodeExFn encode_ex{nullptr};
    DecodeExFn decode_ex{nullptr};
    // Le format porte-t-il des couleurs de fil réelles (PES, JEF) ?
    bool carries_colors{false};
};

[[nodiscard]] std::span<const FormatInfo> registered_formats();
[[nodiscard]] const FormatInfo* find_format(std::string_view id);
[[nodiscard]] const FormatInfo* find_format_for_extension(std::string_view extension_no_dot);

} // namespace openstitch::formats
