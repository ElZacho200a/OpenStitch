// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include "openstitch/autodigitize/quality_metrics.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/stitch_generation/overrides.hpp"

using namespace openstitch;
using namespace openstitch::autodigitize;

namespace {

image::Image ring_image(int outer, int inner) {
    image::Image img;
    img.width = 200;
    img.height = 200;
    img.rgba.assign(200 * 200 * 4, 255);
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            const double d = std::hypot(x - 99.5, y - 99.5);
            if (d <= outer && d >= inner) {
                std::uint8_t* p = img.rgba.data() + (static_cast<std::size_t>(y) * 200 + x) * 4;
                p[0] = 200;
                p[1] = 30;
                p[2] = 30;
            }
        }
    }
    return img;
}

struct Measured {
    AutoResult result;
    AutoQualityReport report;
};

Measured digitize_and_measure(int outer, int inner, bool satinBands) {
    const auto seg =
        segmentation::segment(ring_image(outer, inner), {.max_colors = 2, .min_region_px = 4});
    REQUIRE(seg.has_value());
    IdGenerator<ObjectId> ids;
    AutoOptions opt;
    opt.mm_per_px = Millimeters{0.2};
    opt.skip_largest_region = true;
    opt.satin_bands = satinBands;
    auto res = auto_digitize(*seg, ids, opt);
    REQUIRE(res.has_value());
    Measured m;
    m.report = measure_auto_quality(res->vectors, res->embroideries, opt);
    m.result = std::move(*res);
    return m;
}

} // namespace

TEST_CASE("qualite : bande fine cousue en satin -> taux 1, aucun satin hors maximum") {
    // Anneau de 7 px a 0,2 mm/px = 1,4 mm de large.
    const auto m = digitize_and_measure(40, 33, true);
    CHECK(m.report.thin_band_count == 1);
    CHECK(m.report.thin_band_satin_count == 1);
    CHECK(m.report.thin_band_satin_rate == 1.0);
    CHECK(m.report.satin_count == 1);
    CHECK(m.report.compact_satin_count == 0);
    CHECK(m.report.satin_over_max_count == 0);
    CHECK(m.report.satin_max_width_mm > 0.0);
    CHECK(m.report.satin_max_width_mm <= 6.0);
    CHECK(m.report.embroidered_area_mm2 > 0.0);
}

TEST_CASE("qualite : meme bande en tatami (satin_bands desactive) -> taux 0") {
    const auto m = digitize_and_measure(40, 33, false);
    CHECK(m.report.thin_band_count == 1);
    CHECK(m.report.thin_band_satin_count == 0);
    CHECK(m.report.thin_band_satin_rate == 0.0);
    CHECK(m.report.satin_count == 0);
    CHECK(m.report.fill_count == 1);
}

TEST_CASE("qualite : disque compact en satin a cordes paralleles, sous son maximum") {
    const auto m = digitize_and_measure(25, 0, true);
    CHECK(m.report.satin_count == 1);
    CHECK(m.report.compact_satin_count == 1);
    CHECK(m.report.satin_over_max_count == 0);
    // Une forme compacte n'est pas une bande fine : elle ne compte pas dans le taux.
    CHECK(m.report.thin_band_count == 0);
    CHECK(m.report.thin_band_satin_rate == 1.0);
}

TEST_CASE("qualite : anneau trop large -> ni bande ni satin, tatami") {
    // 60 px a 0,2 mm = 12 mm, au-dessus de satin_max_width.
    const auto m = digitize_and_measure(80, 20, true);
    CHECK(m.report.thin_band_count == 0);
    CHECK(m.report.satin_count == 0);
    CHECK(m.report.satin_over_max_count == 0);
    CHECK(m.report.fill_count == 1);
}

TEST_CASE("qualite : la mesure est deterministe et ne depend que du resultat") {
    const auto a = digitize_and_measure(40, 33, true);
    const auto b = digitize_and_measure(40, 33, true);
    CHECK(a.report.embroidered_area_mm2 == b.report.embroidered_area_mm2);
    CHECK(a.report.satin_max_width_mm == b.report.satin_max_width_mm);
    CHECK(a.report.object_count == b.report.object_count);
}

TEST_CASE("qualite : un satin hors maximum est signale") {
    // On force un objet auto-satin sur un anneau de 12 mm : la metrique doit le compter.
    auto m = digitize_and_measure(80, 20, false);
    REQUIRE(m.result.embroideries.size() == 1);
    m.result.embroideries[0].params = document::AutoSatinParams{};
    AutoOptions opt;
    opt.mm_per_px = Millimeters{0.2};
    const auto rep = measure_auto_quality(m.result.vectors, m.result.embroideries, opt);
    CHECK(rep.satin_count == 1);
    CHECK(rep.satin_over_max_count == 1);
    CHECK(rep.satin_max_width_mm > 6.0);
}

