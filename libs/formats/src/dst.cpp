// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/dst.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>

#include "openstitch/formats/machine.hpp"

namespace openstitch::formats {

namespace {

constexpr std::size_t kHeaderSize = 512;
constexpr int kMaxDelta = 121; // ±12,1 mm par enregistrement
constexpr std::int32_t kUmPerDstUnit = 100;

// Point en unités DST (0,1 mm) -- uniquement pour le calcul de l'en-tête
// (bornes, dernière position) : la quantification elle-même est faite par
// `normalize_for_machine`/`sequence_from_machine_records` (HP-FMT-001).
struct DstPoint {
    std::int32_t x{0};
    std::int32_t y{0};
};

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
    std::uint8_t b2 = 0x03; // bits toujours à 1

    const auto set = [](std::uint8_t& b, int digit, std::uint8_t plus, std::uint8_t minus) {
        if (digit > 0) {
            b |= plus;
        } else if (digit < 0) {
            b |= minus;
        }
    };
    set(b0, x[0], 0x01, 0x02); // x ±1
    set(b0, x[2], 0x04, 0x08); // x ±9
    set(b0, y[2], 0x20, 0x10); // y ±9
    set(b0, y[0], 0x80, 0x40); // y ±1
    set(b1, x[1], 0x01, 0x02); // x ±3
    set(b1, x[3], 0x04, 0x08); // x ±27
    set(b1, y[3], 0x20, 0x10); // y ±27
    set(b1, y[1], 0x80, 0x40); // y ±3
    set(b2, x[4], 0x04, 0x08); // x ±81
    set(b2, y[4], 0x20, 0x10); // y ±81

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

// Contraintes machine DST (HP-FMT-001) pour l'ÉCRITURE : `trim_zero_jump_count`
// vient de `DstWriteOptions::trim_jumps` (paramétrable par l'appelant).
MachineConstraints write_constraints(const DstWriteOptions& options) {
    MachineConstraints c;
    c.resolution_um = kUmPerDstUnit;
    c.max_record_delta = kMaxDelta;
    c.trim_encoding = MachineConstraints::TrimEncoding::RepeatedZeroJumps;
    c.trim_zero_jump_count = std::max(1, options.trim_jumps);
    c.trim_jumps_with_movement = options.trim_jumps_with_movement;
    c.merge_stop_into_color_change = true;
    return c;
}

// Contraintes machine DST pour la LECTURE : convention FIXE du format (3
// sauts nuls = une coupe), indépendante de `trim_jumps` -- qui n'est pas
// stocké dans le fichier, cf. l'ancien `decode_dst` (toujours ">= 3", jamais
// une valeur lue depuis `options`).
MachineConstraints read_constraints() {
    MachineConstraints c;
    c.resolution_um = kUmPerDstUnit;
    c.max_record_delta = kMaxDelta;
    c.trim_encoding = MachineConstraints::TrimEncoding::RepeatedZeroJumps;
    c.trim_zero_jump_count = 3;
    c.trim_jumps_with_movement = true; // relit aussi nos coupes à sauts non nuls
    c.merge_stop_into_color_change = true;
    return c;
}

} // namespace

Result<std::vector<std::uint8_t>> encode_dst(const stitch::StitchSequence& sequence,
                                             const DstWriteOptions& options) {
    // HP-FMT-001 : toute la normalisation (découpage, quantification sans
    // dérive, représentation des coupes, fusion Stop/ColorChange) est faite
    // ici ; ce qui suit n'est que sérialisation bit à bit + en-tête.
    auto normalized = normalize_for_machine(sequence, write_constraints(options));
    if (!normalized) {
        return std::unexpected(normalized.error());
    }

    std::vector<std::uint8_t> body;
    body.reserve(normalized->records.size() * 3 + 3);

    DstPoint pos{0, 0};
    DstPoint minP{0, 0};
    DstPoint maxP{0, 0};
    std::size_t colorChanges = 0;
    for (const auto& rec : normalized->records) {
        RecordType type = RecordType::Normal;
        switch (rec.type) {
        case MachineRecordType::Stitch:
            type = RecordType::Normal;
            break;
        case MachineRecordType::Jump:
            type = RecordType::Jump;
            break;
        case MachineRecordType::ColorChange:
            type = RecordType::ColorChange;
            ++colorChanges;
            break;
        case MachineRecordType::Stop:
        case MachineRecordType::Trim:
            // DST n'émet jamais ces types avec `write_constraints`
            // (RepeatedZeroJumps + fusion Stop) : garde défensive.
            return fail(ErrorCategory::Internal, "Enregistrement machine non représentable en DST");
        }
        const auto rec3 = encode_record(rec.dx, rec.dy, type);
        body.insert(body.end(), rec3.begin(), rec3.end());

        pos.x += rec.dx;
        pos.y += rec.dy;
        minP.x = std::min(minP.x, pos.x);
        minP.y = std::min(minP.y, pos.y);
        maxP.x = std::max(maxP.x, pos.x);
        maxP.y = std::max(maxP.y, pos.y);
    }
    body.insert(body.end(), {0x00, 0x00, 0xF3});

    // En-tête calculé depuis le corps réellement encodé.
    const std::size_t records = normalized->records.size();
    std::string name = options.design_name.substr(0, 16);
    std::string header;
    header += fmt::format("LA:{:<16}\r", name);
    header += fmt::format("ST:{:>7}\r", records);
    header += fmt::format("CO:{:>3}\r", colorChanges);
    header += fmt::format("+X:{:>5}\r", maxP.x);
    header += fmt::format("-X:{:>5}\r", -minP.x);
    header += fmt::format("+Y:{:>5}\r", maxP.y);
    header += fmt::format("-Y:{:>5}\r", -minP.y);
    header += fmt::format("AX:{}{:>5}\r", pos.x >= 0 ? '+' : '-', std::abs(pos.x));
    header += fmt::format("AY:{}{:>5}\r", pos.y >= 0 ? '+' : '-', std::abs(pos.y));
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
    //
    // Cette boucle ne fait QUE l'extraction bit à bit (dx, dy, type) propre
    // au format DST et la détection de son marqueur de fin -- toute la
    // reconstruction (positions absolues, run de sauts nuls -> Trim logique,
    // `End` final) est déléguée à `sequence_from_machine_records`
    // (HP-FMT-001).
    std::vector<MachineRecord> rawRecords;
    bool ended = false;
    for (std::size_t i = kHeaderSize; i + 2 < bytes.size(); i += 3) {
        const DecodedRecord rec = decode_record(bytes[i], bytes[i + 1], bytes[i + 2]);
        if (rec.end) {
            ended = true;
            break;
        }
        const MachineRecordType type = rec.type == RecordType::Normal ? MachineRecordType::Stitch
                                       : rec.type == RecordType::Jump
                                           ? MachineRecordType::Jump
                                           : MachineRecordType::ColorChange;
        rawRecords.push_back({rec.dx, rec.dy, type});
    }
    if (rawRecords.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le fichier DST ne contient aucun point");
    }
    if (!ended) {
        return fail(ErrorCategory::InvalidFile,
                    "Fichier DST sans marqueur de fin (fichier tronqué ?)");
    }
    return sequence_from_machine_records(rawRecords, read_constraints());
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
        return fail(ErrorCategory::UserInput, "Impossible d'écrire le fichier : " + path.string());
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

} // namespace openstitch::formats
