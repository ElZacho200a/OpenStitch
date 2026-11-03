// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/vp3.hpp"

#include <fmt/format.h>

#include "codec_common.hpp"

namespace openstitch::formats {

namespace {

constexpr int kMaxShort = 127;
constexpr int kMaxLong = 32767;
constexpr int kMaxDecodedColors = 4096;
constexpr char kMagic[] = {'%', 'v', 's', 'm', '%', '\0'};
constexpr const char* kProducer = "OpenStitch";

void put_i32be(detail::ByteVec& out, std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    detail::put_u16be(out, u >> 16);
    detail::put_u16be(out, u & 0xFFFFu);
}
void put_u32be(detail::ByteVec& out, std::uint32_t v) {
    put_i32be(out, static_cast<std::int32_t>(v));
}
void put_str16(detail::ByteVec& out, const std::string& s) {
    detail::put_u16be(out, static_cast<unsigned>(s.size()));
    detail::put_text(out, s);
}
std::uint8_t signed_byte(int v) {
    return static_cast<std::uint8_t>(static_cast<std::int8_t>(v));
}

void put_move(detail::ByteVec& out, int code, int dx, int dy) {
    detail::put_u8(out, 0x80);
    detail::put_u8(out, static_cast<unsigned>(code));
    detail::put_u16be(out, static_cast<unsigned>(dx) & 0xFFFFu);
    detail::put_u16be(out, static_cast<unsigned>(dy) & 0xFFFFu);
}

struct Reader {
    std::span<const std::uint8_t> b;
    std::size_t pos{0};
    bool ok{true};
    [[nodiscard]] std::size_t left() const { return pos <= b.size() ? b.size() - pos : 0; }
    std::uint32_t u8() {
        if (left() < 1) {
            ok = false;
            return 0;
        }
        return b[pos++];
    }
    std::uint32_t u16() {
        const auto hi = u8();
        return (hi << 8) | u8();
    }
    std::uint32_t u32() {
        const auto hi = u16();
        return (hi << 16) | u16();
    }
    void skip(std::size_t n) {
        if (left() < n) {
            ok = false;
            pos = b.size();
            return;
        }
        pos += n;
    }
    void skip_str16() { skip(u16()); }
};

int s16(std::uint32_t v) {
    return static_cast<int>(static_cast<std::int16_t>(static_cast<std::uint16_t>(v)));
}

} // namespace

MachineConstraints vp3_constraints() {
    MachineConstraints c;
    c.resolution_um = 10;
    c.max_record_delta = kMaxLong;
    c.trim_encoding = MachineConstraints::TrimEncoding::Native;
    c.merge_stop_into_color_change = true;
    return c;
}

Result<std::vector<std::uint8_t>> encode_vp3(const stitch::StitchSequence& sequence,
                                             const MachineExportOptions& options) {
    const auto prepared = detail::prepare_design(sequence, options);
    const MachineConstraints constraints = vp3_constraints();
    auto normalized = normalize_for_machine(prepared.sequence, constraints);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    const auto& records = normalized->records;
    const auto bounds = detail::record_bounds(records);
    if (auto ok = detail::check_limits(constraints, prepared, bounds, "VP3"); !ok) {
        return std::unexpected(ok.error());
    }

    // --- Segments : un par couleur, flux d'octets propre.
    std::vector<detail::ByteVec> streams(1);
    for (const auto& rec : records) {
        auto& s = streams.back();
        switch (rec.type) {
        case MachineRecordType::Stitch:
            if (rec.dx >= -kMaxShort && rec.dx <= kMaxShort && rec.dy >= -kMaxShort &&
                rec.dy <= kMaxShort) {
                detail::put_u8(s, signed_byte(rec.dx));
                detail::put_u8(s, signed_byte(rec.dy));
            } else {
                put_move(s, 0x01, rec.dx, rec.dy);
            }
            break;
        case MachineRecordType::Jump:
            put_move(s, 0x02, rec.dx, rec.dy);
            break;
        case MachineRecordType::Trim:
            detail::put_u8(s, 0x80);
            detail::put_u8(s, 0x03);
            break;
        case MachineRecordType::ColorChange:
        case MachineRecordType::Stop:
            if (rec.dx != 0 || rec.dy != 0) {
                put_move(s, 0x02, rec.dx, rec.dy);
            }
            streams.emplace_back();
            break;
        }
    }

    const int cx = detail::floor_half(bounds.min_x + bounds.max_x);
    const int cy = detail::floor_half(bounds.min_y + bounds.max_y);
    const int halfW = (bounds.width() + 1) / 2;
    const int halfH = (bounds.height() + 1) / 2;

    detail::ByteVec body;
    put_str16(body, detail::ascii_name(options.design_name, 64));
    for (const int v : {-halfW, -halfH, halfW, halfH}) {
        put_i32be(body, v);
    }
    detail::put_u16be(body, static_cast<unsigned>(streams.size()));
    const int startX = -cx;
    const int startY = -cy;
    for (std::size_t i = 0; i < streams.size(); ++i) {
        const auto& seg = prepared.segments[std::min(i, prepared.segments.size() - 1)];
        detail::ByteVec section;
        put_i32be(section, i == 0 ? startX : 0);
        put_i32be(section, i == 0 ? startY : 0);
        for (const auto c : seg.rgb) {
            detail::put_u8(section, c);
        }
        put_str16(section, fmt::format("#{:02X}{:02X}{:02X}", seg.rgb[0], seg.rgb[1], seg.rgb[2]));
        put_u32be(section, static_cast<std::uint32_t>(streams[i].size()));
        section.insert(section.end(), streams[i].begin(), streams[i].end());
        put_u32be(body, static_cast<std::uint32_t>(section.size()));
        body.insert(body.end(), section.begin(), section.end());
    }

    detail::ByteVec out;
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    put_str16(out, kProducer);
    for (const unsigned b : {0x00u, 0x02u, 0x00u}) {
        detail::put_u8(out, b);
    }
    put_u32be(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

Result<DecodedDesign> decode_vp3(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < sizeof(kMagic) + 2 + 3 + 4 ||
        !std::equal(std::begin(kMagic), std::end(kMagic) - 1, bytes.begin())) {
        return fail(ErrorCategory::InvalidFile, "Fichier VP3 invalide (marqueur %vsm% absent)",
                    fmt::format("taille = {} octets", bytes.size()));
    }
    Reader r{bytes, sizeof(kMagic)};
    r.skip_str16();
    r.skip(3);
    r.skip(4); // taille du reste : non fiable, on lit jusqu'à la fin des données
    DecodedDesign design;
    {
        const auto n = r.u16();
        design.name.reserve(n);
        for (std::uint32_t i = 0; i < n && r.ok; ++i) {
            const auto c = static_cast<char>(r.u8());
            design.name.push_back(c >= 0x20 && c < 0x7F ? c : '?');
        }
    }
    r.skip(16);
    const auto colorCount = r.u16();
    if (!r.ok) {
        return fail(ErrorCategory::InvalidFile, "Fichier VP3 tronqué (en-tête)");
    }
    if (colorCount > static_cast<std::uint32_t>(kMaxDecodedColors)) {
        return fail(ErrorCategory::InvalidFile, "Fichier VP3 : nombre de couleurs invraisemblable",
                    fmt::format("{} couleurs", colorCount));
    }

    std::vector<MachineRecord> records;
    const auto sbyte = [](std::uint8_t b) { return static_cast<int>(static_cast<std::int8_t>(b)); };
    for (std::uint32_t k = 0; k < colorCount && r.left() >= 4; ++k) {
        const std::size_t sectionSize = r.u32();
        const std::size_t sectionEnd = std::min(bytes.size(), r.pos + sectionSize);
        r.skip(8); // départ (informatif : les positions sont relatives au premier point)
        const auto red = static_cast<std::uint8_t>(r.u8());
        const auto green = static_cast<std::uint8_t>(r.u8());
        const auto blue = static_cast<std::uint8_t>(r.u8());
        r.skip_str16();
        const std::size_t streamSize = r.u32();
        if (!r.ok) {
            break;
        }
        design.block_colors.push_back({red, green, blue});
        if (k > 0) {
            records.push_back({0, 0, MachineRecordType::ColorChange});
        }
        const std::size_t end = std::min({sectionEnd, bytes.size(), r.pos + streamSize});
        bool broken = false;
        for (std::size_t i = r.pos; i + 1 < end && !broken;) {
            if (bytes[i] != 0x80) {
                records.push_back(
                    {sbyte(bytes[i]), sbyte(bytes[i + 1]), MachineRecordType::Stitch});
                i += 2;
                continue;
            }
            const std::uint8_t code = bytes[i + 1];
            if (code == 0x03) {
                records.push_back({0, 0, MachineRecordType::Trim});
                i += 2;
            } else if ((code == 0x01 || code == 0x02) && i + 5 < end) {
                Reader m{bytes, i + 2};
                const int dx = s16(m.u16());
                const int dy = s16(m.u16());
                records.push_back(
                    {dx, dy, code == 0x01 ? MachineRecordType::Stitch : MachineRecordType::Jump});
                i += 6;
            } else {
                broken = true; // code inconnu ou tronqué : fin tolérante du segment
            }
        }
        if (sectionEnd < r.pos) {
            break;
        }
        r.pos = sectionEnd;
    }
    if (records.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le fichier VP3 ne contient aucun point");
    }
    design.sequence = sequence_from_machine_records(records, vp3_constraints());
    return design;
}

} // namespace openstitch::formats
