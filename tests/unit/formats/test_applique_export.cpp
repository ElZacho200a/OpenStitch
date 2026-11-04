// SPDX-License-Identifier: Apache-2.0
// HP-SPEC-001 : contour de découpe (SVG/DXF) et arrêts machine d'un appliqué dans les codecs.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "openstitch/formats/cut_svg.hpp"
#include "openstitch/formats/dxf.hpp"
#include "openstitch/formats/format_registry.hpp"
#include "openstitch/stitch_generation/applique.hpp"
#include "openstitch/stitch_generation/generate.hpp"

using namespace openstitch;

namespace {

Vec2um um(int x, int y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

geometry::Path rect(int x0, int y0, int w, int h) {
    geometry::Path p;
    p.closed = true;
    for (const Vec2um v : {um(x0, y0), um(x0 + w, y0), um(x0 + w, y0 + h), um(x0, y0 + h)}) {
        p.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
    }
    return p;
}

} // namespace

TEST_CASE("applique : SVG de decoupe en mm, Y vers le bas, un trace par contour") {
    const std::vector<geometry::Path> paths{rect(5'000, 5'000, 20'000, 10'000),
                                            rect(10'000, 8'000, 4'000, 4'000)};
    const std::string svg = formats::encode_cut_svg(paths);
    CHECK(svg.find("viewBox=\"0 0 20.000 10.000\"") != std::string::npos);
    CHECK(svg.find("width=\"20.000mm\"") != std::string::npos);
    CHECK(svg.find("M0.000 10.000L20.000 10.000") != std::string::npos);
    std::size_t count = 0;
    for (std::size_t pos = svg.find("<path"); pos != std::string::npos;
         pos = svg.find("<path", pos + 1)) {
        ++count;
    }
    CHECK(count == 2);
    CHECK(svg == formats::encode_cut_svg(paths)); // déterministe
    CHECK(formats::encode_cut_svg({}).find("<path") == std::string::npos);
}

TEST_CASE("applique : le contour de decoupe s'exporte aussi en DXF et se relit") {
    const std::vector<geometry::PathSet> regions{{rect(0, 0, 30'000, 20'000), {}}};
    const auto cut = stitch_generation::applique_cut_paths(regions, document::AppliqueParams{});
    const auto dxf = formats::encode_dxf(cut);
    REQUIRE(dxf.has_value());
    const auto back = formats::decode_dxf(*dxf);
    REQUIRE(back.has_value());
    CHECK(back->size() == cut.size());
}

TEST_CASE("applique : les arrets machine survivent a chaque codec d'ecriture") {
    document::Project project;
    document::VectorObject vec;
    vec.id = project.object_ids.next();
    vec.paths.push_back(geometry::PathSet{rect(0, 0, 30'000, 30'000), {}});
    project.vector_objects.push_back(vec);
    document::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vec.id;
    emb.params = document::AppliqueParams{};
    project.embroidery_objects.push_back(emb);
    const auto seq = stitch_generation::generate_sequence(project);
    REQUIRE(seq.has_value());

    std::size_t tested = 0;
    for (const auto& info : formats::registered_formats()) {
        if (!info.can_write || !info.can_read || info.encode == nullptr || info.decode == nullptr) {
            continue;
        }
        const auto bytes = info.encode(*seq);
        REQUIRE(bytes.has_value());
        const auto decoded = info.decode(*bytes);
        REQUIRE(decoded.has_value());
        std::size_t pauses = 0;
        for (const auto& c : decoded->commands) {
            pauses +=
                (c.type == stitch::CommandType::Stop || c.type == stitch::CommandType::ColorChange)
                    ? 1
                    : 0;
        }
        INFO(info.id);
        CHECK(pauses >= 2);
        ++tested;
    }
    CHECK(tested >= 2);
}
