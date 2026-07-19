// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "openstitch/stitch_analysis/analyze.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

bool has_category(const std::vector<Finding>& f, const std::string& cat) {
    return std::any_of(f.begin(), f.end(), [&](const Finding& x) { return x.category == cat; });
}

std::size_t count_category(const std::vector<Finding>& f, const std::string& cat) {
    return static_cast<std::size_t>(
        std::count_if(f.begin(), f.end(), [&](const Finding& x) { return x.category == cat; }));
}

}  // namespace

TEST_CASE("sequence vide -> une erreur 'vide'") {
    const auto f = analyze({});
    REQUIRE(f.size() == 1);
    CHECK(f[0].severity == Severity::Error);
    CHECK(f[0].category == "vide");
}

TEST_CASE("motif correct -> aucun probleme") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{}},
        {um(3'000, 0), CommandType::Stitch, ObjectId{}},
        {um(6'000, 0), CommandType::Stitch, ObjectId{}},
        {um(6'000, 0), CommandType::End, ObjectId{}},
    };
    CHECK(analyze(seq).empty());
}

TEST_CASE("point trop court detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(200, 0), CommandType::Stitch, ObjectId{1}},  // 0,2 mm < 0,5 mm
        {um(3'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto f = analyze(seq);
    CHECK(has_category(f, "point-court"));
    CHECK(count_category(f, "point-court") == 1);
}

TEST_CASE("point trop long detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(9'000, 0), CommandType::Stitch, ObjectId{1}},  // 9 mm > 7 mm
    };
    CHECK(has_category(analyze(seq), "point-long"));
}

TEST_CASE("saut trop long detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(50'000, 0), CommandType::Jump, ObjectId{1}},  // 50 mm > 30 mm
        {um(50'000, 0), CommandType::Stitch, ObjectId{1}},
    };
    CHECK(has_category(analyze(seq), "saut-long"));
}

TEST_CASE("point hors cadre detecte") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(60'000, 0), CommandType::Stitch, ObjectId{1}},  // hors d'un cadre 100x100 centre
    };
    AnalysisOptions opts;
    opts.hoop = stitch::BoundsUm{um(-50'000, -50'000), um(50'000, 50'000)};
    const auto f = analyze(seq, opts);
    REQUIRE(has_category(f, "hors-cadre"));
    // Les erreurs sont triees en tete.
    CHECK(f.front().severity == Severity::Error);
}

TEST_CASE("plafond par categorie respecte") {
    stitch::StitchSequence seq;
    // 200 points tres courts d'affilee.
    for (int i = 0; i < 200; ++i) {
        seq.commands.push_back({um(i * 100, 0), CommandType::Stitch, ObjectId{1}});
    }
    AnalysisOptions opts;
    opts.max_findings_per_category = 10;
    CHECK(count_category(analyze(seq, opts), "point-court") == 10);
}

TEST_CASE("deterministe") {
    stitch::StitchSequence seq;
    seq.commands = {
        {um(0, 0), CommandType::Stitch, ObjectId{1}},
        {um(9'000, 0), CommandType::Stitch, ObjectId{1}},
        {um(9'100, 0), CommandType::Stitch, ObjectId{1}},
    };
    const auto a = analyze(seq);
    const auto b = analyze(seq);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].category == b[i].category);
    }
}
