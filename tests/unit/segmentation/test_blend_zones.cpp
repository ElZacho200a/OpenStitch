// SPDX-License-Identifier: Apache-2.0
// Zones de fondu (2026-11-17) : un dégradé lent reste une zone, un bord net sépare.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <tuple>

#include "openstitch/segmentation/blend_zones.hpp"

using namespace openstitch;

namespace {

// 200 × 100 px à 0,5 mm/px. Moitié gauche : dégradé lent de ciel, gris bleu -> bleu pâle de haut
// en bas (≈ 0,3 ΔE par pixel, gradient Sobel ≈ 0,5 < seuil 2,5). Moitié droite : aplat orange
// (bord net au milieu).
image::Image gradient_and_flat() {
    image::Image img;
    img.width = 200;
    img.height = 100;
    img.rgba.resize(static_cast<std::size_t>(img.width * img.height) * 4);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            auto* p = &img.rgba[static_cast<std::size_t>(y * img.width + x) * 4];
            if (x < 100) {
                p[0] = static_cast<std::uint8_t>(120 + y * 80 / 99);
                p[1] = static_cast<std::uint8_t>(140 + y * 60 / 99);
                p[2] = static_cast<std::uint8_t>(200 + y * 30 / 99);
            } else {
                p[0] = 230;
                p[1] = 120;
                p[2] = 30;
            }
            p[3] = 255;
        }
    }
    return img;
}

std::set<std::uint32_t> labels_in(const segmentation::Segmentation& seg, int x0, int x1) {
    std::set<std::uint32_t> out;
    for (int y = 0; y < seg.height; ++y) {
        for (int x = x0; x < x1; ++x) {
            out.insert(seg.labels[static_cast<std::size_t>(y * seg.width + x)]);
        }
    }
    return out;
}

} // namespace

TEST_CASE("blend zones: a slow gradient stays one zone, a sharp edge splits",
          "[segmentation][blend_zones]") {
    const auto img = gradient_and_flat();
    auto seg = segmentation::segment_blend_zones(img, Millimeters{0.5});
    REQUIRE(seg);
    CHECK(seg->width == 200);
    CHECK(seg->height == 100);
    CHECK(seg->region_count() == 2);
    // Loin du bord net (le flou de 0,75 mm l'étale sur quelques pixels).
    const auto left = labels_in(*seg, 0, 95);
    const auto right = labels_in(*seg, 105, 200);
    REQUIRE(left.size() == 1);
    REQUIRE(right.size() == 1);
    CHECK(*left.begin() != *right.begin());
    CHECK(*left.begin() != 0u);
    // Couleur moyenne de la zone droite : l'aplat orange.
    const auto* r = seg->find(RegionId{*right.begin()});
    REQUIRE(r != nullptr);
    CHECK(r->rgb[0] > 200);
    CHECK(r->rgb[2] < 60);
    // Déterminisme.
    const auto again = segmentation::segment_blend_zones(img, Millimeters{0.5});
    REQUIRE(again);
    CHECK(again->labels == seg->labels);
}

TEST_CASE("blend zones: finer image is reduced to the working step",
          "[segmentation][blend_zones]") {
    // Même motif à 0,25 mm/px (400 × 200 px) : mêmes zones, étiquettes à la taille d'origine.
    const auto coarse = gradient_and_flat();
    image::Image fine;
    fine.width = 400;
    fine.height = 200;
    fine.rgba.resize(static_cast<std::size_t>(fine.width * fine.height) * 4);
    for (int y = 0; y < fine.height; ++y) {
        for (int x = 0; x < fine.width; ++x) {
            for (int c = 0; c < 4; ++c) {
                fine.rgba[static_cast<std::size_t>(y * fine.width + x) * 4 + c] =
                    coarse.rgba[static_cast<std::size_t>((y / 2) * coarse.width + x / 2) * 4 + c];
            }
        }
    }
    auto seg = segmentation::segment_blend_zones(fine, Millimeters{0.25});
    REQUIRE(seg);
    CHECK(seg->width == 400);
    CHECK(seg->labels.size() == 400u * 200u);
    CHECK(seg->region_count() == 2);
}

TEST_CASE("blend zones: refuses an empty image", "[segmentation][blend_zones]") {
    CHECK_FALSE(segmentation::segment_blend_zones(image::Image{}, Millimeters{0.5}));
}

namespace {

struct ThreadCountOverride {
    explicit ThreadCountOverride(const char* value) {
#ifdef _WIN32
        _putenv_s("OPENSTITCH_THREADS", value);
#else
        setenv("OPENSTITCH_THREADS", value, 1);
#endif
    }
    ~ThreadCountOverride() {
#ifdef _WIN32
        _putenv_s("OPENSTITCH_THREADS", "");
#else
        unsetenv("OPENSTITCH_THREADS");
#endif
    }
};

// Tailles qui ne tombent pas juste sur le pas de travail, transparence partielle, plusieurs
// zones : exerce les bornes de bandes de la reduction et l'ordre des emplacements.
image::Image odd_sized_with_holes(int width, int height) {
    image::Image img;
    img.width = width;
    img.height = height;
    img.rgba.resize(static_cast<std::size_t>(width * height) * 4);
    std::uint32_t rng = 777;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto* p = &img.rgba[static_cast<std::size_t>(y * width + x) * 4];
            rng = rng * 1664525U + 1013904223U;
            const int grain = static_cast<int>((rng >> 24) % 5) - 2;
            const bool left = x * 5 < width * 2;
            p[0] = static_cast<std::uint8_t>(
                std::clamp((left ? 90 + y * 70 / height : 220) + grain, 0, 255));
            p[1] = static_cast<std::uint8_t>(std::clamp((left ? 130 : 110) + grain, 0, 255));
            p[2] = static_cast<std::uint8_t>(std::clamp((left ? 200 : 40) + grain, 0, 255));
            const bool hole =
                (x - width / 2) * (x - width / 2) + (y - height / 2) * (y - height / 2) <
                (height * height) / 36;
            p[3] = hole ? 0 : 255;
        }
    }
    return img;
}

