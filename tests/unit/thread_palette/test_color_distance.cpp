// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include "openstitch/thread_palette/color_distance.hpp"

using namespace openstitch::thread_palette;

namespace {

bool approx(double actual, double expected, double tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

} // namespace

TEST_CASE("to_cielab matches known sRGB to CIELAB reference values") {
    // Valeurs de reference standard sRGB (D65, observateur 2 degres) -> CIELAB,
    // largement publiees (ex. calculatrices colorimetriques de reference).
    constexpr double kTolerance = 0.5;

    SECTION("white") {
        const CieLab lab = to_cielab({255, 255, 255});
        CHECK(approx(lab.l, 100.0, kTolerance));
        CHECK(approx(lab.a, 0.0, kTolerance));
        CHECK(approx(lab.b, 0.0, kTolerance));
    }
    SECTION("black") {
        const CieLab lab = to_cielab({0, 0, 0});
        CHECK(approx(lab.l, 0.0, kTolerance));
        CHECK(approx(lab.a, 0.0, kTolerance));
        CHECK(approx(lab.b, 0.0, kTolerance));
    }
    SECTION("red") {
        const CieLab lab = to_cielab({255, 0, 0});
        CHECK(approx(lab.l, 53.24, kTolerance));
        CHECK(approx(lab.a, 80.09, kTolerance));
        CHECK(approx(lab.b, 67.20, kTolerance));
    }
    SECTION("green") {
        const CieLab lab = to_cielab({0, 255, 0});
        CHECK(approx(lab.l, 87.73, kTolerance));
        CHECK(approx(lab.a, -86.18, kTolerance));
        CHECK(approx(lab.b, 83.18, kTolerance));
    }
    SECTION("blue") {
        const CieLab lab = to_cielab({0, 0, 255});
        CHECK(approx(lab.l, 32.30, kTolerance));
        CHECK(approx(lab.a, 79.19, kTolerance));
        CHECK(approx(lab.b, -107.86, kTolerance));
    }
}

TEST_CASE("ciede2000 of a color against itself is zero") {
    const std::array<std::array<std::uint8_t, 3>, 5> samples = {{
        {255, 255, 255},
        {0, 0, 0},
        {255, 0, 0},
        {0, 255, 0},
        {123, 45, 200},
    }};
    for (const auto& rgb : samples) {
        const CieLab lab = to_cielab(rgb);
        CHECK(approx(ciede2000(lab, lab), 0.0, 1e-9));
    }
}

namespace {

// Jeu de reference publie par Sharma, Wu & Dalal (2005), "The CIEDE2000
// color-difference formula: Implementation notes, supplementary test data,
// and mathematical observations" -- 34 paires Lab + distance CIEDE2000
// attendue. Donnees numeriques (coordonnees colorimetriques, resultat d'une
// formule publique) : republiees verbatim par de nombreuses implementations
// open-source non-GPL de reference (ex. python-colormath (BSD), de
// nombreux ports MIT/BSD de CIEDE2000) ; traitees ici comme des faits
// scientifiques, pas une oeuvre protegee -- meme logique que S1-POLICY-1
// pour les codes de fils. Tolerance : la table publiee est arrondie a 4
// decimales, donc 1e-4.
struct ReferencePair {
    CieLab a;
    CieLab b;
    double expected_de2000;
};

constexpr double kReferenceTolerance = 1e-4;

const std::array<ReferencePair, 34> kSharmaWuDalal2005 = {{
    {{50.0000, 2.6772, -79.7751}, {50.0000, 0.0000, -82.7485}, 2.0425},
    {{50.0000, 3.1571, -77.2803}, {50.0000, 0.0000, -82.7485}, 2.8615},
    {{50.0000, 2.8361, -74.0200}, {50.0000, 0.0000, -82.7485}, 3.4412},
    {{50.0000, -1.3802, -84.2814}, {50.0000, 0.0000, -82.7485}, 1.0000},
    {{50.0000, -1.1848, -84.8006}, {50.0000, 0.0000, -82.7485}, 1.0000},
    {{50.0000, -0.9009, -85.5211}, {50.0000, 0.0000, -82.7485}, 1.0000},
    {{50.0000, 0.0000, 0.0000}, {50.0000, -1.0000, 2.0000}, 2.3669},
    {{50.0000, -1.0000, 2.0000}, {50.0000, 0.0000, 0.0000}, 2.3669},
    {{50.0000, 2.4900, -0.0010}, {50.0000, -2.4900, 0.0009}, 7.1792},
    {{50.0000, 2.4900, -0.0010}, {50.0000, -2.4900, 0.0010}, 7.1792},
    {{50.0000, 2.4900, -0.0010}, {50.0000, -2.4900, 0.0011}, 7.2195},
    {{50.0000, 2.4900, -0.0010}, {50.0000, -2.4900, 0.0012}, 7.2195},
    {{50.0000, -0.0010, 2.4900}, {50.0000, 0.0009, -2.4900}, 4.8045},
    {{50.0000, -0.0010, 2.4900}, {50.0000, 0.0010, -2.4900}, 4.8045},
    {{50.0000, -0.0010, 2.4900}, {50.0000, 0.0011, -2.4900}, 4.7461},
    {{50.0000, 2.5000, 0.0000}, {50.0000, 0.0000, -2.5000}, 4.3065},
    {{50.0000, 2.5000, 0.0000}, {73.0000, 25.0000, -18.0000}, 27.1492},
    {{50.0000, 2.5000, 0.0000}, {61.0000, -5.0000, 29.0000}, 22.8977},
    {{50.0000, 2.5000, 0.0000}, {56.0000, -27.0000, -3.0000}, 31.9030},
    {{50.0000, 2.5000, 0.0000}, {58.0000, 24.0000, 15.0000}, 19.4535},
    {{50.0000, 2.5000, 0.0000}, {50.0000, 3.1736, 0.5854}, 1.0000},
    {{50.0000, 2.5000, 0.0000}, {50.0000, 3.2972, 0.0000}, 1.0000},
    {{50.0000, 2.5000, 0.0000}, {50.0000, 1.8634, 0.5757}, 1.0000},
    {{50.0000, 2.5000, 0.0000}, {50.0000, 3.2592, 0.3350}, 1.0000},
    {{60.2574, -34.0099, 36.2677}, {60.4626, -34.1751, 39.4387}, 1.2644},
    {{63.0109, -31.0961, -5.8663}, {62.8187, -29.7946, -4.0864}, 1.2630},
    {{61.2901, 3.7196, -5.3901}, {61.4292, 2.2480, -4.9620}, 1.8731},
    {{35.0831, -44.1164, 3.7933}, {35.0232, -40.0716, 1.5901}, 1.8645},
    {{22.7233, 20.0904, -46.6940}, {23.0331, 14.9730, -42.5619}, 2.0373},
    {{36.4612, 47.8580, 18.3852}, {36.2715, 50.5065, 21.2231}, 1.4146},
    {{90.8027, -2.0831, 1.4410}, {91.1528, -1.6435, 0.0447}, 1.4441},
    {{90.9257, -0.5406, -0.9208}, {88.6381, -0.8985, -0.7239}, 1.5381},
    {{6.7747, -0.2908, -2.4247}, {5.8714, -0.0985, -2.2286}, 0.6377},
    {{2.0776, 0.0795, -1.1350}, {0.9033, -0.0636, -0.5514}, 0.9082},
}};

} // namespace

TEST_CASE("ciede2000 matches Sharma Wu Dalal 2005 reference pairs within tolerance") {
    for (std::size_t i = 0; i < kSharmaWuDalal2005.size(); ++i) {
        const ReferencePair& pair = kSharmaWuDalal2005[i];
        const double actual = ciede2000(pair.a, pair.b);
        INFO("reference pair index " << i);
        CHECK(approx(actual, pair.expected_de2000, kReferenceTolerance));
    }
}

namespace {

ThreadChart make_test_chart() {
    ThreadChart chart;
    chart.chart_id = "test_chart";
    chart.display_name = "Test chart";
    chart.source_note = "donnees synthetiques de test, pas un nuancier reel";
    chart.threads = {
        Thread{.key = ThreadKey{.chart_id = "test_chart", .code = "A"},
               .brand = "Test",
               .range = "Test",
               .name = "Pure red",
               .rgb = {255, 0, 0}},
        Thread{.key = ThreadKey{.chart_id = "test_chart", .code = "B"},
               .brand = "Test",
               .range = "Test",
               .name = "Near red (tie candidate)",
               .rgb = {250, 5, 5}},
        Thread{.key = ThreadKey{.chart_id = "test_chart", .code = "C"},
               .brand = "Test",
               .range = "Test",
               .name = "Pure green",
               .rgb = {0, 255, 0}},
        Thread{.key = ThreadKey{.chart_id = "test_chart", .code = "D"},
               .brand = "Test",
               .range = "Test",
               .name = "Pure blue",
               .rgb = {0, 0, 255}},
        Thread{.key = ThreadKey{.chart_id = "test_chart", .code = "E"},
               .brand = "Test",
               .range = "Test",
               .name = "White",
               .rgb = {255, 255, 255}},
    };
    return chart;
}

} // namespace

TEST_CASE("nearest_threads returns exactly top_n results when chart has at least top_n threads") {
    const ThreadChart chart = make_test_chart();
    const auto matches = nearest_threads({255, 0, 0}, chart, 3);
    CHECK(matches.size() == 3);
}

TEST_CASE("nearest_threads sorts by increasing ciede2000 distance") {
    const ThreadChart chart = make_test_chart();
    const auto matches = nearest_threads({255, 0, 0}, chart, chart.threads.size());
    REQUIRE(matches.size() == chart.threads.size());
    for (std::size_t i = 1; i < matches.size(); ++i) {
        CHECK(matches[i - 1].distance <= matches[i].distance);
    }
    // Le fil A (rouge pur) doit etre le plus proche de lui-meme : distance ~0.
    CHECK(matches.front().key.code == "A");
    CHECK(approx(matches.front().distance, 0.0, 1e-6));
}

TEST_CASE("nearest_threads breaks ties by chart declaration order") {
    // Deux fils identiques en couleur (A et un duplicata ajoute en fin de
    // nuancier) doivent ressortir dans l'ordre de declaration en cas
    // d'egalite exacte de distance.
    ThreadChart chart = make_test_chart();
    chart.threads.push_back(
        Thread{.key = ThreadKey{.chart_id = "test_chart", .code = "A_DUPLICATE"},
               .brand = "Test",
               .range = "Test",
               .name = "Pure red duplicate",
               .rgb = {255, 0, 0}});

    const auto matches = nearest_threads({255, 0, 0}, chart, 2);
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].key.code == "A");
    CHECK(matches[1].key.code == "A_DUPLICATE");
}

