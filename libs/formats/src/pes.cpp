// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/pes.hpp"

#include <fmt/format.h>

#include <cmath>

#include "codec_common.hpp"
#include "machine_palettes.hpp"

namespace openstitch::formats {

namespace {

constexpr int kMaxDelta = 2047;     // forme longue PEC : 12 bits signés
constexpr int kMaxColors = 255;     // octet « nombre de couleurs - 1 » de l'en-tête PEC
constexpr int kMaxPesCoord = 32767; // coordonnées 16 bits de la section PES
constexpr std::size_t kPecHeaderSize = 512;
constexpr std::size_t kPecBlockPrefix = 16; // 2 + 3 (longueur) + 3 (signature) + 4 x 2
constexpr std::size_t kThumbW = 48;
constexpr std::size_t kThumbH = 38;
constexpr std::size_t kThumbBytes = (kThumbW / 8) * kThumbH; // 228
constexpr std::size_t kMaxSectionPoints = 30000;

struct Pos {
    int x{0};
    int y{0}; // unités natives, axe Y vers le haut (cœur)
};

// --- Flux de points PEC ---------------------------------------------------------------------

void put_pec_value(detail::ByteVec& out, int v, bool long_form, unsigned flags) {
    if (!long_form) {
        detail::put_u8(out, static_cast<unsigned>(v) & 0x7Fu);
        return;
    }
    const unsigned u = static_cast<unsigned>(v) & 0x0FFFu;
    detail::put_u8(out, 0x80u | flags | (u >> 8));
    detail::put_u8(out, u & 0xFFu);
}

constexpr unsigned kFlagJump = 0x10;
constexpr unsigned kFlagTrim = 0x20;

// --- Vignettes 48x38 ------------------------------------------------------------------------

using Thumb = std::array<std::uint8_t, kThumbBytes>;

void set_pixel(Thumb& t, int x, int y) {
    if (x < 0 || y < 0 || x >= static_cast<int>(kThumbW) || y >= static_cast<int>(kThumbH)) {
        return;
    }
    t[static_cast<std::size_t>(y) * (kThumbW / 8) + static_cast<std::size_t>(x) / 8] |=
        static_cast<std::uint8_t>(1u << (x % 8)); // bit de poids faible d'abord
}

void draw_line(Thumb& t, int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        set_pixel(t, x0, y0);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

Thumb blank_thumb() {
    Thumb t{};
    // Cadre arrondi simple.
    for (int x = 4; x <= 43; ++x) {
        set_pixel(t, x, 1);
        set_pixel(t, x, 36);
    }
    for (int y = 4; y <= 33; ++y) {
        set_pixel(t, 1, y);
        set_pixel(t, 46, y);
    }
    constexpr std::array<std::array<int, 2>, 8> corners = {
        {{2, 3}, {3, 2}, {44, 2}, {45, 3}, {2, 34}, {3, 35}, {44, 35}, {45, 34}}};
    for (const auto& c : corners) {
        set_pixel(t, c[0], c[1]);
    }
    return t;
}

struct ThumbMap {
    double cx{0};
    double cy{0};
    double scale{1};
    [[nodiscard]] std::pair<int, int> map(const Pos& p) const {
        return {static_cast<int>(std::floor(24.0 + (p.x - cx) * scale)),
                static_cast<int>(std::floor(19.0 - (p.y - cy) * scale))};
    }
};

// --- Section PES (blocs CEmbOne / CSewSeg) -----------------------------------------------------

struct Section {
    int flag{0}; // 0 = points cousus, 1 = saut
    int color{1};
    std::vector<Pos> points;
};

void put_i16(detail::ByteVec& out, int v) {
    detail::put_u16le(out, static_cast<unsigned>(v) & 0xFFFFu);
}

} // namespace

MachineConstraints pes_constraints() {
    MachineConstraints c;
    c.resolution_um = 100;
    c.max_record_delta = kMaxDelta;
    c.trim_encoding = MachineConstraints::TrimEncoding::Native;
    c.merge_stop_into_color_change = true;
    c.max_colors = kMaxColors;
    c.max_extent_um = static_cast<std::int64_t>(kMaxPesCoord) * 100;
    return c;
}

Result<std::vector<std::uint8_t>> encode_pes(const stitch::StitchSequence& sequence,
                                             const MachineExportOptions& options) {
    using detail::put_u16le;
    using detail::put_u8;

    const auto prepared = detail::prepare_design(sequence, options);
    const MachineConstraints constraints = pes_constraints();
    auto normalized = normalize_for_machine(prepared.sequence, constraints);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    const auto& records = normalized->records;
    const auto bounds = detail::record_bounds(records);
    if (auto ok = detail::check_limits(constraints, prepared, bounds, "PES"); !ok) {
        return std::unexpected(ok.error());
    }

    // Positions absolues (unités natives, Y haut) et boîte de TOUS les enregistrements.
    std::vector<Pos> abs(records.size());
    int allMinX = 0;
    int allMaxX = 0;
    int allMinY = 0;
    int allMaxY = 0;
    {
        Pos p;
        for (std::size_t i = 0; i < records.size(); ++i) {
            p.x += records[i].dx;
            p.y += records[i].dy;
            abs[i] = p;
            allMinX = std::min(allMinX, p.x);
            allMaxX = std::max(allMaxX, p.x);
            allMinY = std::min(allMinY, p.y);
            allMaxY = std::max(allMaxY, p.y);
        }
    }
    if (allMaxX - allMinX > kMaxPesCoord || allMaxY - allMinY > kMaxPesCoord) {
        return fail(ErrorCategory::OperationImpossible,
                    "Coordonnées hors plage pour le format PES : le motif (sauts compris) "
                    "dépasse 3276,7 mm.");
    }

    // --- Indices de fils PEC : un par bloc (un arrêt reprend le fil précédent).
    std::size_t changeRecords = 0;
    for (const auto& r : records) {
        changeRecords +=
            (r.type == MachineRecordType::ColorChange || r.type == MachineRecordType::Stop) ? 1 : 0;
    }
    std::vector<int> colorIndex;
    {
        int last = 0;
        for (std::size_t i = 0; i <= changeRecords; ++i) {
            const auto& seg = prepared.segments[std::min(i, prepared.segments.size() - 1)];
            if (i > 0 && seg.kind == detail::SegmentKind::Stop) {
                colorIndex.push_back(last);
                continue;
            }
            last = detail::nearest_pec_index(seg.rgb, i > 0 ? last : 0);
            colorIndex.push_back(last);
        }
    }

    // --- Flux PEC.
    detail::ByteVec stream;
    stream.reserve(records.size() * 2 + 16);
    const int cx = detail::floor_half(bounds.min_x + bounds.max_x);
    const int cy = detail::floor_half(bounds.min_y + bounds.max_y);
    bool inJump = false;
    int toggle = 2;
    const auto put_move = [&](int dx, int dy_up, unsigned flags) {
        put_pec_value(stream, dx, true, flags);
        put_pec_value(stream, -dy_up, true, flags);
    };
    if (cx != 0 || cy != 0) {
        for (const auto& [sx, sy] : detail::split_move(-cx, -cy, kMaxDelta)) {
            put_move(sx, sy, kFlagJump);
        }
        inJump = true;
    }
    for (const auto& rec : records) {
        switch (rec.type) {
        case MachineRecordType::Stitch: {
            const int dy = -rec.dy;
            if (inJump && rec.dx != 0 && dy != 0) {
                put_u8(stream, 0);
                put_u8(stream, 0);
            }
            put_pec_value(stream, rec.dx, std::abs(rec.dx) > 63, 0);
            put_pec_value(stream, dy, std::abs(dy) > 63, 0);
            inJump = false;
            break;
        }
        case MachineRecordType::Jump:
            put_move(rec.dx, rec.dy, kFlagJump);
            inJump = true;
            break;
        case MachineRecordType::Trim:
            put_move(rec.dx, rec.dy, kFlagTrim);
            inJump = true;
            break;
        case MachineRecordType::ColorChange:
        case MachineRecordType::Stop:
            if (rec.dx != 0 || rec.dy != 0) {
                put_move(rec.dx, rec.dy, kFlagJump);
                inJump = true;
            }
            if (inJump) {
                put_u8(stream, 0);
                put_u8(stream, 0);
            }
            put_u8(stream, 0xFE);
            put_u8(stream, 0xB0);
            put_u8(stream, static_cast<unsigned>(toggle));
            toggle = 3 - toggle;
            inJump = false;
            break;
        }
    }
    put_u8(stream, 0xFF);
    put_u8(stream, 0x00);

    // --- Vignettes : cartes de points cousus par bloc.
    std::vector<std::vector<std::vector<Pos>>> runs(colorIndex.size());
    {
        std::size_t seg = 0;
        std::vector<Pos> run;
        Pos prev;
        const auto close = [&] {
            if (run.size() > 1) {
                runs[seg].push_back(run);
            }
            run.clear();
        };
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto type = records[i].type;
            if (type == MachineRecordType::Stitch) {
                if (run.empty()) {
                    run.push_back(prev);
                }
                run.push_back(abs[i]);
            } else {
                close();
                if (type == MachineRecordType::ColorChange || type == MachineRecordType::Stop) {
                    seg = std::min(seg + 1, runs.size() - 1);
                }
            }
            prev = abs[i];
        }
        close();
    }
    ThumbMap map;
    {
        const double w = std::max(1, bounds.width());
        const double h = std::max(1, bounds.height());
        map.scale = std::min(38.0 / w, 28.0 / h);
        map.cx = (bounds.min_x + bounds.max_x) / 2.0;
        map.cy = (bounds.min_y + bounds.max_y) / 2.0;
    }
    const auto render = [&](std::size_t only) {
        Thumb t = blank_thumb();
        for (std::size_t s = 0; s < runs.size(); ++s) {
            if (only != static_cast<std::size_t>(-1) && only != s) {
                continue;
            }
            for (const auto& run : runs[s]) {
                for (std::size_t k = 1; k < run.size(); ++k) {
                    const auto a = map.map(run[k - 1]);
                    const auto b = map.map(run[k]);
                    draw_line(t, a.first, a.second, b.first, b.second);
                }
            }
        }
        return t;
    };

