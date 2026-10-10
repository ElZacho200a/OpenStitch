// SPDX-License-Identifier: Apache-2.0
// Briques internes partagées par les codecs PES / JEF / EXP (HP-FMT-002..005).
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "openstitch/formats/machine.hpp"
#include "openstitch/formats/machine_design.hpp"
#include "openstitch/stitch/sequence.hpp"

namespace openstitch::formats::detail {

using ByteVec = std::vector<std::uint8_t>;
using Rgb = std::array<std::uint8_t, 3>;

enum class SegmentKind : std::uint8_t { Start, ColorChange, Stop };

// Un segment = la portion de motif entre deux changements de couleur/arrêts.
struct Segment {
    Rgb rgb{};
    SegmentKind kind{SegmentKind::Start};
};

// Séquence à encoder après application des options (coupes/arrêts/couleurs) + la liste des
// segments (un de plus que le nombre de commandes ColorChange/Stop restantes).
struct PreparedDesign {
    stitch::StitchSequence sequence;
    std::vector<Segment> segments;
};

[[nodiscard]] Rgb default_block_color(std::size_t block_index);
[[nodiscard]] PreparedDesign prepare_design(const stitch::StitchSequence& sequence,
                                            const MachineExportOptions& options);

// Boîte englobante cumulée (unités natives) d'une séquence normalisée : positions des
// enregistrements `Stitch` ; repli sur tous les enregistrements s'il n'y a aucun point cousu.
struct UnitBounds {
    int min_x{0};
    int min_y{0};
    int max_x{0};
    int max_y{0};
    [[nodiscard]] int width() const { return max_x - min_x; }
    [[nodiscard]] int height() const { return max_y - min_y; }
};
[[nodiscard]] UnitBounds record_bounds(std::span<const MachineRecord> records);

// Retire les enregistrements `Jump` non nuls en tête de flux (déplacement de positionnement
// depuis le centre/l'origine du format, écrit par nos encodeurs) : le décodeur fixe l'origine
// du motif au point d'arrivée, comme `decode_dst` (positions relatives au point de départ).
void strip_leading_positioning_jumps(std::vector<MachineRecord>& records);

// Découpe un déplacement en pas dont |dx|,|dy| <= max_delta (même algorithme que
// `normalize_for_machine`) ; sert au déplacement de positionnement initial.
[[nodiscard]] std::vector<std::pair<int, int>> split_move(int dx, int dy, int max_delta);

// Division entière par 2 arrondie vers -infini (centre de boîte invariant par translation).
[[nodiscard]] inline int floor_half(int v) {
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

// Vérifie les limites de format avant encodage : nombre de couleurs, étendue.
[[nodiscard]] Result<void> check_limits(const MachineConstraints& constraints,
                                        const PreparedDesign& design, const UnitBounds& bounds,
                                        const char* format_name);

inline void put_u8(ByteVec& out, unsigned v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFFu));
}
inline void put_u16le(ByteVec& out, unsigned v) {
    put_u8(out, v);
    put_u8(out, v >> 8);
}
inline void put_u16be(ByteVec& out, unsigned v) {
    put_u8(out, v >> 8);
    put_u8(out, v);
}
inline void put_u24le(ByteVec& out, unsigned v) {
    put_u8(out, v);
    put_u8(out, v >> 8);
    put_u8(out, v >> 16);
}
inline void put_u32le(ByteVec& out, std::uint32_t v) {
    put_u16le(out, v & 0xFFFFu);
    put_u16le(out, v >> 16);
}
inline void put_i32le(ByteVec& out, std::int32_t v) {
    put_u32le(out, static_cast<std::uint32_t>(v));
}
inline void put_f32le(ByteVec& out, float f) {
    static_assert(sizeof(float) == 4);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    put_u32le(out, bits);
}
inline void put_text(ByteVec& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
}
inline void put_fill(ByteVec& out, std::size_t count, std::uint8_t value) {
    out.insert(out.end(), count, value);
}

// Nom de motif ASCII imprimable, tronqué à `max_len` (les autres caractères deviennent '?').
[[nodiscard]] std::string ascii_name(const std::string& name, std::size_t max_len);

} // namespace openstitch::formats::detail
