// SPDX-License-Identifier: Apache-2.0
//
// Équivalence de `vectorize_region` (masque restreint à la boîte englobante
// de la région, audit de performance 2026-09) avec l'implémentation
// d'origine (masque pleine image), recopiée ici comme référence.
#include <catch2/catch_test_macros.hpp>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "openstitch/geometry/clean.hpp"
#include "openstitch/geometry/simplify.hpp"
#include "openstitch/vectorization/vectorize.hpp"

using namespace openstitch;

namespace {

geometry::PathNode ref_to_model(const cv::Point& px, int width, int height, double mmPerPx) {
    const double xMm = (static_cast<double>(px.x) + 0.5 - width / 2.0) * mmPerPx;
    const double yMm = (height / 2.0 - (static_cast<double>(px.y) + 0.5)) * mmPerPx;
    return geometry::PathNode{
        Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(xMm * 1000.0))},
               Micrometers{static_cast<std::int32_t>(std::lround(yMm * 1000.0))}},
        geometry::NodeType::Corner, std::nullopt, std::nullopt};
}

// Implémentation d'origine (avant l'audit), sans les contrôles d'entrée.
Result<std::vector<geometry::PathSet>>
reference_vectorize(const segmentation::Segmentation& seg, RegionId id,
                    const vectorization::VectorizeOptions& options) {
    cv::Mat mask(seg.height, seg.width, CV_8U, cv::Scalar(0));
    const auto label = static_cast<std::uint32_t>(id.value);
    for (int y = 0; y < seg.height; ++y) {
        for (int x = 0; x < seg.width; ++x) {
            if (seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(seg.width) +
                           static_cast<std::size_t>(x)] == label) {
                mask.at<std::uint8_t>(y, x) = 255;
            }
        }
    }
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
        return fail(ErrorCategory::Internal, "La région n'a produit aucun contour");
    }
    std::vector<geometry::Path> raw;
    for (const auto& contour : contours) {
        if (contour.size() < 3) {
            continue;
        }
        geometry::Path path;
        path.closed = true;
        for (const cv::Point& pt : contour) {
            path.nodes.push_back(ref_to_model(pt, seg.width, seg.height, options.mm_per_px.value));
        }
        double perim = 0.0;
        for (std::size_t i = 0; i < path.nodes.size(); ++i) {
            perim += length_um(path.nodes[(i + 1) % path.nodes.size()].pos - path.nodes[i].pos);
        }
        const auto adaptiveTol = Micrometers{static_cast<std::int32_t>(
            std::min<double>(static_cast<double>(options.simplify_tolerance.value), perim / 16.0))};
        raw.push_back(geometry::simplify(path, adaptiveTol));
    }
    auto sets = geometry::clean_to_path_sets(raw);
    if (!sets) {
        return std::unexpected(sets.error());
    }
    if (sets->empty()) {
        return fail(ErrorCategory::OperationImpossible, "trop petite");
    }
    return sets;
}

// Segmentation synthétique : quelques labels en taches (disques, bandes,
// anneaux) + bruit ; les régions touchent souvent les bords de l'image.
segmentation::Segmentation noisy_segmentation(int w, int h, std::uint32_t seed) {
    segmentation::Segmentation seg;
    seg.width = w;
    seg.height = h;
    seg.labels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    std::uint32_t state = seed;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    constexpr std::uint32_t kLabels = 6;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int ring = static_cast<int>(std::hypot(x - w / 2, y - h / 3)) / 6;
            std::uint32_t l = static_cast<std::uint32_t>(((x / 9) + ring) % kLabels) + 1;
            if (next() % 11 == 0) {
                l = next() % (kLabels + 1); // bruit, y compris fond (0)
            }
            seg.labels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                       static_cast<std::size_t>(x)] = l;
        }
    }
    for (std::uint32_t l = 1; l <= kLabels; ++l) {
        segmentation::Region r;
        r.id = RegionId{l};
        r.pixel_count =
            static_cast<std::size_t>(std::count(seg.labels.begin(), seg.labels.end(), l));
        seg.region_slots.push_back(r);
    }
    return seg;
}

void check_same(const Result<std::vector<geometry::PathSet>>& a,
                const Result<std::vector<geometry::PathSet>>& b) {
    REQUIRE(a.has_value() == b.has_value());
    if (!a) {
        CHECK(a.error().message == b.error().message);
        return;
    }
    REQUIRE(a->size() == b->size());
    for (std::size_t i = 0; i < a->size(); ++i) {
        const auto& p = (*a)[i];
        const auto& q = (*b)[i];
        REQUIRE(p.outer.nodes.size() == q.outer.nodes.size());
        for (std::size_t k = 0; k < p.outer.nodes.size(); ++k) {
            CHECK(p.outer.nodes[k].pos == q.outer.nodes[k].pos);
        }
        REQUIRE(p.holes.size() == q.holes.size());
        for (std::size_t hIdx = 0; hIdx < p.holes.size(); ++hIdx) {
            REQUIRE(p.holes[hIdx].nodes.size() == q.holes[hIdx].nodes.size());
            for (std::size_t k = 0; k < p.holes[hIdx].nodes.size(); ++k) {
                CHECK(p.holes[hIdx].nodes[k].pos == q.holes[hIdx].nodes[k].pos);
            }
        }
    }
}

} // namespace

TEST_CASE("vectorize_region (boite englobante) : identique a l'implementation de reference",
          "[vectorization][perf]") {
    for (const auto [w, h, seed] :
         {std::tuple{40, 30, 3u}, std::tuple{97, 61, 11u}, std::tuple{150, 120, 29u}}) {
        const auto seg = noisy_segmentation(w, h, seed);
        for (const double mmPerPx : {25.4 / 96.0, 0.1}) {
            const vectorization::VectorizeOptions opts{Millimeters{mmPerPx}, Micrometers{200}};
            for (const auto& slot : seg.region_slots) {
                INFO("image " << w << "x" << h << " label " << slot->id.value << " mm/px "
                              << mmPerPx);
                check_same(vectorization::vectorize_region(seg, slot->id, opts),
                           reference_vectorize(seg, slot->id, opts));
            }
        }
    }
}

TEST_CASE("vectorize_region (boite englobante) : region collee aux quatre bords de l'image",
          "[vectorization][perf]") {
    // Cadre plein de 1 px sur tout le pourtour + tache centrale : la boîte
    // englobante est l'image entière, la marge sort de l'image.
    segmentation::Segmentation seg;
    seg.width = 20;
    seg.height = 16;
    seg.labels.assign(20 * 16, 0);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 20; ++x) {
            const bool frame = x < 2 || y < 2 || x >= 18 || y >= 14;
            const bool blob = x >= 7 && x < 13 && y >= 6 && y < 10;
            seg.labels[static_cast<std::size_t>(y) * 20 + static_cast<std::size_t>(x)] =
                frame || blob ? 1u : 2u;
        }
    }
    for (std::uint32_t l = 1; l <= 2; ++l) {
        segmentation::Region r;
        r.id = RegionId{l};
        seg.region_slots.push_back(r);
    }
    const vectorization::VectorizeOptions opts{Millimeters{0.5}, Micrometers{200}};
    check_same(vectorization::vectorize_region(seg, RegionId{1}, opts),
               reference_vectorize(seg, RegionId{1}, opts));
    check_same(vectorization::vectorize_region(seg, RegionId{2}, opts),
               reference_vectorize(seg, RegionId{2}, opts));
}
