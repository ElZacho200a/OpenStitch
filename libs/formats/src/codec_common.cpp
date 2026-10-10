// SPDX-License-Identifier: Apache-2.0
#include "codec_common.hpp"

#include <fmt/format.h>

namespace openstitch::formats::detail {

namespace {

constexpr std::array<Rgb, 8> kDefaultColors = {{
    {0, 0, 0},
    {237, 23, 31},
    {10, 85, 163},
    {112, 188, 31},
    {255, 255, 0},
    {145, 54, 151},
    {254, 158, 50},
    {0, 135, 119},
}};

} // namespace

Rgb default_block_color(std::size_t block_index) {
    return kDefaultColors[block_index % kDefaultColors.size()];
}

PreparedDesign prepare_design(const stitch::StitchSequence& sequence,
                              const MachineExportOptions& options) {
    using stitch::CommandType;
    PreparedDesign out;
    out.sequence.commands.reserve(sequence.commands.size());

    const auto color_for = [&](std::size_t block) -> Rgb {
        if (options.block_colors.empty()) {
            return default_block_color(block);
        }
        return options.block_colors[std::min(block, options.block_colors.size() - 1)];
    };

    // Retire une commande en conservant son déplacement (saut) si la position change.
    const auto drop = [&](const stitch::StitchCommand& cmd) {
        if (out.sequence.commands.empty() || out.sequence.commands.back().pos != cmd.pos) {
            stitch::StitchCommand jump = cmd;
            jump.type = CommandType::Jump;
            out.sequence.commands.push_back(jump);
        }
    };

    out.segments.push_back({color_for(0), SegmentKind::Start});
    std::size_t closed = 0; // blocs non vides déjà fermés (même règle que `color_blocks`)
    std::size_t start = 0;
    for (std::size_t i = 0; i < sequence.commands.size(); ++i) {
        const auto& cmd = sequence.commands[i];
        const bool boundary = cmd.type == CommandType::ColorChange ||
                              cmd.type == CommandType::Stop || cmd.type == CommandType::End;
        if (boundary) {
            if (i > start) {
                ++closed;
            }
            start = i + 1;
        }
        switch (cmd.type) {
        case CommandType::Trim:
            if (options.trims == TrimMode::Drop) {
                drop(cmd);
            } else {
                out.sequence.commands.push_back(cmd);
            }
            break;
        case CommandType::ColorChange:
            if (options.color_changes == ColorChangeMode::Drop) {
                drop(cmd);
            } else {
                out.sequence.commands.push_back(cmd);
                out.segments.push_back({color_for(closed), SegmentKind::ColorChange});
            }
            break;
        case CommandType::Stop:
            if (options.stops == StopMode::Drop) {
                drop(cmd);
            } else if (options.stops == StopMode::AsColorChange) {
                stitch::StitchCommand asChange = cmd;
                asChange.type = CommandType::ColorChange;
                out.sequence.commands.push_back(asChange);
                out.segments.push_back({color_for(closed), SegmentKind::ColorChange});
            } else {
                out.sequence.commands.push_back(cmd);
                out.segments.push_back({color_for(closed), SegmentKind::Stop});
            }
            break;
        default:
            out.sequence.commands.push_back(cmd);
            break;
        }
    }
    return out;
}

UnitBounds record_bounds(std::span<const MachineRecord> records) {
    const auto compute = [&](bool onlyStitches) {
        UnitBounds b;
        bool any = false;
        int x = 0;
        int y = 0;
        for (const auto& r : records) {
            x += r.dx;
            y += r.dy;
            if (onlyStitches && r.type != MachineRecordType::Stitch) {
                continue;
            }
            if (!any) {
                b = {x, y, x, y};
                any = true;
            } else {
                b.min_x = std::min(b.min_x, x);
                b.min_y = std::min(b.min_y, y);
                b.max_x = std::max(b.max_x, x);
                b.max_y = std::max(b.max_y, y);
            }
        }
        return std::pair{b, any};
    };
    auto [bounds, any] = compute(true);
    if (!any) {
        bounds = compute(false).first;
    }
    return bounds;
}

std::vector<std::pair<int, int>> split_move(int dx, int dy, int max_delta) {
    std::vector<std::pair<int, int>> steps;
    const int maxDelta = std::max(1, max_delta);
    while (std::abs(dx) > maxDelta || std::abs(dy) > maxDelta) {
        const int n = std::max((std::abs(dx) + maxDelta - 1) / maxDelta,
                               (std::abs(dy) + maxDelta - 1) / maxDelta);
        const int sx = dx / n;
        const int sy = dy / n;
        steps.emplace_back(sx, sy);
        dx -= sx;
        dy -= sy;
    }
    steps.emplace_back(dx, dy);
    return steps;
}

void strip_leading_positioning_jumps(std::vector<MachineRecord>& records) {
    std::size_t n = 0;
    while (n < records.size() && records[n].type == MachineRecordType::Jump &&
           (records[n].dx != 0 || records[n].dy != 0)) {
        ++n;
    }
    records.erase(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(n));
}

Result<void> check_limits(const MachineConstraints& constraints, const PreparedDesign& design,
                          const UnitBounds& bounds, const char* format_name) {
    if (constraints.max_colors &&
        static_cast<int>(design.segments.size()) > *constraints.max_colors) {
        return fail(ErrorCategory::OperationImpossible,
                    fmt::format("Trop de couleurs pour le format {} : {} blocs de couleur, "
                                "maximum {}.",
                                format_name, design.segments.size(), *constraints.max_colors),
                    "Fusionnez des couleurs ou exportez en plusieurs fichiers.");
    }
    if (constraints.max_extent_um) {
        const std::int64_t w =
            static_cast<std::int64_t>(bounds.width()) * constraints.resolution_um;
        const std::int64_t h =
            static_cast<std::int64_t>(bounds.height()) * constraints.resolution_um;
        if (w > *constraints.max_extent_um || h > *constraints.max_extent_um) {
            return fail(ErrorCategory::OperationImpossible,
                        fmt::format("Motif trop grand pour le format {} : {:.1f} x {:.1f} mm, "
                                    "maximum {:.1f} mm par côté.",
                                    format_name, static_cast<double>(w) / 1000.0,
                                    static_cast<double>(h) / 1000.0,
                                    static_cast<double>(*constraints.max_extent_um) / 1000.0));
        }
    }
    return {};
}

std::string ascii_name(const std::string& name, std::size_t max_len) {
    std::string out;
    for (const char c : name) {
        if (out.size() >= max_len) {
            break;
        }
        const auto u = static_cast<unsigned char>(c);
        out.push_back(u >= 0x20 && u < 0x7F ? c : '?');
    }
    return out;
}

} // namespace openstitch::formats::detail
