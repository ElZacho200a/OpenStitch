// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_generation/pull_map.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace openstitch::stitch_generation {

namespace {

constexpr std::size_t kMaxCells = 512;
constexpr double kReachSigmas = 3.0;

std::int32_t round_um(double v) {
    return static_cast<std::int32_t>(std::lround(v));
}

} // namespace

std::vector<PullSource> collect_pull_sources(const stitch::StitchSequence& sequence) {
    std::vector<PullSource> sources;
    const auto& cmds = sequence.commands;
    for (std::size_t i = 1; i < cmds.size(); ++i) {
        const stitch::StitchCommand& a = cmds[i - 1];
        const stitch::StitchCommand& b = cmds[i];
        if (a.type == stitch::CommandType::Stitch && b.type == stitch::CommandType::Stitch &&
            a.source == b.source && b.pass != stitch::StitchPass::Travel &&
            b.pass != stitch::StitchPass::Lock && a.pos != b.pos) {
            sources.push_back({a.pos, b.pos, 1.0, static_cast<std::uint32_t>(i)});
        }
    }
    return sources;
}

FabricResponse fabric_preset(std::string_view name) {
    FabricResponse f;
    if (name == "linen") {
        f.gain = 0.030;
        f.max_displacement_mm = 1.0;
        f.grain_anisotropy = 1.3;
    } else if (name == "stable") {
        f.gain = 0.012;
        f.max_displacement_mm = 0.5;
    }
    return f;
}

const std::vector<std::string_view>& fabric_preset_names() {
    static const std::vector<std::string_view> names{"default", "linen", "stable"};
    return names;
}

PullMap compute_pull_map(const stitch::StitchSequence& sequence, const FabricResponse& fabric,
                         Micrometers cell) {
    PullMap map;
    std::vector<PullSource> sources = collect_pull_sources(sequence);
    if (sources.empty() || cell.value <= 0) {
        return map;
    }
    const double reach =
        kReachSigmas * std::max(fabric.sigma_along_mm, fabric.sigma_across_mm) * 1000.0;
    double x0 = 1e18;
    double y0 = 1e18;
    double x1 = -1e18;
    double y1 = -1e18;
    for (const PullSource& s : sources) {
        for (const Vec2um p : {s.a, s.b}) {
            x0 = std::min(x0, static_cast<double>(p.x.value));
            x1 = std::max(x1, static_cast<double>(p.x.value));
            y0 = std::min(y0, static_cast<double>(p.y.value));
            y1 = std::max(y1, static_cast<double>(p.y.value));
        }
    }
    x0 -= reach;
    y0 -= reach;
    x1 += reach;
    y1 += reach;

    double side = static_cast<double>(cell.value);
    side = std::max(side, std::ceil((x1 - x0) / static_cast<double>(kMaxCells)));
    side = std::max(side, std::ceil((y1 - y0) / static_cast<double>(kMaxCells)));
    map.cell = Micrometers{round_um(side)};
    map.origin = {{round_um(std::floor(x0))}, {round_um(std::floor(y0))}};
    map.columns = static_cast<std::size_t>(std::ceil((x1 - x0) / side));
    map.rows = static_cast<std::size_t>(std::ceil((y1 - y0) / side));
    map.samples.resize(map.columns * map.rows);

    const PullField field(std::move(sources), fabric);
    for (std::size_t row = 0; row < map.rows; ++row) {
        for (std::size_t col = 0; col < map.columns; ++col) {
            const Vec2um centre{
                {round_um(map.origin.x.value + (static_cast<double>(col) + 0.5) * side)},
                {round_um(map.origin.y.value + (static_cast<double>(row) + 0.5) * side)}};
            map.samples[row * map.columns + col] = field.sample(centre);
        }
    }
    return map;
}

PullPeak find_pull_peak(const PullMap& map) {
    PullPeak peak;
    double best = 0.0;
    for (std::size_t row = 0; row < map.rows; ++row) {
        for (std::size_t col = 0; col < map.columns; ++col) {
            const PullSample& s = map.at(col, row);
            const double mag = std::hypot(static_cast<double>(s.displacement.x.value),
                                          static_cast<double>(s.displacement.y.value));
            if (mag > best) { // strict : premier maximum en ordre ligne par ligne (déterministe)
                best = mag;
                peak.found = true;
                peak.displacement = s.displacement;
                peak.position = {{round_um(map.origin.x.value +
                                           (static_cast<double>(col) + 0.5) * map.cell.value)},
                                 {round_um(map.origin.y.value +
                                           (static_cast<double>(row) + 0.5) * map.cell.value)}};
            }
        }
    }
    peak.magnitude_mm = best / 1000.0;
    return peak;
}

PullRaster rasterize_pull_map(const PullMap& map, double scale_mm) {
    PullRaster raster;
    if (map.empty()) {
        return raster;
    }
    raster.width = static_cast<int>(map.columns);
    raster.height = static_cast<int>(map.rows);
    if (scale_mm <= 0.0) {
        scale_mm = std::max(find_pull_peak(map).magnitude_mm, 0.1);
    }
    raster.scale_mm = scale_mm;
    raster.rgba.assign(map.columns * map.rows * 4, 0);
    for (std::size_t row = 0; row < map.rows; ++row) {
        const std::size_t imageRow = map.rows - 1 - row;
        for (std::size_t col = 0; col < map.columns; ++col) {
            const PullSample& s = map.at(col, row);
            if (s.contributors == 0) {
                continue;
            }
            const double mm = std::hypot(static_cast<double>(s.displacement.x.value),
                                         static_cast<double>(s.displacement.y.value)) /
                              1000.0;
            const double t = std::clamp(mm / scale_mm, 0.0, 1.0);
            // bleu (0) -> vert (0,5) -> rouge (1)
            const double r = std::clamp(2.0 * t - 0.5, 0.0, 1.0);
            const double g = 1.0 - std::abs(2.0 * t - 1.0);
            const double b = std::clamp(1.0 - 2.0 * t, 0.0, 1.0);
            const std::size_t at = (imageRow * map.columns + col) * 4;
            raster.rgba[at + 0] = static_cast<std::uint8_t>(std::lround(r * 255.0));
            raster.rgba[at + 1] = static_cast<std::uint8_t>(std::lround(g * 255.0));
            raster.rgba[at + 2] = static_cast<std::uint8_t>(std::lround(b * 255.0));
            raster.rgba[at + 3] = static_cast<std::uint8_t>(std::lround(70.0 + 130.0 * t));
        }
    }
    return raster;
}

stitch::StitchSequence simulate_fabric_pull(const stitch::StitchSequence& sequence,
                                            const FabricResponse& fabric) {
    stitch::StitchSequence out = sequence;
    std::vector<PullSource> sources = collect_pull_sources(sequence);
    if (sources.empty()) {
        return out;
    }
    const PullField field(std::move(sources), fabric);
    const auto& cmds = sequence.commands;
    // Chaque point est posé sur un tissu déjà déformé par ce qui précède et suit ensuite le tissu :
    // son décalage final est le tirage des segments de rang >= au sien (lui compris). Le premier
    // point subit donc tout le champ, le dernier presque rien : un contour cousu après son
    // remplissage retombe à sa place nominale alors que le bord du remplissage s'est rétracté.
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        if (cmds[i].type == stitch::CommandType::Stitch) {
            out.commands[i].pos =
                cmds[i].pos + field.sample(cmds[i].pos, static_cast<std::uint32_t>(i)).displacement;
        }
    }
    return out;
}

} // namespace openstitch::stitch_generation
