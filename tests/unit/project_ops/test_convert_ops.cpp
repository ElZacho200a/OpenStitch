// SPDX-License-Identifier: Apache-2.0
// HP-AUTO-012 : operation convert_vectors.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/operations.hpp"

namespace {

using namespace openstitch;

document::VectorObject square(ObjectId id, int size_um) {
    document::VectorObject v;
    v.id = id;
    geometry::Path p;
    p.closed = true;
    auto n = [](int x, int y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    p.nodes = {n(0, 0), n(size_um, 0), n(size_um, size_um), n(0, size_um)};
    v.paths.push_back(geometry::PathSet{std::move(p), {}});
    return v;
}

} // namespace

TEST_CASE("convert_vectors : numerise les formes sans broderie", "[project_ops][ops]") {
    document::Project project;
    project.vector_objects.push_back(square(project.object_ids.next(), 30'000));
    project.vector_objects.push_back(square(project.object_ids.next(), 40'000));
    const auto r = project_ops::apply_operation(project, R"({"op":"convert_vectors"})");
    REQUIRE(r.ok);
    CHECK(project.embroidery_objects.size() == 2);
    CHECK(project.vector_objects.size() == 2);

    // Deuxieme appel : plus rien a convertir, refus nomme, document intact.
    const auto again = project_ops::apply_operation(project, R"({"op":"convert_vectors"})");
    CHECK_FALSE(again.ok);
    CHECK(project.embroidery_objects.size() == 2);
}
