// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <stop_token>

#include "openstitch/geometry/polyline.hpp"
#include "openstitch/vectorization/vectorize.hpp"

using namespace openstitch;
using namespace openstitch::vectorization;

namespace {

void set_px(image::Image& img, int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::uint8_t* px =
        img.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) +
                           static_cast<std::size_t>(x)) *
                              4;
    px[0] = r;
    px[1] = g;
    px[2] = b;
    px[3] = 255;
}

// Image 20x20 : anneau rouge (carre exterieur 16x16, trou 6x6) sur fond blanc.
image::Image ring_image() {
    image::Image img;
    img.width = 20;
    img.height = 20;
    img.rgba.assign(20 * 20 * 4, 0);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            const bool inOuter = (x >= 2 && x < 18 && y >= 2 && y < 18);
            const bool inHole = (x >= 7 && x < 13 && y >= 7 && y < 13);
            if (inOuter && !inHole) {
                set_px(img, x, y, 220, 30, 30);
            } else {
                set_px(img, x, y, 250, 250, 250);
            }
        }
    }
    return img;
}

} // namespace

TEST_CASE("anneau -> un PathSet avec exterieur et un trou") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());

    // La region rouge est celle du pixel (2,2).
    const auto red = segmentation::region_at(*seg, 2, 2);
    REQUIRE(red.has_value());

    const VectorizeOptions options{.mm_per_px = Millimeters{1.0},
                                   .simplify_tolerance = Micrometers{200}};
    const auto sets = vectorize_region(*seg, *red, options);
    REQUIRE(sets.has_value());
    REQUIRE(sets->size() == 1);
    CHECK((*sets)[0].holes.size() == 1);

    // Aire exterieure ~ 16x16 mm = 256 mm² (contour par centres de pixels :
    // ~15x15 = 225 mm²). Trou nominal 6x6 = 36 mm² : le contour d'un trou
    // (RETR_CCOMP) longe les pixels adjacents, soit ~7x7 = 49 mm².
    const double outerArea = std::abs(geometry::signed_area_um2((*sets)[0].outer)) / 1e6;
    const double holeArea = std::abs(geometry::signed_area_um2((*sets)[0].holes[0])) / 1e6;
    CHECK(outerArea > 180.0);
    CHECK(outerArea < 260.0);
    CHECK(holeArea > 20.0);
    CHECK(holeArea < 55.0);
    CHECK(holeArea < outerArea);

    // La simplification reduit le rectangle a une poignee de noeuds.
    CHECK((*sets)[0].outer.nodes.size() <= 8);
}

TEST_CASE("coordonnees physiques : centre de l'image = origine, Y vers le haut") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto red = segmentation::region_at(*seg, 2, 2);
    REQUIRE(red.has_value());

    const auto sets = vectorize_region(
        *seg, *red, {.mm_per_px = Millimeters{1.0}, .simplify_tolerance = Micrometers{200}});
    REQUIRE(sets.has_value());

    // L'anneau est centre : ses bornes doivent etre symetriques autour de 0,
    // et contenues dans la moitie de l'image (10 mm).
    std::int32_t minX = INT32_MAX, maxX = INT32_MIN, minY = INT32_MAX, maxY = INT32_MIN;
    for (const auto& n : (*sets)[0].outer.nodes) {
        minX = std::min(minX, n.pos.x.value);
        maxX = std::max(maxX, n.pos.x.value);
        minY = std::min(minY, n.pos.y.value);
        maxY = std::max(maxY, n.pos.y.value);
    }
    CHECK(minX == -maxX);
    CHECK(minY == -maxY);
    CHECK(maxX < 10'000);
    CHECK(maxY < 10'000);
}

TEST_CASE("region inexistante -> erreur interne propre") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    CHECK_FALSE(vectorize_region(*seg, RegionId{9999}, {}).has_value());
}

TEST_CASE("deux morceaux de meme region... deviennent deux PathSet") {
    // Deux carres rouges disjoints de la MEME couleur : deux regions
    // distinctes, chacune vectorisee separement produit un PathSet.
    image::Image img;
    img.width = 16;
    img.height = 8;
    img.rgba.assign(16 * 8 * 4, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            const bool left = (x >= 1 && x < 6 && y >= 1 && y < 7);
            const bool right = (x >= 10 && x < 15 && y >= 1 && y < 7);
            set_px(img, x, y, (left || right) ? 220 : 250, (left || right) ? 30 : 250,
                   (left || right) ? 30 : 250);
        }
    }
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto a = segmentation::region_at(*seg, 2, 2);
    const auto b = segmentation::region_at(*seg, 12, 2);
    REQUIRE((a && b));
    CHECK(*a != *b); // deux regions distinctes malgre la meme couleur (§4.3)

    const auto setsA = vectorize_region(*seg, *a, {.mm_per_px = Millimeters{1.0}});
    REQUIRE(setsA.has_value());
    CHECK(setsA->size() == 1);
}

