// SPDX-License-Identifier: Apache-2.0
// Mesures de qualité (Lot G, audit marine plein cadre 2026-09-22) : ce que
// `openstitch-cli stats`/`digitize` et l'analyse pré-export rapportent.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "openstitch/stitch_analysis/analyze.hpp"
#include "openstitch/stitch_analysis/metrics.hpp"
#include "openstitch/stitch_analysis/project_metrics.hpp"

using namespace openstitch;
using namespace openstitch::stitch_analysis;
using stitch::CommandType;
using stitch::StitchPass;

namespace {

Vec2um um(std::int32_t x, std::int32_t y) {
    return Vec2um{Micrometers{x}, Micrometers{y}};
}

stitch::StitchCommand st(std::int32_t x, std::int32_t y, StitchPass p = StitchPass::TopStitch) {
    return {um(x, y), CommandType::Stitch, ObjectId{}, p};
}
stitch::StitchCommand jmp(std::int32_t x, std::int32_t y) {
    return {um(x, y), CommandType::Jump, ObjectId{}, StitchPass::Travel};
}
stitch::StitchCommand trim(std::int32_t x, std::int32_t y) {
    return {um(x, y), CommandType::Trim, ObjectId{}, StitchPass::Travel};
}

} // namespace

TEST_CASE("sequence_metrics : deplacements, coupes, deplacements longs sans coupe") {
    stitch::StitchSequence seq;
    seq.commands = {st(0, 0),        st(3'000, 0),   jmp(4'000, 0), st(4'000, 0), // 1 mm : court
                    st(7'000, 0),    jmp(20'000, 0), st(20'000, 0), // 13 mm sans coupe
                    trim(20'000, 0), jmp(40'000, 0), st(40'000, 0), st(43'000, 0)}; // 20 mm coupé
    const auto m = sequence_metrics(seq);
    CHECK(m.stitches == 7);
    CHECK(m.moves == 3);
    CHECK(m.trims == 1);
    CHECK(m.long_moves_without_trim == 1);
}

TEST_CASE("sequence_metrics : tolerance de resolution sur la longueur des deplacements") {
    // 3,05 mm : long à la précision du µm, pas à la résolution d'un DST.
    stitch::StitchSequence seq;
    seq.commands = {st(0, 0), st(1'000, 0), jmp(4'050, 0), st(4'050, 0)};
    CHECK(sequence_metrics(seq).long_moves_without_trim == 1);
    SequenceMetricsOptions o;
    o.length_tolerance = Micrometers{100};
    CHECK(sequence_metrics(seq, o).long_moves_without_trim == 0);
}

TEST_CASE("sequence_metrics : points courts hors verrous") {
    stitch::StitchSequence seq;
    seq.commands = {st(0, 0), st(300, 0),         // court
                    st(600, 0, StitchPass::Lock), // court mais verrou : exclu
                    st(3'600, 0), st(3'700, 0)};  // court
    const auto m = sequence_metrics(seq);
    CHECK(m.short_stitches == 2);
    CHECK(m.short_lock_stitches == 1);
}

TEST_CASE(
    "sequence_metrics : sans passes (DST relu), les allers-retours sont reconnus comme verrous") {
    stitch::StitchSequence seq;
    // Verrou aller-retour 0,3 mm (A, P, A, P, A) puis vrais points.
    seq.commands = {st(0, 0), st(300, 0),   st(0, 0),    st(300, 0),
                    st(0, 0), st(3'000, 0), st(3'200, 0)};
    SequenceMetricsOptions o;
    o.infer_locks = true;
    const auto m = sequence_metrics(seq, o);
    CHECK(m.short_stitches == 1); // seul le dernier (0,2 mm) compte
    CHECK(m.short_lock_stitches == 4);
}

TEST_CASE("sequence_metrics : histogramme des directions de points (modulo 180 degres)") {
    stitch::StitchSequence seq;
    seq.commands = {st(0, 0),        st(3'000, 0), st(0, 0), // 0° (aller-retour)
                    st(0, 3'000),    st(0, 0),               // 90°
                    st(2'121, 2'121)};                       // 45°
    const auto m = sequence_metrics(seq);
    CHECK(m.direction_histogram[0] == 2);
    CHECK(m.direction_histogram[90 / 5] == 2);
    CHECK(m.direction_histogram[45 / 5] == 1);
}

TEST_CASE("analyse pre-export : saut long sans coupe signale, verrous jamais 'point-court'") {
    stitch::StitchSequence seq;
    seq.commands = {st(0, 0),
                    st(3'000, 0),
                    st(3'200, 0, StitchPass::Lock),
                    st(3'000, 0, StitchPass::Lock),
                    jmp(10'000, 0),
                    st(10'000, 0),
                    st(13'000, 0)};
    const auto f = analyze(seq);
    bool longMove = false;
    bool shortStitch = false;
    for (const auto& x : f) {
        longMove = longMove || x.category == "saut-sans-coupe";
        shortStitch = shortStitch || x.category == "point-court";
    }
    CHECK(longMove);
    CHECK_FALSE(shortStitch);
}

namespace {

// Projet : image 20x10 px à 1 mm/px (repère vectoriel centré : x -10..10,
// y -5..5 mm), deux régions (gauche rouge, droite bleue), deux tatami.
document::Project two_region_project() {
    document::Project p;
    p.mm_per_px = Millimeters{1.0};
    segmentation::Segmentation seg;
    seg.width = 20;
    seg.height = 10;
    seg.labels.resize(200);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 20; ++x) {
            seg.labels[static_cast<std::size_t>(y * 20 + x)] = x < 10 ? 1u : 2u;
        }
    }
    seg.region_slots.push_back(segmentation::Region{RegionId{1}, {200, 30, 30}, 100});
    seg.region_slots.push_back(segmentation::Region{RegionId{2}, {30, 30, 200}, 100});
    p.segmentation = std::move(seg);

    const auto rect = [](std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
        geometry::Path path;
        path.closed = true;
        const auto n = [](std::int32_t x, std::int32_t y) {
            return geometry::PathNode{Vec2um{Micrometers{x}, Micrometers{y}},
                                      geometry::NodeType::Corner, std::nullopt, std::nullopt};
        };
        path.nodes = {n(x0, y0), n(x1, y0), n(x1, y1), n(x0, y1)};
        return path;
    };
    const std::array<double, 2> angles{0.0, std::numbers::pi / 4.0};
    for (int i = 0; i < 2; ++i) {
        document::VectorObject v;
        v.id = p.object_ids.next();
        v.paths.push_back(geometry::PathSet{
            i == 0 ? rect(-10'000, -5'000, 0, 5'000) : rect(0, -5'000, 10'000, 5'000), {}});
        p.vector_objects.push_back(v);
        document::EmbroideryObject e;
        e.id = p.object_ids.next();
        e.source_vector = v.id;
        document::TatamiParams tp;
        tp.angle = Angle{angles[static_cast<std::size_t>(i)]};
        e.params = tp;
        p.embroidery_objects.push_back(e);
    }
    // Un tout petit objet (2 mm²) en plus.
    document::VectorObject tiny;
    tiny.id = p.object_ids.next();
    tiny.paths.push_back(geometry::PathSet{rect(0, 0, 2'000, 1'000), {}});
    p.vector_objects.push_back(tiny);
    document::EmbroideryObject te;
    te.id = p.object_ids.next();
    te.source_vector = tiny.id;
    p.embroidery_objects.push_back(te);
    return p;
}

} // namespace

TEST_CASE("project_metrics : surface non couverte, petits objets, angles de remplissage") {
    const auto project = two_region_project();
    // Rangées horizontales espacées de 0,4 mm sur la moitié gauche seulement.
    stitch::StitchSequence seq;
    for (int r = 0; r < 26; ++r) {
        const std::int32_t y = -5'000 + r * 400;
        seq.commands.push_back(st(-10'000, y));
        seq.commands.push_back(st(0, y));
    }
    const auto m = project_metrics(project, seq, {});
    REQUIRE(m.uncovered_ratio.has_value());
    CHECK(*m.uncovered_ratio > 0.45);
    CHECK(*m.uncovered_ratio < 0.55);
    CHECK(m.small_objects == 1);
    CHECK(m.fill_angles_deg.size() == 2);
    CHECK(m.fill_angles_deg.contains(0));
    CHECK(m.fill_angles_deg.contains(45));

    // Couleur exclue (fond ignoré) : la moitié droite ne compte plus.
    ProjectMetricsOptions o;
    o.excluded_rgb = std::array<std::uint8_t, 3>{30, 30, 200};
    const auto m2 = project_metrics(project, seq, o);
    REQUIRE(m2.uncovered_ratio.has_value());
    CHECK(*m2.uncovered_ratio < 0.05);
}
