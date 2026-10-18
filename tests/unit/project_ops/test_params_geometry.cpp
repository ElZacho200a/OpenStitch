// SPDX-License-Identifier: Apache-2.0
// set_params : guides, lignes de rupture, degrades, rails, barreaux (polylignes en mm).
#include <catch2/catch_test_macros.hpp>

#include <string>

#include <nlohmann/json.hpp>

#include "openstitch/project_ops/inspect.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
namespace po = openstitch::project_ops;
using nlohmann::json;

namespace {

geometry::Path polyline(std::initializer_list<std::pair<int, int>> pts) {
    geometry::Path p;
    p.closed = false;
    for (const auto& [x, y] : pts) {
        geometry::PathNode n;
        n.pos = Vec2um{Micrometers{x}, Micrometers{y}};
        p.nodes.push_back(n);
    }
    return p;
}

document::Project square_project(document::StitchParams params) {
    document::Project p;
    document::VectorObject v;
    v.id = p.object_ids.next();
    v.name = "carre";
    geometry::PathSet set;
    set.outer =
        polyline({{-10'000, -10'000}, {10'000, -10'000}, {10'000, 10'000}, {-10'000, 10'000}});
    set.outer.closed = true;
    v.paths.push_back(set);
    document::EmbroideryObject e;
    e.id = p.object_ids.next();
    e.name = "objet";
    e.source_vector = v.id;
    e.params = std::move(params);
    p.vector_objects.push_back(v);
    p.embroidery_objects.push_back(e);
    return p;
}

po::BatchReport run(document::Project& p, const std::string& op) {
    return po::apply_operation(p, op);
}

} // namespace

TEST_CASE("set_params edits directional guides, break lines and density gradient",
          "[project_ops][params]") {
    auto project = square_project(document::DirectionalFillParams{});
    const auto report =
        run(project, R"({"op":"set_params","object":2,"params":{)"
                     R"("guides":[[[-8,0],[8,0]],[[-8,3],[8,5]]],)"
                     R"("break_lines":[[[0,-12],[0,12]]],)"
                     R"("density_gradient":{"from_mm":[-8,0],"to_mm":[8,0],)"
                     R"("spacing_from_mm":0.4,"spacing_to_mm":0.8,"interpolate_density":true}}})");
    INFO(report.to_json());
    REQUIRE(report.ok);
    const auto* d =
        std::get_if<document::DirectionalFillParams>(&project.embroidery_objects[0].params);
    REQUIRE(d != nullptr);
    REQUIRE(d->guides.size() == 2);
    CHECK(d->guides[1].nodes[1].pos == Vec2um{Micrometers{8'000}, Micrometers{5'000}});
    REQUIRE(d->break_lines.size() == 1);
    REQUIRE(d->density_gradient.has_value());
    CHECK(d->density_gradient->spacing_to.value == 800);
    CHECK(d->density_gradient->interpolate_density);
    CHECK(stitch_generation::effective_sequence(project));

    // Retrait du degrade et des guides.
    REQUIRE(run(project,
                R"({"op":"set_params","object":2,"params":{"density_gradient":null,"guides":[]}})")
                .ok);
    d = std::get_if<document::DirectionalFillParams>(&project.embroidery_objects[0].params);
    CHECK_FALSE(d->density_gradient.has_value());
    CHECK(d->guides.empty());
}

TEST_CASE("set_params accepts a density curve and inspect publishes it", "[project_ops][params]") {
    auto project = square_project(document::DirectionalFillParams{});
    const auto report =
        run(project, R"({"op":"set_params","object":2,"params":{)"
                     R"("density_gradient":{"from_mm":[-8,0],"to_mm":[8,0],)"
                     R"("spacing_from_mm":0.4,"spacing_to_mm":0.8,"curve":"smooth"}}})");
    INFO(report.to_json());
    REQUIRE(report.ok);
    const auto curveOf = [&] {
        return std::get<document::DirectionalFillParams>(project.embroidery_objects[0].params)
            .density_gradient->curve;
    };
    CHECK(curveOf() == document::DensityCurve::Smooth);
    const auto text = po::inspect_object_json(project, ObjectId{2});
    REQUIRE(text);
    CHECK(json::parse(*text)["object"]["params"]["density_gradient"]["curve"] == "smooth");

    // Sans « curve » : linéaire. Valeur inconnue : refusée, le document ne change pas.
    REQUIRE(run(project, R"({"op":"set_params","object":2,"params":{)"
                         R"("density_gradient":{"from_mm":[-8,0],"to_mm":[8,0],)"
                         R"("spacing_from_mm":0.4,"spacing_to_mm":0.8}}})")
                .ok);
    CHECK(curveOf() == document::DensityCurve::Linear);
    REQUIRE(run(project, R"({"op":"set_params","object":2,"params":{)"
                         R"("density_gradient":{"from_mm":[-8,0],"to_mm":[8,0],)"
                         R"("spacing_from_mm":0.4,"spacing_to_mm":0.8,"curve":"steep"}}})")
                .ok);
    const auto bad = run(project, R"({"op":"set_params","object":2,"params":{)"
                                  R"("density_gradient":{"from_mm":[-8,0],"to_mm":[8,0],)"
                                  R"("spacing_from_mm":0.4,"spacing_to_mm":0.8,"curve":"wavy"}}})");
    CHECK_FALSE(bad.ok);
    CHECK(curveOf() == document::DensityCurve::Steep);
}

