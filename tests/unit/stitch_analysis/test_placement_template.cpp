// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/stitch_analysis/placement_template.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

stitch::StitchSequence rectangle(std::int32_t w, std::int32_t h) {
    using T = stitch::CommandType;
    stitch::StitchSequence seq;
    seq.commands = {{um(0, 0), T::Jump, ObjectId{1}},   {um(0, 0), T::Stitch, ObjectId{1}},
                    {um(w, 0), T::Stitch, ObjectId{1}}, {um(w, h), T::Stitch, ObjectId{1}},
                    {um(0, h), T::Stitch, ObjectId{1}}, {um(0, 0), T::Stitch, ObjectId{1}},
                    {um(0, 0), T::End, ObjectId{0}}};
    return seq;
}

} // namespace

TEST_CASE("gabarit de placement : petit motif sur une page avec croix et regle") {
    const auto tpl = make_placement_template(rectangle(40'000, 30'000));
    REQUIRE(tpl.pages.size() == 1);
    CHECK(tpl.columns == 1);
    CHECK(tpl.rows == 1);
    CHECK(tpl.design_width.value == 40'000);
    CHECK(tpl.design_height.value == 30'000);
    const auto& p = tpl.pages[0];
    CHECK(p.stitches.size() == 4);
    CHECK(p.crosshair.size() == 2);
    CHECK(p.axes.size() == 2);
    REQUIRE(p.ruler.size() >= 1);
    // La regle mesure exactement 100 mm.
    CHECK(p.ruler[0].b.x.value - p.ruler[0].a.x.value == 100'000);
    // Le centre du motif tombe au centre de la zone utile (A4 : 105 mm, 148,5 mm).
    CHECK(p.crosshair[0].a.x.value == 100'000);
    CHECK(p.crosshair[0].b.x.value == 110'000);
}

TEST_CASE("gabarit de placement : un grand motif est decoupe en pages qui se recouvrent") {
    const auto tpl = make_placement_template(rectangle(400'000, 150'000));
    CHECK(tpl.columns == 3); // zone utile 190 mm, pas 180 mm : 3 colonnes pour 400 mm
    CHECK(tpl.rows == 1);
    REQUIRE(tpl.pages.size() == 3);
    // Seule la page centrale porte la croix de centrage ; la regle est sur la premiere.
    CHECK(tpl.pages[0].crosshair.empty());
    CHECK(tpl.pages[1].crosshair.size() == 2);
    CHECK(!tpl.pages[0].ruler.empty());
    CHECK(tpl.pages[1].ruler.empty());
    for (const auto& pg : tpl.pages) {
        for (const auto& s : pg.stitches) {
            CHECK(s.a.x.value >= 10'000);
            CHECK(s.b.x.value <= 200'000);
            CHECK(s.a.y.value >= 10'000);
            CHECK(s.b.y.value <= 287'000);
        }
    }
}

TEST_CASE("gabarit de placement : orientation paysage et sequence vide") {
    PlacementTemplateOptions opt;
    opt.landscape = true;
    const auto tpl = make_placement_template(stitch::StitchSequence{}, opt);
    CHECK(tpl.page_width.value == 297'000);
    CHECK(tpl.page_height.value == 210'000);
    CHECK(tpl.pages.size() == 1);
}

TEST_CASE("gabarit de placement : PDF a l'echelle 1:1, multi-pages et deterministe") {
    const auto tpl = make_placement_template(rectangle(400'000, 150'000));
    const std::string a = placement_template_to_pdf(tpl);
    const std::string b = placement_template_to_pdf(tpl);
    CHECK(a == b);
    CHECK(a.rfind("%PDF-1.4", 0) == 0);
    CHECK(a.find("/Count 3") != std::string::npos);
    // A4 = 595,28 x 841,89 pt : 1 pt = 25,4/72 mm exactement.
    CHECK(a.find("/MediaBox [0 0 595.28 841.89]") != std::string::npos);
    CHECK(a.find("%%EOF") != std::string::npos);
}
