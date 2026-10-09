// SPDX-License-Identifier: Apache-2.0
// Intégration de l'auto-satin par squelette dans la génération de points :
// document::AutoSatinParams -> stitch_generation::generate_sequence.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "openstitch/auto_satin/shapes.hpp"
#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;
namespace as = openstitch::auto_satin;
namespace sg = openstitch::stitch_generation;

namespace {

document::Project project_with(const std::string& shape, const document::AutoSatinParams& params) {
    document::Project project;
    const auto region = as::make_shape(shape);
    REQUIRE(region.has_value());
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(*region);
    project.vector_objects.push_back(vec);

    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.name = "auto-satin " + shape;
    emb.source_vector = vec.id;
    emb.rgb = {20, 40, 200};
    emb.params = params;
    project.embroidery_objects.push_back(emb);
    return project;
}

std::vector<Vec2um> top_stitches(const stitch::StitchSequence& seq) {
    std::vector<Vec2um> out;
    for (const auto& c : seq.commands) {
        if (c.type == stitch::CommandType::Stitch && c.pass == stitch::StitchPass::TopStitch) {
            out.push_back(c.pos);
        }
    }
    return out;
}

double len(Vec2um a, Vec2um b) {
    return std::hypot(static_cast<double>(a.x.value - b.x.value),
                      static_cast<double>(a.y.value - b.y.value));
}

} // namespace

TEST_CASE("generation: un auto-satin produit sous-couche et points de dessus", "[skeleton_satin]") {
    const auto project = project_with("capsule", {});
    const auto seq = sg::generate_sequence(project);
    REQUIRE(seq.has_value());
    int underlay = 0;
    int top = 0;
    for (const auto& c : seq->commands) {
        if (c.type != stitch::CommandType::Stitch) {
            continue;
        }
        underlay += c.pass == stitch::StitchPass::Underlay ? 1 : 0;
        top += c.pass == stitch::StitchPass::TopStitch ? 1 : 0;
    }
    CHECK(underlay > 5); // sous-couche centrale activée par défaut
    CHECK(top > 100);
    CHECK(seq->commands.back().type == stitch::CommandType::End);
}

TEST_CASE("generation: deterministe, deux executions identiques", "[skeleton_satin]") {
    for (const char* shape : {"capsule", "t", "ring", "s"}) {
        INFO(shape);
        const auto project = project_with(shape, {});
        const auto a = sg::generate_sequence(project);
        const auto b = sg::generate_sequence(project);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE(a->commands.size() == b->commands.size());
        for (std::size_t i = 0; i < a->commands.size(); ++i) {
            CHECK(a->commands[i].pos == b->commands[i].pos);
            CHECK(a->commands[i].type == b->commands[i].type);
        }
    }
}