    // --- Bloc PEC.
    detail::ByteVec pec;
    {
        pec.reserve(kPecHeaderSize + kPecBlockPrefix + stream.size() +
                    (colorIndex.size() + 1) * kThumbBytes);
        detail::put_text(pec, "LA:");
        std::string name = detail::ascii_name(options.design_name, 16);
        name.resize(16, ' ');
        detail::put_text(pec, name);
        put_u8(pec, 0x0D);
        detail::put_fill(pec, 12, 0x20);
        put_u8(pec, 0xFF);
        put_u8(pec, 0x00);
        put_u8(pec, static_cast<unsigned>(kThumbW / 8));
        put_u8(pec, static_cast<unsigned>(kThumbH));
        detail::put_fill(pec, 12, 0x20);
        put_u8(pec, static_cast<unsigned>(colorIndex.size() - 1U));
        for (const int idx : colorIndex) {
            put_u8(pec, static_cast<unsigned>(idx));
        }
        detail::put_fill(pec, 463 - colorIndex.size(), 0x20);
        // pec.size() == 512 ici (20 + 12 + 4 + 12 + 1 + 463).
        put_u8(pec, 0);
        put_u8(pec, 0);
        detail::put_u24le(pec, static_cast<unsigned>(kPecBlockPrefix + stream.size()));
        put_u8(pec, 0x31);
        put_u8(pec, 0xFF);
        put_u8(pec, 0xF0);
        put_u16le(pec, static_cast<unsigned>(bounds.width()));
        put_u16le(pec, static_cast<unsigned>(bounds.height()));
        put_u16le(pec, 0x01E0);
        put_u16le(pec, 0x01B0);
        pec.insert(pec.end(), stream.begin(), stream.end());
        const Thumb overview = render(static_cast<std::size_t>(-1));
        pec.insert(pec.end(), overview.begin(), overview.end());
        for (std::size_t s = 0; s < colorIndex.size(); ++s) {
            const Thumb t = render(s);
            pec.insert(pec.end(), t.begin(), t.end());
        }
    }

