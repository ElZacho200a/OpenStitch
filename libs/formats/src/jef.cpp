// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/jef.hpp"

#include <fmt/format.h>

#include "codec_common.hpp"
#include "machine_palettes.hpp"

namespace openstitch::formats {

namespace {

constexpr int kMaxDelta = 127;
constexpr std::size_t kHeaderSize = 0x74;
constexpr int kMaxDecodedColors = 4096;

std::uint8_t signed_byte(int v) {
    return static_cast<std::uint8_t>(static_cast<std::int8_t>(v));
}

// Code de cadre Janome selon la taille du motif (0,1 mm) : règles publiées du format.
int hoop_code(int width, int height) {
    if (width < 500 && height < 500) {
        return 1; // 50 x 50
    }
    if (width < 1260 && height < 1100) {
        return 3; // 126 x 110
    }
    if (width < 1400 && height < 2000) {
        return 2; // 140 x 200
    }
    if (width < 2000 && height < 2000) {
        return 4; // 200 x 200
    }
    return 0; // 110 x 110 (repli du format)
}

void put_hoop_margins(detail::ByteVec& out, int hoop_half_w, int hoop_half_h, int half_w,
                      int half_h) {
    const int x = hoop_half_w - half_w;
    const int y = hoop_half_h - half_h;
    if (x >= 0 && y >= 0) {
        for (const int v : {x, y, x, y}) {
            detail::put_i32le(out, v);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            detail::put_i32le(out, -1);
        }
    }
}

std::string fixed_timestamp(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < 14; ++i) {
        out.push_back(i < s.size() && s[i] >= '0' && s[i] <= '9' ? s[i] : '0');
    }
    return out;
}

} // namespace

MachineConstraints jef_constraints() {
    MachineConstraints c;
    c.resolution_um = 100;
    c.max_record_delta = kMaxDelta;
    c.trim_encoding = MachineConstraints::TrimEncoding::RepeatedZeroJumps;
    c.trim_zero_jump_count = 3;
    c.trim_jumps_with_movement = false;
    c.merge_stop_into_color_change = false;
    return c;
}

Result<std::vector<std::uint8_t>> encode_jef(const stitch::StitchSequence& sequence,
                                             const MachineExportOptions& options) {
    const auto prepared = detail::prepare_design(sequence, options);
    const MachineConstraints constraints = jef_constraints();
    auto normalized = normalize_for_machine(prepared.sequence, constraints);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    const auto& records = normalized->records;
    const auto bounds = detail::record_bounds(records);
    if (auto ok = detail::check_limits(constraints, prepared, bounds, "JEF"); !ok) {
        return std::unexpected(ok.error());
    }

    // --- Flux de points : déplacement de positionnement depuis le centre, puis enregistrements.
    detail::ByteVec stitches;
    stitches.reserve(records.size() * 2 + 16);
    const auto jump = [&](int dx, int dy) {
        detail::put_u8(stitches, 0x80);
        detail::put_u8(stitches, 0x02);
        detail::put_u8(stitches, signed_byte(dx));
        detail::put_u8(stitches, signed_byte(dy));
    };
    const int cx = detail::floor_half(bounds.min_x + bounds.max_x);
    const int cy = detail::floor_half(bounds.min_y + bounds.max_y);
    if (cx != 0 || cy != 0) {
        for (const auto& [sx, sy] : detail::split_move(-cx, -cy, kMaxDelta)) {
            jump(sx, sy);
        }
    }
    std::size_t changeRecords = 0;
    for (const auto& rec : records) {
        switch (rec.type) {
        case MachineRecordType::Stitch:
            detail::put_u8(stitches, signed_byte(rec.dx));
            detail::put_u8(stitches, signed_byte(rec.dy));
            break;
        case MachineRecordType::Jump:
            jump(rec.dx, rec.dy);
            break;
        case MachineRecordType::Trim:
            return fail(ErrorCategory::Internal, "Enregistrement de coupe natif inattendu en JEF");
        case MachineRecordType::ColorChange:
        case MachineRecordType::Stop:
            if (rec.dx != 0 || rec.dy != 0) {
                jump(rec.dx, rec.dy);
            }
            for (const unsigned b : {0x80u, 0x01u, 0x00u, 0x00u}) {
                detail::put_u8(stitches, b);
            }
            ++changeRecords;
            break;
        }
    }
    stitches.push_back(0x80);
    stitches.push_back(0x10);

    // --- Table de fils : une entrée par bloc (0 = arrêt machine, même fil que le précédent).
    std::vector<int> table;
    {
        int lastThread = 0;
        const auto& segs = prepared.segments;
        const std::size_t count = changeRecords + 1;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& seg = segs[std::min(i, segs.size() - 1)];
            if (i > 0 && seg.kind == detail::SegmentKind::Stop) {
                table.push_back(0);
                continue;
            }
            lastThread = detail::nearest_jef_index(seg.rgb, i > 0 ? lastThread : 0);
            table.push_back(lastThread);
        }
    }

    // --- En-tête.
    const int width = bounds.width();
    const int height = bounds.height();
    const int halfW = (width + 1) / 2;
    const int halfH = (height + 1) / 2;

    detail::ByteVec out;
    out.reserve(kHeaderSize + table.size() * 8 + stitches.size());
    detail::put_u32le(out, static_cast<std::uint32_t>(kHeaderSize + table.size() * 8));
    detail::put_u32le(out, 0x14);
    detail::put_text(out, fixed_timestamp(options.jef_timestamp));
    detail::put_u8(out, 0);
    detail::put_u8(out, 0);
    detail::put_u32le(out, static_cast<std::uint32_t>(table.size()));
    detail::put_u32le(out, static_cast<std::uint32_t>(stitches.size() / 2));
    detail::put_u32le(out, static_cast<std::uint32_t>(hoop_code(width, height)));
    for (const int v : {halfW, halfH, halfW, halfH}) {
        detail::put_i32le(out, v);
    }
    put_hoop_margins(out, 550, 550, halfW, halfH);  // 110 x 110
    put_hoop_margins(out, 250, 250, halfW, halfH);  // 50 x 50
    put_hoop_margins(out, 700, 1000, halfW, halfH); // 140 x 200
    put_hoop_margins(out, 700, 1000, halfW, halfH); // cadre personnalisé (mêmes valeurs)
    for (const int index : table) {
        detail::put_i32le(out, index);
    }
    for (std::size_t i = 0; i < table.size(); ++i) {
        detail::put_i32le(out, 0x0D);
    }
    out.insert(out.end(), stitches.begin(), stitches.end());
    return out;
}