TEST_CASE("nearest_threads is deterministic across repeated calls") {
    const ThreadChart chart = make_test_chart();
    const auto first = nearest_threads({200, 20, 20}, chart, 3);
    const auto second = nearest_threads({200, 20, 20}, chart, 3);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].key == second[i].key);
        CHECK(first[i].distance == second[i].distance);
    }
}

namespace {

// Nuancier synthétique de test (aucune donnée de fabricant).
ThreadChart synthetic_chart(const std::vector<std::array<std::uint8_t, 3>>& colors) {
    ThreadChart chart;
    chart.chart_id = "synthetic";
    chart.display_name = "Synthetic";
    for (std::size_t i = 0; i < colors.size(); ++i) {
        Thread t;
        t.key = ThreadKey{"synthetic", std::to_string(i)};
        t.rgb = colors[i];
        chart.threads.push_back(t);
    }
    return chart;
}

} // namespace

TEST_CASE("wcag_contrast spans 1 to 21 and is symmetric") {
    CHECK(approx(wcag_contrast({0, 0, 0}, {255, 255, 255}), 21.0, 0.01));
    CHECK(approx(wcag_contrast({255, 255, 255}, {0, 0, 0}), 21.0, 0.01));
    CHECK(approx(wcag_contrast({120, 40, 200}, {120, 40, 200}), 1.0, 1e-12));
    CHECK(wcag_contrast({10, 200, 90}, {250, 30, 30}) ==
          wcag_contrast({250, 30, 30}, {10, 200, 90}));
}