TEST_CASE("generation: Lmax et y sont distincts et bornent la longueur des points",
          "[skeleton_satin]") {
    // 'wide' : ruban de 100 x 20 mm, donc des traversées de 20 mm.
    document::AutoSatinParams params;
    params.split_stitch = document::SatinSplit::Simple;
    params.split_threshold = Micrometers{7'000};
    params.split_length = Micrometers{4'000};
    params.center_underlay = false;
    const auto seq = sg::generate_sequence(project_with("wide", params));
    REQUIRE(seq.has_value());
    const auto pts = top_stitches(*seq);
    REQUIRE(pts.size() > 100);
    double longest = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        longest = std::max(longest, len(pts[i - 1], pts[i]));
    }
    // 20 mm / y = 4 mm -> 5 segments de ~4 mm, sur les traversées ET les retours.
    CHECK(longest <= 4'000.0 * 1.05);

    // Sans fractionnement : des points de pleine largeur (20 mm).
    params.split_stitch = document::SatinSplit::Disabled;
    const auto raw = sg::generate_sequence(project_with("wide", params));
    REQUIRE(raw.has_value());
    const auto rawPts = top_stitches(*raw);
    double rawLongest = 0.0;
    for (std::size_t i = 1; i < rawPts.size(); ++i) {
        rawLongest = std::max(rawLongest, len(rawPts[i - 1], rawPts[i]));
    }
    CHECK(rawLongest > 15'000.0);
}

TEST_CASE("generation: le seuil Lmax ne fractionne pas les traversees plus courtes",
          "[skeleton_satin]") {
    // 'capsule' : traversées de moins de 7 mm -> aucune subdivision, même avec y petit.
    document::AutoSatinParams a;
    a.split_stitch = document::SatinSplit::Simple;
    a.split_threshold = Micrometers{20'000};
    a.split_length = Micrometers{1'000};
    a.center_underlay = false;
    document::AutoSatinParams b = a;
    b.split_stitch = document::SatinSplit::Disabled;
    const auto withSplit = sg::generate_sequence(project_with("capsule", a));
    const auto without = sg::generate_sequence(project_with("capsule", b));
    REQUIRE(withSplit.has_value());
    REQUIRE(without.has_value());
    CHECK(withSplit->commands.size() == without->commands.size());
}

TEST_CASE("generation: un reseau en T couvre ses trois branches avec des sauts entre colonnes",
          "[skeleton_satin]") {
    document::AutoSatinParams params;
    params.center_underlay = false;
    const auto seq = sg::generate_sequence(project_with("t", params));
    REQUIRE(seq.has_value());
    int jumps = 0;
    for (const auto& c : seq->commands) {
        jumps += c.type == stitch::CommandType::Jump ? 1 : 0;
    }
    CHECK(jumps >= 3); // une entrée par colonne (3 branches), au moins
    // Tous les points restent dans la boîte englobante de la forme (à la tolérance près).
    const auto region = as::make_shape("t");
    std::int32_t minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
    for (const auto& n : region->outer.nodes) {
        minx = std::min(minx, n.pos.x.value);
        maxx = std::max(maxx, n.pos.x.value);
        miny = std::min(miny, n.pos.y.value);
        maxy = std::max(maxy, n.pos.y.value);
    }
    for (const auto& c : seq->commands) {
        if (c.type != stitch::CommandType::Stitch) {
            continue;
        }
        CHECK(c.pos.x.value >= minx - 10);
        CHECK(c.pos.x.value <= maxx + 10);
        CHECK(c.pos.y.value >= miny - 10);
        CHECK(c.pos.y.value <= maxy + 10);
    }
}

TEST_CASE("generation: le point d'entree choisit le sens de couture", "[skeleton_satin]") {
    const auto region = as::make_shape("rectangle");
    REQUIRE(region.has_value());
    std::int32_t minx = INT32_MAX, maxx = INT32_MIN;
    for (const auto& n : region->outer.nodes) {
        minx = std::min(minx, n.pos.x.value);
        maxx = std::max(maxx, n.pos.x.value);
    }
    document::AutoSatinParams params;
    params.center_underlay = false;

    params.entry_point = Vec2um{Micrometers{minx}, Micrometers{0}};
    const auto leftStart = top_stitches(*sg::generate_sequence(project_with("rectangle", params)));
    params.entry_point = Vec2um{Micrometers{maxx}, Micrometers{0}};
    const auto rightStart = top_stitches(*sg::generate_sequence(project_with("rectangle", params)));
    REQUIRE_FALSE(leftStart.empty());
    REQUIRE_FALSE(rightStart.empty());
    CHECK(leftStart.front().x.value < rightStart.front().x.value);
}

TEST_CASE("generation: un guide absolu incline les traversees", "[skeleton_satin]") {
    const auto region = as::make_shape("rectangle");
    std::int64_t sx = 0, sy = 0;
    for (const auto& n : region->outer.nodes) {
        sx += n.pos.x.value;
        sy += n.pos.y.value;
    }
    const auto count = static_cast<std::int64_t>(region->outer.nodes.size());
    document::AutoSatinParams params;
    params.center_underlay = false;
    params.split_stitch = document::SatinSplit::Disabled;
    params.guides.push_back({Vec2um{Micrometers{static_cast<std::int32_t>(sx / count)},
                                    Micrometers{static_cast<std::int32_t>(sy / count)}},
                             Angle{0.5}, false});
    const auto guided = top_stitches(*sg::generate_sequence(project_with("rectangle", params)));
    params.guides.clear();
    const auto plain = top_stitches(*sg::generate_sequence(project_with("rectangle", params)));
    REQUIRE(guided.size() > 10);
    REQUIRE(plain.size() > 10);
    // Sans guide : traversées verticales (même x aux deux extrémités). Avec guide : non.
    const auto slant = [](const std::vector<Vec2um>& pts) {
        return std::abs(pts[1].x.value - pts[0].x.value);
    };
    CHECK(slant(plain) < 5);
    CHECK(slant(guided) > 500);
}
