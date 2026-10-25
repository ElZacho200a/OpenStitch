// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_render/view_display.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace openstitch::stitch_render {

bool pass_visible(const PassDisplay& display, stitch::StitchPass pass) {
    switch (pass) {
    case stitch::StitchPass::Underlay:
        return display.underlay;
    case stitch::StitchPass::TopStitch:
        return display.top;
    case stitch::StitchPass::Travel:
        return display.travel;
    case stitch::StitchPass::Lock:
        return display.lock;
    case stitch::StitchPass::Manual:
        return display.manual;
    }
    return true;
}

std::array<std::uint8_t, 3> pass_tint(stitch::StitchPass pass) {
    switch (pass) {
    case stitch::StitchPass::Underlay:
        return {0x2E, 0x86, 0xC1}; // bleu
    case stitch::StitchPass::TopStitch:
        return {0x1E, 0x84, 0x49}; // vert
    case stitch::StitchPass::Travel:
        return {0x7F, 0x8C, 0x8D}; // gris
    case stitch::StitchPass::Lock:
        return {0xC0, 0x39, 0x2B}; // rouge
    case stitch::StitchPass::Manual:
        return {0xD6, 0x8F, 0x00}; // ambre
    }
    return {0, 0, 0};
}

PassCounts count_passes(const stitch::StitchSequence& sequence) {
    PassCounts counts;
    for (const auto& cmd : sequence.commands) {
        switch (cmd.type) {
        case stitch::CommandType::Stitch:
            ++counts.stitches[static_cast<std::size_t>(cmd.pass)];
            break;
        case stitch::CommandType::Jump:
            ++counts.jumps;
            break;
        case stitch::CommandType::Trim:
            ++counts.trims;
            break;
        default:
            break;
        }
    }
    return counts;
}

std::vector<FunctionMarker> collect_function_markers(const stitch::StitchSequence& sequence) {
    std::vector<FunctionMarker> out;
    bool has_pos = false;
    bool chain_open = false; // un enchaînement de pénétrations est en cours
    float last_x = 0.0F;
    float last_y = 0.0F;
    std::uint64_t last_source = 0;
    for (const auto& cmd : sequence.commands) {
        const float x = static_cast<float>(static_cast<double>(cmd.pos.x.value) / 1000.0);
        const float y = static_cast<float>(-static_cast<double>(cmd.pos.y.value) / 1000.0);
        switch (cmd.type) {
        case stitch::CommandType::Stitch:
            if (!chain_open || last_source != cmd.source.value) {
                out.push_back(FunctionMarker{x, y, FunctionKind::Entry});
            }
            chain_open = true;
            last_x = x;
            last_y = y;
            last_source = cmd.source.value;
            has_pos = true;
            break;
        case stitch::CommandType::Jump:
            last_x = x;
            last_y = y;
            last_source = cmd.source.value;
            has_pos = true;
            break;
        case stitch::CommandType::Trim:
            if (has_pos) {
                out.push_back(FunctionMarker{last_x, last_y, FunctionKind::Cut});
            }
            chain_open = false;
            break;
        case stitch::CommandType::ColorChange:
            chain_open = false;
            break;
        default:
            break;
        }
    }
    return out;
}

GridSettings sanitized(GridSettings settings) {
    if (!std::isfinite(settings.step_mm) || settings.step_mm <= 0.0) {
        settings.step_mm = 0.0;
    } else {
        settings.step_mm = std::clamp(settings.step_mm, kGridStepMinMm, kGridStepMaxMm);
    }
    settings.major_every = std::clamp(settings.major_every, 1, 50);
    return settings;
}

namespace {

double nice_step_mm(double px_per_mm, double min_pixels) {
    static constexpr double steps[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500};
    for (const double s : steps) {
        if (s * px_per_mm >= min_pixels) {
            return s;
        }
    }
    return 1000.0;
}

} // namespace

GridPlan plan_grid(const GridSettings& raw, double px_per_mm) {
    const GridSettings settings = sanitized(raw);
    GridPlan plan;
    if (!settings.visible || !(px_per_mm > 0.0)) {
        return plan;
    }
    if (settings.step_mm <= 0.0) {
        plan.minor_mm = nice_step_mm(px_per_mm, 8.0);
        plan.major_mm = nice_step_mm(px_per_mm, 40.0);
        return plan;
    }
    constexpr double kMinPixels = 6.0;
    // À fort dézoom, le pas fin est regroupé par multiples de `major_every` (ou doublé).
    const double factor =
        settings.major_every > 1 ? static_cast<double>(settings.major_every) : 2.0;
    double minor = settings.step_mm;
    while (minor * px_per_mm < kMinPixels && minor < 100000.0) {
        minor *= factor;
    }
    plan.minor_mm = minor;
    plan.major_mm = settings.major_every > 1 ? minor * settings.major_every : minor;
    if (settings.major_every <= 1) {
        plan.minor_mm = 0.0; // un seul niveau : les lignes sont toutes majeures
    }
    return plan;
}

std::vector<double> grid_positions(double step_mm, double lo, double hi, std::size_t max_lines) {
    std::vector<double> out;
    if (!(step_mm > 0.0) || !std::isfinite(lo) || !std::isfinite(hi) || hi < lo) {
        return out;
    }
    const double first = std::ceil(lo / step_mm);
    const double last = std::floor(hi / step_mm);
    if (last < first || last - first + 1.0 > static_cast<double>(max_lines)) {
        return out;
    }
    for (double k = first; k <= last; k += 1.0) {
        out.push_back(k * step_mm);
    }
    return out;
}

std::vector<Guide> normalized(std::vector<Guide> guides) {
    std::sort(guides.begin(), guides.end());
    guides.erase(std::unique(guides.begin(), guides.end()), guides.end());
    return guides;
}

std::string serialize_guides(const std::vector<Guide>& guides) {
    std::string out;
    for (const Guide& g : normalized(guides)) {
        if (!out.empty()) {
            out += ';';
        }
        out += g.axis == GuideAxis::Vertical ? "v:" : "h:";
        out += std::to_string(g.position.value);
    }
    return out;
}

std::vector<Guide> parse_guides(std::string_view text) {
    std::vector<Guide> out;
    while (!text.empty() && out.size() < kMaxGuides) {
        const std::size_t cut = text.find(';');
        const std::string_view item = text.substr(0, cut);
        text = cut == std::string_view::npos ? std::string_view{} : text.substr(cut + 1);
        if (item.size() < 3 || item[1] != ':' || (item[0] != 'v' && item[0] != 'h')) {
            continue;
        }
        const std::string_view number = item.substr(2);
        std::int32_t value = 0;
        const auto [ptr, ec] = std::from_chars(number.data(), number.data() + number.size(), value);
        if (ec != std::errc{} || ptr != number.data() + number.size()) {
            continue;
        }
        out.push_back(Guide{item[0] == 'v' ? GuideAxis::Vertical : GuideAxis::Horizontal,
                            Micrometers{value}});
    }
    return normalized(std::move(out));
}

} // namespace openstitch::stitch_render