TEST_CASE("best_thread_pair finds the exact threads when they exist") {
    const auto chart = synthetic_chart({{255, 0, 0}, {255, 180, 190}, {255, 255, 255}, {0, 0, 0}});
    const auto pair = best_thread_pair({255, 0, 0}, {255, 255, 255}, chart);
    REQUIRE(pair.has_value());
    CHECK(pair->first.code == "0");
    CHECK(pair->second.code == "2");
    CHECK(approx(pair->cost, 0.0, 1e-9));
    CHECK(approx(pair->distance_first, 0.0, 1e-9));
    CHECK(approx(pair->distance_second, 0.0, 1e-9));
}

TEST_CASE("best_thread_pair keeps the contrast of the original colours") {
    // Deux gris proches : le fil le plus proche des deux est le même (75), ce
    // qui écraserait le fondu (contraste 1). Une forte pondération impose deux
    // fils contrastés au moins autant que les couleurs d'origine.
    const std::array<std::uint8_t, 3> s1{60, 60, 60};
    const std::array<std::uint8_t, 3> s2{90, 90, 90};
    const auto chart = synthetic_chart({{75, 75, 75}, {0, 0, 0}, {150, 150, 150}});

    const auto free = best_thread_pair(s1, s2, chart, 0.0);
    REQUIRE(free.has_value());
    CHECK(free->first.code == "0");
    CHECK(free->second.code == "0");
    CHECK(approx(free->contrast, 1.0, 1e-12));

    const auto kept = best_thread_pair(s1, s2, chart, 1000.0);
    REQUIRE(kept.has_value());
    CHECK(kept->first.code != kept->second.code);
    CHECK(kept->contrast >= wcag_contrast(s1, s2));
}

TEST_CASE("best_thread_pair is deterministic and breaks ties by chart order") {
    const auto chart = synthetic_chart({{200, 30, 30}, {200, 30, 30}, {30, 30, 200}});
    const auto a = best_thread_pair({200, 30, 30}, {30, 30, 200}, chart);
    const auto b = best_thread_pair({200, 30, 30}, {30, 30, 200}, chart);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->first == b->first);
    CHECK(a->second == b->second);
    CHECK(a->cost == b->cost);
    CHECK(a->first.code == "0"); // doublon identique : le premier déclaré gagne
    CHECK(a->second.code == "2");
}

TEST_CASE("best_thread_pair on an empty chart returns nothing") {
    CHECK_FALSE(best_thread_pair({1, 2, 3}, {4, 5, 6}, ThreadChart{}).has_value());
}
