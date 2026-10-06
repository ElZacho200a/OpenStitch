// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/format_registry.hpp"

#include <algorithm>
#include <cctype>

#include "openstitch/formats/dst.hpp"

namespace openstitch::formats {

namespace {

// Adaptateurs vers les signatures `EncodeFn`/`DecodeFn` (sans
// `DstWriteOptions`, qui n'a pas sa place dans un pointeur de fonction
// générique au registre) -- `encode_dst`/`decode_dst` gardent leur API
// publique inchangée (cf. `docs/source/dst-format.md`), ceci n'en est qu'un
// alias pour la ligne de registre ci-dessous.
Result<std::vector<std::uint8_t>> encode_dst_for_registry(const stitch::StitchSequence& sequence) {
    return encode_dst(sequence);
}
Result<stitch::StitchSequence> decode_dst_for_registry(std::span<const std::uint8_t> bytes) {
    return decode_dst(bytes);
}

const std::vector<FormatInfo>& all_formats() {
    static const std::vector<FormatInfo> formats = {
        FormatInfo{
            .id = "dst",
            .display_name = "Tajima DST",
            .extensions = {"dst"},
            .can_read = true,
            .can_write = true,
            .default_constraints = MachineConstraints{}, // valeurs DST par défaut (cf. machine.hpp)
            .encode = &encode_dst_for_registry,
            .decode = &decode_dst_for_registry,
        },
        // Ligne de registre S2b (PES), S2c (JEF, EXP) : ajouter ici (droit
        // d'inscription R3, cf. specs/arch-plan -- Shared-File and Section
        // Ownership, ligne `libs/formats`).
    };
    return formats;
}

} // namespace

std::span<const FormatInfo> registered_formats() {
    return all_formats();
}

const FormatInfo* find_format(std::string_view id) {
    const auto& formats = all_formats();
    const auto it = std::find_if(formats.begin(), formats.end(),
                                 [&](const FormatInfo& f) { return f.id == id; });
    return it != formats.end() ? &*it : nullptr;
}

const FormatInfo* find_format_for_extension(std::string_view extension_no_dot) {
    std::string lower(extension_no_dot);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& format : all_formats()) {
        if (std::find(format.extensions.begin(), format.extensions.end(), lower) !=
            format.extensions.end()) {
            return &format;
        }
    }
    return nullptr;
}

} // namespace openstitch::formats
