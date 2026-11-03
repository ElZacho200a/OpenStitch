// SPDX-License-Identifier: Apache-2.0
//
// Tirage de NIVEAU 3 (`PullMode::Predicted`) : passe finale de `effective_sequence`.
// Valide la GEOMETRIE et les invariants (determinisme, absence d'effet hors mode, influence des
// voisins), jamais une efficacite textile reelle.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>
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

void add_contour(document::Project& project, std::int32_t x0) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(rect(x0, 20'000, 10'000));
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::ContourFillParams{};
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

TEST_CASE("predicted_pull: conserve un socle adaptatif en tatami", "[predicted_pull]") {
    document::Project manual;
    document::Project adaptive;
    document::Project predicted;
    add_tatami(manual, 0, document::PullMode::Manual);
    add_tatami(adaptive, 0, document::PullMode::Adaptive);
    add_tatami(predicted, 0, document::PullMode::Predicted);

    const auto m = effective(manual);
    const auto a = effective(adaptive);
    const auto p = effective(predicted);
    const ObjectId id = predicted.embroidery_objects.front().id;
    const auto extension = max_x(a, id) - max_x(m, id);
    REQUIRE(extension > 0);
    // Le champ ajoute une correction ; il ne doit plus repartir d'une
    // géométrie sans compensation, comme c'était le cas avant ce correctif.
    CHECK(max_x(p, id) >= max_x(a, id));
    CHECK(max_x(p, id) - max_x(a, id) <= 800);
}

TEST_CASE("predicted_pull: tissu et entoilage modulent un profil non calibre",
          "[predicted_pull][fabric]") {
    document::Project project;
    const auto initial = predicted_fabric_response(project);
    CHECK(initial.gain == FabricResponse{}.gain);

    project.fabric.material = "jersey";
    const auto jersey = predicted_fabric_response(project);
    CHECK(std::abs(jersey.gain - initial.gain * 1.20) < 1e-12);
    CHECK_FALSE(jersey.calibrated); // ne jamais prétendre être un profil mesuré

    project.fabric.stabilizer = "cut-away";
    const auto cutAway = predicted_fabric_response(project);
    CHECK(std::abs(cutAway.gain - jersey.gain * 0.80) < 1e-12);

    project.fabric.material = "felt";
    CHECK(predicted_fabric_response(project).gain < cutAway.gain);
    project.fabric.material = "unknown-custom-textile";
    project.fabric.stabilizer = "unknown-stabilizer";
    CHECK(predicted_fabric_response(project).gain == initial.gain);

    FabricResponse measured;
    measured.gain = 0.067;
    measured.calibrated = true;
    project.fabric.material = "jersey";
    project.fabric.stabilizer = "cut-away";
    CHECK(predicted_fabric_response(project, measured).gain == measured.gain);
}

TEST_CASE("predicted_pull: la matiere du projet affecte uniquement les objets Predicted",
          "[predicted_pull][fabric]") {
    document::Project project;
    add_tatami(project, 0, document::PullMode::Predicted);
    auto jersey = project;
    jersey.fabric.material = "jersey";
    jersey.fabric.stabilizer = "none";
    CHECK(effective(project).commands != effective(jersey).commands);

    // Un changement de matière ne modifie pas le comportement d'un projet
    // dépourvu d'objets Predicted (compatibilité des anciens .osp).
    project.embroidery_objects.front().params = document::TatamiParams{};
    jersey.embroidery_objects.front().params = document::TatamiParams{};
    CHECK(effective(project).commands == effective(jersey).commands);
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

TEST_CASE("predicted_pull: refresh_context identique a la sequence exportee", "[predicted_pull]") {
    // Le desktop utilise refresh_context ; l'export passe par effective_sequence.
    // En mode predit, les deux doivent appliquer la meme passe avant les finitions.
    for (const auto mode : {document::PullMode::Manual, document::PullMode::Auto,
                            document::PullMode::Adaptive, document::PullMode::Predicted}) {
        document::Project project;
        add_tatami(project, 0, mode);
        add_tatami(project, 20'800, document::PullMode::Manual);
        const auto expected = effective(project);
        const ObjectId id = project.embroidery_objects.front().id;

        const auto desktop = refresh_context(project, id);
        REQUIRE(desktop.has_value());
        CHECK(desktop->effective.commands == expected.commands);
        REQUIRE(desktop->target_view.has_value());

        const auto withoutTarget = refresh_context(project, std::nullopt);
        REQUIRE(withoutTarget.has_value());
        CHECK(withoutTarget->effective.commands == expected.commands);
    }
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

TEST_CASE("predicted_pull: un remplissage sans reglage de tirage suit la prediction du projet",
          "[predicted_pull]") {
    // Sans objet predit, le contour (pas de pull_mode) n'est jamais deplace.
    document::Project plain;
    add_tatami(plain, 0, document::PullMode::Manual);
    add_contour(plain, 22'000);
    // Avec un tatami predit, le contour tire le meme tissu : il est compense lui aussi.
    document::Project predicted;
    add_tatami(predicted, 0, document::PullMode::Predicted);
    add_contour(predicted, 22'000);

    const ObjectId contourPlain = plain.embroidery_objects.back().id;
    const ObjectId contourPredicted = predicted.embroidery_objects.back().id;
    const auto positions = [](const stitch::StitchSequence& s, ObjectId who) {
        std::vector<Vec2um> out;
        for (const auto& c : s.commands) {
            if (c.type == stitch::CommandType::Stitch && c.source == who &&
                c.pass == stitch::StitchPass::TopStitch) {
                out.push_back(c.pos);
            }
        }
        return out;
    };
    const auto pa = positions(effective(plain), contourPlain);
    const auto pb = positions(effective(predicted), contourPredicted);
    REQUIRE_FALSE(pa.empty());
    REQUIRE(pa.size() == pb.size());
    CHECK(pa != pb);
}

TEST_CASE("predicted_pull: les passes de compensation convergent", "[predicted_pull]") {
    document::Project project;
    add_tatami(project, 0, document::PullMode::Predicted);
    add_tatami(project, 22'000, document::PullMode::Predicted);
    const auto run = [&](int passes) {
        FabricResponse fabric;
        fabric.compensation_passes = passes;
        // Sequence nominale : meme geometrie sans la passe predite (socle adaptatif).
        document::Project nominal = project;
        for (auto& o : nominal.embroidery_objects) {
            std::visit(
                [](auto& p) {
                    if constexpr (requires { p.pull_mode; }) {
                        p.pull_mode = document::PullMode::Adaptive;
                    }
                },
                o.params);
        }
        auto seq = effective(nominal);
        apply_predicted_pull(seq, project, fabric);
        return seq;
    };
    const auto gap = [](const stitch::StitchSequence& a, const stitch::StitchSequence& b) {
        double sum = 0.0;
        for (std::size_t i = 0; i < a.commands.size(); ++i) {
            sum += std::hypot(
                static_cast<double>(a.commands[i].pos.x.value - b.commands[i].pos.x.value),
                static_cast<double>(a.commands[i].pos.y.value - b.commands[i].pos.y.value));
        }
        return sum;
    };
    const auto p1 = run(1);
    const auto p2 = run(2);
    const auto p3 = run(3);
    const auto p4 = run(4);
    REQUIRE(p1.commands.size() == p4.commands.size());
    // Chaque passe corrige moins que la precedente : le point fixe est contractant.
    CHECK(gap(p1, p2) > 0.0);
    CHECK(gap(p3, p4) < gap(p1, p2));
    // Deterministe.
    CHECK(run(3).commands == p3.commands);
}
