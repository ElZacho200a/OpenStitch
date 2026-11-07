// SPDX-License-Identifier: Apache-2.0
#include "thread_palette_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "openstitch/commands/colorway_commands.hpp"
#include "openstitch/commands/thread_commands.hpp"
#include "openstitch/core/atomic_file.hpp"
#include <fstream>

#include "openstitch/project_io/thread_chart_io.hpp"
#include "openstitch/thread_palette/catalog.hpp"
#include "openstitch/thread_palette/color_distance.hpp"
#include "thread_plan.hpp"

namespace openstitch::desktop_v2 {

namespace tp = openstitch::thread_palette;

Result<tp::ThreadStock> loadThreadStock(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        if (error)
            return fail(ErrorCategory::OperationImpossible, "Lecture du stock impossible",
                        error.message());
        return tp::ThreadStock{};
    }
    const auto size = std::filesystem::file_size(file, error);
    if (error || size > 8 * 1024 * 1024) {
        return fail(ErrorCategory::InvalidFile, "Stock de fils illisible ou trop volumineux");
    }
    std::ifstream input(file, std::ios::binary);
    if (!input)
        return fail(ErrorCategory::OperationImpossible, "Ouverture du stock impossible");
    std::ostringstream data;
    data << input.rdbuf();
    if (input.bad())
        return fail(ErrorCategory::OperationImpossible, "Lecture du stock interrompue");
    return tp::stock_from_json(data.str());
}

Result<void> saveThreadStock(const std::filesystem::path& file, const tp::ThreadStock& stock) {
    const auto json = tp::stock_to_json(stock);
    if (json.size() > 8 * 1024 * 1024) {
        return fail(ErrorCategory::UserInput, "Stock de fils trop volumineux");
    }
    std::error_code error;
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path(), error);
    if (error)
        return fail(ErrorCategory::OperationImpossible, "Dossier du stock inaccessible",
                    error.message());
    return write_file_atomic(file, std::string_view(json));
}

Result<ThreadColorComparison> compareThreadColors(Rgb a, Rgb b, double ratioB) {
    if (!std::isfinite(ratioB) || ratioB < 0 || ratioB > 1) {
        return fail(ErrorCategory::UserInput, "Part du second fil : entre 0 et 100 %");
    }
    return ThreadColorComparison{blendRgb(a, b, ratioB),
                                 tp::ciede2000(tp::to_cielab(a), tp::to_cielab(b))};
}

Result<document::Project> colorwayPreviewProject(const document::Project& project, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= project.colorways.size()) {
        return fail(ErrorCategory::UserInput, "Ce coloris n'existe pas");
    }
    auto copy = project;
    commands::SetActiveColorwayCommand command(index);
    command.apply(copy);
    return copy;
}

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

// Nuanciers importés par l'utilisateur, dans l'ordre d'import (jamais de conteneur non ordonné).
std::vector<tp::ThreadChart>& userCharts() {
    static std::vector<tp::ThreadChart> charts;
    return charts;
}

const tp::ThreadChart* findAnyChart(const std::string& id) {
    if (const auto* builtin = tp::find_chart(id)) {
        return builtin;
    }
    for (const auto& chart : userCharts()) {
        if (chart.chart_id == id) {
            return &chart;
        }
    }
    return nullptr;
}

std::optional<tp::Thread> findAnyThread(const std::string& chartId, const std::string& code) {
    if (const auto* chart = findAnyChart(chartId)) {
        for (const auto& t : chart->threads) {
            if (t.key.code == code) {
                return t;
            }
        }
    }
    return std::nullopt;
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
            {chart.chart_id, chart.display_name, chart.source_note, chart.threads.size(), false});
    }
    for (const auto& chart : userCharts()) {
        out.push_back(
            {chart.chart_id, chart.display_name, chart.source_note, chart.threads.size(), true});
    }
    return out;
}

std::vector<ThreadChoice> stockChoices(const tp::ThreadStock& stock) {
    std::vector<ThreadChoice> result;
    for (const auto& key : stock.owned_keys()) {
        if (const auto thread = findAnyThread(key.chart_id, key.code)) {
            result.push_back(choiceOf(*thread, 0));
        } else {
            result.push_back({key.chart_id, key.code, "Nuancier indisponible", {}, {0, 0, 0}, 0});
        }
    }
    for (const auto& thread : stock.personal_threads())
        result.push_back(choiceOf(thread, 0));
    return result;
}

Result<ChartInfo> importThreadChart(const std::filesystem::path& file,
                                    const std::filesystem::path& directory) {
    auto chart = project_io::read_thread_chart_file(file);
    if (!chart) {
        return std::unexpected(chart.error());
    }
    if (findAnyChart(chart->chart_id) != nullptr) {
        return fail(ErrorCategory::UserInput,
                    "Un nuancier « " + chart->display_name + " » existe déjà",
                    "Retirez-le d'abord, ou renommez le fichier.");
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const auto target = directory / (chart->chart_id + file.extension().string());
    std::filesystem::copy_file(file, target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return fail(ErrorCategory::OperationImpossible,
                    "Copie du nuancier impossible : " + ec.message());
    }
    ChartInfo info{chart->chart_id, chart->display_name, chart->source_note, chart->threads.size(),
                   true};
    userCharts().push_back(std::move(*chart));
    return info;
}

std::vector<std::string> loadUserCharts(const std::filesystem::path& directory) {
    std::vector<std::string> problems;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) {
        return problems;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        const auto ext = lower(entry.path().extension().string());
        if (entry.is_regular_file() && (ext == ".csv" || ext == ".json")) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end()); // ordre stable d'un démarrage à l'autre
    for (const auto& f : files) {
        auto chart = project_io::read_thread_chart_file(f);
        if (!chart) {
            problems.push_back(f.filename().string() + " : " + chart.error().message);
        } else if (findAnyChart(chart->chart_id) == nullptr) {
            userCharts().push_back(std::move(*chart));
        }
    }
    return problems;
}

bool removeUserChart(const std::string& chartId, const std::filesystem::path& directory) {
    auto& charts = userCharts();
    const auto it = std::find_if(charts.begin(), charts.end(),
                                 [&](const tp::ThreadChart& c) { return c.chart_id == chartId; });
    if (it == charts.end()) {
        return false;
    }
    charts.erase(it);
    std::error_code ec;
    for (const char* ext : {".csv", ".json"}) {
        std::filesystem::remove(directory / (chartId + ext), ec);
    }
    return true;
}

std::vector<ThreadChoice> searchThreads(const std::string& chartId, const std::string& query,
                                        std::size_t limit) {
    std::vector<ThreadChoice> out;
    const auto* chart = findAnyChart(chartId);
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
    const auto* chart = findAnyChart(chartId);
    if (chart == nullptr || topN == 0) {
        return out;
    }
    for (const auto& match : tp::nearest_threads(rgb, *chart, topN)) {
        if (const auto thread = findAnyThread(match.key.chart_id, match.key.code)) {
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
    const auto thread = findAnyThread(chartId, code);
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
