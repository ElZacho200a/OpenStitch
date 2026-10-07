// SPDX-License-Identifier: Apache-2.0
#include "openstitch/formats/machine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace openstitch::formats {

namespace {

struct NativePoint {
    int x{0};
    int y{0};
};

NativePoint quantize(Vec2um pos, std::int32_t resolution_um) {
    const double r = static_cast<double>(resolution_um);
    return {static_cast<int>(std::lround(static_cast<double>(pos.x.value) / r)),
            static_cast<int>(std::lround(static_cast<double>(pos.y.value) / r))};
}

// Découpe un déplacement quelconque en enregistrements dont |dx|,|dy| <=
// maxDelta. Tous les morceaux intermédiaires sont des `Jump` ; le dernier
// porte le type réellement demandé -- même convention que l'ancien
// `emit_move` de `dst.cpp` (ADR-008), généralisée au `maxDelta` du format.
void emit_move(std::vector<MachineRecord>& out, int dx, int dy, MachineRecordType type,
               int maxDelta) {
    while (std::abs(dx) > maxDelta || std::abs(dy) > maxDelta) {
        const int steps = std::max((std::abs(dx) + maxDelta - 1) / maxDelta,
                                   (std::abs(dy) + maxDelta - 1) / maxDelta);
        const int sx = dx / steps;
        const int sy = dy / steps;
        out.push_back({sx, sy, MachineRecordType::Jump});
        dx -= sx;
        dy -= sy;
    }
    out.push_back({dx, dy, type});
}

} // namespace

Result<MachineSequence> normalize_for_machine(const stitch::StitchSequence& sequence,
                                              const MachineConstraints& constraints) {
    std::vector<const stitch::StitchCommand*> moves;
    for (const auto& cmd : sequence.commands) {
        if (cmd.type != stitch::CommandType::End) {
            moves.push_back(&cmd);
        }
    }
    if (moves.empty()) {
        return fail(ErrorCategory::UserInput, "Aucun point à normaliser");
    }

    const int maxDelta = std::max(1, constraints.max_record_delta);
    const int trimCount = std::max(1, constraints.trim_zero_jump_count);
    const bool repeatedZeroJumps =
        constraints.trim_encoding == MachineConstraints::TrimEncoding::RepeatedZeroJumps;

    MachineSequence result;
    result.records.reserve(moves.size());

    NativePoint prev = quantize(moves.front()->pos, constraints.resolution_um);
    // Dernier enregistrement émis : un saut (Jump) de delta nul -- évite que
    // plusieurs sauts sous la résolution d'affilée soient relus comme une
    // coupe fantôme (convention `trim_zero_jump_count`, cf. tests dst).
    bool lastWasZeroJump = false;

    for (const auto* cmd : moves) {
        const NativePoint target = quantize(cmd->pos, constraints.resolution_um);
        const int dx = target.x - prev.x;
        const int dy = target.y - prev.y;
        if (cmd->type != stitch::CommandType::Jump) {
            lastWasZeroJump = (cmd->type == stitch::CommandType::Trim && dx == 0 && dy == 0);
        }
        switch (cmd->type) {
        case stitch::CommandType::Stitch:
            emit_move(result.records, dx, dy, MachineRecordType::Stitch, maxDelta);
            break;
        case stitch::CommandType::Jump:
            if (dx != 0 || dy != 0 || !lastWasZeroJump) {
                emit_move(result.records, dx, dy, MachineRecordType::Jump, maxDelta);
            }
            lastWasZeroJump = (dx == 0 && dy == 0);
            break;
        case stitch::CommandType::Trim:
            if (repeatedZeroJumps) {
                for (int i = 0; i < trimCount; ++i) {
                    emit_move(result.records, 0, 0, MachineRecordType::Jump, maxDelta);
                }
                if (dx != 0 || dy != 0) {
                    emit_move(result.records, dx, dy, MachineRecordType::Jump, maxDelta);
                }
            } else {
                emit_move(result.records, dx, dy, MachineRecordType::Trim, maxDelta);
            }
            break;
        case stitch::CommandType::ColorChange:
            emit_move(result.records, dx, dy, MachineRecordType::ColorChange, maxDelta);
            break;
        case stitch::CommandType::Stop:
            emit_move(result.records, dx, dy,
                      constraints.merge_stop_into_color_change ? MachineRecordType::ColorChange
                                                               : MachineRecordType::Stop,
                      maxDelta);
            break;
        case stitch::CommandType::End:
            break; // filtré dans `moves`, jamais atteint
        }
        prev = target;
    }
    return result;
}

stitch::StitchSequence sequence_from_machine_records(std::span<const MachineRecord> records,
                                                     const MachineConstraints& constraints) {
    stitch::StitchSequence sequence;
    const int trimCount = std::max(1, constraints.trim_zero_jump_count);
    const bool repeatedZeroJumps =
        constraints.trim_encoding == MachineConstraints::TrimEncoding::RepeatedZeroJumps;

    std::int32_t px = 0;
    std::int32_t py = 0;
    int pendingZeroJumps = 0;

    const auto make_pos = [&] {
        return Vec2um{Micrometers{px * constraints.resolution_um},
                      Micrometers{py * constraints.resolution_um}};
    };
    const auto flush_pending = [&] {
        if (repeatedZeroJumps && pendingZeroJumps >= trimCount) {
            sequence.commands.push_back({make_pos(), stitch::CommandType::Trim, ObjectId{}});
        } else {
            for (int i = 0; i < pendingZeroJumps; ++i) {
                sequence.commands.push_back({make_pos(), stitch::CommandType::Jump, ObjectId{}});
            }
        }
        pendingZeroJumps = 0;
    };

    for (const auto& rec : records) {
        if (repeatedZeroJumps && rec.type == MachineRecordType::Jump && rec.dx == 0 &&
            rec.dy == 0) {
            ++pendingZeroJumps;
            continue;
        }
        flush_pending();
        px += rec.dx;
        py += rec.dy;
        const Vec2um p = make_pos();
        switch (rec.type) {
        case MachineRecordType::Stitch:
            sequence.commands.push_back({p, stitch::CommandType::Stitch, ObjectId{}});
            break;
        case MachineRecordType::Jump:
            sequence.commands.push_back({p, stitch::CommandType::Jump, ObjectId{}});
            break;
        case MachineRecordType::ColorChange:
            sequence.commands.push_back({p, stitch::CommandType::ColorChange, ObjectId{}});
            break;
        case MachineRecordType::Stop:
            sequence.commands.push_back({p, stitch::CommandType::Stop, ObjectId{}});
            break;
        case MachineRecordType::Trim:
            sequence.commands.push_back({p, stitch::CommandType::Trim, ObjectId{}});
            break;
        }
    }
    flush_pending();

    if (!sequence.commands.empty()) {
        sequence.commands.push_back(
            {sequence.commands.back().pos, stitch::CommandType::End, ObjectId{}});
    }
    return sequence;
}

} // namespace openstitch::formats
