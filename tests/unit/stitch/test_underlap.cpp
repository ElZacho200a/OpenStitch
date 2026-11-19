// SPDX-License-Identifier: Apache-2.0
// HiddenTravel::underlap : le debord oriente passe UNIQUEMENT sous les remplissages poses apres.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um pt(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::PathSet rect(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    geometry::PathSet set;
    set.outer.closed = true;
    for (const Vec2um p : {pt(x0, y0), pt(x1, y0), pt(x1, y1), pt(x0, y1)}) {
        set.outer.nodes.push_back(geometry::PathNode{p, geometry::NodeType::Corner, {}, {}});
    }
    return set;
}

ObjectId add_fill(document::Project& project, geometry::PathSet shape) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths = {std::move(shape)};
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::TatamiParams{};
    project.embroidery_objects.push_back(emb);
    return emb.id;
}

struct Scene {
    document::Project project;
    ObjectId first;  // 0..10 mm, cousu d'abord
    ObjectId second; // 10..20 mm, cousu apres
};

Scene make_scene(std::int32_t underlapUm) {
    Scene s;
    s.first = add_fill(s.project, rect(0, 0, 10'000, 10'000));
    s.second = add_fill(s.project, rect(10'000, 0, 20'000, 10'000));
    s.project.finishing.hidden_travel.enabled = underlapUm > 0;
    s.project.finishing.hidden_travel.underlap = Micrometers{underlapUm};
    return s;
}

std::int32_t max_x(const stitch::StitchSequence& seq, ObjectId id) {
    std::int32_t m = std::numeric_limits<std::int32_t>::min();
    for (const auto& c : seq.commands) {
        if (c.source == id && c.type == stitch::CommandType::Stitch) {
            m = std::max(m, c.pos.x.value);
        }
    }
    return m;
}

std::int32_t min_x(const stitch::StitchSequence& seq, ObjectId id) {
    std::int32_t m = std::numeric_limits<std::int32_t>::max();
    for (const auto& c : seq.commands) {
        if (c.source == id && c.type == stitch::CommandType::Stitch) {
            m = std::min(m, c.pos.x.value);
        }
    }
    return m;
}

} // namespace

TEST_CASE("underlap : la zone cousue d'abord deborde sous la suivante, jamais l'inverse") {
    const Scene off = make_scene(0);
    const Scene on = make_scene(600);
    const auto seqOff = generate_sequence(off.project);
    const auto seqOn = generate_sequence(on.project);
    REQUIRE(seqOff.has_value());
    REQUIRE(seqOn.has_value());

    // Sans debord, la premiere zone s'arrete avant la frontiere (retrait du bord).
    CHECK(max_x(*seqOff, off.first) <= 10'000);
    // Avec debord, elle passe franchement sous la seconde...
    CHECK(max_x(*seqOn, on.first) >= 10'300);
    // ... mais pas au-dela du debord demande.
    CHECK(max_x(*seqOn, on.first) <= 10'700);
    // La seconde (posee apres) ne deborde jamais sur la premiere : meme bord gauche qu'avant.
    CHECK(min_x(*seqOn, on.second) == min_x(*seqOff, off.second));
}

TEST_CASE("underlap : sans effet quand les trajets optimises sont desactives") {
    Scene a = make_scene(600);
    a.project.finishing.hidden_travel.enabled = false;
    const Scene off = make_scene(0);
    CHECK(generate_sequence(a.project)->commands == generate_sequence(off.project)->commands);

    Scene b = make_scene(600);
    b.project.finishing.hidden_travel.between_objects = false;
    CHECK(generate_sequence(b.project)->commands == generate_sequence(off.project)->commands);
}

TEST_CASE("underlap : l'export impose son debord, deterministe") {
    Scene scene = make_scene(0);
    document::HiddenTravel travel;
    travel.underlap = Micrometers{600};
    const auto a = effective_sequence_report(scene.project, &travel);
    const auto b = effective_sequence_report(scene.project, &travel);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->sequence.commands == b->sequence.commands);
    CHECK(max_x(a->sequence, scene.first) >= 10'300);
    travel.underlap = Micrometers{0};
    const auto c = effective_sequence_report(scene.project, &travel);
    REQUIRE(c.has_value());
    CHECK(max_x(c->sequence, scene.first) <= 10'000);
}
