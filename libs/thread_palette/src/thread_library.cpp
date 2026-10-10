// SPDX-License-Identifier: Apache-2.0
#include "openstitch/thread_palette/thread_library.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "openstitch/thread_palette/catalog.hpp"

namespace openstitch::thread_palette {

namespace {

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto eq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    };
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), eq) !=
           haystack.end();
}

Thread generic(const char* code, const char* name, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return Thread{.key = ThreadKey{.chart_id = "generic", .code = code},
                  .brand = "Générique",
                  .range = "Couleurs usuelles",
                  .name = name,
                  .rgb = {r, g, b}};
}

} // namespace

ThreadChart generic_basic_chart() {
    ThreadChart chart;
    chart.chart_id = "generic";
    chart.display_name = "Générique (sans marque)";
    chart.source_note = "Couleurs usuelles définies pour OpenStitch Studio ; aucune donnée de "
                        "fabricant. Libre de droits (Apache-2.0).";
    chart.threads = {
        generic("G01", "Blanc", 255, 255, 255),      generic("G02", "Noir", 0, 0, 0),
        generic("G03", "Gris clair", 192, 192, 192), generic("G04", "Gris", 128, 128, 128),
        generic("G05", "Gris foncé", 64, 64, 64),    generic("G06", "Rouge", 200, 16, 46),
        generic("G07", "Rouge foncé", 128, 0, 0),    generic("G08", "Bordeaux", 128, 0, 32),
        generic("G09", "Orange", 255, 127, 0),       generic("G10", "Jaune", 255, 221, 0),
        generic("G11", "Jaune pâle", 255, 244, 153), generic("G12", "Or", 212, 175, 55),
        generic("G13", "Vert clair", 144, 238, 144), generic("G14", "Vert", 0, 154, 68),
        generic("G15", "Vert foncé", 0, 100, 0),     generic("G16", "Olive", 128, 128, 0),
        generic("G17", "Turquoise", 64, 224, 208),   generic("G18", "Sarcelle", 0, 128, 128),
        generic("G19", "Bleu ciel", 135, 206, 235),  generic("G20", "Bleu", 0, 114, 206),
        generic("G21", "Bleu marine", 0, 0, 128),    generic("G22", "Violet", 128, 0, 128),
        generic("G23", "Mauve", 148, 0, 211),        generic("G24", "Rose pâle", 255, 182, 193),
        generic("G25", "Rose vif", 255, 20, 147),    generic("G26", "Marron", 101, 67, 33),
        generic("G27", "Beige", 245, 222, 179),      generic("G28", "Chair", 241, 194, 125),
    };
    return chart;
}

bool is_demo_chart(const ThreadChart& chart) noexcept {
    return chart.source_note.starts_with("DONNEES PLACEHOLDER");
}

ThreadLibrary ThreadLibrary::with_builtin() {
    ThreadLibrary lib;
    lib.charts_.push_back(generic_basic_chart());
    for (const ThreadChart& chart : all_charts()) {
        lib.charts_.push_back(chart);
    }
    lib.builtin_count_ = lib.charts_.size();
    return lib;
}

Result<void> ThreadLibrary::add_chart(ThreadChart chart) {
    if (chart.chart_id.empty()) {
        return fail(ErrorCategory::UserInput, "Identifiant de nuancier vide");
    }
    if (find_chart(chart.chart_id) != nullptr) {
        return fail(ErrorCategory::UserInput, "Un nuancier « " + chart.chart_id + " » existe déjà");
    }
    if (chart.threads.empty()) {
        return fail(ErrorCategory::UserInput, "Le nuancier ne contient aucun fil");
    }
    std::set<std::string> codes;
    for (Thread& t : chart.threads) {
        t.key.chart_id = chart.chart_id; // cohérence : l'identité suit le nuancier
        if (!codes.insert(t.key.code).second) {
            return fail(ErrorCategory::UserInput, "Code en double : " + t.key.code);
        }
    }
    charts_.push_back(std::move(chart));
    return {};
}

bool ThreadLibrary::remove_chart(std::string_view chart_id) {
    for (std::size_t i = builtin_count_; i < charts_.size(); ++i) {
        if (charts_[i].chart_id == chart_id) {
            charts_.erase(charts_.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

bool ThreadLibrary::is_builtin(std::string_view chart_id) const noexcept {
    for (std::size_t i = 0; i < builtin_count_ && i < charts_.size(); ++i) {
        if (charts_[i].chart_id == chart_id) {
            return true;
        }
    }
    return false;
}

const ThreadChart* ThreadLibrary::find_chart(std::string_view chart_id) const noexcept {
    for (const ThreadChart& c : charts_) {
        if (c.chart_id == chart_id) {
            return &c;
        }
    }
    return nullptr;
}

std::optional<Thread> ThreadLibrary::find(const ThreadKey& key) const {
    const ThreadChart* chart = find_chart(key.chart_id);
    if (chart == nullptr) {
        return std::nullopt;
    }
    for (const Thread& t : chart->threads) {
        if (t.key.code == key.code) {
            return t;
        }
    }
    return std::nullopt;
}

std::vector<Thread> ThreadLibrary::search(std::string_view needle, std::string_view chart_id,
                                          std::size_t limit) const {
    std::vector<Thread> out;
    for (const ThreadChart& chart : charts_) {
        if (!chart_id.empty() && chart.chart_id != chart_id) {
            continue;
        }
        for (const Thread& t : chart.threads) {
            if (contains_ci(t.key.code, needle) || contains_ci(t.name, needle) ||
                contains_ci(t.range, needle) || contains_ci(t.brand, needle)) {
                out.push_back(t);
                if (limit != 0 && out.size() >= limit) {
                    return out;
                }
            }
        }
    }
    return out;
}

std::vector<ThreadMatch> ThreadLibrary::nearest(std::array<std::uint8_t, 3> rgb,
                                                std::string_view chart_id,
                                                std::size_t top_n) const {
    std::vector<ThreadMatch> all;
    for (const ThreadChart& chart : charts_) {
        if (!chart_id.empty() && chart.chart_id != chart_id) {
            continue;
        }
        auto part = nearest_threads(rgb, chart, top_n);
        all.insert(all.end(), part.begin(), part.end());
    }
    std::stable_sort(all.begin(), all.end(), [](const ThreadMatch& a, const ThreadMatch& b) {
        return a.distance < b.distance;
    });
    if (all.size() > top_n) {
        all.resize(top_n);
    }
    return all;
}

} // namespace openstitch::thread_palette
