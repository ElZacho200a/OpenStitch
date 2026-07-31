// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/dst.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

namespace openstitch::formats {

namespace {

constexpr std::size_t kHeaderSize = 512;
constexpr int kMaxDelta = 121;  // ±12,1 mm par enregistrement
constexpr std::int32_t kUmPerDstUnit = 100;

// Point en unités DST (0,1 mm).
struct DstPoint {
    std::int32_t x{0};
    std::int32_t y{0};
};

DstPoint quantize(Vec2um pos) {
    return {static_cast<std::int32_t>(std::lround(pos.x.value / 100.0)),
            static_cast<std::int32_t>(std::lround(pos.y.value / 100.0))};
}

// Décomposition en ternaire équilibré : v = d0·1 + d1·3 + d2·9 + d3·27 + d4·81,
// chaque digit dans {-1, 0, +1}. Unique pour |v| <= 121.
std::array<int, 5> balanced_ternary(int v) {
    std::array<int, 5> digits{};
    int r = v;
    for (std::size_t i = 0; i < 5; ++i) {
        int d = ((r % 3) + 3) % 3;
        if (d == 2) {
            d = -1;
        }
        digits[i] = d;
        r = (r - d) / 3;
    }
    return digits;
}

enum class RecordType { Normal, Jump, ColorChange };

// Table de bits documentée du format DST.
std::array<std::uint8_t, 3> encode_record(int dx, int dy, RecordType type) {
    const auto x = balanced_ternary(dx);
    const auto y = balanced_ternary(dy);
    std::uint8_t b0 = 0;
    std::uint8_t b1 = 0;
    std::uint8_t b2 = 0x03;  // bits toujours à 1

    const auto set = [](std::uint8_t& b, int digit, std::uint8_t plus, std::uint8_t minus) {
        if (digit > 0) {
            b |= plus;
        } else if (digit < 0) {
            b |= minus;
        }
    };
    set(b0, x[0], 0x01, 0x02);  // x ±1
    set(b0, x[2], 0x04, 0x08);  // x ±9
    set(b0, y[2], 0x20, 0x10);  // y ±9
    set(b0, y[0], 0x80, 0x40);  // y ±1
    set(b1, x[1], 0x01, 0x02);  // x ±3
    set(b1, x[3], 0x04, 0x08);  // x ±27
    set(b1, y[3], 0x20, 0x10);  // y ±27
    set(b1, y[1], 0x80, 0x40);  // y ±3
    set(b2, x[4], 0x04, 0x08);  // x ±81
    set(b2, y[4], 0x20, 0x10);  // y ±81

    if (type == RecordType::Jump) {
        b2 |= 0x80;
    } else if (type == RecordType::ColorChange) {
        b2 |= 0xC0;
    }
    return {b0, b1, b2};
}

struct DecodedRecord {
    int dx{0};
    int dy{0};
    RecordType type{RecordType::Normal};
    bool end{false};
};

DecodedRecord decode_record(std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) {
    DecodedRecord rec;
    if (b2 == 0xF3 && b0 == 0x00 && b1 == 0x00) {
        rec.end = true;
        return rec;
    }
    if ((b2 & 0xC0) == 0xC0) {
        rec.type = RecordType::ColorChange;
    } else if ((b2 & 0x80) != 0) {
        rec.type = RecordType::Jump;
    }
    const auto add = [](int& v, std::uint8_t b, std::uint8_t plus, std::uint8_t minus, int amount) {
        if ((b & plus) != 0) {
            v += amount;
        }
        if ((b & minus) != 0) {
            v -= amount;
        }
    };
    add(rec.dx, b0, 0x01, 0x02, 1);
    add(rec.dx, b0, 0x04, 0x08, 9);
    add(rec.dy, b0, 0x80, 0x40, 1);
    add(rec.dy, b0, 0x20, 0x10, 9);
    add(rec.dx, b1, 0x01, 0x02, 3);
    add(rec.dx, b1, 0x04, 0x08, 27);
    add(rec.dy, b1, 0x80, 0x40, 3);
    add(rec.dy, b1, 0x20, 0x10, 27);
    add(rec.dx, b2, 0x04, 0x08, 81);
    add(rec.dy, b2, 0x20, 0x10, 81);
    return rec;
}

// Découpe un déplacement quelconque en enregistrements <= ±121. Tous les
// morceaux intermédiaires sont des Jump ; le dernier porte le type demandé.
void emit_move(std::vector<std::uint8_t>& out, int dx, int dy, RecordType type) {
    while (std::abs(dx) > kMaxDelta || std::abs(dy) > kMaxDelta) {
        const int steps = std::max((std::abs(dx) + kMaxDelta - 1) / kMaxDelta,
                                   (std::abs(dy) + kMaxDelta - 1) / kMaxDelta);
        const int sx = dx / steps;
        const int sy = dy / steps;
        const auto rec = encode_record(sx, sy, RecordType::Jump);
        out.insert(out.end(), rec.begin(), rec.end());
        dx -= sx;
        dy -= sy;
    }
    const auto rec = encode_record(dx, dy, type);
    out.insert(out.end(), rec.begin(), rec.end());
}

}  // namespace

Result<std::vector<std::uint8_t>> encode_dst(const stitch::StitchSequence& sequence,
                                             const DstWriteOptions& options) {
    // Seules les commandes de mouvement sont encodables ; End est implicite.
    std::vector<const stitch::StitchCommand*> moves;
    for (const auto& cmd : sequence.commands) {
        if (cmd.type != stitch::CommandType::End) {
            moves.push_back(&cmd);
        }
    }
    if (moves.empty()) {
        return fail(ErrorCategory::UserInput, "Aucun point à exporter");
    }

    const DstPoint origin = quantize(moves.front()->pos);
    std::vector<std::uint8_t> body;
    body.reserve(moves.size() * 3 + 3);

    DstPoint prev = origin;
    DstPoint minP{0, 0};
    DstPoint maxP{0, 0};
    std::size_t colorChanges = 0;

    for (const auto* cmd : moves) {
        const DstPoint target = quantize(cmd->pos);
        const int dx = target.x - prev.x;
        const int dy = target.y - prev.y;
        switch (cmd->type) {
        case stitch::CommandType::Stitch:
            emit_move(body, dx, dy, RecordType::Normal);
            break;
        case stitch::CommandType::Jump:
            emit_move(body, dx, dy, RecordType::Jump);
            break;
        case stitch::CommandType::Trim:
            // Convention : N sauts de délta nul déclenchent la coupe.
            for (int i = 0; i < std::max(1, options.trim_jumps); ++i) {
                emit_move(body, 0, 0, RecordType::Jump);
            }
            if (dx != 0 || dy != 0) {
                emit_move(body, dx, dy, RecordType::Jump);
            }
            break;
        case stitch::CommandType::ColorChange:
        case stitch::CommandType::Stop:
            // DST ne distingue pas Stop d'un changement de fil : même arrêt.
            emit_move(body, dx, dy, RecordType::ColorChange);
            ++colorChanges;
            break;
        case stitch::CommandType::End:
            break;
        }
        prev = target;
        minP.x = std::min(minP.x, target.x - origin.x);
        minP.y = std::min(minP.y, target.y - origin.y);
        maxP.x = std::max(maxP.x, target.x - origin.x);
        maxP.y = std::max(maxP.y, target.y - origin.y);
    }
    body.insert(body.end(), {0x00, 0x00, 0xF3});

    // En-tête calculé depuis le corps réellement encodé.
    const std::size_t records = body.size() / 3 - 1;
    std::string name = options.design_name.substr(0, 16);
    std::string header;
    header += fmt::format("LA:{:<16}\r", name);
    header += fmt::format("ST:{:>7}\r", records);
    header += fmt::format("CO:{:>3}\r", colorChanges);
    header += fmt::format("+X:{:>5}\r", maxP.x);
    header += fmt::format("-X:{:>5}\r", -minP.x);
    header += fmt::format("+Y:{:>5}\r", maxP.y);
    header += fmt::format("-Y:{:>5}\r", -minP.y);
    const DstPoint last{prev.x - origin.x, prev.y - origin.y};
    header += fmt::format("AX:{}{:>5}\r", last.x >= 0 ? '+' : '-', std::abs(last.x));
    header += fmt::format("AY:{}{:>5}\r", last.y >= 0 ? '+' : '-', std::abs(last.y));
    header += "MX:+    0\rMY:+    0\rPD:******\r";
    header += '\x1a';

    std::vector<std::uint8_t> out(kHeaderSize, 0x20);
    std::copy(header.begin(), header.end(), out.begin());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

Result<stitch::StitchSequence> decode_dst(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize + 3) {
        return fail(ErrorCategory::InvalidFile,
                    "Fichier DST trop court (en-tête de 512 octets attendu)",
                    fmt::format("taille = {} octets", bytes.size()));
    }
    // La zone des points doit contenir des enregistrements de 3 octets, mais on
    // ne rejette PAS un reliquat : certains logiciels (p. ex. Hatch) ajoutent un
    // octet de fin DOS (0x1A) après le marqueur `00 00 F3`. La boucle s'arrête au
    // marqueur de fin ; tout ce qui suit est ignoré. L'absence de marqueur (vrai
    // fichier tronqué) est détectée plus bas via `ended`.

    stitch::StitchSequence sequence;
    DstPoint pos{0, 0};
    int pendingZeroJumps = 0;
    bool ended = false;

    const auto flushZeroJumps = [&] {
        if (pendingZeroJumps >= 3) {
            // Convention inverse de l'encodeur : rafale de sauts nuls = coupe.
            sequence.commands.push_back({Vec2um{Micrometers{pos.x * kUmPerDstUnit},
                                                Micrometers{pos.y * kUmPerDstUnit}},
                                         stitch::CommandType::Trim, ObjectId{}});
        } else {
            for (int i = 0; i < pendingZeroJumps; ++i) {
                sequence.commands.push_back({Vec2um{Micrometers{pos.x * kUmPerDstUnit},
                                                    Micrometers{pos.y * kUmPerDstUnit}},
                                             stitch::CommandType::Jump, ObjectId{}});
            }
        }
        pendingZeroJumps = 0;
    };

    for (std::size_t i = kHeaderSize; i + 2 < bytes.size(); i += 3) {
        const DecodedRecord rec = decode_record(bytes[i], bytes[i + 1], bytes[i + 2]);
        if (rec.end) {
            ended = true;
            break;
        }
        if (rec.type == RecordType::Jump && rec.dx == 0 && rec.dy == 0) {
            ++pendingZeroJumps;
            continue;
        }
        flushZeroJumps();
        pos.x += rec.dx;
        pos.y += rec.dy;
        const Vec2um p{Micrometers{pos.x * kUmPerDstUnit}, Micrometers{pos.y * kUmPerDstUnit}};
        switch (rec.type) {
        case RecordType::Normal:
            sequence.commands.push_back({p, stitch::CommandType::Stitch, ObjectId{}});
            break;
        case RecordType::Jump:
            sequence.commands.push_back({p, stitch::CommandType::Jump, ObjectId{}});
            break;
        case RecordType::ColorChange:
            sequence.commands.push_back({p, stitch::CommandType::ColorChange, ObjectId{}});
            break;
        }
    }
    flushZeroJumps();

    if (sequence.commands.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le fichier DST ne contient aucun point");
    }
    if (!ended) {
        return fail(ErrorCategory::InvalidFile,
                    "Fichier DST sans marqueur de fin (fichier tronqué ?)");
    }
    sequence.commands.push_back(
        {sequence.commands.back().pos, stitch::CommandType::End, ObjectId{}});
    return sequence;
}

Result<void> write_dst_file(const std::filesystem::path& path,
                            const stitch::StitchSequence& sequence,
                            const DstWriteOptions& options) {
    auto bytes = encode_dst(sequence, options);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(ErrorCategory::UserInput,
                    "Impossible d'écrire le fichier : " + path.string());
    }
    file.write(reinterpret_cast<const char*>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
    if (!file) {
        return fail(ErrorCategory::Internal, "Échec d'écriture : " + path.string());
    }
    return {};
}

Result<stitch::StitchSequence> read_dst_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCategory::UserInput,
                    "Fichier introuvable ou illisible : " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    return decode_dst(bytes);
}

}  // namespace openstitch::formats