    // --- Section PES version 1 complète.
    std::vector<Section> sections;
    {
        std::size_t seg = 0;
        const auto toFile = [&](const Pos& p) {
            // X relatif au minimum, Y relatif au maximum de l'axe Y vers le bas.
            return Pos{p.x - allMinX, allMinY - p.y};
        };
        Pos prev;
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto type = records[i].type;
            const Pos cur = abs[i];
            if (type == MachineRecordType::Stitch) {
                if (sections.empty() || sections.back().flag != 0 ||
                    sections.back().color != colorIndex[seg] ||
                    sections.back().points.size() >= kMaxSectionPoints) {
                    Section s;
                    s.flag = 0;
                    s.color = colorIndex[seg];
                    s.points.push_back(toFile(prev));
                    sections.push_back(std::move(s));
                }
                sections.back().points.push_back(toFile(cur));
            } else {
                if (cur.x != prev.x || cur.y != prev.y) {
                    Section s;
                    s.flag = 1;
                    s.color = colorIndex[seg];
                    s.points = {toFile(prev), toFile(cur)};
                    sections.push_back(std::move(s));
                }
                if (type == MachineRecordType::ColorChange || type == MachineRecordType::Stop) {
                    seg = std::min(seg + 1, colorIndex.size() - 1);
                }
            }
            prev = cur;
        }
    }

