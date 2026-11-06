// SPDX-License-Identifier: Apache-2.0
#include "thread_palette_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/thread_palette/catalog.hpp"
#include "openstitch/thread_palette/color_distance.hpp"
#include "thread_plan.hpp"

namespace openstitch::desktop_v2 {

namespace tp = openstitch::thread_palette;

namespace {
std::string lower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

ThreadChoice choiceOf(const tp::Thread& t, double distance) {
    return {t.key.chart_id, t.key.code, t.name, t.brand, t.rgb, distance};
}

double toLinear(std::uint8_t v) {
    const double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

std::uint8_t fromLinear(double l) {
    const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0));
}

std::string hex(Rgb c) {
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", c[0], c[1], c[2]);
    return buffer;
}
} // namespace

std::vector<ChartInfo> chartInfos() {
    std::vector<ChartInfo> out;
    for (const auto& chart : tp::all_charts()) {
        out.push_back(
            {chart.chart_id, chart.display_name, chart.source_note, chart.threads.size()});
    }
    return out;
}

std::vector<ThreadChoice> searchThreads(const std::string& chartId, const std::string& query,
                                        std::size_t limit) {
    std::vector<ThreadChoice> out;
    const auto* chart = tp::find_chart(chartId);
    if (chart == nullptr) {
        return out;
    }
    const std::string needle = lower(query);
    for (const auto& t : chart->threads) {
        if (out.size() >= limit) {
            break;
        }
        if (needle.empty() || lower(t.name).find(needle) != std::string::npos ||
            lower(t.key.code).find(needle) != std::string::npos) {
            out.push_back(choiceOf(t, 0.0));
        }
    }
    return out;
}

std::vector<ThreadChoice> nearestThreads(Rgb rgb, const std::string& chartId, std::size_t topN) {
    std::vector<ThreadChoice> out;
    const auto* chart = tp::find_chart(chartId);
    if (chart == nullptr || topN == 0) {
        return out;
    }
    for (const auto& match : tp::nearest_threads(rgb, *chart, topN)) {
        if (const auto thread = tp::find_by_code(match.key.chart_id, match.key.code)) {
            out.push_back(choiceOf(*thread, match.distance));
        }
    }
    return out;
}

ServiceResult assignChartThread(DocumentSession& session, std::size_t entry,
                                const std::string& chartId, const std::string& code) {
    const auto entries = threadEntries(session.project());
    if (entry >= entries.size()) {
        return {false, "Choisissez un fil existant du plan de fils."};
    }
    const auto thread = tp::find_by_code(chartId, code);
    if (!thread) {
        return {false, "Fil inconnu dans ce nuancier."};
    }
    std::vector<ObjectId> ids;
    for (const auto id : entries[entry].objects) {
        ids.push_back(ObjectId{id});
    }
    session.execute(std::make_unique<commands::SetObjectThreadCommand>(
        ids, thread->key, thread->rgb, "Assigner un fil de nuancier"));
    return {true, {}};
}

Rgb blendRgb(Rgb a, Rgb b, double ratioB) {
    const double t = std::clamp(std::isfinite(ratioB) ? ratioB : 0.5, 0.0, 1.0);
    Rgb out{};
    for (std::size_t i = 0; i < 3; ++i) {
        out[i] = fromLinear((1.0 - t) * toLinear(a[i]) + t * toLinear(b[i]));
    }
    return out;
}

std::string BlendSuggestion::summary() const {
    char buffer[420];
    std::snprintf(buffer, sizeof buffer,
                  "%d %% %s (%s) + %d %% %s (%s) donnent %s, écart %.1f à la cible ; le meilleur "
                  "fil seul est à %.1f.%s",
                  static_cast<int>(std::lround((1.0 - ratioB) * 100.0)), a.code.c_str(),
                  a.name.c_str(), static_cast<int>(std::lround(ratioB * 100.0)), b.code.c_str(),
                  b.name.c_str(), hex(mixed).c_str(), mixedError, singleError,
                  betterThanSingle() ? " Le mélange est plus proche qu'un fil seul."
                                     : " Un fil seul fait aussi bien : le mélange n'apporte rien.");
    return buffer;
}

std::optional<BlendSuggestion> suggestBlend(Rgb target, const std::string& chartId,
                                            std::size_t candidates) {
    const auto pool = nearestThreads(target, chartId, std::max<std::size_t>(candidates, 2));
    if (pool.size() < 2) {
        return std::nullopt;
    }
    const auto targetLab = tp::to_cielab(target);
    std::optional<BlendSuggestion> best;
    for (std::size_t i = 0; i < pool.size(); ++i) {
        for (std::size_t j = i + 1; j < pool.size(); ++j) {
            for (int step = 1; step <= 9; ++step) { // ordre fixe : déterministe
                const double ratio = step / 10.0;
                const Rgb mixed = blendRgb(pool[i].rgb, pool[j].rgb, ratio);
                const double error = tp::ciede2000(tp::to_cielab(mixed), targetLab);
                if (!best || error < best->mixedError - 1e-12) {
                    best = BlendSuggestion{pool[i], pool[j], ratio,
                                           mixed,   error,   pool.front().distance};
                }
            }
        }
    }
    return best;
}

} // namespace openstitch::desktop_v2
