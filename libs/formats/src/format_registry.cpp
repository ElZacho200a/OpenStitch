// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/format_registry.hpp"

#include <algorithm>
#include <cctype>

#include "openstitch/formats/dst.hpp"
#include "openstitch/formats/exp.hpp"
#include "openstitch/formats/jef.hpp"
#include "openstitch/formats/pes.hpp"

namespace openstitch::formats {

namespace {

// Adaptateurs vers les signatures `EncodeFn`/`DecodeFn` (sans
// `DstWriteOptions`, qui n'a pas sa place dans un pointeur de fonction
// générique au registre) -- `encode_dst`/`decode_dst` gardent leur API
// publique inchangée (cf. `docs/source/dst-format.md`), ceci n'en est qu'un
// alias pour la ligne de registre ci-dessous.
Result<std::vector<std::uint8_t>> encode_dst_for_registry(const stitch::StitchSequence& sequence) {
    // Export vers une machine : coupes en sauts NON nuls (les sauts nuls sont ignorés ou
    // supprimés par certaines machines et logiciels de transfert, d'où des fils non coupés).
    DstWriteOptions options;
    options.trim_jumps_with_movement = true;
    return encode_dst(sequence, options);
}
Result<stitch::StitchSequence> decode_dst_for_registry(std::span<const std::uint8_t> bytes) {
    return decode_dst(bytes);
}

// Les options de coupe/arrêt/couleur n'existent pas pour le DST (pas de couleur, coupes en
// sauts) : seul le nom du motif est pris en compte.
Result<std::vector<std::uint8_t>> encode_dst_ex(const stitch::StitchSequence& sequence,
                                                const MachineExportOptions& options) {
    DstWriteOptions dst;
    dst.design_name = options.design_name;
    dst.trim_jumps_with_movement = true;
    return encode_dst(sequence, dst);
}
Result<DecodedDesign> decode_dst_ex(std::span<const std::uint8_t> bytes) {
    auto sequence = decode_dst(bytes);
    if (!sequence) {
        return std::unexpected(sequence.error());
    }
    DecodedDesign design;
    design.sequence = std::move(*sequence);
    return design;
}

Result<std::vector<std::uint8_t>> encode_pes_simple(const stitch::StitchSequence& s) {
    return encode_pes(s);
}
Result<stitch::StitchSequence> decode_pes_simple(std::span<const std::uint8_t> bytes) {
    auto d = decode_pes(bytes);
    if (!d) {
        return std::unexpected(d.error());
    }
    return std::move(d->sequence);
}
Result<std::vector<std::uint8_t>> encode_jef_simple(const stitch::StitchSequence& s) {
    return encode_jef(s);
}
Result<stitch::StitchSequence> decode_jef_simple(std::span<const std::uint8_t> bytes) {
    auto d = decode_jef(bytes);
    if (!d) {
        return std::unexpected(d.error());
    }
    return std::move(d->sequence);
}
Result<std::vector<std::uint8_t>> encode_exp_simple(const stitch::StitchSequence& s) {
    return encode_exp(s);
}
Result<stitch::StitchSequence> decode_exp_simple(std::span<const std::uint8_t> bytes) {
    auto d = decode_exp(bytes);
    if (!d) {
        return std::unexpected(d.error());
    }
    return std::move(d->sequence);
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
            .encode_ex = &encode_dst_ex,
            .decode_ex = &decode_dst_ex,
            .carries_colors = false,
        },
        FormatInfo{
            .id = "pes",
            .display_name = "Brother PES",
            .extensions = {"pes"},
            .can_read = true,
            .can_write = true,
            .default_constraints = pes_constraints(),
            .encode = &encode_pes_simple,
            .decode = &decode_pes_simple,
            .encode_ex = &encode_pes,
            .decode_ex = &decode_pes,
            .carries_colors = true,
        },
        FormatInfo{
            .id = "jef",
            .display_name = "Janome JEF",
            .extensions = {"jef"},
            .can_read = true,
            .can_write = true,
            .default_constraints = jef_constraints(),
            .encode = &encode_jef_simple,
            .decode = &decode_jef_simple,
            .encode_ex = &encode_jef,
            .decode_ex = &decode_jef,
            .carries_colors = true,
        },
        FormatInfo{
            .id = "exp",
            .display_name = "Melco EXP",
            .extensions = {"exp"},
            .can_read = true,
            .can_write = true,
            .default_constraints = exp_constraints(),
            .encode = &encode_exp_simple,
            .decode = &decode_exp_simple,
            .encode_ex = &encode_exp,
            .decode_ex = &decode_exp,
            .carries_colors = false,
        },
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
