// SPDX-License-Identifier: Apache-2.0
#include "openstitch/project_io/thread_chart_io.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>

#include <nlohmann/json.hpp>

#include "archive.hpp"

namespace openstitch::project_io {

namespace {

using nlohmann::json;
using thread_palette::Thread;
using thread_palette::ThreadChart;

std::string string_or(const json& j, const char* key, const std::string& fallback) {
    if (j.is_object() && j.contains(key) && j.at(key).is_string()) {
        return j.at(key).get<std::string>();
    }
    return fallback;
}

std::optional<std::array<std::uint8_t, 3>> rgb_of(const json& v) {
    if (v.is_string()) {
        return thread_palette::parse_hex_color(v.get<std::string>());
    }
    if (v.is_array() && v.size() == 3) {
        std::array<std::uint8_t, 3> rgb{};
        for (std::size_t i = 0; i < 3; ++i) {
            if (!v[i].is_number_integer()) {
                return std::nullopt;
            }
            const auto c = v[i].get<long long>();
            if (c < 0 || c > 255) {
                return std::nullopt;
            }
            rgb[i] = static_cast<std::uint8_t>(c);
        }
        return rgb;
    }
    return std::nullopt;
}

} // namespace

Result<ThreadChart> parse_thread_chart_json(std::string_view text,
                                            const thread_palette::ChartImportInfo& defaults) {
    json root;
    try {
        root = json::parse(text);
    } catch (const json::exception& ex) {
        return fail(ErrorCategory::InvalidFile, "Nuancier JSON illisible", ex.what());
    }
    if (!root.is_object() || !root.contains("threads") || !root.at("threads").is_array()) {
        return fail(ErrorCategory::InvalidFile,
                    "Nuancier JSON invalide : le tableau « threads » est obligatoire");
    }
    ThreadChart chart;
    chart.display_name = string_or(root, "name", defaults.display_name);
    chart.chart_id = thread_palette::make_user_chart_id(
        chart.display_name.empty() ? defaults.chart_id : chart.display_name);
    chart.source_note = string_or(root, "source", defaults.source_note);

    std::set<std::string> seen;
    std::size_t index = 0;
    for (const json& tj : root.at("threads")) {
        ++index;
        const std::string where = " (fil n° " + std::to_string(index) + ")";
        if (!tj.is_object() || !tj.contains("code") || !tj.at("code").is_string() ||
            tj.at("code").get<std::string>().empty()) {
            return fail(ErrorCategory::InvalidFile, "Code de fil manquant" + where);
        }
        Thread t;
        t.key = {chart.chart_id, tj.at("code").get<std::string>()};
        t.name = string_or(tj, "name", "");
        t.brand = string_or(tj, "brand", chart.display_name);
        t.range = string_or(tj, "range", "");
        const auto rgb = tj.contains("rgb") ? rgb_of(tj.at("rgb")) : std::nullopt;
        if (!rgb) {
            return fail(ErrorCategory::InvalidFile,
                        "Couleur « rgb » illisible (attendu #RRGGBB ou [r, g, b])" + where);
        }
        t.rgb = *rgb;
        if (!seen.insert(t.key.code).second) {
            return fail(ErrorCategory::InvalidFile,
                        "Code en double « " + t.key.code + " »" + where);
        }
        chart.threads.push_back(std::move(t));
    }
    if (chart.threads.empty()) {
        return fail(ErrorCategory::InvalidFile, "Le nuancier ne contient aucun fil");
    }
    return chart;
}

Result<ThreadChart> read_thread_chart_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail(ErrorCategory::InvalidFile,
                    "Impossible d'ouvrir le fichier : " + detail::path_utf8(path));
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string ext = detail::path_utf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string stem = detail::path_utf8(path.stem());

    thread_palette::ChartImportInfo info;
    info.display_name = stem;
    info.chart_id = thread_palette::make_user_chart_id(stem);
    info.source_note = "Importé depuis " + detail::path_utf8(path.filename());

    if (ext == ".csv") {
        return thread_palette::parse_chart_csv(text, info);
    }
    if (ext == ".json") {
        return parse_thread_chart_json(text, info);
    }
    return fail(ErrorCategory::UnsupportedFormat,
                "Format de nuancier non pris en charge (attendu .csv ou .json)");
}

} // namespace openstitch::project_io
