// SPDX-License-Identifier: Apache-2.0
//
// HP-OBJ-024 : le rang du calque est la clé principale de l'ordre de couture ; un calque masqué
// n'est pas cousu ; un document sans calque est inchangé.
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::Path square_at(std::int32_t x) {
    geometry::Path square;
    square.closed = true;
    const std::int32_t s = 10'000;
    square.nodes = {
        {Vec2um{Micrometers{x}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{x + s}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{x + s}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
        {Vec2um{Micrometers{x}, Micrometers{s}}, geometry::NodeType::Corner, {}, {}},
    };
    return square;
}

struct Fixture {
    document::Project project;
    ObjectId shapeA;
    ObjectId shapeB;
    ObjectId embA;
    ObjectId embB;
    ObjectId calque1;
    ObjectId calque2;
};

// Deux formes cousues dans l'ordre du document : A puis B (couleurs différentes).
Fixture make_fixture() {
    Fixture f;
    auto& p = f.project;
    for (int i = 0; i < 2; ++i) {
        document::VectorObject v;
        v.id = p.object_ids.next();
        v.paths.push_back(geometry::PathSet{square_at(i * 30'000), {}});
        (i == 0 ? f.shapeA : f.shapeB) = v.id;
        p.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.source_vector = v.id;
        e.rgb = {static_cast<std::uint8_t>(i == 0 ? 200 : 20), 30, 30};
        (i == 0 ? f.embA : f.embB) = e.id;
        p.embroidery_objects.push_back(e);
    }
    f.calque1 = p.object_ids.next();
    f.calque2 = p.object_ids.next();
    p.calques = {{f.calque1, "Principal", true, false}, {f.calque2, "Effets", true, false}};
    return f;
}

// Objets dans l'ordre où ils apparaissent dans la séquence (sans doublon consécutif).
std::vector<ObjectId> sewn_order(const stitch::StitchSequence& seq) {
    std::vector<ObjectId> order;
    for (const auto& c : seq.commands) {
        if (c.type == stitch::CommandType::Stitch && c.source.valid() &&
            (order.empty() || order.back() != c.source)) {
            order.push_back(c.source);
        }
    }
    return order;
}

} // namespace

TEST_CASE("calques : le calque du dessus est cousu apres, quel que soit l'ordre du document") {
    auto f = make_fixture();
    // A (premier du document) passe dans le calque du dessus : B (calque du fond) est cousu avant.
    f.project.findObject(f.shapeA)->calque = f.calque2;
    f.project.findObject(f.shapeB)->calque = f.calque1;
    const auto seq = generate_sequence(f.project);
    REQUIRE(seq.has_value());
    const auto order = sewn_order(*seq);
    REQUIRE(order.size() == 2);
    CHECK(order[0] == f.embB);
    CHECK(order[1] == f.embA);
}

TEST_CASE("calques : sans calque (ou a rang egal) l'ordre du document est conserve") {
    auto f = make_fixture();
    f.project.calques.clear();
    const auto alone = generate_sequence(f.project);
    REQUIRE(alone.has_value());
    const auto aloneOrder = sewn_order(*alone);
    REQUIRE(aloneOrder.size() == 2);
    CHECK(aloneOrder[0] == f.embA);
    CHECK(aloneOrder[1] == f.embB);

    auto g = make_fixture(); // deux calques, mais les deux formes dans le même
    const auto same = generate_sequence(g.project);
    REQUIRE(same.has_value());
    CHECK(sewn_order(*same) == aloneOrder);
}

TEST_CASE("calques : un calque masque n'est pas couse") {
    auto f = make_fixture();
    f.project.findObject(f.shapeB)->calque = f.calque2;
    f.project.calques[1].visible = false;
    const auto seq = generate_sequence(f.project);
    REQUIRE(seq.has_value());
    const auto order = sewn_order(*seq);
    REQUIRE(order.size() == 1);
    CHECK(order[0] == f.embA);
}

TEST_CASE("calques : sewingOrder trie de facon stable par rang de calque") {
    auto f = make_fixture();
    f.project.findObject(f.shapeA)->calque = f.calque2;
    const auto order = f.project.sewingOrder();
    REQUIRE(order.size() == 2);
    CHECK(order[0] == 1); // B (calque du fond) puis A
    CHECK(order[1] == 0);
    const auto grouped = f.project.groupedByCalque({f.embA, f.embB});
    CHECK(grouped == std::vector<ObjectId>{f.embB, f.embA});
}