namespace {

// Disque rouge de rayon `r` px (anneau si `hole` > 0) sur fond blanc.
image::Image disc_image(int size, int r, int hole) {
    image::Image img;
    img.width = size;
    img.height = size;
    img.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);
    const double c = (size - 1) / 2.0;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double d = std::hypot(x - c, y - c);
            if (d <= r && d >= hole) {
                set_px(img, x, y, 220, 30, 30);
            } else {
                set_px(img, x, y, 250, 250, 250);
            }
        }
    }
    return img;
}

std::size_t total_nodes(const std::vector<geometry::PathSet>& sets) {
    std::size_t n = 0;
    for (const auto& s : sets) {
        n += s.outer.nodes.size();
        for (const auto& h : s.holes) {
            n += h.nodes.size();
        }
    }
    return n;
}

} // namespace

TEST_CASE("courbes : un disque tient en au moins 3 fois moins de noeuds qu'en polygone") {
    const auto img = disc_image(240, 100, 0);
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto red = segmentation::region_at(*seg, 120, 120);
    REQUIRE(red.has_value());

    constexpr double kMmPerPx = 0.2646; // 96 dpi
    const Micrometers tol{400};         // ~1,5 px
    const VectorizeOptions poly{.mm_per_px = Millimeters{kMmPerPx}, .simplify_tolerance = tol};
    VectorizeOptions curved = poly;
    curved.curve_tolerance = tol;

    const auto a = vectorize_region(*seg, *red, poly);
    const auto b = vectorize_region(*seg, *red, curved);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(b->size() == 1);

    // Critere de la feuille de route : nombre de noeuds divise par >= 3.
    CHECK(total_nodes(*b) * 3 <= total_nodes(*a));
    // Les courbes sont bien presentes (tangentes de Bezier).
    bool any_curve = false;
    for (const auto& nd : (*b)[0].outer.nodes) {
        any_curve = any_curve || nd.tan_out.has_value();
    }
    CHECK(any_curve);
    // Meme emprise et meme aire qu'en polygone (a quelques % pres).
    const double areaPoly = std::abs(geometry::signed_area_um2((*a)[0].outer));
    const geometry::Polyline flat = geometry::flatten((*b)[0].outer, Micrometers{20});
    geometry::Path flatPath;
    for (const Vec2um& p : flat.points) {
        flatPath.nodes.push_back({p, geometry::NodeType::Corner, std::nullopt, std::nullopt});
    }
    const double areaCurve = std::abs(geometry::signed_area_um2(flatPath));
    CHECK(areaCurve == Catch::Approx(areaPoly).epsilon(0.03));
}

TEST_CASE("courbes : un anneau garde son trou, resultat deterministe, defaut inchange") {
    const auto img = disc_image(240, 100, 50);
    const auto seg = segmentation::segment(img, {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto red = segmentation::region_at(*seg, 120, 20);
    REQUIRE(red.has_value());

    VectorizeOptions curved{.mm_per_px = Millimeters{0.2646},
                            .simplify_tolerance = Micrometers{400}};
    curved.curve_tolerance = Micrometers{400};
    const auto a = vectorize_region(*seg, *red, curved);
    const auto b = vectorize_region(*seg, *red, curved);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->size() == 1);
    CHECK((*a)[0].holes.size() == 1);
    CHECK(*a == *b);

    // curve_tolerance = 0 (defaut) : strictement le comportement polygonal historique.
    const VectorizeOptions legacy{.mm_per_px = Millimeters{0.2646},
                                  .simplify_tolerance = Micrometers{400}};
    const auto p = vectorize_region(*seg, *red, legacy);
    REQUIRE(p.has_value());
    for (const auto& nd : (*p)[0].outer.nodes) {
        CHECK_FALSE(nd.tan_out.has_value());
    }
}

TEST_CASE("vectorize_region : annulation avant calcul sans geometrie partielle", "[task]") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    const auto red = segmentation::region_at(*seg, 2, 2);
    REQUIRE(red.has_value());

    std::stop_source stop;
    stop.request_stop();
    const TaskContext task{.stop = stop.get_token()};
    const auto result = vectorize_region(*seg, *red, {}, task);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().category == ErrorCategory::Cancelled);
}

