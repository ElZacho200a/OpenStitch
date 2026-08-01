// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "openstitch/stitch_generation/satin_guides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {
geometry::Path rail(std::int32_t y) {
    geometry::Path path;
    path.closed = false;
    path.nodes = {{{Micrometers{0}, Micrometers{y}}},
                  {{Micrometers{10'000}, Micrometers{y}}}};
    return path;
}

document::SatinParams satin() {
    document::SatinParams p;
    p.rail_a = rail(0);
    p.rail_b = rail(4'000);
    p.density = Micrometers{400};
    p.rungs = {{{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
               {{Micrometers{5'000}, Micrometers{0}}, {Micrometers{5'000}, Micrometers{4'000}}},
               {{Micrometers{10'000}, Micrometers{0}}, {Micrometers{10'000}, Micrometers{4'000}}}};
    return p;
}
}  // namespace

TEST_CASE("un guide satin deplace se projette exactement sur son rail") {
    const auto p = satin();
    const auto moved = move_satin_guide_endpoint(
        p, 1, SatinGuideSide::RailA, {Micrometers{6'000}, Micrometers{1'500}});
    REQUIRE(moved.has_value());
    CHECK(moved->a == Vec2um{Micrometers{6'000}, Micrometers{0}});
    CHECK(moved->b == p.rungs[1].b);
}

TEST_CASE("un guide satin ne peut pas franchir son voisin sur un seul rail") {
    const auto p = satin();
    CHECK_FALSE(move_satin_guide_endpoint(
                    p, 1, SatinGuideSide::RailA,
                    {Micrometers{9'950}, Micrometers{0}})
                    .has_value());
}

TEST_CASE("un index de guide satin obsolete est refuse") {
    const auto p = satin();
    CHECK_FALSE(move_satin_guide_endpoint(p, 99, SatinGuideSide::RailB, {}).has_value());
}

TEST_CASE("un nouveau guide satin partage le plus grand intervalle") {
    auto p = satin();
    p.rungs[1].a.x = Micrometers{2'000};
    p.rungs[1].b.x = Micrometers{2'000};
    const auto insertion = make_satin_guide_in_largest_gap(p);
    REQUIRE(insertion.has_value());
    CHECK(insertion->index == 2);
    CHECK(insertion->guide.a == Vec2um{Micrometers{6'000}, Micrometers{0}});
    CHECK(insertion->guide.b == Vec2um{Micrometers{6'000}, Micrometers{4'000}});
}

TEST_CASE("aucun guide satin ajoute dans un intervalle trop court") {
    auto p = satin();
    p.density = Micrometers{6'000};
    CHECK_FALSE(make_satin_guide_in_largest_gap(p).has_value());
}

TEST_CASE("un nouveau guide satin preserve un ordre de guides inverse") {
    auto p = satin();
    std::reverse(p.rungs.begin(), p.rungs.end());
    const auto insertion = make_satin_guide_in_largest_gap(p);
    REQUIRE(insertion.has_value());
    CHECK(insertion->index == 2);
    p.rungs.insert(p.rungs.begin() + static_cast<std::ptrdiff_t>(insertion->index),
                   insertion->guide);
    CHECK(p.rungs[0].a.x.value > p.rungs[1].a.x.value);
    CHECK(p.rungs[1].a.x.value > p.rungs[2].a.x.value);
}

TEST_CASE("un guide terminal de jonction est structurel meme si les rungs sont inverses") {
    auto p = satin();
    p.topology = document::SatinSectionTopology{0, 3, 7, std::nullopt};
    CHECK(satin_guide_junction(p, 0) == std::optional<std::uint32_t>{7});
    CHECK_FALSE(satin_guide_junction(p, 1).has_value());
    CHECK_FALSE(move_satin_guide_endpoint(
                    p, 0, SatinGuideSide::RailA,
                    {Micrometers{1'000}, Micrometers{0}})
                    .has_value());
    CHECK(move_satin_guide_endpoint(
              p, 1, SatinGuideSide::RailA,
              {Micrometers{6'000}, Micrometers{0}})
              .has_value());

    std::reverse(p.rungs.begin(), p.rungs.end());
    CHECK(satin_guide_junction(p, 2) == std::optional<std::uint32_t>{7});
    CHECK_FALSE(satin_guide_junction(p, 0).has_value());
}

TEST_CASE("les guides d'une jonction sont retrouves par reseau et tries par section") {
    document::Project project;
    const ObjectId source{42};
    const auto addSection = [&](std::uint32_t section, std::uint32_t junction,
                                ObjectId sectionSource, bool reverse) {
        document::EmbroideryObject object;
        object.id = project.object_ids.next();
        object.source_vector = sectionSource;
        auto params = satin();
        params.topology = document::SatinSectionTopology{
            section, 3, junction, std::nullopt};
        if (reverse) {
            std::reverse(params.rungs.begin(), params.rungs.end());
        }
        object.params = std::move(params);
        project.embroidery_objects.push_back(std::move(object));
        return project.embroidery_objects.back().id;
    };

    const ObjectId section2 = addSection(2, 7, source, false);
    addSection(1, 7, ObjectId{99}, false);  // même ID de jonction, autre réseau
    const ObjectId section0 = addSection(0, 7, source, true);
    addSection(1, 8, source, false);  // même réseau, autre jonction

    const auto refs = satin_junction_guides(project, source, 7);
    REQUIRE(refs.size() == 2);
    CHECK(refs[0] == SatinJunctionGuideRef{section0, 2, 0});
    CHECK(refs[1] == SatinJunctionGuideRef{section2, 0, 2});
    CHECK(satin_junction_guides(project, ObjectId{}, 7).empty());

    project.embroidery_objects.pop_back();  // section 1 du réseau absente
    CHECK(satin_junction_guides(project, source, 7).empty());
}
