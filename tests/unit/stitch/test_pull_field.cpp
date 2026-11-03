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
    iso.compression = 0.0; // isole l'effet du grain de l'arrondi du tassement
    FabricResponse aniso = iso;
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

TEST_CASE("pull_field: une intensite plus forte augmente la correction sans fausser la confiance",
          "[pull_field]") {
    const std::vector<PullSource> sources{seg(-2000, 0, 2000, 0)};
    FabricResponse low;
    low.compensation_strength = 1.0;
    FabricResponse high;
    high.compensation_strength = 3.0;
    const PullField weak(sources, low);
    const PullField strong(sources, high);
    const Vec2um position{{2000}, {0}};
    const auto a = weak.compensation_at(position);
    const auto b = strong.compensation_at(position);

    CHECK(b.x.value > a.x.value);
    CHECK(b.x.value >= 2 * a.x.value);
    CHECK(strong.sample(position).confidence == weak.sample(position).confidence);
    CHECK(strong.sample(position).displacement == weak.sample(position).displacement);
    CHECK(strong.compensation_at(position) == strong.compensation_at(position));
}

TEST_CASE("pull_field: intensite elevee toujours bornee par le plafond physique", "[pull_field]") {
    std::vector<PullSource> sources;
    for (int i = 0; i < 150; ++i) {
        sources.push_back(seg(-6000, i * 12, 6000, i * 12, 3.0));
    }
    FabricResponse response;
    response.gain = 0.2;
    response.max_displacement_mm = 0.25;
    response.uncalibrated_confidence = 1.0;
    response.compensation_strength = 6.0;
    const PullField field(sources, response);
    for (int x = -7000; x <= 7000; x += 500) {
        const auto pull = field.compensation_at({{x}, {2000}});
        CHECK(norm(pull) <= 251.0);
    }
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

TEST_CASE("pull_field: le tassement fait saturer l'empilement de segments", "[pull_field]") {
    const auto stack = [](int n) {
        std::vector<PullSource> v;
        for (int i = 0; i < n; ++i) {
            v.push_back(seg(-3000, i * 100, 3000, i * 100));
        }
        return v;
    };
    FabricResponse linear;
    linear.compression = 0.0;
    linear.max_displacement_mm = 3.0;
    FabricResponse packed = linear;
    packed.compression = 0.2;
    const Vec2um p{{3000}, {0}};
    const auto mag = [&](int n, const FabricResponse& f) {
        return norm(PullField(stack(n), f).sample(p).displacement);
    };
    // Sans tassement, l'empilement croit presque lineairement ; avec, il sature nettement.
    const double linearGrowth = mag(8, linear) / mag(2, linear);
    const double packedGrowth = mag(8, packed) / mag(2, packed);
    CHECK(linearGrowth > 2.5);
    CHECK(packedGrowth < linearGrowth);
    CHECK(mag(8, packed) < mag(8, linear));
    CHECK(mag(8, packed) > 0.0);
}

TEST_CASE("pull_field: le plafond doux ne depasse jamais le maximum", "[pull_field]") {
    std::vector<PullSource> v;
    for (int i = 0; i < 100; ++i) {
        v.push_back(seg(-6000, i * 40, 6000, i * 40, 3.0));
    }
    FabricResponse fr;
    fr.gain = 0.2;
    fr.compression = 0.0;
    fr.max_displacement_mm = 0.4;
    const PullField f(v, fr);
    for (int x = 0; x <= 6000; x += 1000) {
        CHECK(norm(f.sample({{x}, {2000}}).displacement) < 400.0 + 1.0);
    }
}

TEST_CASE("pull_field: la poussee laterale suit la souplesse du tissu selon l'angle",
          "[pull_field]") {
    // Segment le long de la chaine (X) : sa normale (Y) est dans le sens de la trame.
    const std::vector<PullSource> alongWarp{seg(-2000, 0, 2000, 0)};
    // Segment le long de la trame (Y) : son axe est le plus souple.
    const std::vector<PullSource> alongWeft{seg(0, -2000, 0, 2000)};
    FabricResponse iso;
    iso.compression = 0.0;
    FabricResponse soft = iso;
    soft.grain_anisotropy = 2.0;
    const double warpIso = norm(PullField(alongWarp, iso).sample({{1500}, {600}}).displacement);
    const double warpSoft = norm(PullField(alongWarp, soft).sample({{1500}, {600}}).displacement);
    const double weftIso = norm(PullField(alongWeft, iso).sample({{600}, {1500}}).displacement);
    const double weftSoft = norm(PullField(alongWeft, soft).sample({{600}, {1500}}).displacement);
    CHECK(warpSoft > warpIso);
    CHECK(weftSoft > weftIso * 1.5);
}

TEST_CASE("pull_field: un point ne subit que le tirage des segments cousus apres lui",
          "[pull_field]") {
    PullSource early = seg(-2000, 0, 2000, 0);
    early.order = 0;
    PullSource late = seg(-2000, 300, 2000, 300);
    late.order = 10;
    FabricResponse fr;
    fr.compression = 0.5;
    const Vec2um p{{2000}, {150}};
    const PullField both({early, late}, fr);
    const PullField lateOnly({late}, fr);

    const PullSample all = both.sample(p, 0);
    const PullSample after = both.sample(p, 5); // seul `late` tire
    const PullSample none = both.sample(p, 11);
    CHECK(norm(after.displacement) < norm(all.displacement));
    CHECK(none.displacement == Vec2um{});
    CHECK(none.contributors == 0);
    // `early`, cousu avant, raidit le tissu : le tirage de `late` est plus faible que s'il etait
    // seul sur un tissu intact.
    CHECK(norm(after.displacement) < norm(lateOnly.sample(p, 5).displacement));
    CHECK(both.sample(p, 5).displacement == both.sample(p, 5).displacement);
}

TEST_CASE("pull_field: la compensation d'un point depend de son rang de couture", "[pull_field]") {
    PullSource a = seg(-2000, 0, 2000, 0);
    a.order = 1;
    PullSource b = seg(-2000, 400, 2000, 400);
    b.order = 20;
    const PullField f({a, b}, FabricResponse{});
    const Vec2um p{{2000}, {0}};
    CHECK(norm(f.compensation_at(p, 1)) > norm(f.compensation_at(p, 20)));
}