TEST_CASE("qualite : source introuvable -> compte par type, sans aire") {
    document::EmbroideryObject e;
    e.id = ObjectId{1};
    e.source_vector = ObjectId{99};
    e.params = document::TatamiParams{};
    const auto rep = measure_auto_quality({}, {e}, AutoOptions{});
    CHECK(rep.fill_count == 1);
    CHECK(rep.embroidered_area_mm2 == 0.0);
    CHECK(rep.thin_band_satin_rate == 1.0);
}

// --- Satin guide sur formes larges (HP-ENG-009) ---

namespace {

geometry::Path ellipse_path(double cx, double cy, double rx, double ry, int n = 360) {
    geometry::Path p;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * 3.14159265358979323846 * i / n;
        geometry::PathNode node;
        node.pos = {{static_cast<std::int32_t>(std::lround(cx + rx * std::cos(a)))},
                    {static_cast<std::int32_t>(std::lround(cy + ry * std::sin(a)))}};
        p.nodes.push_back(node);
    }
    return p;
}

// Tous les rayons en micrometres.
geometry::PathSet ellipse_set(double rx, double ry) {
    geometry::PathSet s;
    s.outer = ellipse_path(0, 0, rx, ry);
    return s;
}

geometry::PathSet ring_set(double rOut, double rIn, double holeDx = 0.0) {
    geometry::PathSet s;
    s.outer = ellipse_path(0, 0, rOut, rOut);
    s.holes.push_back(ellipse_path(holeDx, 0, rIn, rIn));
    return s;
}

} // namespace

