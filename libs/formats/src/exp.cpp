// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/exp.hpp"

#include "codec_common.hpp"

namespace openstitch::formats {

namespace {

constexpr int kMaxDelta = 127;

std::uint8_t signed_byte(int v) {
    return static_cast<std::uint8_t>(static_cast<std::int8_t>(v));
}

} // namespace

MachineConstraints exp_constraints() {
    MachineConstraints c;
    c.resolution_um = 100;
    c.max_record_delta = kMaxDelta;
    c.trim_encoding = MachineConstraints::TrimEncoding::Native;
    c.merge_stop_into_color_change = true;
    return c;
}

Result<std::vector<std::uint8_t>> encode_exp(const stitch::StitchSequence& sequence,
                                             const MachineExportOptions& options) {
    const auto prepared = detail::prepare_design(sequence, options);
    auto normalized = normalize_for_machine(prepared.sequence, exp_constraints());
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    detail::ByteVec out;
    out.reserve(normalized->records.size() * 2 + 16);

    const auto jump = [&](int dx, int dy) {
        detail::put_u8(out, 0x80);
        detail::put_u8(out, 0x04);
        detail::put_u8(out, signed_byte(dx));
        detail::put_u8(out, signed_byte(dy));
    };
    for (const auto& rec : normalized->records) {
        switch (rec.type) {
        case MachineRecordType::Stitch:
            detail::put_u8(out, signed_byte(rec.dx));
            detail::put_u8(out, signed_byte(rec.dy));
            break;
        case MachineRecordType::Jump:
            jump(rec.dx, rec.dy);
            break;
        case MachineRecordType::Trim:
            for (const unsigned b : {0x80u, 0x80u, 0x07u, 0x00u}) {
                detail::put_u8(out, b);
            }
            if (rec.dx != 0 || rec.dy != 0) {
                jump(rec.dx, rec.dy);
            }
            break;
        case MachineRecordType::ColorChange:
        case MachineRecordType::Stop:
            if (rec.dx != 0 || rec.dy != 0) {
                jump(rec.dx, rec.dy);
            }
            for (const unsigned b : {0x80u, 0x01u, 0x00u, 0x00u}) {
                detail::put_u8(out, b);
            }
            break;
        }
    }
    return out;
}

Result<DecodedDesign> decode_exp(std::span<const std::uint8_t> bytes) {
    std::vector<MachineRecord> records;
    const auto sbyte = [](std::uint8_t b) { return static_cast<int>(static_cast<std::int8_t>(b)); };
    for (std::size_t i = 0; i + 1 < bytes.size();) {
        if (bytes[i] != 0x80) {
            records.push_back({sbyte(bytes[i]), sbyte(bytes[i + 1]), MachineRecordType::Stitch});
            i += 2;
            continue;
        }
        if (i + 3 >= bytes.size()) {
            break; // commande tronquée
        }
        const std::uint8_t code = bytes[i + 1];
        const int dx = sbyte(bytes[i + 2]);
        const int dy = sbyte(bytes[i + 3]);
        i += 4;
        bool known = true;
        switch (code) {
        case 0x80:
            records.push_back({0, 0, MachineRecordType::Trim});
            break;
        case 0x04:
            records.push_back({dx, dy, MachineRecordType::Jump});
            break;
        case 0x01:
            records.push_back({dx, dy, MachineRecordType::ColorChange});
            break;
        case 0x02:
            records.push_back({dx, dy, MachineRecordType::Stitch});
            break;
        default:
            known = false;
            break;
        }
        if (!known) {
            break;
        }
    }
    if (records.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le fichier EXP ne contient aucun point");
    }
    DecodedDesign design;
    design.sequence = sequence_from_machine_records(records, exp_constraints());
    return design;
}

} // namespace openstitch::formats
