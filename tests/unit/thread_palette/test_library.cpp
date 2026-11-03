// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "openstitch/thread_palette/chart_import.hpp"
#include "openstitch/thread_palette/color_reduction.hpp"
#include "openstitch/thread_palette/thread_library.hpp"

using namespace openstitch::thread_palette;

namespace {

ThreadChart tiny_chart(const std::string& id) {
    ThreadChart c;
    c.chart_id = id;
    c.display_name = id;
    c.threads = {Thread{ThreadKey{id, "A1"}, "B", "R", "Rouge vif", {250, 0, 0}},
                 Thread{ThreadKey{id, "B2"}, "B", "R", "Bleu nuit", {0, 0, 120}}};
    return c;
}

} // namespace

TEST_CASE("generic chart is free of brand data and has unique codes") {
    const ThreadChart chart = generic_basic_chart();
    CHECK(chart.chart_id == "generic");
    CHECK(chart.threads.size() >= 24);
    CHECK_FALSE(is_demo_chart(chart));
    for (std::size_t i = 0; i < chart.threads.size(); ++i) {
        CHECK(chart.threads[i].key.chart_id == "generic");
        for (std::size_t j = i + 1; j < chart.threads.size(); ++j) {
            CHECK(chart.threads[i].key.code != chart.threads[j].key.code);
        }
    }
}

TEST_CASE("library lists builtin charts first, generic first, demo charts flagged") {
    const ThreadLibrary lib = ThreadLibrary::with_builtin();
    REQUIRE(lib.charts().size() >= 3);
    CHECK(lib.charts()[0].chart_id == "generic");
    CHECK(lib.is_builtin("generic"));
    CHECK(is_demo_chart(*lib.find_chart("madeira_polyneon")));
}

TEST_CASE("library add_chart rejects duplicates and empty charts, remove only user charts") {
    ThreadLibrary lib = ThreadLibrary::with_builtin();
    CHECK(lib.add_chart(tiny_chart("user_x")).has_value());
    CHECK_FALSE(lib.add_chart(tiny_chart("user_x")).has_value());
    CHECK_FALSE(lib.add_chart(tiny_chart("generic")).has_value());
    ThreadChart empty;
    empty.chart_id = "user_empty";
    CHECK_FALSE(lib.add_chart(empty).has_value());
    ThreadChart dup = tiny_chart("user_dup");
    dup.threads.push_back(dup.threads.front());
    CHECK_FALSE(lib.add_chart(dup).has_value());

    CHECK_FALSE(lib.remove_chart("generic"));
    CHECK(lib.remove_chart("user_x"));
    CHECK(lib.find_chart("user_x") == nullptr);
}

TEST_CASE("library find and search") {
    ThreadLibrary lib = ThreadLibrary::with_builtin();
    REQUIRE(lib.add_chart(tiny_chart("user_x")).has_value());
    const auto t = lib.find(ThreadKey{"user_x", "B2"});
    REQUIRE(t.has_value());
    CHECK(t->name == "Bleu nuit");
    CHECK_FALSE(lib.find(ThreadKey{"user_x", "ZZ"}).has_value());

    const auto byName = lib.search("BLEU", "user_x");
    REQUIRE(byName.size() == 1);
    CHECK(byName[0].key.code == "B2");
    CHECK(lib.search("b2", "user_x").size() == 1); // par code
    CHECK(lib.search("", "user_x").size() == 2);
    CHECK(lib.search("", "", 3).size() == 3); // limite
}

TEST_CASE("library nearest finds the closest thread across charts, deterministically") {
    ThreadLibrary lib = ThreadLibrary::with_builtin();
    REQUIRE(lib.add_chart(tiny_chart("user_x")).has_value());
    const auto m = lib.nearest({250, 0, 0}, "", 3);
    REQUIRE(m.size() == 3);
    CHECK(m[0].key == ThreadKey{"user_x", "A1"});
    CHECK(m[0].distance == 0.0);
    CHECK(m[0].distance <= m[1].distance);
    CHECK(m[1].distance <= m[2].distance);
    CHECK(lib.nearest({250, 0, 0}, "", 3)[1].key == m[1].key);

    const auto inChart = lib.nearest({10, 10, 10}, "generic", 1);
    REQUIRE(inChart.size() == 1);
    CHECK(inChart[0].key == ThreadKey{"generic", "G02"}); // noir
}

