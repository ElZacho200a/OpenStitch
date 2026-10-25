// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

#include "openstitch/document/hoop_library.hpp"
#include "openstitch/document/machine_profile.hpp"

using namespace openstitch;
using namespace openstitch::document;

namespace {
Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}
} // namespace

TEST_CASE("hoop shape: rectangle keeps the exact bounds test") {
    Canvas c;
    c.width = Micrometers{100'000};
    c.height = Micrometers{60'000};
    c.margin = Micrometers{5'000};
    CHECK(c.contains_usable(um(45'000, 25'000)));
    CHECK(!c.contains_usable(um(45'001, 0)));
    CHECK(!c.contains_usable(um(0, -25'001)));
}

TEST_CASE("hoop shape: ellipse excludes the corners of the bounding box") {
    Canvas c = canvas_from_hoop(*find_hoop("round-100"));
    CHECK(c.shape == HoopShape::Ellipse);
    CHECK(c.contains_usable(um(50'000, 0)));
    CHECK(c.contains_usable(um(0, -50'000)));
    CHECK(c.contains_usable(um(35'000, 35'000)));  // sur le disque (r = 49,5 mm)
    CHECK(!c.contains_usable(um(40'000, 40'000))); // dans la boite, hors du disque
    CHECK(!c.contains_usable(um(50'001, 0)));

    // La marge reduit l'ovale a la zone brodable.
    c.margin = Micrometers{10'000};
    CHECK(c.contains_usable(um(40'000, 0)));
    CHECK(!c.contains_usable(um(41'000, 0)));
}

TEST_CASE("hoop shape: rounded rectangle cuts only the corner arcs") {
    Canvas c;
    c.width = Micrometers{100'000};
    c.height = Micrometers{60'000};
    c.shape = HoopShape::RoundedRectangle;
    c.corner_radius = Micrometers{10'000};
    CHECK(c.contains_usable(um(50'000, 0)));       // milieu d'un cote
    CHECK(c.contains_usable(um(40'000, 30'000)));  // fin de l'arc, sur le bord
    CHECK(!c.contains_usable(um(50'000, 30'000))); // coin vif exclu
    CHECK(c.contains_usable(um(47'000, 27'000)));  // dans l'arc (4,2 mm du centre du coin)
}

TEST_CASE("hoop library: identifiers are unique and sizes sane") {
    std::set<std::string> ids;
    for (const HoopSpec& h : hoop_library()) {
        CHECK(ids.insert(std::string(h.id)).second);
        CHECK(!h.family.empty());
        CHECK(!h.name.empty());
        CHECK(h.width_mm >= 10);
        CHECK(h.height_mm >= 10);
        if (h.shape == HoopShape::RoundedRectangle) {
            CHECK(h.corner_radius_mm > 0);
            CHECK(2 * h.corner_radius_mm <= std::min(h.width_mm, h.height_mm));
        }
    }
    CHECK(find_hoop("nope") == nullptr);
    REQUIRE(find_hoop("rect-130x180") != nullptr);
    CHECK(canvas_from_hoop(*find_hoop("rect-130x180"), Micrometers{3'000}).margin.value == 3'000);
}

TEST_CASE("machine profiles: hoops exist and fit the machine field") {
    std::set<std::string> ids;
    for (const MachineProfile& m : machine_profiles()) {
        CHECK(ids.insert(std::string(m.id)).second);
        CHECK(m.needles >= 1);
        CHECK(m.format_count >= 1);
        CHECK(m.accepts_format(m.preferred_format));
        CHECK(!m.accepts_format("zzz"));
        CHECK(m.hoop_count >= 1);
        for (const std::string_view hoopId : m.hoops()) {
            const HoopSpec* h = find_hoop(hoopId);
            REQUIRE(h != nullptr);
            CHECK(h->width_mm <= m.max_field_width_mm);
            CHECK(h->height_mm <= m.max_field_height_mm);
        }
    }
    CHECK(find_machine("") == nullptr);
    CHECK(find_machine("generic-multi-6") != nullptr);
}
