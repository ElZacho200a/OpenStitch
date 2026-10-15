// SPDX-License-Identifier: Apache-2.0
// Controle qualite : rapport JSON `openstitch.quality/1` et SVG de diagnostic.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openstitch/document/project.hpp"
#include "openstitch/project_ops/quality.hpp"

using namespace openstitch;
using namespace openstitch::project_ops;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

// Carre tatami de cote 2*half (um) centre sur l'origine.
ObjectId add_square(document::Project& project, std::int32_t half) {
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
    obj.params = document::TatamiParams{};
    project.vector_objects.push_back(vec);
    project.embroidery_objects.push_back(obj);
    return obj.id;
}

bool has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// Position de `key` dans le texte, npos si absente.
std::size_t at(const std::string& text, const std::string& key) {
    return text.find("\"" + key + "\"");
}

} // namespace

TEST_CASE("quality report : projet propre, cles presentes dans l'ordre fixe") {
    document::Project project;
    const ObjectId id = add_square(project, 5'000);
    const auto report = build_quality_report(project);
    REQUIRE(report.has_value());
    const std::string& j = report->json;
    CHECK(has(j, "\"schema\": \"openstitch.quality/1\""));
    CHECK(report->errors == 0);
    CHECK(report->passed());
    CHECK(has(j, "\"passed\": true"));
    CHECK(has(j, "\"fits\": true"));

    // Ordre fixe des sections de premier niveau.
    const char* order[] = {
        "schema",          "summary", "sequence",           "frame", "findings", "suppressed",
        "project_metrics", "objects", "fill_params_checks", "limits"};
    std::size_t previous = 0;
    for (const char* key : order) {
        const std::size_t pos = at(j, key);
        REQUIRE(pos != std::string::npos);
        CHECK(pos >= previous);
        previous = pos;
    }
    for (const char* key : {"stitches",
                            "jumps",
                            "trims",
                            "color_changes",
                            "width_mm",
                            "height_mm",
                            "thread_m",
                            "moves",
                            "long_moves_without_trim",
                            "short_stitches",
                            "short_lock_stitches",
                            "direction_histogram_5deg",
                            "moves_between",
                            "moves_within",
                            "margin_mm",
                            "small_objects",
                            "uncovered_ratio",
                            "fill_angles_deg",
                            "moves_by_kind",
                            "short_stitches_by_kind",
                            "region_area_mm2",
                            "covered_ratio",
                            "overflow_ratio",
                            "measured",
                            "findings_count",
                            "min_stitch_mm",
                            "max_stitch_mm",
                            "max_jump_mm",
                            "trim_threshold_mm",
                            "max_layer_thickness"}) {
        INFO(key);
        CHECK(at(j, key) != std::string::npos);
    }
    CHECK(has(j, "\"id\": " + std::to_string(id.value)));
    CHECK(has(j, "\"type\": \"tatami\""));
    CHECK(has(j, "\"measured\": true"));
}

TEST_CASE("quality report : forme plus grande que le canevas, erreurs hors cadre") {
    document::Project project; // canevas 100 x 100 mm
    add_square(project, 80'000);
    const auto report = build_quality_report(project);
    REQUIRE(report.has_value());
    CHECK(report->errors > 0);
    CHECK_FALSE(report->passed());
    CHECK(has(report->json, "\"passed\": false"));
    CHECK(has(report->json, "\"fits\": false"));
    CHECK(has(report->json, "\"category\": \"hors-cadre\""));
}

TEST_CASE("quality report : projet sans point, erreur vide") {
    document::Project project;
    const auto report = build_quality_report(project);
    REQUIRE(report.has_value());
    CHECK(report->errors == 1);
    CHECK(has(report->json, "\"category\": \"vide\""));
}

TEST_CASE("quality report : deterministe et sans detail par objet sur demande") {
    document::Project project;
    add_square(project, 5'000);
    const auto first = build_quality_report(project);
    const auto second = build_quality_report(project);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(first->json == second->json);

    QualityOptions options;
    options.per_object = false;
    const auto light = build_quality_report(project, options);
    REQUIRE(light.has_value());
    CHECK(at(light->json, "objects") == std::string::npos);
}

TEST_CASE("quality report : objet inconnu refuse") {
    document::Project project;
    add_square(project, 5'000);
    QualityOptions options;
    options.only_object = ObjectId{999};
    CHECK_FALSE(build_quality_report(project, options).has_value());
}

TEST_CASE("quality svg : elements attendus et determinisme") {
    document::Project project;
    const ObjectId id = add_square(project, 5'000);
    QualitySvgOptions options;
    options.directions = true;
    const auto svg = render_quality_svg(project, options);
    REQUIRE(svg.has_value());
    CHECK(has(*svg, "<svg"));
    CHECK(has(*svg, "</svg>"));
    CHECK(has(*svg, "stroke=\"#888\""));    // contour d'origine
    CHECK(has(*svg, "stroke=\"#0066cc\"")); // fleche de direction
    CHECK(has(*svg, "stroke=\"black\""));   // points cousus
    CHECK(*svg == *render_quality_svg(project, options));

    options.only_object = id;
    options.outlines = false;
    options.directions = false;
    const auto only = render_quality_svg(project, options);
    REQUIRE(only.has_value());
    CHECK_FALSE(has(*only, "stroke=\"#888\""));
    CHECK_FALSE(has(*only, "#0066cc"));
}

TEST_CASE("quality svg : marqueur de constat hors cadre") {
    document::Project project;
    add_square(project, 80'000);
    const auto svg = render_quality_svg(project);
    REQUIRE(svg.has_value());
    CHECK(has(*svg, "stroke=\"#d00000\""));
    CHECK(has(*svg, "hors-cadre"));
}
