// SPDX-License-Identifier: Apache-2.0
// Couverture par objet : region d'origine vs zone balayee par les points de l'objet.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "openstitch/stitch_analysis/object_coverage.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;
using stitch::StitchPass;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Carre de cote 2*half (um) centre sur l'origine : objet vectoriel + objet de broderie.
ObjectId add_square(document::Project& project, std::int32_t half, bool tatami = true) {
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.name = "carre";
    geometry::PathSet set;
    for (const auto& p : {um(-half, -half), um(half, -half), um(half, half), um(-half, half)}) {
        set.outer.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
    }
    vec.paths.push_back(set);
    document::EmbroideryObject obj;
    obj.id = project.object_ids.next();
    obj.name = "objet";
    obj.source_vector = vec.id;
    if (tatami) {
        obj.params = document::TatamiParams{};
    }
    project.vector_objects.push_back(vec);
    project.embroidery_objects.push_back(obj);
    return obj.id;
}

// Rangees horizontales en boustrophedon (ecart `spacing`) dans [-reach, reach]^2 decale de `dx`.
void add_rows(stitch::StitchSequence& seq, ObjectId source, std::int32_t reach,
              std::int32_t spacing, std::int32_t dx = 0, StitchPass pass = StitchPass::TopStitch) {
    bool left = true;
    for (std::int32_t y = -reach; y <= reach; y += spacing) {
        const std::int32_t a = (left ? -reach : reach) + dx;
        const std::int32_t b = (left ? reach : -reach) + dx;
        seq.commands.push_back({um(a, y), CommandType::Stitch, source, pass});
        seq.commands.push_back({um(b, y), CommandType::Stitch, source, pass});
        left = !left;
    }
}

} // namespace

TEST_CASE("object_coverage : tatami standard sur un carre, couverture totale") {
    document::Project project;
    const ObjectId id = add_square(project, 5'000);
    stitch::StitchSequence seq;
    add_rows(seq, id, 4'800, 400);

    const auto cov = object_coverage(project, seq);
    REQUIRE(cov.size() == 1);
    CHECK(cov[0].object == id);
    CHECK(cov[0].measured);
    CHECK(cov[0].region_area_mm2 == Catch::Approx(100.0));
    CHECK(cov[0].covered_ratio > 0.97);
    CHECK(cov[0].overflow_ratio < 0.08);
}

TEST_CASE("object_coverage : points decales, couverture reduite et debordement mesures") {
    document::Project project;
    const ObjectId id = add_square(project, 5'000);
    stitch::StitchSequence seq;
    add_rows(seq, id, 4'800, 400, 3'000); // decale de 3 mm vers la droite

    const auto cov = object_coverage(project, seq);
    REQUIRE(cov.size() == 1);
    REQUIRE(cov[0].measured);
    CHECK(cov[0].covered_ratio < 0.9);
    CHECK(cov[0].covered_ratio > 0.5);
    CHECK(cov[0].overflow_ratio > 0.2);
}

TEST_CASE("object_coverage : points reduits, couverture partielle sans debordement") {
    document::Project project;
    const ObjectId id = add_square(project, 5'000);
    stitch::StitchSequence seq;
    add_rows(seq, id, 2'400, 400); // moitie centrale seulement

    const auto cov = object_coverage(project, seq);
    REQUIRE(cov.size() == 1);
    REQUIRE(cov[0].measured);
    CHECK(cov[0].covered_ratio < 0.4);
    CHECK(cov[0].overflow_ratio < 0.08);
}

TEST_CASE("object_coverage : objet sans point, contour et objet invisible") {
    document::Project project;
    const ObjectId empty = add_square(project, 5'000);
    const ObjectId contour = add_square(project, 5'000, false);
    project.findEmbroidery(contour)->params = document::RunningStitchParams{};
    const ObjectId hidden = add_square(project, 5'000);
    project.findEmbroidery(hidden)->visible = false;

    const auto cov = object_coverage(project, {});
    REQUIRE(cov.size() == 2); // l'objet invisible est omis
    CHECK(cov[0].object == empty);
    CHECK_FALSE(cov[0].measured);
    CHECK(cov[0].reason == "aucun point");
    CHECK(cov[1].object == contour);
    CHECK_FALSE(cov[1].measured);
    CHECK_FALSE(cov[1].reason.empty());
}

TEST_CASE("object_coverage : seules les passes visibles de l'objet comptent") {
    document::Project project;
    const ObjectId a = add_square(project, 5'000);
    const ObjectId b = add_square(project, 5'000);
    stitch::StitchSequence seq;
    add_rows(seq, a, 4'800, 400, 0, StitchPass::Underlay); // sous-couche : ignoree
    add_rows(seq, b, 4'800, 400);

    const auto cov = object_coverage(project, seq);
    REQUIRE(cov.size() == 2);
    CHECK_FALSE(cov[0].measured);
    CHECK(cov[0].reason == "aucun point");
    CHECK(cov[1].measured);
    CHECK(cov[1].covered_ratio > 0.97);
}

TEST_CASE("object_coverage : region avec trou, le trou n'est pas a couvrir") {
    document::Project project;
    const ObjectId id = add_square(project, 5'000);
    geometry::Path hole;
    for (const auto& p :
         {um(-2'000, -2'000), um(-2'000, 2'000), um(2'000, 2'000), um(2'000, -2'000)}) {
        hole.nodes.push_back({p, geometry::NodeType::Corner, {}, {}});
    }
    project.vector_objects.front().paths.front().holes.push_back(hole);
    stitch::StitchSequence seq;
    add_rows(seq, id, 4'800, 400);

    const auto cov = object_coverage(project, seq);
    REQUIRE(cov.size() == 1);
    CHECK(cov[0].region_area_mm2 == Catch::Approx(84.0));
    // Les rangees traversent le trou : elles le balaient, c'est du debordement hors region.
    CHECK(cov[0].covered_ratio > 0.97);
    CHECK(cov[0].overflow_ratio > 0.1);
}

TEST_CASE("object_coverage : deterministe") {
    document::Project project;
    const ObjectId id = add_square(project, 5'000);
    stitch::StitchSequence seq;
    add_rows(seq, id, 4'000, 350, 500);
    const auto first = object_coverage(project, seq);
    const auto second = object_coverage(project, seq);
    REQUIRE(first.size() == second.size());
    CHECK(first[0].covered_ratio == second[0].covered_ratio);
    CHECK(first[0].overflow_ratio == second[0].overflow_ratio);
    CHECK(first[0].region_area_mm2 == second[0].region_area_mm2);
}

TEST_CASE("object_coverage : plafond de cellules, pas de mesure agrandi sans casser le resultat") {
    document::Project project;
    const ObjectId id = add_square(project, 5'000);
    stitch::StitchSequence seq;
    add_rows(seq, id, 4'800, 400);
    ObjectCoverageOptions options;
    options.max_cells = 400; // force plusieurs doublements du pas
    const auto cov = object_coverage(project, seq, options);
    REQUIRE(cov.size() == 1);
    REQUIRE(cov[0].measured);
    CHECK(cov[0].covered_ratio > 0.9);
}