TEST_CASE("set_params keeps a curved guide untouched when its vertices are unchanged",
          "[project_ops][params]") {
    document::DirectionalFillParams params;
    auto curved = polyline({{-8'000, 0}, {8'000, 0}});
    curved.nodes[0].tan_out = Vec2um{Micrometers{3'000}, Micrometers{2'000}};
    params.guides.push_back(curved);
    auto project = square_project(params);
    // Reecriture a l'identique de ce que publie l'inspection : la courbe est conservee.
    REQUIRE(
        run(project, R"({"op":"set_params","object":2,"params":{"guides":[[[-8,0],[8,0]]]}})").ok);
    const auto* d =
        std::get_if<document::DirectionalFillParams>(&project.embroidery_objects[0].params);
    CHECK(d->guides[0].nodes[0].tan_out.has_value());
    // Un sommet change : la polyligne remplace la courbe.
    REQUIRE(
        run(project, R"({"op":"set_params","object":2,"params":{"guides":[[[-8,0],[8,1]]]}})").ok);
    d = std::get_if<document::DirectionalFillParams>(&project.embroidery_objects[0].params);
    CHECK_FALSE(d->guides[0].nodes[0].tan_out.has_value());
}

TEST_CASE("set_params edits auto satin guides and rejects malformed ones",
          "[project_ops][params]") {
    auto project = square_project(document::AutoSatinParams{});
    REQUIRE(run(project, R"({"op":"set_params","object":2,"params":{"guides":[)"
                         R"({"anchor_mm":[0,0],"angle_deg":30},)"
                         R"({"anchor_mm":[2,1],"angle_deg":90,"absolute":true}]}})")
                .ok);
    const auto* a = std::get_if<document::AutoSatinParams>(&project.embroidery_objects[0].params);
    REQUIRE(a->guides.size() == 2);
    CHECK(a->guides[1].absolute);
    const auto before = *a;
    for (const char* bad : {R"({"guides":[{"anchor_mm":[0,0]}]})", R"({"guides":"x"})",
                            R"({"guides":[{"anchor_mm":[0],"angle_deg":1}]})",
                            R"({"guides":[{"anchor_mm":[0,0],"angle_deg":999}]})"}) {
        const auto r =
            run(project, std::string(R"({"op":"set_params","object":2,"params":)") + bad + "}");
        INFO(bad);
        CHECK_FALSE(r.ok);
    }
    a = std::get_if<document::AutoSatinParams>(&project.embroidery_objects[0].params);
    CHECK(*a == before);
}

TEST_CASE("set_params edits satin rails and rungs", "[project_ops][params]") {
    document::SatinParams satin;
    satin.rail_a = polyline({{-10'000, -2'000}, {10'000, -2'000}});
    satin.rail_b = polyline({{-10'000, 2'000}, {10'000, 2'000}});
    auto project = square_project(satin);
    REQUIRE(run(project,
                R"({"op":"set_params","object":2,"params":{)"
                R"("rail_a":[[-10,-3],[0,-3],[10,-3]],)"
                R"("rungs":[{"a_mm":[-10,-3],"b_mm":[-10,2]},{"a_mm":[10,-3],"b_mm":[10,2]}]}})")
                .ok);
    const auto* s = std::get_if<document::SatinParams>(&project.embroidery_objects[0].params);
    CHECK(s->rail_a.nodes.size() == 3);
    CHECK(s->rungs.size() == 2);
    const auto r = run(project, R"({"op":"set_params","object":2,"params":{"rail_b":[[0,0]]}})");
    CHECK_FALSE(r.ok);
    CHECK(r.to_json().find("E_USAGE") != std::string::npos);
    // Champ de rail sur un tatami : refus nomme.
    auto tatami = square_project(document::TatamiParams{});
    CHECK(run(tatami, R"({"op":"set_params","object":2,"params":{"rail_a":[[0,0],[1,1]]}})")
              .to_json()
              .find("E_WRONG_STITCH_TYPE") != std::string::npos);
}

TEST_CASE("inspect publishes geometry params under the names set_params accepts",
          "[project_ops][params]") {
    document::DirectionalFillParams params;
    params.guides.push_back(polyline({{-8'000, 0}, {8'000, 0}}));
    params.break_lines.push_back(polyline({{0, -12'000}, {0, 12'000}}));
    document::DensityGradient g;
    g.from = Vec2um{Micrometers{-8'000}, Micrometers{0}};
    g.to = Vec2um{Micrometers{8'000}, Micrometers{0}};
    g.spacing_from = Micrometers{400};
    g.spacing_to = Micrometers{900};
    params.density_gradient = g;
    auto project = square_project(params);
    const auto text = po::inspect_object_json(project, ObjectId{2});
    REQUIRE(text);
    const json doc = json::parse(*text);
    const json p = doc["object"]["params"];
    REQUIRE(p["guides"].size() == 1);
    // Re-ecrire exactement ce que l'inspection publie ne change rien.
    json op = {{"op", "set_params"}, {"object", 2}, {"params", json::object()}};
    for (const char* k : {"guides", "break_lines", "density_gradient", "row_spacing_mm"}) {
        op["params"][k] = p[k];
    }
    const auto before = project.embroidery_objects[0].params;
    const auto report = po::apply_operation(project, op.dump());
    INFO(report.to_json());
    REQUIRE(report.ok);
    CHECK(project.embroidery_objects[0].params == before);
}
