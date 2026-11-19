// SPDX-License-Identifier: Apache-2.0
// VEC-ADAPT-01 lot A : banc des tolérances de vectorisation (caché, lancé à la demande).
//
//   test_project_ops "[.vec_adapt_bench]"
//   OPENSTITCH_VEC_BENCH_OSP=D:\...\MonetGasque.osp test_project_ops "[.vec_adapt_bench]"
//
// Pour chaque réglage : nœuds, écart de Hausdorff (approché, bidirectionnel) et écart d'aire par
// rapport à la référence polygonale la plus fine (0,01 mm), trous conservés, temps. Les « petites »
// régions (aire < 25 mm²) sont isolées : ce sont les fleurs et pétales du cas pilote.
// Mesure seulement : aucune assertion de qualité, le banc sert à décider du lot B.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <numbers>
#include <string>
#include <vector>

#include "openstitch/document/project.hpp"
#include "openstitch/geometry/curve_fit.hpp"
#include "openstitch/geometry/path.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/project_ops/operations.hpp"
#include "openstitch/project_ops/revectorize.hpp"

using namespace openstitch;

namespace {

// 400 x 400 px à 0,05 mm/px (20 x 20 mm) : disque, petite fleur à six pétales, pointe fine,
// anneau à trou minuscule, sur fond blanc. Couleurs franches pour une segmentation exacte.
document::Project synthetic_project() {
    document::Project project;
    image::Image img;
    img.width = 400;
    img.height = 400;
    img.rgba.assign(static_cast<std::size_t>(img.width * img.height * 4), 255);
    auto put = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
        const auto i = static_cast<std::size_t>((y * img.width + x) * 4);
        img.rgba[i] = r;
        img.rgba[i + 1] = g;
        img.rgba[i + 2] = b;
    };
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const double dx1 = x - 100.0, dy1 = y - 100.0;
            if (std::hypot(dx1, dy1) < 70.0) {
                put(x, y, 220, 30, 30); // grand disque
            }
            const double dx2 = x - 300.0, dy2 = y - 100.0;
            const double r2 = std::hypot(dx2, dy2);
            const double th = std::atan2(dy2, dx2);
            if (r2 < 30.0 * (0.45 + 0.55 * std::abs(std::cos(3.0 * th)))) {
                put(x, y, 30, 30, 220); // fleur de 3 mm, six pétales
            }
            // Pointe : triangle très allongé (base 24 px, longueur 160 px).
            if (y > 220 && y < 380 && std::abs(x - 100.0) < 12.0 * (380.0 - y) / 160.0) {
                put(x, y, 30, 160, 30);
            }
            const double r4 = std::hypot(x - 300.0, y - 300.0);
            if (r4 < 60.0 && r4 >= 4.0) {
                put(x, y, 200, 160, 20); // anneau, trou de 0,4 mm
            }
        }
    }
    project.imageSource().original = std::move(img);
    project.imageSource().mm_per_px = Millimeters{0.05};
    return project;
}

struct Setting {
    std::string name;
    project_ops::RevectorizeOptions options;
};

struct Shapes {
    std::map<std::uint64_t, std::vector<geometry::PathSet>> by_region;
    double ms{0.0};
};

Shapes run(const document::Project& source, const project_ops::RevectorizeOptions& options) {
    document::Project project = source;
    project.vector_objects.clear();
    const auto t0 = std::chrono::steady_clock::now();
    const auto plan = project_ops::revectorize(project, options);
    Shapes out;
    out.ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    for (const auto& shape : plan.add) {
        if (shape.source_region) {
            out.by_region[shape.source_region->value] = shape.paths;
        }
    }
    return out;
}

std::size_t nodes(const std::vector<geometry::PathSet>& sets) {
    std::size_t n = 0;
    for (const auto& s : sets) {
        n += s.outer.nodes.size();
        for (const auto& h : s.holes) {
            n += h.nodes.size();
        }
    }
    return n;
}

std::size_t holes(const std::vector<geometry::PathSet>& sets) {
    std::size_t n = 0;
    for (const auto& s : sets) {
        n += s.holes.size();
    }
    return n;
}

double area_mm2(const std::vector<geometry::PathSet>& sets) {
    double a = 0.0;
    for (const auto& s : sets) {
        a += std::abs(geometry::signed_area_um2(s.outer));
        for (const auto& h : s.holes) {
            a -= std::abs(geometry::signed_area_um2(h));
        }
    }
    return a / 1e6;
}

