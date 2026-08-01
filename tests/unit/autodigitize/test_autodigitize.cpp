// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <set>

#include "openstitch/auto_satin/satin_column.hpp"
#include "openstitch/autodigitize/autodigitize.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

void set_px(image::Image& img, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::uint8_t* px = img.rgba.data() +
                       (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                        static_cast<std::size_t>(x)) * 4;
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = 255;
}

image::Image blank(int w, int h) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    return img;
}

AutoOptions opts() {
    AutoOptions o;
    o.mm_per_px = Millimeters{1.0};  // 1 px = 1 mm (formes de test en mm)
    o.min_fill_area_mm2 = 20.0;
    o.satin_max_width = Micrometers{6'000};
    return o;
}

}  // namespace

TEST_CASE("grande zone pleine -> tatami editable") {
    // Carré plein 30x30 mm rouge.
    image::Image img = blank(30, 30);
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 30; ++x) {
            set_px(img, x, y, 220, 30, 30);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto result = auto_digitize(*seg, ids, opts());
    REQUIRE(result.has_value());
    REQUIRE(result->vectors.size() == 1);
    REQUIRE(result->embroideries.size() == 1);
    CHECK(result->embroideries[0].is_tatami());
    // Editable : la geometrie vectorielle est presente.
    CHECK_FALSE(result->vectors[0].paths.empty());
    // Lien objet -> vecteur coherent.
    CHECK(result->embroideries[0].source_vector == result->vectors[0].id);
}

TEST_CASE("bande fine -> satin topologique par defaut") {
    // Bande 40x3 mm. Le moteur topologique doit construire deux rails et des
    // barreaux editables sans recourir au decoupage naif du contour.
    image::Image img = blank(44, 8);
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 42; ++x) {
            set_px(img, x, y, 30, 30, 220);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto result = auto_digitize(*seg, ids, opts());
    REQUIRE(result.has_value());
    bool anySatin = false;
    for (const auto& e : result->embroideries) {
        anySatin = anySatin || e.is_satin();
        if (e.is_satin()) {
            const auto& satin = std::get<document::SatinParams>(e.params);
            CHECK(satin.rungs.size() >= 2);
        }
    }
    CHECK(anySatin);
}

TEST_CASE("bande fine -> satin quand use_naive_satin est active") {
    // Meme bande, mais on reactive explicitement le satin naif.
    image::Image img = blank(44, 8);
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 42; ++x) {
            set_px(img, x, y, 30, 30, 220);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.use_auto_satin = false;
    o.use_naive_satin = true;
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());
    bool anySatin = false;
    for (const auto& e : result->embroideries) {
        anySatin = anySatin || e.is_satin();
    }
    CHECK(anySatin);
}

