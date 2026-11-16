// SPDX-License-Identifier: Apache-2.0
// Couchage d'un tatami (STCH-CROS-01, point de Bayeux) : fils perpendiculaires au fil posé,
// attachés à chaque pénétration.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "openstitch/stitch_generation/couching.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

geometry::PathNode node(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}}, geometry::NodeType::Corner,
                              std::nullopt, std::nullopt};
}

geometry::PathSet square(std::int32_t side) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {node(0, 0), node(side, 0), node(side, side), node(0, side)};
    return geometry::PathSet{p, {}};
}

// Attache = q -> q + h·x -> q - h·x -> q pour un fil posé horizontal (angle 0).
int count_tacks(const std::vector<FillStitch>& s) {
    int tacks = 0;
    for (std::size_t i = 3; i < s.size(); ++i) {
        if (s[i - 2].jump || s[i - 1].jump || s[i].jump) {
            continue;
        }
        const auto& q = s[i - 3].pos;
        if (s[i - 2].pos.y == q.y && s[i - 1].pos.y == q.y && s[i].pos == q &&
            s[i - 2].pos.x.value - q.x.value == 600 && q.x.value - s[i - 1].pos.x.value == 600) {
            ++tacks;
        }
    }
    return tacks;
}

} // namespace

TEST_CASE("couching: perpendicular couched threads tied at every penetration") {
    document::TatamiParams p;
    CHECK(couching_pass(square(20'000), p).empty()); // 0 = no couching (legacy)
    p.couching_spacing = Micrometers{5'000};
    p.couching_tie_spacing = Micrometers{3'000};
    const auto pass = couching_pass(square(20'000), p);
    REQUIRE(!pass.empty());
    CHECK(couching_pass(square(20'000), p) == pass); // deterministic
    // 4 vertical couched threads, ~7 penetrations each.
    CHECK(count_tacks(pass) >= 20);
    for (const auto& f : pass) {
        CHECK(f.pos.x.value >= 0);
        CHECK(f.pos.x.value <= 20'000);
        CHECK(f.pos.y.value >= 0);
        CHECK(f.pos.y.value <= 20'000);
    }
}

TEST_CASE("couching: the tatami object sews its couching pass after the laid fill") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(square(20'000));
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    document::TatamiParams laid;
    laid.stitch_length = Micrometers{7'000};
    emb.params = laid;
    project.embroidery_objects.push_back(emb);
    const auto plain = generate_sequence(project);
    laid.couching_spacing = Micrometers{5'000};
    project.embroidery_objects.front().params = laid;
    const auto couched = generate_sequence(project);
    REQUIRE(plain.has_value());
    REQUIRE(couched.has_value());
    CHECK(couched->commands.size() > plain->commands.size() + 60);
    const auto again = generate_sequence(project);
    REQUIRE(again.has_value());
    CHECK(again->commands == couched->commands);
}
