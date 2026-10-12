// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "openstitch/stipple/stipple.hpp"

using namespace openstitch;
using namespace openstitch::stipple;

namespace {

DensityMap flat(int w, int h, float v) {
    DensityMap d;
    d.width = w;
    d.height = h;
    d.values.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), v);
    return d;
}

double nearest_neighbor_min(const std::vector<PointD>& pts) {
    double best = 1e18;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        for (std::size_t j = i + 1; j < pts.size(); ++j) {
            best = std::min(best, std::hypot(pts[i].x - pts[j].x, pts[i].y - pts[j].y));
        }
    }
    return best;
}

double nearest_neighbor_mean(const std::vector<PointD>& pts) {
    double sum = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        double best = 1e18;
        for (std::size_t j = 0; j < pts.size(); ++j) {
            if (i != j) {
                best = std::min(best, std::hypot(pts[i].x - pts[j].x, pts[i].y - pts[j].y));
            }
        }
        sum += best;
    }
    return sum / static_cast<double>(pts.size());
}

image::Image gradient_image(int w, int h) {
    image::Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = img.rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            const auto v = static_cast<std::uint8_t>(255 * x / (w - 1)); // noir a gauche
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }
    return img;
}

} // namespace

TEST_CASE("densite : noir = dense, blanc = vide, alpha nul = vide, sous-echantillonnage") {
    image::Image img = gradient_image(64, 8);
    // Colonne transparente.
    for (int y = 0; y < 8; ++y) {
        img.rgba[(static_cast<std::size_t>(y) * 64 + 10) * 4 + 3] = 0;
    }
    const auto d = density_from_image(img);
    REQUIRE(d.has_value());
    REQUIRE(d->width == 64);
    CHECK(d->at(0, 3) > 0.99F);
    CHECK(d->at(63, 3) < 0.01F);
    CHECK(d->at(10, 3) == 0.0F);
    CHECK(d->at(20, 3) > d->at(40, 3));

    const auto inv = density_from_image(img, {.invert = true});
    REQUIRE(inv.has_value());
    CHECK(inv->at(63, 3) > 0.99F);

    const auto small = density_from_image(gradient_image(1000, 500), {.max_dimension = 100});
    REQUIRE(small.has_value());
    CHECK(small->width == 100);
    CHECK(small->height == 50);
    CHECK(small->scale == 10.0);

    CHECK_FALSE(density_from_image(image::Image{}).has_value());
    CHECK_FALSE(density_from_image(img, {.gamma = 0.0}).has_value());
}

TEST_CASE("stippling : deterministe pour une graine, different pour une autre") {
    const auto d = flat(60, 60, 1.0F);
    const StippleOptions o{.point_count = 200, .iterations = 5, .seed = 5};
    const auto a = stipple_points(d, o);
    const auto b = stipple_points(d, o);
    const auto c = stipple_points(d, {.point_count = 200, .iterations = 5, .seed = 6});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(c.has_value());
    REQUIRE(a->points.size() == 200);
    for (std::size_t i = 0; i < a->points.size(); ++i) {
        CHECK(a->points[i].x == b->points[i].x);
        CHECK(a->points[i].y == b->points[i].y);
    }
    bool differs = false;
    for (std::size_t i = 0; i < a->points.size(); ++i) {
        differs = differs || a->points[i].x != c->points[i].x;
    }
    CHECK(differs);
}

TEST_CASE("stippling : Lloyd regularise le semis (bruit bleu)") {
    const auto d = flat(100, 100, 1.0F);
    const auto raw = stipple_points(d, {.point_count = 300, .iterations = 0, .seed = 2});
    const auto relaxed = stipple_points(d, {.point_count = 300, .iterations = 40, .seed = 2});
    REQUIRE(raw.has_value());
    REQUIRE(relaxed.has_value());
    // Les points ne se chevauchent plus et l'espacement augmente nettement.
    CHECK(nearest_neighbor_min(relaxed->points) > 2.0 * nearest_neighbor_min(raw->points));
    CHECK(nearest_neighbor_mean(relaxed->points) > 1.15 * nearest_neighbor_mean(raw->points));
    // Tous les points restent dans la carte.
    for (const auto& p : relaxed->points) {
        CHECK(p.x >= 0.0);
        CHECK(p.x <= 100.0);
        CHECK(p.y >= 0.0);
        CHECK(p.y <= 100.0);
    }
}

