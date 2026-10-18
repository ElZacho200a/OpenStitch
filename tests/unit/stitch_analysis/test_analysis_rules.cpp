// SPDX-License-Identifier: Apache-2.0
#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/analyze.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;
using stitch::StitchPass;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

void stitch_at(stitch::StitchSequence& s, std::int32_t x, std::int32_t y, std::uint64_t source,
               StitchPass pass = StitchPass::TopStitch) {
    s.commands.push_back({um(x, y), CommandType::Stitch, ObjectId{source}, pass});
}

std::vector<Finding> of_category(const std::vector<Finding>& all, const std::string& category) {
    std::vector<Finding> out;
    std::copy_if(all.begin(), all.end(), std::back_inserter(out),
                 [&](const Finding& f) { return f.category == category; });
    return out;
}

// Objet 1 : carré de 5 mm ; objet 2 : détail de 0,6 x 0,4 mm.
stitch::StitchSequence big_and_tiny() {
    stitch::StitchSequence s;
    stitch_at(s, 0, 0, 1);
    stitch_at(s, 5'000, 0, 1);
    stitch_at(s, 5'000, 5'000, 1);
    stitch_at(s, 0, 5'000, 1);
    s.commands.push_back({um(20'000, 0), CommandType::Jump, ObjectId{2}});
    stitch_at(s, 20'000, 0, 2);
    stitch_at(s, 20'600, 400, 2);
    return s;
}

} // namespace

TEST_CASE("rule detail-trop-petit flags objects under the minimum size on both axes") {
    const auto findings = of_category(analyze(big_and_tiny()), "detail-trop-petit");
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].object == ObjectId{2});
    CHECK(findings[0].severity == Severity::Info);
    CHECK(findings[0].location == um(20'000, 0));
}

TEST_CASE("rule detail-trop-petit is disabled by a zero minimum size and ignores lock stitches") {
    AnalysisOptions off;
    off.min_object_size = Micrometers{0};
    CHECK(of_category(analyze(big_and_tiny(), off), "detail-trop-petit").empty());

    // Les points d'arrêt, éloignés, ne comptent pas dans la boîte de l'objet.
    stitch::StitchSequence s = big_and_tiny();
    stitch_at(s, 29'000, 9'000, 2, StitchPass::Lock);
    CHECK(of_category(analyze(s), "detail-trop-petit").size() == 1);
}

TEST_CASE("rule objet-sans-arret is opt-in and satisfied by a lock pass") {
    stitch::StitchSequence s = big_and_tiny();
    CHECK(of_category(analyze(s), "objet-sans-arret").empty());

    AnalysisOptions options;
    options.check_lock_stitches = true;
    const auto none = of_category(analyze(s, options), "objet-sans-arret");
    REQUIRE(none.size() == 2);
    CHECK(none[0].object == ObjectId{1});
    CHECK(none[1].object == ObjectId{2});

    stitch_at(s, 100, 0, 1, StitchPass::Lock);
    const auto one = of_category(analyze(s, options), "objet-sans-arret");
    REQUIRE(one.size() == 1);
    CHECK(one[0].object == ObjectId{2});
}

TEST_CASE("options from a project enable the lock stitch rule only with finishing") {
    document::Project p;
    p.finishing.enabled = true;
    CHECK(options_from_project(p).check_lock_stitches);
    p.finishing.enabled = false;
    CHECK_FALSE(options_from_project(p).check_lock_stitches);
}

TEST_CASE("rule trop-de-coupes counts trims against the limit") {
    stitch::StitchSequence s = big_and_tiny();
    for (int i = 0; i < 3; ++i) {
        s.commands.push_back({um(0, 0), CommandType::Trim, ObjectId{1}});
    }
    CHECK(of_category(analyze(s), "trop-de-coupes").empty()); // désactivée par défaut
    AnalysisOptions options;
    options.max_trims = 3;
    CHECK(of_category(analyze(s, options), "trop-de-coupes").empty()); // 3 n'est pas > 3
    options.max_trims = 2;
    const auto f = of_category(analyze(s, options), "trop-de-coupes");
    REQUIRE(f.size() == 1);
    CHECK(f[0].severity == Severity::Warning);
}

TEST_CASE("rule densite-excessive reports over-dense zones when enabled") {
    stitch::StitchSequence s;
    for (int k = 0; k < 8; ++k) { // 8 piqûres dans la même case de 1 mm
        stitch_at(s, 100 + k * 100, 100 + k * 100, 5);
    }
    stitch_at(s, 9'000, 9'000, 5);
    CHECK(of_category(analyze(s), "densite-excessive").empty()); // désactivée par défaut
    AnalysisOptions options;
    options.max_penetrations_per_mm2 = 4.0;
    const auto f = of_category(analyze(s, options), "densite-excessive");
    REQUIRE(f.size() == 1);
    CHECK(f[0].object == ObjectId{5});
    CHECK(f[0].location == um(500, 500));
    const auto a = analyze(s, options);
    const auto b = analyze(s, options);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].message == b[i].message);
    }
}
