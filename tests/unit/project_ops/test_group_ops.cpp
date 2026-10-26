// SPDX-License-Identifier: Apache-2.0
// HP-OBJ-009 / 011 / 015 : operations group, ungroup, stack_order et place.
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/operations.hpp"

namespace {

using namespace openstitch;

// Trois carres de 20 mm, cousus dans l'ordre : broderies 2, 4, 6 (formes 1, 3, 5).
document::Project make_project() {
    document::Project p;
    for (int i = 0; i < 3; ++i) {
        document::VectorObject v;
        v.id = p.object_ids.next();
        geometry::PathSet set;
        for (const auto& [x, y] :
             {std::pair{0, 0}, std::pair{20, 0}, std::pair{20, 20}, std::pair{0, 20}}) {
            geometry::PathNode n;
            n.pos = Vec2um{Micrometers{(x + i * 30) * 1000}, Micrometers{y * 1000}};
            set.outer.nodes.push_back(n);
        }
        set.outer.closed = true;
        v.paths.push_back(set);
        p.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.source_vector = v.id;
        e.params = document::TatamiParams{};
        p.embroidery_objects.push_back(e);
    }
    return p;
}

} // namespace

TEST_CASE("group ops : grouper, empiler et dissoudre", "[project_ops][ops]") {
    auto project = make_project();
    auto r =
        project_ops::apply_operation(project, R"({"op":"group","objects":[2,4],"name":"Duo"})");
    REQUIRE(r.ok);
    REQUIRE(project.groups.size() == 1);
    CHECK(project.groups.front().name == "Duo");
    CHECK(project.findObject(ObjectId{1})->group == project.groups.front().id);
    // L'ordre de couture ne change pas.
    CHECK(project.embroidery_objects.front().id.value == 2);

    r = project_ops::apply_operation(project,
                                     R"({"op":"stack_order","objects":[2],"move":"front"})");
    REQUIRE(r.ok);
    CHECK(project.embroidery_objects.back().id.value == 2);

    const std::string ungroup =
        R"({"op":"ungroup","group":)" + std::to_string(project.groups.front().id.value) + "}";
    r = project_ops::apply_operation(project, ungroup);
    REQUIRE(r.ok);
    CHECK(project.groups.empty());
    CHECK_FALSE(project.findObject(ObjectId{1})->group.valid());
}

TEST_CASE("group ops : un groupe d'un seul element est refuse", "[project_ops][ops]") {
    auto project = make_project();
    const auto r = project_ops::apply_operation(project, R"({"op":"group","objects":[2]})");
    CHECK_FALSE(r.ok);
    CHECK(project.groups.empty());
}

TEST_CASE("group ops : place centre et redimensionne en mm", "[project_ops][ops]") {
    auto project = make_project();
    const auto r = project_ops::apply_operation(
        project, R"({"op":"place","objects":[2],"x":0,"y":0,"width":40})");
    REQUIRE(r.ok);
    const auto& nodes = project.findObject(ObjectId{1})->paths.front().outer.nodes;
    CHECK(nodes[0].pos.x.value == -20'000);
    CHECK(nodes[1].pos.x.value == 20'000);
    CHECK(nodes[0].pos.y.value == -10'000);
    CHECK(nodes[2].pos.y.value == 10'000);
}
