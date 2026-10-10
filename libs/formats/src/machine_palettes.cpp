// SPDX-License-Identifier: Apache-2.0
#include "machine_palettes.hpp"

#include <string>
#include <vector>

#include "openstitch/thread_palette/color_distance.hpp"
#include "openstitch/thread_palette/thread.hpp"

namespace openstitch::formats::detail {

namespace {

// Valeurs de référence publiées par les tables de pyembroidery (licence MIT), elles-mêmes
// issues de la rétro-ingénierie des palettes des machines. Données factuelles (approximation
// RGB de fils), recopiées comme table de valeurs ; aucun code n'est repris.
constexpr std::array<Rgb, kPecPaletteSize> kPec = {{
    {14, 31, 124},   {10, 85, 163},   {0, 135, 119},   {75, 107, 175},  {237, 23, 31},
    {209, 92, 0},    {145, 54, 151},  {228, 154, 203}, {145, 95, 172},  {158, 214, 125},
    {232, 169, 0},   {254, 186, 53},  {255, 255, 0},   {112, 188, 31},  {186, 152, 0},
    {168, 168, 168}, {125, 111, 0},   {255, 255, 179}, {79, 85, 86},    {0, 0, 0},
    {11, 61, 145},   {119, 1, 118},   {41, 49, 51},    {42, 19, 1},     {246, 74, 138},
    {178, 118, 36},  {252, 187, 197}, {254, 55, 15},   {240, 240, 240}, {106, 28, 138},
    {168, 221, 196}, {37, 132, 187},  {254, 179, 67},  {255, 243, 107}, {208, 166, 96},
    {209, 84, 0},    {102, 186, 73},  {19, 74, 70},    {135, 135, 135}, {216, 204, 198},
    {67, 86, 7},     {253, 217, 222}, {249, 147, 188}, {0, 56, 34},     {178, 175, 212},
    {104, 106, 176}, {239, 227, 185}, {247, 56, 102},  {181, 75, 100},  {19, 43, 26},
    {199, 1, 86},    {254, 158, 50},  {168, 222, 235}, {0, 103, 62},    {78, 41, 144},
    {47, 126, 32},   {255, 204, 204}, {255, 217, 17},  {9, 91, 166},    {240, 249, 112},
    {227, 243, 91},  {255, 153, 0},   {255, 240, 141}, {255, 200, 200},
}};

constexpr std::array<Rgb, kJefPaletteSize> kJef = {{
    {0, 0, 0},       {255, 255, 255}, {255, 255, 23},  {255, 102, 0},   {47, 89, 51},
    {35, 115, 54},   {101, 194, 200}, {171, 90, 150},  {246, 105, 160}, {255, 0, 0},
    {177, 112, 78},  {11, 47, 132},   {228, 195, 93},  {72, 26, 5},     {172, 156, 199},
    {252, 242, 148}, {249, 153, 183}, {250, 179, 129}, {201, 164, 128}, {151, 5, 51},
    {160, 184, 204}, {127, 194, 28},  {229, 229, 229}, {136, 155, 155}, {152, 214, 189},
    {178, 225, 227}, {54, 139, 160},  {79, 131, 171},  {56, 106, 145},  {7, 22, 80},
    {249, 153, 162}, {249, 103, 107}, {227, 49, 31},   {226, 161, 136}, {181, 148, 116},
    {228, 207, 153}, {255, 203, 0},   {225, 173, 212}, {195, 0, 126},   {128, 0, 75},
    {84, 5, 113},    {177, 5, 37},    {202, 224, 192}, {137, 152, 86},  {92, 148, 26},
    {0, 49, 20},     {93, 174, 148},  {76, 191, 143},  {0, 119, 114},   {89, 91, 97},
    {255, 255, 242}, {177, 88, 24},   {203, 138, 7},   {152, 108, 128}, {152, 105, 45},
    {77, 52, 25},    {76, 51, 11},    {51, 32, 10},    {82, 58, 151},   {13, 33, 126},
    {30, 119, 172},  {178, 221, 83},  {243, 54, 137},  {222, 100, 158}, {152, 65, 97},
    {76, 86, 18},    {76, 136, 31},   {228, 222, 121}, {203, 138, 26},  {203, 162, 28},
    {255, 152, 5},   {252, 178, 87},  {255, 229, 5},   {240, 51, 31},   {26, 132, 45},
    {56, 108, 174},  {227, 196, 180}, {227, 172, 129},
}};

template <std::size_t N>
thread_palette::ThreadChart make_chart(const char* id, const std::array<Rgb, N>& table) {
    thread_palette::ThreadChart chart;
    chart.chart_id = id;
    chart.display_name = id;
    chart.source_note = "table interne de format machine (cf. docs/source/formats-pes-jef-exp.md)";
    chart.threads.reserve(N);
    for (std::size_t i = 0; i < N; ++i) {
        thread_palette::Thread t;
        t.key = {id, std::to_string(i + 1)};
        t.rgb = table[i];
        chart.threads.push_back(std::move(t));
    }
    return chart;
}

int nearest(const thread_palette::ThreadChart& chart, const Rgb& rgb, int avoid) {
    const auto matches = thread_palette::nearest_threads(rgb, chart, 2);
    for (const auto& m : matches) {
        const int index = std::stoi(m.key.code);
        if (index != avoid) {
            return index;
        }
    }
    return 1;
}

const thread_palette::ThreadChart& pec_chart() {
    static const auto chart = make_chart("brother_pec", kPec);
    return chart;
}
const thread_palette::ThreadChart& jef_chart() {
    static const auto chart = make_chart("janome_jef", kJef);
    return chart;
}

} // namespace

std::optional<Rgb> pec_rgb(int index) noexcept {
    if (index < 1 || index > kPecPaletteSize) {
        return std::nullopt;
    }
    return kPec[static_cast<std::size_t>(index - 1)];
}

std::optional<Rgb> jef_rgb(int index) noexcept {
    if (index < 1 || index > kJefPaletteSize) {
        return std::nullopt;
    }
    return kJef[static_cast<std::size_t>(index - 1)];
}

int nearest_pec_index(const Rgb& rgb, int avoid) {
    return nearest(pec_chart(), rgb, avoid);
}
int nearest_jef_index(const Rgb& rgb, int avoid) {
    return nearest(jef_chart(), rgb, avoid);
}

} // namespace openstitch::formats::detail