    detail::ByteVec out;
    detail::put_text(out, "#PES0001");
    const std::size_t pecOffsetAt = out.size();
    detail::put_u32le(out, 0); // décalage du bloc PEC, patché plus bas
    put_u16le(out, 1);         // « scale to fit »
    put_u16le(out, 1);         // sélecteur de cadre
    put_u16le(out, 1);         // nombre d'objets
    put_u16le(out, 0xFFFF);
    put_u16le(out, 0);
    put_u16le(out, 7);
    detail::put_text(out, "CEmbOne");
    for (int i = 0; i < 8; ++i) {
        put_u16le(out, 0);
    }
    const double width = bounds.width();
    const double height = bounds.height();
    for (const float f : {1.0F, 0.0F, 0.0F, 1.0F, static_cast<float>(1000.0 - width / 2.0),
                          static_cast<float>(1000.0 + height / 2.0)}) {
        detail::put_f32le(out, f);
    }
    put_u16le(out, 1);
    put_u16le(out, 0);
    put_u16le(out, 0);
    put_i16(out, bounds.width());
    put_i16(out, bounds.height());
    detail::put_fill(out, 8, 0);
    put_i16(out, static_cast<int>(sections.size()));
    put_u16le(out, 0xFFFF);
    put_u16le(out, 0);
    put_u16le(out, 7);
    detail::put_text(out, "CSewSeg");
    std::vector<std::pair<int, int>> colorLog;
    for (std::size_t s = 0; s < sections.size(); ++s) {
        if (s > 0) {
            put_u16le(out, 0x8003);
        }
        put_i16(out, sections[s].flag);
        put_i16(out, sections[s].color);
        put_i16(out, static_cast<int>(sections[s].points.size()));
        for (const auto& p : sections[s].points) {
            put_i16(out, p.x);
            put_i16(out, p.y);
        }
        if (colorLog.empty() || colorLog.back().second != sections[s].color) {
            colorLog.emplace_back(static_cast<int>(s), sections[s].color);
        }
    }
    put_i16(out, static_cast<int>(colorLog.size()));
    for (const auto& [sectionIndex, code] : colorLog) {
        put_i16(out, sectionIndex);
        put_i16(out, code);
    }
    put_u16le(out, 0);
    put_u16le(out, 0);

