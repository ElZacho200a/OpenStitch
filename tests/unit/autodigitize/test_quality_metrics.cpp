// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include "openstitch/autodigitize/quality_metrics.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/segmentation/segmentation.hpp"

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