// Écart entre deux formes d'une même région : pire Hausdorff des contours appariés par rang
// (extérieurs, puis trous). Approximation suffisante tant que le nombre de morceaux est égal ;
// les morceaux en trop ou manquants sont comptés à part.
double deviation_um(const std::vector<geometry::PathSet>& a,
                    const std::vector<geometry::PathSet>& b) {
    double worst = 0.0;
    const auto n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        worst = std::max(worst, geometry::hausdorff_distance_um(a[i].outer, b[i].outer));
        const auto nh = std::min(a[i].holes.size(), b[i].holes.size());
        for (std::size_t j = 0; j < nh; ++j) {
            worst = std::max(worst, geometry::hausdorff_distance_um(a[i].holes[j], b[i].holes[j]));
        }
    }
    return worst;
}

void report(const char* corpus, const document::Project& project) {
    const double mm_per_px = project.imageSource().mm_per_px.value;
    std::vector<Setting> settings;
    for (double mm : {0.2, 0.05, 0.01}) {
        settings.push_back({"poly " + std::to_string(mm).substr(0, 4) + " mm",
                            {.precision_mm = mm, .curves = false}});
    }
    for (double px : {1.5, 0.5, 0.25, 0.1}) {
        settings.push_back({"bezier " + std::to_string(px).substr(0, 4) + " px",
                            {.precision_mm = 0.2, .curves = true, .curve_tolerance_px = px}});
    }
    settings.push_back({"poly auto 0.2", {.precision_mm = 0.2, .curves = false, .adaptive = true}});
    settings.push_back(
        {"bezier auto 1.5",
         {.precision_mm = 0.2, .curves = true, .curve_tolerance_px = 1.5, .adaptive = true}});
    settings.push_back({"bezier auto loc",
                        {.precision_mm = 0.2,
                         .curves = true,
                         .curve_tolerance_px = 1.5,
                         .adaptive = true,
                         .adaptive_local = true}});
    const auto reference = run(project, {.precision_mm = 0.01, .curves = false});

    std::printf("\n== %s : %zu regions, %.4f mm/px ==\n", corpus, reference.by_region.size(),
                mm_per_px);
    std::printf("%-16s %9s %9s %10s %10s %10s %9s %7s %8s\n", "reglage", "noeuds", "n_petites",
                "max_um", "moy_um", "max_pet", "aire_%", "trous", "ms");
    for (const auto& setting : settings) {
        const auto shapes = run(project, setting.options);
        std::size_t total = 0, small_nodes = 0, kept_holes = 0, ref_holes = 0, missing = 0;
        double max_dev = 0.0, sum_dev = 0.0, max_small = 0.0, area_err = 0.0;
        int compared = 0;
        for (const auto& [id, ref] : reference.by_region) {
            const auto it = shapes.by_region.find(id);
            ref_holes += holes(ref);
            if (it == shapes.by_region.end()) {
                ++missing;
                continue;
            }
            const auto& got = it->second;
            const std::size_t n = nodes(got);
            total += n;
            kept_holes += std::min(holes(got), holes(ref));
            const double ref_area = area_mm2(ref);
            const double dev = deviation_um(got, ref);
            max_dev = std::max(max_dev, dev);
            sum_dev += dev;
            ++compared;
            if (ref_area > 0.0) {
                area_err += std::abs(area_mm2(got) - ref_area) / ref_area;
            }
            if (ref_area < 25.0) {
                small_nodes += n;
                max_small = std::max(max_small, dev);
            }
        }
        std::printf("%-16s %9zu %9zu %10.0f %10.1f %10.0f %9.2f %3zu/%-3zu %8.0f%s\n",
                    setting.name.c_str(), total, small_nodes, max_dev,
                    compared ? sum_dev / compared : 0.0, max_small,
                    compared ? 100.0 * area_err / compared : 0.0, kept_holes, ref_holes, shapes.ms,
                    missing ? " (regions manquantes)" : "");
    }
}

} // namespace

TEST_CASE("vec adapt bench: tolerance sweep", "[.vec_adapt_bench]") {
    auto synthetic = synthetic_project();
    REQUIRE(project_ops::apply_operation(
                synthetic, R"({"op":"segment_image","max_colors":5,"min_region_px":4})")
                .ok);
    report("synthetique", synthetic);

    if (const char* path = std::getenv("OPENSTITCH_VEC_BENCH_OSP"); path && *path) {
        auto loaded = project_io::load_project(std::filesystem::path{path});
        REQUIRE(loaded);
        auto& project = *loaded;
        if (project.hasActiveImage() && project.imageSource().segmentation) {
            report(std::filesystem::path{path}.filename().string().c_str(), project);
        } else {
            WARN("OSP sans segmentation : rien a mesurer");
        }
    }
}