    const auto pecOffset = static_cast<std::uint32_t>(out.size());
    for (int i = 0; i < 4; ++i) {
        out[pecOffsetAt + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((pecOffset >> (8 * i)) & 0xFFu);
    }
    out.insert(out.end(), pec.begin(), pec.end());
    return out;
}

Result<DecodedDesign> decode_pes(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 12 || bytes[0] != '#' || bytes[1] != 'P' || bytes[2] != 'E' ||
        bytes[3] != 'S') {
        return fail(ErrorCategory::InvalidFile,
                    "Ce fichier n'est pas un PES (signature #PES absente)");
    }
    const std::uint32_t pec = static_cast<std::uint32_t>(bytes[8]) |
                              (static_cast<std::uint32_t>(bytes[9]) << 8) |
                              (static_cast<std::uint32_t>(bytes[10]) << 16) |
                              (static_cast<std::uint32_t>(bytes[11]) << 24);
    if (pec < 12 ||
        static_cast<std::uint64_t>(pec) + kPecHeaderSize + kPecBlockPrefix > bytes.size()) {
        return fail(ErrorCategory::InvalidFile, "Fichier PES : décalage du bloc PEC invalide",
                    fmt::format("décalage = {}, taille = {}", pec, bytes.size()));
    }
    DecodedDesign design;
    // Nom.
    {
        std::string name(reinterpret_cast<const char*>(bytes.data()) + pec + 3, 16);
        while (!name.empty() &&
               (name.back() == ' ' || name.back() == '\0' || name.back() == '\r')) {
            name.pop_back();
        }
        design.name = detail::ascii_name(name, 16);
    }
    // Table des fils.
    std::vector<int> colorIndex;
    {
        const int count = static_cast<int>(bytes[pec + 48]) + 1;
        for (int i = 0; i < count && pec + 49 + static_cast<std::size_t>(i) < pec + kPecHeaderSize;
             ++i) {
            colorIndex.push_back(bytes[pec + 49 + static_cast<std::size_t>(i)]);
        }
    }
    // Bornes du flux de points.
    const std::size_t block = pec + kPecHeaderSize;
    std::size_t end = bytes.size();
    {
        const std::size_t len = static_cast<std::size_t>(bytes[block + 2]) |
                                (static_cast<std::size_t>(bytes[block + 3]) << 8) |
                                (static_cast<std::size_t>(bytes[block + 4]) << 16);
        if (len >= kPecBlockPrefix && block + len <= bytes.size()) {
            end = block + len;
        }
    }
    const auto sext = [](unsigned v, unsigned bits) {
        const unsigned sign = 1u << (bits - 1);
        return (v & sign) != 0 ? static_cast<int>(v) - static_cast<int>(sign << 1)
                               : static_cast<int>(v);
    };

    std::vector<MachineRecord> records;
    std::size_t i = block + kPecBlockPrefix;
    while (i + 1 < end) {
        const std::uint8_t b0 = bytes[i];
        if (b0 == 0xFF) {
            break;
        }
        if (b0 == 0xFE && bytes[i + 1] == 0xB0) {
            records.push_back({0, 0, MachineRecordType::ColorChange});
            i += 3;
            continue;
        }
        bool jump = false;
        bool trim = false;
        const auto read_value = [&](int& v) -> bool {
            if (i >= end) {
                return false;
            }
            const std::uint8_t a = bytes[i];
            if (a & 0x80) {
                if (i + 1 >= end) {
                    return false;
                }
                trim = trim || (a & kFlagTrim) != 0;
                jump = jump || (a & kFlagJump) != 0;
                v = sext((static_cast<unsigned>(a & 0x0F) << 8) | bytes[i + 1], 12);
                i += 2;
            } else {
                v = sext(a, 7);
                i += 1;
            }
            return true;
        };
        int dx = 0;
        int dy = 0;
        if (!read_value(dx) || !read_value(dy)) {
            break; // enregistrement tronqué
        }
        const MachineRecordType type = trim   ? MachineRecordType::Trim
                                       : jump ? MachineRecordType::Jump
                                              : MachineRecordType::Stitch;
        records.push_back({dx, -dy, type});
    }
    detail::strip_leading_positioning_jumps(records);
    if (records.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le fichier PES ne contient aucun point");
    }

    // Couleurs : le k-ième changement de couleur passe à l'entrée k+1 ; même indice que
    // l'entrée précédente = arrêt machine.
    detail::Rgb current{0, 0, 0};
    const auto rgb_of = [&](std::size_t k) {
        if (k < colorIndex.size()) {
            if (const auto rgb = detail::pec_rgb(colorIndex[k])) {
                return *rgb;
            }
        }
        return current;
    };
    current = rgb_of(0);
    design.block_colors.push_back(current);
    std::size_t changes = 0;
    for (auto& rec : records) {
        if (rec.type != MachineRecordType::ColorChange) {
            continue;
        }
        ++changes;
        if (changes < colorIndex.size() && colorIndex[changes] == colorIndex[changes - 1]) {
            rec.type = MachineRecordType::Stop;
        }
        current = rgb_of(changes);
        design.block_colors.push_back(current);
    }
    design.sequence = sequence_from_machine_records(records, pes_constraints());
    return design;
}

} // namespace openstitch::formats