TEST_CASE("satin guide large : anneaux larges, le guide bat le tatami sur la mesure") {
    const AutoOptions opt;
    // Anneaux de 7 a 10 mm de large (au-dela de satin_max_width = 6 mm).
    for (const auto& ring :
         {ring_set(15000, 6000), ring_set(20000, 10000), ring_set(12000, 5000)}) {
        const auto plan = plan_wide_guided_satin(ring, opt);
        REQUIRE(plan.has_value());
        INFO("couverture " << plan->coverage << " colonnes " << plan->columns);
        CHECK(plan->better);
        CHECK(plan->columns == 1);
        CHECK(plan->coverage >= opt.satin_min_coverage);
        // Fils normaux au bord (guide) contre rangees a angle fixe (tatami).
        CHECK(plan->guided.edge_misalignment < 0.05);
        CHECK(plan->fill.edge_misalignment > 0.4);
        CHECK(plan->guided.glancing_rate == 0.0);
        CHECK(plan->fill.glancing_rate > 0.1);
        // Direction reguliere, aucun rail croise, fils plus courts que le maximum d'un satin.
        CHECK(plan->guided.max_turn_deg < 5.0);
        CHECK(plan->guided.crossings == 0);
        CHECK(plan->guided.max_thread_um <= 12'000.0);
        CHECK(plan->guided.min_pitch_um >= 160.0);
        CHECK(plan->guided.max_pitch_um <= 700.0);
        CHECK_FALSE(plan->guide.empty());
    }
}

TEST_CASE("satin guide large : disque et ellipse pleins restent en tatami") {
    const AutoOptions opt;
    // Disque : l'axe automatique est droit, les fils sont des cordes paralleles, exactement comme
    // un tatami -> aucun avantage mesure.
    for (const double r : {7500.0, 15000.0}) {
        const auto plan = plan_wide_guided_satin(ellipse_set(r, r), opt);
        REQUIRE(plan.has_value());
        CHECK_FALSE(plan->better);
        CHECK(plan->guided.edge_misalignment ==
              Catch::Approx(plan->fill.edge_misalignment).margin(0.05));
        CHECK(plan->guide.empty());
    }
    // Ellipses larges : bords plus francs mais fils de 14 a 16 mm, au-dela du maximum d'un satin.
    for (const auto& e : {ellipse_set(15000, 8000), ellipse_set(18000, 7000)}) {
        const auto plan = plan_wide_guided_satin(e, opt);
        REQUIRE(plan.has_value());
        CHECK(plan->guided.edge_misalignment < plan->fill.edge_misalignment);
        CHECK(plan->guided.max_thread_um > 12'000.0);
        CHECK_FALSE(plan->better);
    }
}

TEST_CASE("satin guide large : forme a trou excentre refusee (densite non conforme)") {
    const AutoOptions opt;
    const auto plan = plan_wide_guided_satin(ring_set(15000, 6000, 2500), opt);
    REQUIRE(plan.has_value());
    CHECK(plan->guided.min_pitch_um < 160.0);
    CHECK_FALSE(plan->better);
}

TEST_CASE("satin guide large : deux trous ou region vide -> pas de plan") {
    auto two = ring_set(15000, 3000);
    two.holes.push_back(ellipse_path(8000, 0, 2000, 2000));
    CHECK_FALSE(plan_wide_guided_satin(two, AutoOptions{}).has_value());
}

TEST_CASE("satin guide large : la mesure est deterministe") {
    const AutoOptions opt;
    const auto a = plan_wide_guided_satin(ring_set(15000, 6000), opt);
    const auto b = plan_wide_guided_satin(ring_set(15000, 6000), opt);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->guide == b->guide);
    CHECK(a->columns == b->columns);
    CHECK(a->coverage == b->coverage);
    CHECK(a->guided.edge_misalignment == b->guided.edge_misalignment);
    CHECK(a->fill.edge_misalignment == b->fill.edge_misalignment);
    CHECK(a->better == b->better);
}

TEST_CASE("satin guide large : auto_digitize choisit le guide pour un anneau large") {
    // Anneau de 40 px d'epaisseur a 0,2 mm/px = 8 mm, rayon exterieur 16 mm.
    const auto seg =
        segmentation::segment(ring_image(80, 40), {.max_colors = 2, .min_region_px = 4});
    REQUIRE(seg.has_value());
    const auto run = [&](bool satinBands, bool wide) {
        IdGenerator<ObjectId> ids;
        AutoOptions opt;
        opt.mm_per_px = Millimeters{0.2};
        opt.skip_largest_region = true;
        opt.satin_bands = satinBands;
        opt.satin_wide_guided = wide;
        auto res = auto_digitize(*seg, ids, opt);
        REQUIRE(res.has_value());
        return std::move(*res);
    };
    const auto guided = run(true, true);
    REQUIRE(guided.embroideries.size() == 1);
    REQUIRE(guided.embroideries[0].is_auto_satin());
    const auto& sp = std::get<document::AutoSatinParams>(guided.embroideries[0].params);
    CHECK(sp.strategy == document::AutoSatinStrategy::Guided);
    CHECK_FALSE(sp.compact_axis);
    REQUIRE(sp.guides.size() == 1);
    CHECK(sp.guides[0].path.size() > 16);
    CHECK(sp.guides[0].path.front() == sp.guides[0].path.back()); // ligne centrale fermee
    CHECK(sp.pull_mode == document::PullMode::Auto);

    AutoOptions measureOpt;
    measureOpt.mm_per_px = Millimeters{0.2};
    const auto rep = measure_auto_quality(guided.vectors, guided.embroideries, measureOpt);
    CHECK(rep.guided_satin_count == 1);
    CHECK(rep.satin_over_max_count == 0);

    // Option coupee, ou satin_bands coupe : tatami, comme avant.
    for (const auto& r : {run(true, false), run(false, true)}) {
        REQUIRE(r.embroideries.size() == 1);
        CHECK(r.embroideries[0].is_tatami());
    }

    // Deterministe.
    const auto again = run(true, true);
    CHECK(again.embroideries[0].params == guided.embroideries[0].params);

    // Les points : le satin guide produit une sequence non vide, deterministe, du meme ordre de
    // grandeur que le tatami (pas d'explosion de points).
    const auto stitches = [](const AutoResult& r) {
        document::Project p;
        p.vector_objects = r.vectors;
        p.embroidery_objects = r.embroideries;
        const auto seq = stitch_generation::effective_sequence(p);
        REQUIRE(seq.has_value());
        const auto again2 = stitch_generation::effective_sequence(p);
        REQUIRE(again2.has_value());
        REQUIRE(seq->commands.size() == again2->commands.size());
        std::size_t n = 0;
        for (const auto& c : seq->commands) {
            n += c.type == stitch::CommandType::Stitch ? 1 : 0;
        }
        return n;
    };
    const std::size_t satinStitches = stitches(guided);
    const std::size_t fillStitches = stitches(run(true, false));
    INFO("points : satin guide " << satinStitches << ", tatami " << fillStitches);
    CHECK(satinStitches > 100);
    CHECK(satinStitches < 3 * fillStitches);
}
