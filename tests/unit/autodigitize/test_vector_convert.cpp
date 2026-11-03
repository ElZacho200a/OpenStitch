// SPDX-License-Identifier: Apache-2.0
// HP-AUTO-012 : conversion des formes vectorielles sans broderie.
#include <catch2/catch_test_macros.hpp>

#include "openstitch/autodigitize/vector_convert.hpp"

namespace {

using namespace openstitch;
using namespace openstitch::autodigitize;

document::VectorObject rect(ObjectId id, int x0, int y0, int x1, int y1) {
    document::VectorObject v;
    v.id = id;
    v.name = "Forme";
    geometry::Path p;
    p.closed = true;
    auto n = [](int x, int y) {
        return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                  geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    p.nodes = {n(x0, y0), n(x1, y0), n(x1, y1), n(x0, y1)};
    v.paths.push_back(geometry::PathSet{std::move(p), {}});
    return v;
}

AutoOptions options() {
    AutoOptions o;
    o.mm_per_px = Millimeters{1.0};
    o.min_fill_area_mm2 = 20.0;
    return o;
}

} // namespace

TEST_CASE("vector_convert : grande forme -> tatami, petite -> contour, deja cousue ignoree") {
    document::Project p;
    p.vector_objects.push_back(rect(p.object_ids.next(), 0, 0, 30'000, 30'000));
    p.vector_objects.push_back(rect(p.object_ids.next(), 0, 0, 1'000, 1'000));
    p.vector_objects.push_back(rect(p.object_ids.next(), 0, 0, 30'000, 30'000));
    document::EmbroideryObject done;
    done.id = p.object_ids.next();
    done.source_vector = p.vector_objects[2].id;
    p.embroidery_objects.push_back(done);

    auto ids = p.object_ids;
    const auto r = convert_vectors_to_embroidery(p, ids, options());
    REQUIRE(r.has_value());
    CHECK(r->considered == 2);
    CHECK(r->already_embroidered == 1);
    CHECK(r->embroideries.size() == 2);
    CHECK(r->tatami == 1);
    CHECK(r->contour == 1);
    CHECK(r->embroideries[0].source_vector == p.vector_objects[0].id);
}

TEST_CASE("vector_convert : rien a convertir -> erreur propre") {
    document::Project p;
    auto ids = p.object_ids;
    CHECK_FALSE(convert_vectors_to_embroidery(p, ids, options()).has_value());
}
