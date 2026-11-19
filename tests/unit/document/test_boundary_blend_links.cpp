// SPDX-License-Identifier: Apache-2.0
// Fondus à la frontière multiples : une zone porte un lien par voisine, résolus couple par couple
// (docs/studio-fondu-frontiere-spec.md §4.1).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include "openstitch/document/boundary_blend.hpp"
#include "openstitch/document/project.hpp"

using namespace openstitch;

namespace {

// Ids : zone k = vecteur 2k-1 + tatami 2k (A = 2, B = 4, C = 6, D = 8).
document::Project make_zones(int count) {
    document::Project p;
    for (int i = 0; i < count; ++i) {
        document::VectorObject v;
        v.id = p.object_ids.next();
        p.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.source_vector = v.id;
        e.rgb = {static_cast<std::uint8_t>(40 * i), 0, 0};
        e.params = document::TatamiParams{};
        p.embroidery_objects.push_back(std::move(e));
    }
    return p;
}

document::BoundaryBlendLink link_to(std::uint64_t partner, std::int32_t width = 2'500) {
    document::BoundaryBlendLink l;
    l.partner = ObjectId{partner};
    l.width_self = Micrometers{width};
    return l;
}

void link_pair(document::Project& p, std::uint64_t a, std::uint64_t b) {
    document::set_boundary_blend_link(p.findEmbroidery(ObjectId{a})->boundary_blends, ObjectId{b},
                                      link_to(b));
    document::set_boundary_blend_link(p.findEmbroidery(ObjectId{b})->boundary_blends, ObjectId{a},
                                      link_to(a));
}

} // namespace

TEST_CASE("find_boundary_blends resolves two valid neighbours sorted by partner",
          "[document][boundary_blend]") {
    auto p = make_zones(3);
    link_pair(p, 2, 6); // posé d'abord : la liste reste triée par partenaire
    link_pair(p, 2, 4);
    const auto& links = p.findEmbroidery(ObjectId{2})->boundary_blends;
    REQUIRE(links.size() == 2);
    CHECK(links[0].partner == ObjectId{4});
    CHECK(links[1].partner == ObjectId{6});

    const auto pairs = document::find_boundary_blends(p, ObjectId{2});
    REQUIRE(pairs.size() == 2);
    CHECK(pairs[0] == document::BoundaryBlendPair{ObjectId{2}, ObjectId{4}});
    CHECK(pairs[1] == document::BoundaryBlendPair{ObjectId{2}, ObjectId{6}});
    CHECK(document::find_boundary_blend(p, ObjectId{4}, ObjectId{2}).has_value());
    CHECK(document::find_boundary_blends(p, ObjectId{6}).size() == 1);
    CHECK_FALSE(document::find_boundary_blend(p, ObjectId{4}, ObjectId{6}).has_value());
    REQUIRE(document::boundary_blend_link(*p.findEmbroidery(ObjectId{2}), ObjectId{6}) != nullptr);
    CHECK(document::boundary_blend_link(*p.findEmbroidery(ObjectId{4}), ObjectId{6}) == nullptr);
}

TEST_CASE("find_boundary_blends ignores a non reciprocal or inconsistent link",
          "[document][boundary_blend]") {
    auto p = make_zones(4);
    link_pair(p, 2, 4);
    // A -> C sans retour : sans effet, et sans effet sur A-B.
    document::set_boundary_blend_link(p.findEmbroidery(ObjectId{2})->boundary_blends, ObjectId{6},
                                      link_to(6));
    // A <-> D avec des courbes différentes : incohérent.
    link_pair(p, 2, 8);
    p.findEmbroidery(ObjectId{8})->boundary_blends.front().curve = document::DensityCurve::Smooth;

    const auto pairs = document::find_boundary_blends(p, ObjectId{2});
    REQUIRE(pairs.size() == 1);
    CHECK(pairs[0].partner == ObjectId{4});
    CHECK_FALSE(document::find_boundary_blend(p, ObjectId{2}, ObjectId{6}).has_value());
    CHECK_FALSE(document::find_boundary_blend(p, ObjectId{2}, ObjectId{8}).has_value());
    CHECK(document::find_boundary_blends(p, ObjectId{6}).empty());
}

TEST_CASE("set and remap boundary blend links keep one sorted link per partner",
          "[document][boundary_blend]") {
    std::vector<document::BoundaryBlendLink> links;
    document::set_boundary_blend_link(links, ObjectId{9}, link_to(9));
    document::set_boundary_blend_link(links, ObjectId{3}, link_to(3));
    document::set_boundary_blend_link(links, ObjectId{9}, link_to(9, 4'000)); // remplace
    REQUIRE(links.size() == 2);
    CHECK(links[0].partner == ObjectId{3});
    CHECK(links[1].width_self == Micrometers{4'000});
    document::set_boundary_blend_link(links, ObjectId{3}, std::nullopt);
    REQUIRE(links.size() == 1);
    CHECK(links[0].partner == ObjectId{9});

    std::vector<document::BoundaryBlendLink> copy{link_to(3), link_to(9), link_to(12)};
    document::remap_boundary_blend_links(
        copy, {{ObjectId{3}, ObjectId{50}}, {ObjectId{9}, ObjectId{40}}});
    REQUIRE(copy.size() == 2); // partenaire 12 non copié : lien retiré
    CHECK(copy[0].partner == ObjectId{40});
    CHECK(copy[1].partner == ObjectId{50});
}
