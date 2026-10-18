// SPDX-License-Identifier: Apache-2.0
// HP-THR-007 : « Mes fils » (stock possede) et nuancier personnel.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/thread_palette/thread_stock.hpp"

using namespace openstitch;
using namespace openstitch::thread_palette;

namespace {

Thread personal(const char* code, const char* name, std::uint8_t r, std::uint8_t g,
                std::uint8_t b) {
    Thread t;
    t.key = ThreadKey{"ignored", code};
    t.name = name;
    t.brand = "Maison";
    t.range = "Stock";
    t.rgb = {r, g, b};
    return t;
}

} // namespace

TEST_CASE("stock : own, owns et disown sont ordonnes et sans doublon") {
    ThreadStock stock;
    CHECK(stock.empty());
    CHECK(stock.own({"generic", "G20"}));
    CHECK(stock.own({"generic", "G02"}));
    CHECK_FALSE(stock.own({"generic", "G02"}));      // doublon
    CHECK_FALSE(stock.own({"", "G02"}));             // chart vide
    CHECK_FALSE(stock.own({"generic", ""}));         // code vide
    CHECK_FALSE(stock.own({kPersonalChartId, "x"})); // reserve au nuancier personnel
    REQUIRE(stock.owned_keys().size() == 2);
    CHECK(stock.owned_keys()[0].code == "G02");
    CHECK(stock.owned_keys()[1].code == "G20");
    CHECK(stock.owns({"generic", "G20"}));
    CHECK_FALSE(stock.owns({"generic", "G01"}));
    CHECK(stock.disown({"generic", "G20"}));
    CHECK_FALSE(stock.disown({"generic", "G20"}));
    CHECK(stock.size() == 1);
}

TEST_CASE("stock : nuancier personnel ajoute, remplace et retire") {
    ThreadStock stock;
    REQUIRE(stock.add_personal(personal("B", "Bleu maison", 0, 0, 200)).has_value());
    REQUIRE(stock.add_personal(personal("A", "Rouge maison", 200, 0, 0)).has_value());
    REQUIRE(stock.add_personal(personal("A", "Rouge v2", 210, 0, 0)).has_value()); // remplace
    CHECK_FALSE(stock.add_personal(personal("", "sans code", 1, 2, 3)).has_value());
    REQUIRE(stock.personal_threads().size() == 2);
    CHECK(stock.personal_threads()[0].key.code == "A"); // trie par code
    CHECK(stock.personal_threads()[0].key.chart_id == kPersonalChartId);
    CHECK(stock.personal_threads()[0].name == "Rouge v2");
    CHECK(stock.owns({kPersonalChartId, "B"}));
    CHECK(stock.disown({kPersonalChartId, "B"}));
    CHECK_FALSE(stock.owns({kPersonalChartId, "B"}));
}

TEST_CASE("stock : nearest est restreint au stock") {
    const auto lib = ThreadLibrary::with_builtin();
    // Le noir pur existe au catalogue (G02) mais n'est PAS possede.
    ThreadStock stock;
    REQUIRE(stock.own({"generic", "G01"})); // blanc
    REQUIRE(stock.own({"generic", "G20"})); // bleu

    const auto unrestricted = lib.nearest({0, 0, 0}, "generic", 1);
    REQUIRE(unrestricted.size() == 1);
    CHECK(unrestricted[0].key.code == "G02");

    const auto restricted = stock.nearest(lib, {0, 0, 0}, 5);
    REQUIRE(restricted.size() == 2); // seulement les fils possedes
    for (const auto& m : restricted) {
        CHECK(stock.owns(m.key));
    }
    CHECK(restricted[0].distance <= restricted[1].distance);
    CHECK(restricted[0].key.code == "G20"); // le bleu est plus proche du noir que le blanc
}

TEST_CASE("stock : un fil personnel peut gagner et un nuancier absent est ignore") {
    const auto lib = ThreadLibrary::with_builtin();
    ThreadStock stock;
    REQUIRE(stock.own({"user_retire", "X1"})); // nuancier inconnu de la bibliotheque
    REQUIRE(stock.own({"generic", "G01"}));
    REQUIRE(stock.add_personal(personal("ROUGE", "Mon rouge", 201, 17, 45)).has_value());
    CHECK(stock.resolve(lib).size() == 2);
    const auto m = stock.nearest(lib, {200, 16, 46}, 3);
    REQUIRE(m.size() == 2);
    CHECK(m[0].key.chart_id == kPersonalChartId);
    CHECK(m[0].key.code == "ROUGE");
    CHECK(stock.nearest(lib, {1, 2, 3}, 0).empty());
    CHECK(ThreadStock{}.nearest(lib, {1, 2, 3}, 5).empty());
}