TEST_CASE("bande fine -> tatami si les deux moteurs satin sont desactives") {
    image::Image img = blank(44, 8);
    for (int y = 2; y < 5; ++y) {
        for (int x = 2; x < 42; ++x) {
            set_px(img, x, y, 30, 30, 220);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.use_auto_satin = false;
    o.use_naive_satin = false;
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());
    REQUIRE(result->embroideries.size() == 1);
    CHECK(result->embroideries.front().is_tatami());
}

TEST_CASE("reseau en T -> plusieurs sections satin compatibles deux rails") {
    image::Image img = blank(64, 64);
    for (int y = 8; y < 58; ++y) {
        for (int x = 29; x < 35; ++x) set_px(img, x, y, 25, 180, 110);
    }
    for (int y = 8; y < 14; ++y) {
        for (int x = 8; x < 56; ++x) set_px(img, x, y, 25, 180, 110);
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.satin_max_width = Micrometers{8'000};
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());
    REQUIRE(result->vectors.size() == 1);

    std::vector<const document::SatinParams*> satinSections;
    for (const auto& e : result->embroideries) {
        if (!e.is_satin()) continue;
        CHECK(e.source_vector == result->vectors.front().id);
        const auto& satin = std::get<document::SatinParams>(e.params);
        satinSections.push_back(&satin);
        CHECK_FALSE(satin.rail_a.closed);
        CHECK_FALSE(satin.rail_b.closed);
        CHECK(satin.rungs.size() >= 2);
    }
    REQUIRE(satinSections.size() >= 3);
    std::set<std::uint32_t> junctions;
    for (std::size_t i = 0; i < satinSections.size(); ++i) {
        const auto& topology = satinSections[i]->topology;
        REQUIRE(topology.has_value());
        CHECK(topology->section_index == i);
        CHECK(topology->section_count == satinSections.size());
        if (topology->start_junction) junctions.insert(*topology->start_junction);
        if (topology->end_junction) junctions.insert(*topology->end_junction);
    }
    CHECK(junctions.size() == 1);
}

TEST_CASE("anneau fin -> quatre sections satin et trou preserve") {
    image::Image img = blank(64, 64);
    constexpr int center = 32;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const int dx = x - center;
            const int dy = y - center;
            const int radiusSquared = dx * dx + dy * dy;
            if (radiusSquared <= 24 * 24 && radiusSquared >= 18 * 18) {
                set_px(img, x, y, 230, 205, 90);
            }
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions o = opts();
    o.satin_max_width = Micrometers{12'000};
    const auto result = auto_digitize(*seg, ids, o);
    REQUIRE(result.has_value());
    REQUIRE(result->vectors.size() == 1);
    REQUIRE(result->vectors.front().paths.size() == 1);
    auto_satin::SatinColumnsParameters satinOptions;
    satinOptions.analysis.thresholds.max_satin_width = o.satin_max_width;
    const auto direct =
        auto_satin::build_satin_columns(result->vectors.front().paths.front(), satinOptions);
    INFO("refus anneau: " << direct.refusal);
    INFO("trous: " << result->vectors.front().paths.front().holes.size());
    REQUIRE(direct.columns.size() == 4);

    std::vector<const document::SatinParams*> satinSections;
    for (const auto& e : result->embroideries) {
        if (e.is_satin()) {
            satinSections.push_back(&std::get<document::SatinParams>(e.params));
        }
    }
    REQUIRE(satinSections.size() == 4);
    for (std::size_t i = 0; i < satinSections.size(); ++i) {
        const auto& topology = satinSections[i]->topology;
        REQUIRE(topology.has_value());
        CHECK(topology->section_index == i);
        CHECK(topology->section_count == 4);
        CHECK(topology->start_junction ==
              std::optional<std::uint32_t>{static_cast<std::uint32_t>(i)});
        CHECK(topology->end_junction ==
              std::optional<std::uint32_t>{static_cast<std::uint32_t>((i + 1) % 4)});
    }
}

TEST_CASE("petite region -> contour (point triple)") {
    // Petit carre 3x3 mm : aire 9 < 20 mm² -> contour.
    image::Image img = blank(9, 9);
    for (int y = 3; y < 6; ++y) {
        for (int x = 3; x < 6; ++x) {
            set_px(img, x, y, 30, 200, 30);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    const auto result = auto_digitize(*seg, ids, opts());
    REQUIRE(result.has_value());
    bool anyRunning = false;
    for (const auto& e : result->embroideries) {
        if (std::holds_alternative<document::RunningStitchParams>(e.params)) {
            anyRunning = true;
        }
    }
    CHECK(anyRunning);
}

TEST_CASE("identifiants uniques, objets editables, deterministe") {
    image::Image img = blank(30, 30);
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 30; ++x) {
            set_px(img, x, y, x < 15 ? 220 : 30, 30, x < 15 ? 30 : 220);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());

    IdGenerator<ObjectId> ids1;
    const auto a = auto_digitize(*seg, ids1, opts());
    IdGenerator<ObjectId> ids2;
    const auto b = auto_digitize(*seg, ids2, opts());
    REQUIRE((a.has_value() && b.has_value()));
    CHECK(a->vectors.size() == b->vectors.size());
    CHECK(a->embroideries.size() == b->embroideries.size());

    // Tous les ids d'objets sont distincts.
    std::vector<std::uint64_t> allIds;
    for (const auto& v : a->vectors) allIds.push_back(v.id.value);
    for (const auto& e : a->embroideries) allIds.push_back(e.id.value);
    const auto uniqueEnd = std::unique(allIds.begin(), allIds.end());
    CHECK(uniqueEnd == allIds.end());  // deja tous distincts (pas de doublon adjacent)
}

TEST_CASE("segmentation vide -> erreur propre") {
    segmentation::Segmentation seg;
    IdGenerator<ObjectId> ids;
    CHECK_FALSE(auto_digitize(seg, ids, opts()).has_value());
}