// Empreinte FNV-1a des etiquettes et des regions. Les valeurs figees ci-dessous ont ete calculees
// avec l'implementation d'origine (avant R02 lot 2) : toute optimisation doit les conserver.
std::uint64_t fingerprint(const segmentation::Segmentation& seg) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto l : seg.labels) {
        hash = (hash ^ l) * 1099511628211ULL;
    }
    for (const auto& slot : seg.region_slots) {
        for (const auto c : slot->rgb) {
            hash = (hash ^ c) * 1099511628211ULL;
        }
        hash = (hash ^ slot->pixel_count) * 1099511628211ULL;
    }
    return hash;
}

} // namespace

TEST_CASE("blend zones: same result with one thread and with several",
          "[segmentation][blend_zones]") {
    for (const auto& [width, height, mm, golden] :
         {std::tuple{331, 207, 0.1, 0xe970f30ed33428e1ULL},
          std::tuple{640, 480, 0.25, 0x0a6009d67c25abbaULL},
          std::tuple{97, 61, 0.5, 0xfa33906b1e7c0da8ULL}}) {
        const auto img = odd_sized_with_holes(width, height);
        Result<segmentation::Segmentation> single = fail(ErrorCategory::Internal, "non calcule");
        Result<segmentation::Segmentation> many = fail(ErrorCategory::Internal, "non calcule");
        {
            const ThreadCountOverride one("1");
            single = segmentation::segment_blend_zones(img, Millimeters{mm});
        }
        {
            const ThreadCountOverride eight("8");
            many = segmentation::segment_blend_zones(img, Millimeters{mm});
        }
        REQUIRE(single.has_value());
        REQUIRE(many.has_value());
        CHECK(fingerprint(*single) == golden);
        CHECK(fingerprint(*many) == golden);
        CHECK(single->labels == many->labels);
        REQUIRE(single->region_slots.size() == many->region_slots.size());
        for (std::size_t s = 0; s < single->region_slots.size(); ++s) {
            REQUIRE(single->region_slots[s].has_value() == many->region_slots[s].has_value());
            if (single->region_slots[s]) {
                CHECK(single->region_slots[s]->rgb == many->region_slots[s]->rgb);
                CHECK(single->region_slots[s]->pixel_count == many->region_slots[s]->pixel_count);
            }
        }
    }
}

// Banc (cache :`test_segmentation "[.perf_blend_zones]"`, Release). Peinture synthetique
// 2400 x 1800 a 0,1 mm/px : dégradés lents, bruit de toile, trois aplats. Affiche durées et
// empreinte des étiquettes : l'empreinte doit rester identique avant/après toute optimisation.
TEST_CASE("blend zones: bench on a large painting", "[.perf_blend_zones]") {
    image::Image img;
    img.width = 2400;
    img.height = 1800;
    img.rgba.resize(static_cast<std::size_t>(img.width * img.height) * 4);
    std::uint32_t rng = 12345;
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            auto* p = &img.rgba[static_cast<std::size_t>(y * img.width + x) * 4];
            rng = rng * 1664525U + 1013904223U;
            const int grain = static_cast<int>((rng >> 24) % 7) - 3;
            const int band = x * 3 / img.width;
            const int base[3][3] = {{120, 140, 200}, {230, 120, 30}, {40, 160, 90}};
            for (int c = 0; c < 3; ++c) {
                const int slope = band == 1 ? 0 : (y * 60 / img.height);
                p[c] = static_cast<std::uint8_t>(std::clamp(base[band][c] + slope + grain, 0, 255));
            }
            p[3] = 255;
        }
    }
    std::uint64_t hash = 1469598103934665603ULL;
    double best = 1e30;
    for (int rep = 0; rep < 5; ++rep) {
        const auto t0 = std::chrono::steady_clock::now();
        auto seg = segmentation::segment_blend_zones(img, Millimeters{0.1});
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                .count();
        REQUIRE(seg);
        best = std::min(best, ms);
        std::printf("[perf_blend_zones] rep %d : %.1f ms\n", rep, ms);
        hash = 1469598103934665603ULL;
        for (const auto l : seg->labels) {
            hash = (hash ^ l) * 1099511628211ULL;
        }
        for (const auto& slot : seg->region_slots) {
            for (const auto c : slot->rgb) {
                hash = (hash ^ c) * 1099511628211ULL;
            }
            hash = (hash ^ slot->pixel_count) * 1099511628211ULL;
        }
        std::printf("[perf_blend_zones]   regions %zu empreinte %016llx\n", seg->region_count(),
                    static_cast<unsigned long long>(hash));
    }
    std::printf("[perf_blend_zones] meilleur : %.1f ms\n", best);
}
