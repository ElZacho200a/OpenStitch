// SPDX-License-Identifier: Apache-2.0
//
// Tirage de NIVEAU 3 (`PullMode::Predicted`) : passe finale de `effective_sequence`.
// Valide la GEOMETRIE et les invariants (determinisme, absence d'effet hors mode, influence des
// voisins), jamais une efficacite textile reelle.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/stitch_generation/predicted_pull.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::PathNode node(std::int32_t x, std::int32_t y) {
    return geometry::PathNode{um(x, y), geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

geometry::PathSet rect(std::int32_t x0, std::int32_t w, std::int32_t h) {
    geometry::Path p;
    p.closed = true;
    p.nodes = {node(x0, 0), node(x0 + w, 0), node(x0 + w, h), node(x0, h)};
    return geometry::PathSet{p, {}};
}

void add_tatami(document::Project& project, std::int32_t x0, document::PullMode mode) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(rect(x0, 20'000, 10'000));
    project.vector_objects.push_back(vec);
    document::TatamiParams params;
    params.inset = Micrometers{0};
    params.pull_mode = mode;
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = params;
    project.embroidery_objects.push_back(emb);
}

stitch::StitchSequence effective(const document::Project& project) {
    auto seq = effective_sequence(project);
    REQUIRE(seq.has_value());
    return std::move(*seq);
}

std::int32_t max_x(const stitch::StitchSequence& s, ObjectId who) {
    std::int32_t hi = -1'000'000;
    for (const auto& c : s.commands) {
        if (c.type == stitch::CommandType::Stitch && c.source == who &&
            c.pass == stitch::StitchPass::TopStitch) {
            hi = std::max(hi, c.pos.x.value);
        }
    }
    return hi;
}

} // namespace

TEST_CASE("predicted_pull: sans objet predit la sequence est inchangee", "[predicted_pull]") {
    document::Project project;
    add_tatami(project, 0, document::PullMode::Manual);
    add_tatami(project, 22'000, document::PullMode::Auto);
    const auto before = effective(project);
    auto after = before;
    apply_predicted_pull(after, project);
    CHECK(before.commands == after.commands);
}

TEST_CASE("predicted_pull: les bouts de rangee sont repousses vers l'exterieur",
          "[predicted_pull]") {
    document::Project manual;
    add_tatami(manual, 0, document::PullMode::Manual);
    document::Project predicted;
    add_tatami(predicted, 0, document::PullMode::Predicted);

    const auto a = effective(manual);
    const auto b = effective(predicted);
    const ObjectId id = manual.embroidery_objects.front().id;
    // Le tissu se contracte vers le centre : la compensation pousse les bouts vers l'exterieur.
    CHECK(max_x(b, id) > max_x(a, id));
    // Mais de peu : plafond de deplacement x confiance.
    CHECK(max_x(b, id) - max_x(a, id) <= 800);
}

TEST_CASE("predicted_pull: determinisme octet pour octet", "[predicted_pull]") {
    document::Project project;
    add_tatami(project, 0, document::PullMode::Predicted);
    add_tatami(project, 21'000, document::PullMode::Predicted);
    CHECK(effective(project).commands == effective(project).commands);
}

TEST_CASE("predicted_pull: un objet voisin modifie la compensation", "[predicted_pull]") {
    document::Project alone;
    add_tatami(alone, 0, document::PullMode::Predicted);
    document::Project crowded;
    add_tatami(crowded, 0, document::PullMode::Predicted);
    add_tatami(crowded, 20'800, document::PullMode::Manual); // voisin presque jointoyant
    const ObjectId id = alone.embroidery_objects.front().id;
    // Le voisin tire aussi le tissu : l'objet predit n'est pas compense comme s'il etait seul.
    CHECK(max_x(effective(alone), id) != max_x(effective(crowded), id));
}

TEST_CASE("predicted_pull: seuls les points de l'objet predit bougent", "[predicted_pull]") {
    document::Project manual;
    add_tatami(manual, 0, document::PullMode::Predicted);
    add_tatami(manual, 30'000, document::PullMode::Manual);
    auto seq = effective(manual);
    auto copy = seq;
    // Rejouer la passe sur une sequence deja compensee n'est pas idempotent par conception ;
    // on verifie surtout que l'objet manuel n'est pas touche par la passe.
    apply_predicted_pull(copy, manual);
    const ObjectId manualId = manual.embroidery_objects.back().id;
    for (std::size_t i = 0; i < seq.commands.size(); ++i) {
        if (seq.commands[i].source == manualId) {
            CHECK(seq.commands[i].pos == copy.commands[i].pos);
        }
    }
}
