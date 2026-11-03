// SPDX-License-Identifier: Apache-2.0
// HP-QA-011 : la generation d'un projet doit produire les memes octets DST quel que soit le
// nombre de fils de `parallel_for` (OPENSTITCH_THREADS=1 contre 8), sur un corpus qui couvre les
// moteurs a tris (tatami, directionnel, contour, satin) et des formes a trous.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <string>
#include <vector>

#include "openstitch/formats/dst.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;

namespace {

void set_threads(const char* value) {
#ifdef _WIN32
    _putenv_s("OPENSTITCH_THREADS", value);
#else
    setenv("OPENSTITCH_THREADS", value, 1);
#endif
}

geometry::Path polygon(int sides, double rOuter, double rInner, double cx, double cy,
                       double phase) {
    geometry::Path p;
    p.closed = true;
    for (int i = 0; i < sides * 2; ++i) {
        const double a = phase + std::numbers::pi * i / sides;
        const double r = (rInner > 0.0 && (i % 2) == 1) ? rInner : rOuter;
        p.nodes.push_back(
            {Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(cx + r * std::cos(a)))},
                    Micrometers{static_cast<std::int32_t>(std::lround(cy + r * std::sin(a)))}},
             geometry::NodeType::Corner,
             {},
             {}});
    }
    return p;
}

// Corpus : etoiles, disques, anneaux (trou) remplis par les differents moteurs.
document::Project make_corpus() {
    document::Project project;
    for (int i = 0; i < 12; ++i) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        const double cx = 30'000.0 * (i % 4);
        const double cy = 30'000.0 * (i / 4);
        geometry::PathSet set;
        if (i % 3 == 0) {
            set.outer = polygon(5, 12'000.0, 5'000.0, cx, cy, 0.3 * i); // etoile
        } else if (i % 3 == 1) {
            set.outer = polygon(24, 11'000.0, 0.0, cx, cy, 0.0); // disque
        } else {
            set.outer = polygon(16, 12'000.0, 0.0, cx, cy, 0.1); // anneau
            set.holes.push_back(polygon(16, 5'000.0, 0.0, cx, cy, 0.1));
        }
        vec.paths.push_back(set);
        project.vector_objects.push_back(vec);

        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "objet " + std::to_string(i);
        emb.source_vector = vec.id;
        emb.rgb = {static_cast<std::uint8_t>(20 * i), static_cast<std::uint8_t>(255 - 15 * i), 90};
        switch (i % 4) {
        case 0:
            emb.params = document::TatamiParams{};
            break;
        case 1:
            emb.params = document::DirectionalFillParams{};
            break;
        case 2:
            emb.params = document::ContourFillParams{};
            break;
        default:
            break; // contour (point courant) par defaut
        }
        project.embroidery_objects.push_back(emb);
    }
    // Deux colonnes satin.
    for (int i = 0; i < 2; ++i) {
        document::SatinParams sp;
        const std::int32_t y0 = 100'000 + i * 12'000;
        sp.rail_a.closed = false;
        sp.rail_a.nodes = {
            {Vec2um{Micrometers{0}, Micrometers{y0}}, geometry::NodeType::Corner, {}, {}},
            {Vec2um{Micrometers{20'000}, Micrometers{y0}}, geometry::NodeType::Corner, {}, {}}};
        sp.rail_b.closed = false;
        sp.rail_b.nodes = {
            {Vec2um{Micrometers{0}, Micrometers{y0 + 5'000}}, geometry::NodeType::Corner, {}, {}},
            {Vec2um{Micrometers{20'000}, Micrometers{y0 + 5'000}},
             geometry::NodeType::Corner,
             {},
             {}}};
        sp.rungs = {{Vec2um{Micrometers{0}, Micrometers{y0}},
                     Vec2um{Micrometers{0}, Micrometers{y0 + 5'000}}},
                    {Vec2um{Micrometers{20'000}, Micrometers{y0}},
                     Vec2um{Micrometers{20'000}, Micrometers{y0 + 5'000}}}};
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "satin " + std::to_string(i);
        emb.rgb = {200, static_cast<std::uint8_t>(40 * i), 30};
        emb.params = sp;
        project.embroidery_objects.push_back(emb);
    }
    return project;
}

std::vector<std::uint8_t> dst_with_threads(const document::Project& project, const char* threads) {
    set_threads(threads);
    stitch_generation::clear_chunk_cache(); // pas de reutilisation d'un calcul a autre nombre de
                                            // fils
    const auto seq = stitch_generation::effective_sequence(project);
    set_threads("");
    REQUIRE(seq.has_value());
    const auto bytes = formats::encode_dst(*seq);
    REQUIRE(bytes.has_value());
    return *bytes;
}

} // namespace

TEST_CASE("determinisme : octets DST identiques avec 1 fil et avec 8 fils") {
    const document::Project project = make_corpus();
    const auto one = dst_with_threads(project, "1");
    const auto eight = dst_with_threads(project, "8");
    const auto eightAgain = dst_with_threads(project, "8");
    REQUIRE(one.size() > 512); // en-tete DST seul : le corpus doit produire des points
    CHECK(one == eight);
    CHECK(eight == eightAgain);
}