TEST_CASE("stock : egalite de distance departagee par l'ordre du stock") {
    const auto lib = ThreadLibrary::with_builtin();
    ThreadStock stock;
    REQUIRE(stock.add_personal(personal("Z", "z", 10, 20, 30)).has_value());
    REQUIRE(stock.add_personal(personal("A", "a", 10, 20, 30)).has_value());
    const auto m = stock.nearest(lib, {10, 20, 30}, 2);
    REQUIRE(m.size() == 2);
    CHECK(m[0].key.code == "A"); // ordre trie, pas ordre d'insertion
    CHECK(m[1].key.code == "Z");
}

TEST_CASE("stock : JSON deterministe et round-trip exact") {
    ThreadStock stock;
    REQUIRE(stock.own({"generic", "G20"}));
    REQUIRE(stock.own({"madeira_polyneon", "1919"}));
    REQUIRE(stock.add_personal(personal("P\"1", "Caf\xC3\xA9 \\ noir\n", 12, 34, 56)).has_value());
    const std::string a = stock_to_json(stock);
    const std::string b = stock_to_json(stock);
    CHECK(a == b);

    const auto back = stock_from_json(a);
    REQUIRE(back.has_value());
    CHECK(*back == stock);
    CHECK(stock_to_json(*back) == a); // octet pour octet

    // L'ordre d'insertion ne change pas le texte ecrit.
    ThreadStock other;
    REQUIRE(other.add_personal(personal("P\"1", "Caf\xC3\xA9 \\ noir\n", 12, 34, 56)).has_value());
    REQUIRE(other.own({"madeira_polyneon", "1919"}));
    REQUIRE(other.own({"generic", "G20"}));
    CHECK(stock_to_json(other) == a);
}

TEST_CASE("stock : stock vide ecrit et relu") {
    const std::string text = stock_to_json(ThreadStock{});
    const auto back = stock_from_json(text);
    REQUIRE(back.has_value());
    CHECK(back->empty());
}

TEST_CASE("stock : JSON invalide ou futur refuse sans exception") {
    CHECK_FALSE(stock_from_json("").has_value());
    CHECK_FALSE(stock_from_json("[1,2]").has_value());
    CHECK_FALSE(stock_from_json("{\"version\": 1} trailing").has_value());
    CHECK_FALSE(stock_from_json("{\"owned\": []}").has_value()); // version absente
    CHECK_FALSE(stock_from_json("{\"version\": 1, \"owned\": [{\"chart\": \"a\"}]}").has_value());
    CHECK_FALSE(stock_from_json("{\"version\": 1, \"owned\": [{\"chart\": \"a\", \"code\": \"1\"},"
                                " {\"chart\": \"a\", \"code\": \"1\"}]}")
                    .has_value());
    CHECK_FALSE(stock_from_json("{\"version\": 1, \"personal\": [{\"code\": \"A\", "
                                "\"rgb\": [0, 0, 256]}]}")
                    .has_value());
    CHECK_FALSE(stock_from_json("{\"version\": 1, \"personal\": [{\"code\": \"A\", "
                                "\"rgb\": [0, 0]}]}")
                    .has_value());
    CHECK_FALSE(stock_from_json("{\"version\": 1, \"personal\": [{\"code\": \"A\", "
                                "\"rgb\": [0, 0, 1.5]}]}")
                    .has_value());
    CHECK_FALSE(stock_from_json("{\"version\": 1, \"personal\": [{\"code\": \"A\", "
                                "\"rgb\": [0,0,1]}, {\"code\": \"A\", \"rgb\": [0,0,2]}]}")
                    .has_value());

    const auto future = stock_from_json("{\"version\": 99, \"owned\": []}");
    REQUIRE_FALSE(future.has_value());
    CHECK(future.error().category == ErrorCategory::UnsupportedFormat);
}
