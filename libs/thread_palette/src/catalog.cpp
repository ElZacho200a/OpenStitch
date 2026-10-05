// SPDX-License-Identifier: Apache-2.0
#include "openstitch/thread_palette/catalog.hpp"

#include <algorithm>
#include <cctype>

#include "chart_data.hpp"

namespace openstitch::thread_palette {

namespace {

// Registre construit une seule fois (static locale à la fonction), dans
// l'ordre textuel fixe des appels ci-dessous (AD-S1-2) : pas de
// std::unordered_map, pas de scan de répertoire, pas d'ordre dépendant du
// système de fichiers. Output 0 = Madeira Polyneon, output 1 = Isacord 40
// (S1-POLICY-3).
const std::vector<ThreadChart>& registry() {
    static const std::vector<ThreadChart> charts = [] {
        std::vector<ThreadChart> v;
        v.push_back(detail::make_madeira_polyneon_chart());
        v.push_back(detail::make_isacord_40_chart());
        return v;
    }();
    return charts;
}

bool iequals_char(char lhs, char rhs) {
    return std::tolower(static_cast<unsigned char>(lhs)) ==
           std::tolower(static_cast<unsigned char>(rhs));
}

// Sous-chaîne insensible à la casse (ASCII) — pas de dépendance locale.
bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    const auto it =
        std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), iequals_char);
    return it != haystack.end();
}

} // namespace

std::span<const ThreadChart> all_charts() {
    const auto& charts = registry();
    return {charts.data(), charts.size()};
}

const ThreadChart* find_chart(std::string_view chart_id) noexcept {
    const auto& charts = registry();
    const auto it =
        std::find_if(charts.begin(), charts.end(),
                     [chart_id](const ThreadChart& chart) { return chart.chart_id == chart_id; });
    return it != charts.end() ? &*it : nullptr;
}

std::optional<Thread> find_by_code(std::string_view chart_id, std::string_view code) noexcept {
    const ThreadChart* chart = find_chart(chart_id);
    if (chart == nullptr) {
        return std::nullopt;
    }
    const auto it = std::find_if(chart->threads.begin(), chart->threads.end(),
                                 [code](const Thread& thread) { return thread.key.code == code; });
    if (it == chart->threads.end()) {
        return std::nullopt;
    }
    return *it;
}

std::vector<Thread> search_by_name(std::string_view needle) noexcept {
    std::vector<Thread> results;
    for (const ThreadChart& chart : all_charts()) {
        for (const Thread& thread : chart.threads) {
            if (contains_ci(thread.name, needle)) {
                results.push_back(thread);
            }
        }
    }
    return results;
}

} // namespace openstitch::thread_palette