TEST_CASE("csv import: hex columns, semicolon separator, BOM, quotes") {
    const std::string csv = "\xEF\xBB\xBF"
                            "# commentaire\r\n"
                            "Code;Nom;Hex;Marque\r\n"
                            "1001;\"Rouge; vif\";#FF0000;Acme\r\n"
                            "1002;Bleu;0000ff;Acme\r\n";
    const auto r = parse_chart_csv(csv, {"user_acme", "Acme", "fichier test"});
    REQUIRE(r.has_value());
    REQUIRE(r->threads.size() == 2);
    CHECK(r->threads[0].key == ThreadKey{"user_acme", "1001"});
    CHECK(r->threads[0].name == "Rouge; vif");
    CHECK(r->threads[0].rgb == std::array<std::uint8_t, 3>{255, 0, 0});
    CHECK(r->threads[0].brand == "Acme");
    CHECK(r->threads[1].rgb == std::array<std::uint8_t, 3>{0, 0, 255});
    CHECK(r->source_note == "fichier test");
}

TEST_CASE("csv import: r,g,b columns") {
    const auto r = parse_chart_csv("ref,nom,r,g,b\nX1,Vert,0,128,0\n", {"user_v", "V", ""});
    REQUIRE(r.has_value());
    CHECK(r->threads[0].rgb == std::array<std::uint8_t, 3>{0, 128, 0});
    CHECK(r->threads[0].brand == "V"); // marque par défaut = nom du nuancier
}

TEST_CASE("csv import rejects malformed input with a line number") {
    const ChartImportInfo info{"user_t", "T", ""};
    CHECK_FALSE(parse_chart_csv("", info).has_value());
    CHECK_FALSE(parse_chart_csv("code,name\nA,B\n", info).has_value()); // pas de couleur
    CHECK_FALSE(parse_chart_csv("code,name,hex\n", info).has_value());  // aucun fil
    const auto badHex = parse_chart_csv("code,name,hex\nA,B,notacolor\n", info);
    REQUIRE_FALSE(badHex.has_value());
    CHECK(badHex.error().message.find("ligne 2") != std::string::npos);
    CHECK_FALSE(parse_chart_csv("code,name,hex\nA,B,#000000\nA,C,#111111\n", info).has_value());
    CHECK_FALSE(parse_chart_csv("code,name,r,g,b\nA,B,0,0,300\n", info).has_value());
    CHECK_FALSE(parse_chart_csv("code,name,hex\n,B,#000000\n", info).has_value());
}

TEST_CASE("hex helpers and user chart ids") {
    CHECK(parse_hex_color("#0a0B0c") == std::array<std::uint8_t, 3>{10, 11, 12});
    CHECK_FALSE(parse_hex_color("#12345").has_value());
    CHECK_FALSE(parse_hex_color("#GG0000").has_value());
    CHECK(to_hex_color({255, 0, 16}) == "#FF0010");
    CHECK(make_user_chart_id("Mes Fils 2024 !") == "user_mes_fils_2024");
    CHECK(make_user_chart_id("###") == "user_nuancier");
}

TEST_CASE("reduce_colors merges the closest pair and keeps the heaviest hue") {
    const std::vector<WeightedColor> in = {
        {{250, 0, 0}, 1.0},   // rouge, petit
        {{0, 0, 255}, 5.0},   // bleu
        {{255, 10, 10}, 9.0}, // rouge proche, gros
        {{0, 200, 0}, 3.0},   // vert
    };
    const auto r = reduce_colors(in, 3);
    REQUIRE(r.palette.size() == 3);
    REQUIRE(r.mapping.size() == 4);
    CHECK(r.mapping[0] == r.mapping[2]);
    CHECK(r.palette[r.mapping[0]] == std::array<std::uint8_t, 3>{255, 10, 10});
    CHECK(r.mapping[1] != r.mapping[3]);
    // Ordre = première apparition dans l'entrée.
    CHECK(r.mapping[0] == 0);
    CHECK(r.mapping[1] == 1);
    CHECK(r.mapping[3] == 2);
}

TEST_CASE("reduce_colors: no limit only merges exact duplicates; limit larger is a no-op") {
    const std::vector<WeightedColor> in = {{{1, 2, 3}, 1.0}, {{9, 9, 9}, 1.0}, {{1, 2, 3}, 2.0}};
    const auto r = reduce_colors(in, 0);
    CHECK(r.palette.size() == 2);
    CHECK(r.mapping[0] == r.mapping[2]);
    CHECK(reduce_colors(in, 10).palette.size() == 2);
    CHECK(reduce_colors(in, 1).palette.size() == 1);
    CHECK(reduce_colors({}, 3).palette.empty());
}

TEST_CASE("reduce_colors is deterministic") {
    std::vector<WeightedColor> in;
    for (int i = 0; i < 30; ++i) {
        in.push_back({{static_cast<std::uint8_t>(i * 8), static_cast<std::uint8_t>(255 - i * 7),
                       static_cast<std::uint8_t>((i * 37) % 256)},
                      1.0 + (i % 4)});
    }
    const auto a = reduce_colors(in, 5);
    const auto b = reduce_colors(in, 5);
    CHECK(a.palette == b.palette);
    CHECK(a.mapping == b.mapping);
    CHECK(a.palette.size() == 5);
}

