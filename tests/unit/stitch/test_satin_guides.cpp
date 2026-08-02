// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <optional>

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

TEST_CASE("un guide ajoute depuis une jonction reste dans son intervalle adjacent") {
    auto p = satin();
    p.rungs[1].a.x = Micrometers{2'000};
    p.rungs[1].b.x = Micrometers{2'000};
    p.topology = document::SatinSectionTopology{0, 2, 7, std::nullopt};

    const auto insertion = make_satin_guide_next_to_junction(p, 0);
    REQUIRE(insertion.has_value());
    CHECK(insertion->index == 1);
    CHECK(insertion->guide.a == Vec2um{Micrometers{1'000}, Micrometers{0}});
    CHECK(insertion->guide.b == Vec2um{Micrometers{1'000}, Micrometers{4'000}});

    std::reverse(p.rungs.begin(), p.rungs.end());
    const auto reversed = make_satin_guide_next_to_junction(p, 2);
    REQUIRE(reversed.has_value());
    CHECK(reversed->index == 2);
    CHECK(reversed->guide == insertion->guide);
}

TEST_CASE("un ajout adjacent exige une jonction et une progression bilaterale suffisante") {
    auto p = satin();
    CHECK_FALSE(make_satin_guide_next_to_junction(p, 0).has_value());
    p.topology = document::SatinSectionTopology{0, 2, 7, std::nullopt};
    CHECK_FALSE(make_satin_guide_next_to_junction(p, 1).has_value());
    p.density = Micrometers{6'000};
    CHECK_FALSE(make_satin_guide_next_to_junction(p, 0).has_value());
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

TEST_CASE("les identifiants de guides lies sont locaux au reseau et monotones") {
    document::Project project;
    const ObjectId source{42};
    const auto addSection = [&](ObjectId sectionSource, std::optional<std::uint32_t> link) {
        document::EmbroideryObject object;
        object.id = project.object_ids.next();
        object.source_vector = sectionSource;
        auto params = satin();
        params.rungs[1].link_id = link;
        object.params = std::move(params);
        project.embroidery_objects.push_back(std::move(object));
    };
    addSection(source, 2);
    addSection(source, std::nullopt);
    addSection(ObjectId{99}, 200);
    CHECK(next_satin_guide_link_id(project, source) == std::optional<std::uint32_t>{3});
    CHECK_FALSE(next_satin_guide_link_id(project, ObjectId{}).has_value());

    auto& linked = std::get<document::SatinParams>(project.embroidery_objects[1].params);
    linked.rungs[1].link_id = std::numeric_limits<std::uint32_t>::max();
    CHECK_FALSE(next_satin_guide_link_id(project, source).has_value());
}
namespace {

// Réseau à deux sections partageant la jonction #7 : chacune porte un guide
// interne lié (link_id) collé à la jonction, avec sa PROPRE géométrie de rail
// (longueurs différentes) pour que les tests de move_satin_guide_group ne
// puissent pas réussir par accident en recopiant une coordonnée brute d'une
// section à l'autre.
struct LinkedGroupFixture {
    document::Project project;
    ObjectId source{42};
    ObjectId section0{};
    ObjectId section1{};
};

LinkedGroupFixture make_linked_group_fixture(std::uint32_t linkId = 9) {
    LinkedGroupFixture fx;
    const auto addSection = [&](std::uint32_t sectionIndex, std::int32_t railLength,
                                std::int32_t linkedX, std::int32_t otherX) {
        document::SatinParams params;
        params.rail_a = rail(0);
        params.rail_b = rail(4'000);
        params.rail_a.nodes.back().pos.x = Micrometers{railLength};
        params.rail_b.nodes.back().pos.x = Micrometers{railLength};
        params.density = Micrometers{400};
        params.rungs = {
            {{Micrometers{0}, Micrometers{0}}, {Micrometers{0}, Micrometers{4'000}}},
            {{Micrometers{linkedX}, Micrometers{0}},
             {Micrometers{linkedX}, Micrometers{4'000}},
             linkId},
            {{Micrometers{otherX}, Micrometers{0}}, {Micrometers{otherX}, Micrometers{4'000}}},
            {{Micrometers{railLength}, Micrometers{0}},
             {Micrometers{railLength}, Micrometers{4'000}}}};
        params.topology =
            document::SatinSectionTopology{sectionIndex, 2, std::uint32_t{7}, std::nullopt};
        document::EmbroideryObject object;
        object.id = fx.project.object_ids.next();
        object.source_vector = fx.source;
        object.params = std::move(params);
        fx.project.embroidery_objects.push_back(std::move(object));
        return fx.project.embroidery_objects.back().id;
    };
    fx.section0 = addSection(0, 10'000, 1'000, 5'000);  // tA = tB = 0.2
    fx.section1 = addSection(1, 20'000, 4'000, 10'000); // tA = tB = 0.4
    return fx;
}

} // namespace

TEST_CASE("un groupe lie est retrouve par link_id, ordonne, et isole par reseau") {
    auto fx = make_linked_group_fixture();
    const auto refs = satin_linked_guides(fx.project, fx.source, 9);
    REQUIRE(refs.size() == 2);
    CHECK(refs[0] == SatinJunctionGuideRef{fx.section0, 1, 0});
    CHECK(refs[1] == SatinJunctionGuideRef{fx.section1, 1, 1});

    CHECK(satin_linked_guides(fx.project, ObjectId{}, 9).empty());
    CHECK(satin_linked_guides(fx.project, fx.source, 12345).empty());

    // Meme link_id dans un autre reseau (source_vector different) : aucune
    // fuite, chaque reseau reste isole.
    document::EmbroideryObject other;
    other.id = fx.project.object_ids.next();
    other.source_vector = ObjectId{99};
    auto otherParams =
        std::get<document::SatinParams>(fx.project.findEmbroidery(fx.section0)->params);
    other.params = otherParams;
    fx.project.embroidery_objects.push_back(other);
    const auto stillTwo = satin_linked_guides(fx.project, fx.source, 9);
    REQUIRE(stillTwo.size() == 2);
    CHECK(stillTwo[0].embroidery_id == fx.section0);
    CHECK(stillTwo[1].embroidery_id == fx.section1);

    // Deux guides du meme link_id dans une seule section : donnee corrompue,
    // le groupe entier est refuse plutot que de deviner lequel est valide.
    auto& corrupted =
        std::get<document::SatinParams>(fx.project.findEmbroidery(fx.section0)->params);
    corrupted.rungs[2].link_id = 9;
    CHECK(satin_linked_guides(fx.project, fx.source, 9).empty());
}

TEST_CASE("un groupe lie refuse un reseau topologique incomplet") {
    auto fx = make_linked_group_fixture();
    fx.project.embroidery_objects.erase(
        std::remove_if(fx.project.embroidery_objects.begin(), fx.project.embroidery_objects.end(),
                       [&](const auto& object) { return object.id == fx.section1; }),
        fx.project.embroidery_objects.end());
    CHECK(satin_linked_guides(fx.project, fx.source, 9).empty());
}

TEST_CASE("le deplacement de groupe applique le meme delta normalise a chaque section") {
    auto fx = make_linked_group_fixture();
    const auto edits =
        move_satin_guide_group(fx.project, fx.source, 9, fx.section0, SatinGuideSide::RailA,
                               {Micrometers{2'000}, Micrometers{0}});
    REQUIRE(edits.has_value());
    REQUIRE(edits->size() == 2);

    const auto findEdit = [&](ObjectId id) {
        return std::find_if(edits->begin(), edits->end(),
                            [&](const auto& e) { return e.embroidery_id == id; });
    };
    const auto e0 = findEdit(fx.section0);
    const auto e1 = findEdit(fx.section1);
    REQUIRE(e0 != edits->end());
    REQUIRE(e1 != edits->end());
    CHECK(e0->guide_index == 1);
    CHECK(e1->guide_index == 1);

    // Section 0 : tA passe de 0.2 a 0.4 (delta +0.2) -> station 0.4*5000=2000.
    CHECK(e0->guide.a == Vec2um{Micrometers{2'000}, Micrometers{0}});
    CHECK(e0->guide.b == Vec2um{Micrometers{2'000}, Micrometers{4'000}});
    // Section 1 : MEME delta +0.2 mais propre echelle (span 10000) -> tA
    // 0.4->0.6, station 0.6*10000=6000. Une coordonnee brute recopiee de la
    // section 0 aurait donne 2000, pas 6000.
    CHECK(e1->guide.a == Vec2um{Micrometers{6'000}, Micrometers{0}});
    CHECK(e1->guide.b == Vec2um{Micrometers{6'000}, Micrometers{4'000}});
    CHECK(e0->guide.link_id == std::optional<std::uint32_t>{9});
    CHECK(e1->guide.link_id == std::optional<std::uint32_t>{9});
}

TEST_CASE("le deplacement de groupe est refuse en bloc hors de l'intervalle admissible") {
    auto fx = make_linked_group_fixture();
    // tA cible = 4900/5000 = 0.98, au-dela de la marge d'espacement minimal
    // (density=400 -> minimumGap=200 -> borne haute 1-200/5000=0.96).
    const auto edits =
        move_satin_guide_group(fx.project, fx.source, 9, fx.section0, SatinGuideSide::RailA,
                               {Micrometers{4'900}, Micrometers{0}});
    CHECK_FALSE(edits.has_value());

    // Rien n'a ete mute (tout ou rien) : la geometrie d'origine est intacte.
    const auto& section0 =
        std::get<document::SatinParams>(fx.project.findEmbroidery(fx.section0)->params);
    CHECK(section0.rungs[1].a == Vec2um{Micrometers{1'000}, Micrometers{0}});
}

TEST_CASE("le deplacement de groupe refuse une section glissee hors du groupe") {
    auto fx = make_linked_group_fixture();
    const auto edits =
        move_satin_guide_group(fx.project, fx.source, 9, ObjectId{12345}, SatinGuideSide::RailA,
                               {Micrometers{2'000}, Micrometers{0}});
    CHECK_FALSE(edits.has_value());
}

TEST_CASE("le deplacement de groupe refuse un guide qui n'est plus adjacent a sa jonction") {
    auto fx = make_linked_group_fixture();
    // Un guide independant s'insere entre la jonction et le guide lie de la
    // section 0 : celui-ci n'a plus de voisin de jonction univoque, on refuse
    // plutot que de deviner un intervalle arbitraire.
    auto& section0 =
        std::get<document::SatinParams>(fx.project.findEmbroidery(fx.section0)->params);
    section0.rungs.insert(section0.rungs.begin() + 1,
                          document::SatinRung{{Micrometers{500}, Micrometers{0}},
                                              {Micrometers{500}, Micrometers{4'000}}});
    const auto edits =
        move_satin_guide_group(fx.project, fx.source, 9, fx.section0, SatinGuideSide::RailA,
                               {Micrometers{2'000}, Micrometers{0}});
    CHECK_FALSE(edits.has_value());
}

TEST_CASE("le deplacement de groupe refuse des jonctions liees incoherentes") {
    auto fx = make_linked_group_fixture();
    auto& section1 =
        std::get<document::SatinParams>(fx.project.findEmbroidery(fx.section1)->params);
    section1.topology->start_junction = std::uint32_t{8};
    CHECK_FALSE(move_satin_guide_group(fx.project, fx.source, 9, fx.section0, SatinGuideSide::RailA,
                                       {Micrometers{2'000}, Micrometers{0}})
                    .has_value());
}

TEST_CASE("le deplacement de groupe refuse une progression croisee sur les rails") {
    auto fx = make_linked_group_fixture();
    auto& section1 =
        std::get<document::SatinParams>(fx.project.findEmbroidery(fx.section1)->params);
    section1.rungs[2].b.x = Micrometers{2'000};
    CHECK_FALSE(move_satin_guide_group(fx.project, fx.source, 9, fx.section0, SatinGuideSide::RailA,
                                       {Micrometers{2'000}, Micrometers{0}})
                    .has_value());
}

TEST_CASE("un guide satin sans link_id ne forme jamais de groupe") {
    const auto p = satin(); // aucun link_id, satin() de base sans topologie
    document::Project project;
    document::EmbroideryObject object;
    object.id = project.object_ids.next();
    object.source_vector = ObjectId{1};
    object.params = p;
    project.embroidery_objects.push_back(object);
    CHECK(satin_linked_guides(project, ObjectId{1}, 0).empty());
    CHECK_FALSE(move_satin_guide_group(project, ObjectId{1}, 0, object.id, SatinGuideSide::RailA,
                                       {Micrometers{1'000}, Micrometers{0}})
                    .has_value());
}