// SPDX-License-Identifier: Apache-2.0
//
// Fondus à la frontière multiples dans le .osp : un seul lien garde l'objet `boundaryBlend`
// historique (octet pour octet), deux liens ou plus s'écrivent dans le tableau `boundaryBlends`.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "json_serialize.hpp"
#include "openstitch/project_io/project_io.hpp"

using namespace openstitch;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// Ids : 1, 3, 5 vecteurs ; 2, 4, 6 tatamis.
document::Project blend_project() {
    document::Project project;
    project.original.width = 1;
    project.original.height = 1;
    project.original.rgba.assign(4, 255);
    for (int i = 0; i < 3; ++i) {
        document::VectorObject v;
        v.id = project.object_ids.next();
        project.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = project.object_ids.next();
        e.source_vector = v.id;
        e.params = document::TatamiParams{};
        e.rgb = {static_cast<std::uint8_t>(50 * i), 0, 0};
        project.embroidery_objects.push_back(std::move(e));
    }
    return project;
}

document::BoundaryBlendLink link_to(std::uint64_t partner, std::int32_t width) {
    document::BoundaryBlendLink l;
    l.partner = ObjectId{partner};
    l.width_self = Micrometers{width};
    return l;
}

const json& object_json(const json& j, std::uint64_t id) {
    const json* found = nullptr;
    for (const auto& eo : j.at("embroideryObjects")) {
        if (eo.at("id").get<std::uint64_t>() == id) {
            found = &eo;
        }
    }
    REQUIRE(found != nullptr);
    return *found;
}

} // namespace

TEST_CASE("boundary blend persistence: one link keeps the legacy boundaryBlend object",
          "[project_io][boundary_blend]") {
    auto project = blend_project();
    auto ab = link_to(4, 2'000);
    ab.curve = document::DensityCurve::Smooth;
    project.findEmbroidery(ObjectId{2})->boundary_blends = {ab};
    auto ba = link_to(2, 3'000);
    ba.curve = document::DensityCurve::Smooth;
    ba.rows = document::BoundaryBlendRows::Interlaced;
    ba.manual_frontier = {Vec2um{Micrometers{0}, Micrometers{1}},
                          Vec2um{Micrometers{-2}, Micrometers{3}}};
    project.findEmbroidery(ObjectId{4})->boundary_blends = {ba};

    const auto j = project_io::detail::project_to_json(project);
    const auto& a = object_json(j, 2);
    CHECK_FALSE(a.contains("boundaryBlends"));
    CHECK(a.at("boundaryBlend").dump() == R"({"curve":1,"partner":4,"width":2000})");
    const auto& b = object_json(j, 4);
    CHECK(b.at("boundaryBlend").dump() ==
          R"({"curve":1,"frontier":[[0,1],[-2,3]],"partner":2,"rows":"interlaced","width":3000})");
    CHECK_FALSE(object_json(j, 6).contains("boundaryBlend"));
    CHECK_FALSE(object_json(j, 6).contains("boundaryBlends"));
}

TEST_CASE("boundary blend persistence: two links round trip through boundaryBlends",
          "[project_io][boundary_blend]") {
    auto project = blend_project();
    project.findEmbroidery(ObjectId{2})->boundary_blends = {link_to(4, 2'000), link_to(6, 1'500)};
    project.findEmbroidery(ObjectId{4})->boundary_blends = {link_to(2, 3'000)};
    project.findEmbroidery(ObjectId{6})->boundary_blends = {link_to(2, 4'000)};

    const auto j = project_io::detail::project_to_json(project);
    const auto& a = object_json(j, 2);
    CHECK_FALSE(a.contains("boundaryBlend"));
    CHECK(a.at("boundaryBlends").dump() ==
          R"([{"curve":0,"partner":4,"width":2000},{"curve":0,"partner":6,"width":1500}])");

    const auto path = fs::temp_directory_path() / "openstitch_boundary_blends_roundtrip.osp";
    REQUIRE(project_io::save_project(path, project).has_value());
    const auto loaded = project_io::load_project(path);
    fs::remove(path);
    REQUIRE(loaded.has_value());
    for (const std::uint64_t id : {2, 4, 6}) {
        CHECK(loaded->findEmbroidery(ObjectId{id})->boundary_blends ==
              project.findEmbroidery(ObjectId{id})->boundary_blends);
    }
}

TEST_CASE("boundary blend persistence: legacy object and array are merged and sorted",
          "[project_io][boundary_blend]") {
    auto project = blend_project();
    auto j = project_io::detail::project_to_json(project);
    for (auto& eo : j["embroideryObjects"]) {
        if (eo.at("id") == 2) {
            eo["boundaryBlend"] = json::parse(R"({"curve":1,"partner":6,"width":2000})");
            eo["boundaryBlends"] = json::parse(R"([{"partner":4,"width":2500}])");
        }
        if (eo.at("id") == 4) { // ancien fichier : un seul fondu
            eo["boundaryBlend"] = json::parse(R"({"curve":0,"partner":2,"width":2500})");
        }
    }
    const auto loaded = project_io::detail::project_from_json(j);
    REQUIRE(loaded.has_value());
    const auto& links = loaded->findEmbroidery(ObjectId{2})->boundary_blends;
    REQUIRE(links.size() == 2);
    CHECK(links[0].partner == ObjectId{4});
    CHECK(links[1].partner == ObjectId{6});
    CHECK(links[1].curve == document::DensityCurve::Smooth);
    REQUIRE(loaded->findEmbroidery(ObjectId{4})->boundary_blends.size() == 1);
    CHECK(loaded->findEmbroidery(ObjectId{4})->boundary_blends[0].partner == ObjectId{2});
}

TEST_CASE("boundary blend persistence: duplicate partner is a read error",
          "[project_io][boundary_blend]") {
    auto project = blend_project();
    auto j = project_io::detail::project_to_json(project);
    for (auto& eo : j["embroideryObjects"]) {
        if (eo.at("id") == 2) {
            eo["boundaryBlend"] = json::parse(R"({"partner":4,"width":2000})");
            eo["boundaryBlends"] = json::parse(R"([{"partner":4,"width":2500}])");
        }
        if (eo.at("id") == 6) {
            eo["boundaryBlends"] =
                json::parse(R"([{"partner":2,"width":2500},{"partner":2,"width":3000}])");
        }
    }
    const auto loaded = project_io::detail::project_from_json(j);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().message.find("même zone") != std::string::npos);

    auto notArray = project_io::detail::project_to_json(project);
    notArray["embroideryObjects"][0]["boundaryBlends"] = json::object();
    CHECK_FALSE(project_io::detail::project_from_json(notArray).has_value());
}