// HP-PERF-028 : reference naive (ancien algorithme, toutes les paires a chaque fusion) ; la
// version rapide doit donner la meme palette et le meme mapping.
namespace {
ColorReduction reduce_colors_reference(std::span<const WeightedColor> colors,
                                       std::size_t max_colors, double max_distance) {
    struct C {
        std::array<std::uint8_t, 3> rgb;
        CieLab lab;
        double weight, top;
        std::size_t first;
        std::vector<std::size_t> members;
    };
    std::vector<C> cl;
    for (std::size_t i = 0; i < colors.size(); ++i) {
        cl.push_back(
            {colors[i].rgb, to_cielab(colors[i].rgb), colors[i].weight, colors[i].weight, i, {i}});
    }
    const auto merge = [&cl](std::size_t k, std::size_t d) {
        C b = cl[d];
        if (b.top > cl[k].top) {
            cl[k].rgb = b.rgb;
            cl[k].lab = b.lab;
            cl[k].top = b.top;
        }
        cl[k].weight += b.weight;
        cl[k].first = std::min(cl[k].first, b.first);
        cl[k].members.insert(cl[k].members.end(), b.members.begin(), b.members.end());
        cl.erase(cl.begin() + static_cast<std::ptrdiff_t>(d));
    };
    for (std::size_t i = 0; i < cl.size(); ++i) {
        for (std::size_t j = i + 1; j < cl.size();) {
            if (cl[j].rgb == cl[i].rgb) {
                merge(i, j);
            } else {
                ++j;
            }
        }
    }
    while (cl.size() > 1) {
        const bool many = max_colors > 0 && cl.size() > max_colors;
        if (!many && max_distance <= 0.0) {
            break;
        }
        double best = std::numeric_limits<double>::infinity();
        std::size_t bi = 0, bj = 1;
        for (std::size_t i = 0; i < cl.size(); ++i) {
            for (std::size_t j = i + 1; j < cl.size(); ++j) {
                const double d = ciede2000(cl[i].lab, cl[j].lab);
                if (d < best) {
                    best = d;
                    bi = i;
                    bj = j;
                }
            }
        }
        if (!many && best > max_distance) {
            break;
        }
        merge(bi, bj);
    }
    std::vector<std::size_t> order(cl.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(),
                     [&cl](std::size_t a, std::size_t b) { return cl[a].first < cl[b].first; });
    ColorReduction out;
    out.mapping.assign(colors.size(), 0);
    for (std::size_t r = 0; r < order.size(); ++r) {
        out.palette.push_back(cl[order[r]].rgb);
        for (const auto m : cl[order[r]].members) {
            out.mapping[m] = r;
        }
    }
    return out;
}
} // namespace

TEST_CASE("reduce_colors matches the naive reference on many colors") {
    std::uint32_t s = 12345U;
    const auto rnd = [&s](std::uint32_t mod) {
        s = s * 1664525U + 1013904223U;
        return (s >> 8) % mod;
    };
    for (const std::size_t n : {std::size_t{120}, std::size_t{250}}) {
        std::vector<WeightedColor> in;
        for (std::size_t i = 0; i < n; ++i) {
            WeightedColor w;
            // palette restreinte : doublons exacts et egalites de poids frequents
            w.rgb = {static_cast<std::uint8_t>(rnd(24) * 10),
                     static_cast<std::uint8_t>(rnd(24) * 10),
                     static_cast<std::uint8_t>(rnd(24) * 10)};
            w.weight = static_cast<double>(1 + rnd(5));
            in.push_back(w);
        }
        for (const auto& [maxc, maxd] :
             {std::pair<std::size_t, double>{16, 0.0}, std::pair<std::size_t, double>{0, 12.0},
              std::pair<std::size_t, double>{40, 5.0}, std::pair<std::size_t, double>{0, 0.0}}) {
            const auto fast = reduce_colors(in, maxc, maxd);
            const auto ref = reduce_colors_reference(in, maxc, maxd);
            CHECK(fast.palette == ref.palette);
            CHECK(fast.mapping == ref.mapping);
        }
    }
}
// HP-PERF-028 : mesure (2000 couleurs -> 16), aucun seuil de temps dans le test.
TEST_CASE("reduce_colors 2000 colors to 16 (HP-PERF-028 : mesure)") {
    std::uint32_t s = 777U;
    std::vector<WeightedColor> in;
    for (int i = 0; i < 2000; ++i) {
        WeightedColor w;
        for (auto& c : w.rgb) {
            s = s * 1664525U + 1013904223U;
            c = static_cast<std::uint8_t>(s >> 24);
        }
        w.weight = 1.0 + static_cast<double>(i % 7);
        in.push_back(w);
    }
    const auto r = reduce_colors(in, 16);
    CHECK(r.palette.size() == 16);
    CHECK(r.mapping.size() == 2000);
}
