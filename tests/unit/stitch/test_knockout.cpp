// SPDX-License-Identifier: Apache-2.0
// HP-ENG-011 : un remplissage ne coud pas la partie cachee par un remplissage pose apres lui.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "openstitch/document/project.hpp"
#include "openstitch/stitch_generation/generate.hpp"

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

ObjectId add_fill(document::Project& project, geometry::PathSet shape,
                  const document::StitchParams& params) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths = {std::move(shape)};
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = params;
    project.embroidery_objects.push_back(emb);
    return emb.id;
}

// Dessous : 0..20 mm. Dessus (pose apres) : 10..30 mm en x, meme hauteur.
struct Scene {
    document::Project project;
    ObjectId lower;
    ObjectId upper;
};

Scene make_scene(bool knockout) {
    Scene scene;
    document::TatamiParams tatami;
    scene.lower = add_fill(scene.project, rect(0, 0, 20'000, 10'000), tatami);
    scene.upper = add_fill(scene.project, rect(10'000, 0, 30'000, 10'000), tatami);
    scene.project.findEmbroidery(scene.lower)->knockout = knockout;
    return scene;
}

std::size_t count_stitches(const stitch::StitchSequence& seq, ObjectId id) {
    return static_cast<std::size_t>(std::count_if(
        seq.commands.begin(), seq.commands.end(), [id](const stitch::StitchCommand& c) {
            return c.source == id && c.type == stitch::CommandType::Stitch;
        }));
}

} // namespace

TEST_CASE("knockout : le dessous ne coud pas sous le dessus, avec marge de recouvrement") {
    const Scene off = make_scene(false);
    const Scene on = make_scene(true);
    const auto seqOff = generate_sequence(off.project);
    const auto seqOn = generate_sequence(on.project);
    REQUIRE(seqOff.has_value());
    REQUIRE(seqOn.has_value());

    CHECK(count_stitches(*seqOn, on.lower) < count_stitches(*seqOff, off.lower));
    // Le dessus n'est jamais modifie.
    CHECK(count_stitches(*seqOn, on.upper) == count_stitches(*seqOff, off.upper));

    // Marge 0,3 mm : plus aucun point du dessous bien a l'interieur du dessus (x >= 10,6 mm).
    for (const auto& c : seqOn->commands) {
        if (c.source == on.lower && c.type == stitch::CommandType::Stitch) {
            CHECK(c.pos.x.value <= 10'600);
        }
    }
    // Sans l'option, le dessous couvre toute sa forme.
    const bool coversRight = std::any_of(seqOff->commands.begin(), seqOff->commands.end(),
                                         [&](const stitch::StitchCommand& c) {
                                             return c.source == off.lower && c.pos.x.value > 15'000;
                                         });
    CHECK(coversRight);
}

TEST_CASE("knockout : deterministe et sans effet si rien ne recouvre") {
    const Scene a = make_scene(true);
    const Scene b = make_scene(true);
    CHECK(generate_sequence(a.project)->commands == generate_sequence(b.project)->commands);

    // Le dessus seul (rien apres lui) : l'option ne change rien.
    Scene top = make_scene(false);
    const auto before = generate_sequence(top.project);
    top.project.findEmbroidery(top.upper)->knockout = true;
    const auto after = generate_sequence(top.project);
    REQUIRE(before.has_value());
    REQUIRE(after.has_value());
    CHECK(before->commands == after->commands);
}

TEST_CASE("knockout : un dessous entierement cache ne produit aucun point") {
    Scene scene;
    document::TatamiParams tatami;
    scene.lower = add_fill(scene.project, rect(5'000, 2'000, 10'000, 8'000), tatami);
    scene.upper = add_fill(scene.project, rect(0, 0, 20'000, 10'000), tatami);
    scene.project.findEmbroidery(scene.lower)->knockout = true;
    const auto seq = generate_sequence(scene.project);
    REQUIRE(seq.has_value());
    CHECK(count_stitches(*seq, scene.lower) == 0);
    CHECK(count_stitches(*seq, scene.upper) > 0);
}
