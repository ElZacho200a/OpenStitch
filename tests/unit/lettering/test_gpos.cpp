// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "openstitch/lettering/gpos.hpp"

using openstitch::lettering::GposKerning;

namespace {

void put16(std::vector<std::uint8_t>& out, int v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

// Table GPOS minimale : une fonctionnalité « kern », une consultation PairPos portant
// la sous-table donnée. Disposition : en-tête 10 o, FeatureList à 10, LookupList à 24,
// consultation à 28, sous-table à 36.
std::vector<std::uint8_t> gpos_with(const std::vector<std::uint8_t>& sub) {
    std::vector<std::uint8_t> t;
    put16(t, 1);  // majeure
    put16(t, 0);  // mineure
    put16(t, 0);  // ScriptList (non lue)
    put16(t, 10); // FeatureList
    put16(t, 24); // LookupList
    put16(t, 1);  // FeatureList : 1 fonctionnalité
    t.insert(t.end(), {'k', 'e', 'r', 'n'});
    put16(t, 8); // décalage de la fonctionnalité
    put16(t, 0); // FeatureParams
    put16(t, 1); // 1 consultation
    put16(t, 0); // indice 0
    put16(t, 1); // LookupList : 1 consultation
    put16(t, 4); // décalage
    put16(t, 2); // type PairPos
    put16(t, 0); // drapeaux
    put16(t, 1); // 1 sous-table
    put16(t, 8); // décalage de la sous-table
    t.insert(t.end(), sub.begin(), sub.end());
    return t;
}

} // namespace

TEST_CASE("gpos pair format 1 gives the x advance of a listed pair") {
    std::vector<std::uint8_t> sub;
    put16(sub, 1);  // format 1
    put16(sub, 12); // couverture
    put16(sub, 4);  // ValueFormat1 : XAdvance
    put16(sub, 0);  // ValueFormat2
    put16(sub, 1);  // 1 PairSet
    put16(sub, 18); // décalage du PairSet
    put16(sub, 1);  // couverture format 1
    put16(sub, 1);
    put16(sub, 10); // glyphe 10
    put16(sub, 1);  // PairSet : 1 paire
    put16(sub, 20); // second glyphe
    put16(sub, -100);
    const auto kerning = GposKerning::parse(gpos_with(sub));
    REQUIRE(kerning != nullptr);
    CHECK(kerning->pair_adjustment(10, 20) == -100.0);
    CHECK(kerning->pair_adjustment(10, 21) == 0.0);
    CHECK(kerning->pair_adjustment(11, 20) == 0.0);
}

TEST_CASE("gpos pair format 2 resolves glyph classes") {
    std::vector<std::uint8_t> sub;
    put16(sub, 2);  // format 2
    put16(sub, 24); // couverture
    put16(sub, 4);  // ValueFormat1 : XAdvance
    put16(sub, 0);
    put16(sub, 30);  // ClassDef1
    put16(sub, 38);  // ClassDef2
    put16(sub, 2);   // 2 classes de gauche
    put16(sub, 2);   // 2 classes de droite
    put16(sub, 0);   // [0][0]
    put16(sub, 0);   // [0][1]
    put16(sub, 0);   // [1][0]
    put16(sub, -50); // [1][1]
    put16(sub, 1);   // couverture : glyphe 10
    put16(sub, 1);
    put16(sub, 10);
    put16(sub, 1); // ClassDef1 format 1 : glyphe 10 -> classe 1
    put16(sub, 10);
    put16(sub, 1);
    put16(sub, 1);
    put16(sub, 2); // ClassDef2 format 2 : 20..25 -> classe 1
    put16(sub, 1);
    put16(sub, 20);
    put16(sub, 25);
    put16(sub, 1);
    const auto kerning = GposKerning::parse(gpos_with(sub));
    REQUIRE(kerning != nullptr);
    CHECK(kerning->pair_adjustment(10, 22) == -50.0);
    CHECK(kerning->pair_adjustment(10, 26) == 0.0);
    CHECK(kerning->pair_adjustment(12, 22) == 0.0);
}

TEST_CASE("gpos parser refuses truncated or empty tables without crashing") {
    CHECK(GposKerning::parse({}) == nullptr);
    CHECK(GposKerning::parse({0, 1, 0, 0}) == nullptr);
    std::vector<std::uint8_t> sub;
    put16(sub, 1);
    auto table = gpos_with(sub); // sous-table tronquée : lectures hors table = 0
    const auto kerning = GposKerning::parse(table);
    if (kerning != nullptr) {
        CHECK(kerning->pair_adjustment(1, 2) == 0.0);
    }
}