TEST_CASE("vectorize_regions : annulation batch et progres deterministe", "[task]") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    std::vector<RegionId> ids;
    for (const auto& slot : seg->region_slots) {
        if (slot) {
            ids.push_back(slot->id);
        }
    }
    REQUIRE(ids.size() >= 2);
    const VectorizeOptions options{.mm_per_px = Millimeters{1.0},
                                   .simplify_tolerance = Micrometers{200}};
    std::stop_source stop;
    stop.request_stop();
    std::atomic<std::size_t> reports{0};
    std::atomic<std::size_t> totalReported{0};
    TaskContext cancelled;
    cancelled.stop = stop.get_token();
    cancelled.progress = [&](std::size_t, std::size_t total) {
        ++reports;
        totalReported = total;
    };
    const auto batch = vectorize_regions(*seg, ids, options, cancelled);
    REQUIRE(batch.size() == ids.size());
    for (const auto& result : batch) {
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().category == ErrorCategory::Cancelled);
    }
    CHECK(reports.load() == ids.size() + 1); // initialisation + une fin par region
    CHECK(totalReported.load() == ids.size());

    std::atomic<std::size_t> completed{0};
    TaskContext progress;
    progress.progress = [&](std::size_t done, std::size_t total) {
        if (done > 0 && total == ids.size()) {
            ++completed;
        }
    };
    const auto withProgress = vectorize_regions(*seg, ids, options, progress);
    const auto legacy = vectorize_regions(*seg, ids, options);
    REQUIRE(withProgress.size() == legacy.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        REQUIRE(withProgress[i].has_value() == legacy[i].has_value());
        if (legacy[i]) {
            CHECK(*withProgress[i] == *legacy[i]);
        }
    }
    CHECK(completed.load() == ids.size());
}

TEST_CASE("vectorize_regions : meme resultat que la boucle sequentielle, dans l'ordre demande") {
    const auto seg = segmentation::segment(ring_image(), {.max_colors = 2, .min_region_px = 1});
    REQUIRE(seg.has_value());
    std::vector<RegionId> ids;
    for (const auto& slot : seg->region_slots) {
        if (slot) {
            ids.push_back(slot->id);
        }
    }
    REQUIRE(ids.size() >= 2);
    // Une region inexistante au milieu : son erreur reste a SA place.
    ids.insert(ids.begin() + 1, RegionId{987'654});
    const VectorizeOptions options{.mm_per_px = Millimeters{1.0},
                                   .simplify_tolerance = Micrometers{200}};
    const auto batch = vectorize_regions(*seg, ids, options);
    REQUIRE(batch.size() == ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto one = vectorize_region(*seg, ids[i], options);
        REQUIRE(batch[i].has_value() == one.has_value());
        if (one) {
            CHECK(*batch[i] == *one);
        }
    }
    CHECK_FALSE(batch[1].has_value());
    CHECK(vectorize_regions(*seg, {}, options).empty());
}

namespace {

// Carre axis-aligned [x0,x1]x[y0,y1] en mm.
geometry::PathSet square_mm(double x0, double y0, double x1, double y1) {
    const auto node = [](double x, double y) {
        return geometry::PathNode{
            Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(x * 1000.0))},
                   Micrometers{static_cast<std::int32_t>(std::lround(y * 1000.0))}},
            geometry::NodeType::Corner, std::nullopt, std::nullopt};
    };
    geometry::PathSet set;
    set.outer.closed = true;
    set.outer.nodes = {node(x0, y0), node(x1, y0), node(x1, y1), node(x0, y1)};
    return set;
}

} // namespace

TEST_CASE("is_overlap_grown_contour : bande fine oui, fusion ou retouche non") {
    const std::vector<geometry::PathSet> raw{square_mm(0, 0, 20, 20)};

    // Identique ou quasi : rien a restaurer.
    CHECK_FALSE(is_overlap_grown_contour(raw, raw));
    // Recouvrement des voisins : 0,4 mm de plus tout autour -> contour brut a restaurer.
    CHECK(is_overlap_grown_contour({square_mm(-0.4, -0.4, 20.4, 20.4)}, raw));
    // Fusion de deux zones : la region d'origine + une zone voisine de meme taille. Le contour
    // fusionne est voulu, il ne doit jamais redevenir la forme premiere.
    CHECK_FALSE(is_overlap_grown_contour({square_mm(0, 0, 40, 20)}, raw));
    // Meme chose avec une zone voisine plus petite (surface ajoutee > 3 %).
    CHECK_FALSE(is_overlap_grown_contour({square_mm(0, 0, 26, 20)}, raw));
    // Agrandissement homothetique par l'utilisateur.
    CHECK_FALSE(is_overlap_grown_contour({square_mm(-5, -5, 25, 25)}, raw));
    // Forme retouchee plus petite : jamais restauree.
    CHECK_FALSE(is_overlap_grown_contour({square_mm(2, 2, 18, 18)}, raw));
}