TEST_CASE("stippling : la densite des points suit la densite de la carte") {
    // Moitie gauche pleine, tiers de la moitie droite a 25 %, reste vide.
    DensityMap d = flat(120, 60, 0.0F);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 60; ++x) {
            d.values[static_cast<std::size_t>(y) * 120 + x] = 1.0F;
        }
        for (int x = 60; x < 90; ++x) {
            d.values[static_cast<std::size_t>(y) * 120 + x] = 0.25F;
        }
    }
    const auto r = stipple_points(d, {.point_count = 1500, .iterations = 30, .seed = 9});
    REQUIRE(r.has_value());
    int dark = 0;
    int mid = 0;
    int empty = 0;
    for (const auto& p : r->points) {
        (p.x < 60 ? dark : p.x < 90 ? mid : empty)++;
    }
    // Aire x densite : 60*60*1 = 3600 contre 30*60*0,25 = 450 -> rapport 8.
    CHECK(empty <= 15);
    CHECK(dark > 5 * mid);
    CHECK(dark + mid >= 1485);
}

TEST_CASE("stippling : entrees invalides et image vide") {
    CHECK_FALSE(stipple_points(flat(10, 10, 0.0F), {}).has_value());
    CHECK_FALSE(stipple_points(flat(10, 10, 1.0F), {.point_count = 0}).has_value());
    CHECK_FALSE(stipple_points(flat(10, 10, 1.0F), {.iterations = 500}).has_value());
    DensityMap bad = flat(10, 10, 1.0F);
    bad.values.pop_back();
    CHECK_FALSE(stipple_points(bad, {}).has_value());
}

TEST_CASE("chemin de couture : continu, ordre TSP, coupe aux liaisons trop longues") {
    // Deux amas separes par un grand vide : avec max_link, deux polylignes ;
    // sans, une seule.
    DensityMap d = flat(200, 40, 0.0F);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 30; ++x) {
            d.values[static_cast<std::size_t>(y) * 200 + x] = 1.0F;
            d.values[static_cast<std::size_t>(y) * 200 + 170 + x] = 1.0F;
        }
    }
    const auto s = stipple_points(d, {.point_count = 400, .iterations = 20, .seed = 4});
    REQUIRE(s.has_value());

    PathOptions cut;
    cut.mm_per_px = Millimeters{0.2646};
    cut.max_link = Micrometers{8'000};
    const auto split = build_stitch_path(*s, d, 200, 40, cut, {});
    PathOptions whole = cut;
    whole.max_link = Micrometers{0};
    const auto joined = build_stitch_path(*s, d, 200, 40, whole, {});

    CHECK(joined.polylines.size() == 1);
    CHECK(split.polylines.size() >= 2);
    // Le tour optimise est plus court que le tour de Hilbert.
    CHECK(joined.tour_length_px < joined.initial_tour_length_px);
    // Aucune liaison de la version coupee ne depasse la limite.
    for (const auto& line : split.polylines) {
        for (std::size_t i = 1; i < line.size(); ++i) {
            CHECK(length_um(line[i] - line[i - 1]) <= 8'000.0 + 2.0);
        }
    }
    // Tous les points sont conserves (aucun perdu hormis fusions de doublons).
    std::size_t pts = 0;
    for (const auto& line : joined.polylines) {
        pts += line.size();
    }
    CHECK(pts >= 380);
    // Y vers le haut, origine au centre : le premier amas est a gauche (x < 0).
    const auto& first = joined.polylines.front();
    const auto leftmost = std::min_element(
        first.begin(), first.end(), [](Vec2um a, Vec2um b) { return a.x.value < b.x.value; });
    CHECK(leftmost->x.value < 0);
}

TEST_CASE("stippling d'une image : de bout en bout, plus de points a gauche (sombre)") {
    const auto img = gradient_image(160, 80);
    const auto d = density_from_image(img);
    REQUIRE(d.has_value());
    const auto s = stipple_points(*d, {.point_count = 800, .iterations = 25, .seed = 1});
    REQUIRE(s.has_value());
    int left = 0;
    for (const auto& p : s->points) {
        left += p.x < d->width / 2.0 ? 1 : 0;
    }
    CHECK(left > 800 * 0.6);
}
