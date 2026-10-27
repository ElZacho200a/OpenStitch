// SPDX-License-Identifier: Apache-2.0
//
// AdaptivePullCompensation niveau 3 (lot A) : champ de deplacement du tissu.
// Valide la GEOMETRIE du champ (sens, symetrie, bornes, determinisme, anisotropie, confiance),
// jamais une efficacite textile reelle : aucun coefficient n'est mesure.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include "openstitch/stitch_generation/pull_field.hpp"

using namespace openstitch;
using namespace openstitch::stitch_generation;

namespace {

PullSource seg(int ax, int ay, int bx, int by, double w = 1.0) {
    return {{{ax}, {ay}}, {{bx}, {by}}, w};
}

double norm(Vec2um v) {
    return std::hypot(static_cast<double>(v.x.value), static_cast<double>(v.y.value));
}

} // namespace

TEST_CASE("pull_field: sans source le deplacement est nul et certain", "[pull_field]") {
    const PullField f({}, FabricResponse{});
    const PullSample s = f.sample({{1000}, {2000}});
    CHECK(s.displacement == Vec2um{});
    CHECK(s.confidence == 1.0);
    CHECK(s.contributors == 0);
}

TEST_CASE("pull_field: un segment contracte le tissu vers son centre", "[pull_field]") {
    const PullField f({seg(-2000, 0, 2000, 0)}, FabricResponse{});
    const PullSample right = f.sample({{2000}, {0}});
    const PullSample left = f.sample({{-2000}, {0}});
    const PullSample mid = f.sample({{0}, {0}});
    CHECK(right.displacement.x.value < 0);
    CHECK(left.displacement.x.value > 0);
    CHECK(std::abs(mid.displacement.x.value) <= 1);
    // Symetrie exacte du modele autour du centre.
    CHECK(right.displacement.x.value == -left.displacement.x.value);
}

TEST_CASE("pull_field: poussee laterale de signe oppose de part et d'autre", "[pull_field]") {
    const PullField f({seg(-2000, 0, 2000, 0)}, FabricResponse{});
    const PullSample up = f.sample({{1500}, {600}});
    const PullSample down = f.sample({{1500}, {-600}});
    CHECK(up.displacement.y.value == -down.displacement.y.value);
    CHECK(up.displacement.y.value != 0);
}

TEST_CASE("pull_field: hors de portee le champ est nul", "[pull_field]") {
    const PullField f({seg(-2000, 0, 2000, 0)}, FabricResponse{});
    const PullSample s = f.sample({{0}, {50000}});
    CHECK(s.displacement == Vec2um{});
    CHECK(s.contributors == 0);
}

TEST_CASE("pull_field: determinisme octet pour octet", "[pull_field]") {
    std::vector<PullSource> v;
    for (int i = 0; i < 40; ++i) {
        v.push_back(seg(-3000 + i * 40, i * 400, 3000 - i * 25, i * 400 + 150, 1.0 + 0.01 * i));
    }
    const PullField a(v, FabricResponse{});
    const PullField b(v, FabricResponse{});
    for (int y = -1000; y <= 17000; y += 700) {
        for (int x = -4000; x <= 4000; x += 650) {
            const PullSample sa = a.sample({{x}, {y}});
            const PullSample sb = b.sample({{x}, {y}});
            CHECK(sa.displacement == sb.displacement);
            CHECK(sa.confidence == sb.confidence);
            CHECK(sa.contributors == sb.contributors);
        }
    }
}

TEST_CASE("pull_field: anisotropie du sens des fils", "[pull_field]") {
    // Segment vertical : le deplacement axial est selon Y, perpendiculaire aux fils de chaine (X).
    const std::vector<PullSource> v{seg(0, -2000, 0, 2000)};
    FabricResponse iso;
    FabricResponse aniso;
    aniso.grain_anisotropy = 2.0;
    const PullSample a = PullField(v, iso).sample({{0}, {2000}});
    const PullSample b = PullField(v, aniso).sample({{0}, {2000}});
    REQUIRE(a.displacement.y.value != 0);
    const double ratio =
        static_cast<double>(b.displacement.y.value) / static_cast<double>(a.displacement.y.value);
    CHECK_THAT(ratio, Catch::Matchers::WithinAbs(2.0, 0.02));

    // Tourner le grain d'un quart de tour echange les roles : plus d'effet sur le meme segment.
    FabricResponse rotated = aniso;
    rotated.grain_angle_rad = std::numbers::pi / 2.0;
    const PullSample c = PullField(v, rotated).sample({{0}, {2000}});
    CHECK_THAT(static_cast<double>(c.displacement.y.value),
               Catch::Matchers::WithinAbs(static_cast<double>(a.displacement.y.value), 1.0));
}

TEST_CASE("pull_field: la norme du deplacement est plafonnee", "[pull_field]") {
    std::vector<PullSource> v;
    for (int i = 0; i < 200; ++i) {
        v.push_back(seg(-6000, i * 20, 6000, i * 20, 3.0));
    }
    FabricResponse fr;
    fr.gain = 0.2;
    fr.max_displacement_mm = 0.25;
    const PullField f(v, fr);
    for (int x = -7000; x <= 7000; x += 500) {
        CHECK(norm(f.sample({{x}, {2000}}).displacement) <= 250.0 + 1.0);
    }
}

TEST_CASE("pull_field: confiance et calibration", "[pull_field]") {
    const std::vector<PullSource> one{seg(-2000, 0, 2000, 0)};
    FabricResponse uncal;
    FabricResponse cal;
    cal.calibrated = true;
    const double cu = PullField(one, uncal).sample({{1500}, {0}}).confidence;
    const double cc = PullField(one, cal).sample({{1500}, {0}}).confidence;
    CHECK(cc > cu);
    CHECK(cu <= uncal.uncalibrated_confidence);

    // Beaucoup de segments superposes : interaction plus incertaine.
    std::vector<PullSource> many;
    for (int i = 0; i < 30; ++i) {
        many.push_back(seg(-2000, i * 30, 2000, i * 30));
    }
    const double cm = PullField(many, cal).sample({{1500}, {400}}).confidence;
    CHECK(cm < cc);
    CHECK(cm >= 0.0);
}

TEST_CASE("pull_field: compensation opposee au deplacement et attenuee", "[pull_field]") {
    const PullField f({seg(-2000, 0, 2000, 0)}, FabricResponse{});
    const Vec2um p{{2000}, {0}};
    const PullSample s = f.sample(p);
    const Vec2um c = f.compensation_at(p);
    REQUIRE(s.displacement.x.value < 0);
    CHECK(c.x.value > 0);
    CHECK(std::abs(c.x.value) <= std::abs(s.displacement.x.value));
}

TEST_CASE("pull_field: entrees degenerees ne produisent ni NaN ni crash", "[pull_field]") {
    FabricResponse bad;
    bad.sigma_along_mm = 0.0;
    bad.sigma_across_mm = -1.0;
    bad.gain = 1e9;
    bad.grain_anisotropy = 0.0;
    const PullField f({seg(5, 5, 5, 5), seg(0, 0, 1000, 0, 0.0), seg(0, 0, 1000, 0, 1.0)}, bad);
    const PullSample s = f.sample({{900}, {10}});
    CHECK(std::isfinite(s.confidence));
    CHECK(norm(s.displacement) <= 3000.0 + 1.0);
}