Result<DecodedDesign> decode_jef(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) {
        return fail(ErrorCategory::InvalidFile, "Fichier JEF trop court (en-tête de 116 octets)",
                    fmt::format("taille = {} octets", bytes.size()));
    }
    const auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at]) |
               (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    };
    const std::uint32_t offset = u32(0);
    const std::uint32_t colorCount = u32(24);
    if (colorCount > static_cast<std::uint32_t>(kMaxDecodedColors)) {
        return fail(ErrorCategory::InvalidFile, "Fichier JEF : nombre de couleurs invraisemblable",
                    fmt::format("{} couleurs", colorCount));
    }
    if (offset < kHeaderSize || offset > bytes.size()) {
        return fail(ErrorCategory::InvalidFile, "Fichier JEF : décalage des points invalide",
                    fmt::format("décalage = {}, taille = {}", offset, bytes.size()));
    }
    std::vector<int> table;
    for (std::uint32_t i = 0; i < colorCount && kHeaderSize + (i + 1) * 4 <= bytes.size(); ++i) {
        const auto raw = static_cast<std::int32_t>(u32(kHeaderSize + i * 4));
        table.push_back(raw < 0 ? -raw : raw);
    }

    std::vector<MachineRecord> records;
    std::vector<bool> isStop; // pour chaque enregistrement de changement, dans l'ordre
    const auto sbyte = [](std::uint8_t b) { return static_cast<int>(static_cast<std::int8_t>(b)); };
    std::size_t changes = 0;
    for (std::size_t i = offset; i + 1 < bytes.size();) {
        if (bytes[i] != 0x80) {
            records.push_back({sbyte(bytes[i]), sbyte(bytes[i + 1]), MachineRecordType::Stitch});
            i += 2;
            continue;
        }
        const std::uint8_t code = bytes[i + 1];
        if (code == 0x10) {
            break; // fin
        }
        if (i + 3 >= bytes.size()) {
            break;
        }
        const int dx = sbyte(bytes[i + 2]);
        const int dy = sbyte(bytes[i + 3]);
        i += 4;
        if (code == 0x02) {
            records.push_back({dx, dy, MachineRecordType::Jump});
        } else if (code == 0x01) {
            // Entrée de table 0 (ou absente avec un fichier incohérent -> changement ordinaire).
            const std::size_t entry = changes + 1;
            const bool stop = entry < table.size() && table[entry] == 0;
            records.push_back(
                {dx, dy, stop ? MachineRecordType::Stop : MachineRecordType::ColorChange});
            ++changes;
        } else {
            break; // code inconnu : fin de lecture tolérante
        }
    }
    detail::strip_leading_positioning_jumps(records);
    if (records.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le fichier JEF ne contient aucun point");
    }

    DecodedDesign design;
    design.sequence = sequence_from_machine_records(records, jef_constraints());
    // Couleurs : un segment par changement/arrêt (+ le premier).
    detail::Rgb current{0, 0, 0};
    const auto rgb_of = [&](int index, const detail::Rgb& fallback) {
        if (index <= 0) {
            return fallback;
        }
        const int wrapped = (index - 1) % detail::kJefPaletteSize + 1;
        return detail::jef_rgb(wrapped).value_or(fallback);
    };
    current = rgb_of(table.empty() ? 0 : table[0], current);
    design.block_colors.push_back(current);
    for (std::size_t k = 1; k <= changes; ++k) {
        current = rgb_of(k < table.size() ? table[k] : 0, current);
        design.block_colors.push_back(current);
    }
    return design;
}

} // namespace openstitch::formats
