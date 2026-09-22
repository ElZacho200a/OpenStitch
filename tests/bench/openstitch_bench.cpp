// SPDX-License-Identifier: Apache-2.0
//
// Banc de mesure des performances du pipeline (sans Qt). N'est PAS un test :
// il chronomètre chaque étape sur une image donnée et affiche les temps
// (min / médiane sur N répétitions) et la mémoire de pointe du processus.
// Protocole et résultats : docs/performance-audit.md.
//
// Usage : openstitch-bench <image> [--reps N] [--dpi D] [--order]
//                          [--save-osp projet.osp] [--from-osp projet.osp]
//
// Les étapes enchaînées reprennent exactement les appels de l'application :
// segmentation et auto-numérisation avec les valeurs par défaut du dialogue
// desktop (cf. apps/cli/main.cpp run_digitize), puis ce que
// MainWindow::refreshImage recalcule après CHAQUE mutation du document
// (apply_pipeline + refresh_context).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "openstitch/autodigitize/autodigitize.hpp"
#include "openstitch/formats/dst.hpp"
#include "openstitch/image/image.hpp"
#include "openstitch/image/ops.hpp"
#include "openstitch/optimization/order.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/segmentation/segmentation.hpp"
#include "openstitch/stitch/sequence.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "openstitch/vectorization/vectorize.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
// clang-format off
#include <psapi.h>
// clang-format on
#endif

using namespace openstitch;

namespace {

double peak_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
    }
#endif
    return 0.0;
}

double current_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    }
#endif
    return 0.0;
}

// Chronomètre `fn` `reps` fois ; la première exécution est rapportée à part
// (démarrage à froid : caches, allocations initiales, initialisation OpenCV).
void measure(const char* label, int reps, const std::function<void()>& fn) {
    std::vector<double> ms;
    ms.reserve(static_cast<std::size_t>(reps));
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    const double first = ms.front();
    std::vector<double> warm(ms.begin() + (reps > 1 ? 1 : 0), ms.end());
    std::sort(warm.begin(), warm.end());
    std::printf("%-44s froid %9.2f ms | chaud min %9.2f med %9.2f ms | pic %7.1f Mo\n", label,
                first, warm.front(), warm[warm.size() / 2], peak_mb());
    std::fflush(stdout);
}

std::uint64_t fnv(const std::vector<std::uint32_t>& v) {
    std::uint64_t h = 1469598103934665603ull;
    for (const std::uint32_t x : v) {
        h ^= x;
        h *= 1099511628211ull;
    }
    return h;
}

double dist(Vec2um a, Vec2um b) {
    return length_um(a - b);
}

void bench_order(int reps) {
    for (const int n : {100, 1000, 5000}) {
        std::mt19937 rng(42);
        std::vector<optimization::OrderItem> items;
        for (int i = 0; i < n; ++i) {
            optimization::OrderItem it;
            it.id = ObjectId{static_cast<std::uint64_t>(i + 1)};
            const auto c = static_cast<std::uint8_t>(rng() % 8);
            it.rgb = {c, c, c};
            it.centroid = Vec2um{Micrometers{static_cast<std::int32_t>(rng() % 200000)},
                                 Micrometers{static_cast<std::int32_t>(rng() % 200000)}};
            it.area_mm2 = static_cast<double>(rng() % 10000) / 10.0;
            items.push_back(it);
        }
        for (const auto [name, strategy] :
             {std::pair{"ByColor", optimization::OrderStrategy::ByColor},
              std::pair{"ByProximity", optimization::OrderStrategy::ByProximity},
              std::pair{"ColorThenProximity", optimization::OrderStrategy::ColorThenProximity},
              std::pair{"Layered", optimization::OrderStrategy::LayeredColorThenProximity}}) {
            const std::string label = "order " + std::string(name) + " n=" + std::to_string(n);
            measure(label.c_str(), reps, [&] {
                const auto order = optimization::optimize_order(items, strategy);
                if (order.size() != items.size()) {
                    std::abort();
                }
            });
        }
    }
}

// Écart entre le coût estimé par centroïdes (ce que l'optimiseur minimise)
// et le déplacement réel entre la sortie d'un objet et l'entrée du suivant
// dans la séquence effective.
void report_travel(const document::Project& project, const stitch::StitchSequence& seq) {
    std::vector<ObjectId> order;
    std::vector<Vec2um> first;
    std::vector<Vec2um> last;
    for (const auto& c : seq.commands) {
        if (!c.source.valid()) {
            continue;
        }
        if (order.empty() || order.back() != c.source) {
            order.push_back(c.source);
            first.push_back(c.pos);
            last.push_back(c.pos);
        } else {
            last.back() = c.pos;
        }
    }
    double real = 0.0;
    for (std::size_t i = 1; i < order.size(); ++i) {
        real += dist(last[i - 1], first[i]);
    }
    double jumps = 0.0;
    for (std::size_t i = 1; i < seq.commands.size(); ++i) {
        if (seq.commands[i].type == stitch::CommandType::Jump) {
            jumps += dist(seq.commands[i - 1].pos, seq.commands[i].pos);
        }
    }
    // Centroïdes (boîte englobante du contour extérieur, même calcul que
    // autodigitize::order_in_layers) dans l'ordre du document.
    double centroidCost = 0.0;
    std::optional<Vec2um> prev;
    for (const auto& e : project.embroidery_objects) {
        const auto* vec = project.findObject(e.source_vector);
        if (vec == nullptr) {
            continue;
        }
        std::int64_t x0 = INT64_MAX, y0 = INT64_MAX, x1 = INT64_MIN, y1 = INT64_MIN;
        for (const auto& set : vec->paths) {
            for (const auto& n : set.outer.nodes) {
                x0 = std::min<std::int64_t>(x0, n.pos.x.value);
                y0 = std::min<std::int64_t>(y0, n.pos.y.value);
                x1 = std::max<std::int64_t>(x1, n.pos.x.value);
                y1 = std::max<std::int64_t>(y1, n.pos.y.value);
            }
        }
        if (x0 > x1) {
            continue;
        }
        const Vec2um c{Micrometers{static_cast<std::int32_t>((x0 + x1) / 2)},
                       Micrometers{static_cast<std::int32_t>((y0 + y1) / 2)}};
        if (prev) {
            centroidCost += dist(*prev, c);
        }
        prev = c;
    }
    std::printf("travel : centroïdes %.1f mm | sortie->entrée réel %.1f mm | somme sauts %.1f mm "
                "(%zu blocs d'objets)\n",
                centroidCost / 1000.0, real / 1000.0, jumps / 1000.0, order.size());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: openstitch-bench <image> [--reps N] [--dpi D] [--order]\n");
        return 2;
    }
    std::string imagePath = argv[1];
    int reps = 5;
    double dpi = 96.0;
    bool orderOnly = false;
    std::string saveOsp;
    std::string fromOsp;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--reps") == 0 && i + 1 < argc) {
            reps = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--dpi") == 0 && i + 1 < argc) {
            dpi = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--order") == 0) {
            orderOnly = true;
        } else if (std::strcmp(argv[i], "--save-osp") == 0 && i + 1 < argc) {
            saveOsp = argv[++i];
        } else if (std::strcmp(argv[i], "--from-osp") == 0 && i + 1 < argc) {
            fromOsp = argv[++i]; // <image> ignorée : projet déjà numérisé
        }
    }
    if (orderOnly) {
        bench_order(reps);
        return 0;
    }

    std::printf("image %s | reps %d | dpi %.1f | mémoire de départ %.1f Mo\n", imagePath.c_str(),
                reps, dpi, current_mb());

    document::Project project;
    if (!fromOsp.empty()) {
        auto loaded = project_io::load_project(fromOsp);
        if (!loaded) {
            std::fprintf(stderr, "projet illisible : %s\n", loaded.error().message.c_str());
            return 1;
        }
        project = std::move(*loaded);
        std::printf("projet %s : objets vectoriels %zu | objets brodés %zu\n", fromOsp.c_str(),
                    project.vector_objects.size(), project.embroidery_objects.size());
    } else {
    image::Image img;
    measure("load_image", reps, [&] {
        auto r = image::load_image(imagePath);
        if (!r) {
            std::fprintf(stderr, "chargement impossible : %s\n", r.error().message.c_str());
            std::exit(1);
        }
        img = std::move(*r);
    });
    std::printf("  %d x %d px (%.2f Mpx)\n", img.width, img.height,
                img.width * static_cast<double>(img.height) / 1e6);

    const std::vector<image::ImageOp> noOps;
    const std::vector<image::ImageOp> quantOps{image::QuantizeOp{8}};
    const std::vector<image::ImageOp> mixOps{image::MedianDenoiseOp{1},
                                             image::BrightnessContrastOp{10.0, 10.0}};
    measure("apply_pipeline (0 op)", reps, [&] { (void)image::apply_pipeline(img, noOps); });
    measure("apply_pipeline (quantize 8)", reps,
            [&] { (void)image::apply_pipeline(img, quantOps); });
    measure("apply_pipeline (median+bright/contrast)", reps,
            [&] { (void)image::apply_pipeline(img, mixOps); });

    segmentation::Segmentation seg;
    measure("segment (8 coul, min 16, lissage 0)", reps, [&] {
        seg = *segmentation::segment(img,
                                     {.max_colors = 8, .min_region_px = 16, .smoothing_radius_px = 0});
    });
    std::printf("  régions : %zu\n", seg.region_count());
    measure("segment (8 coul, min 16, lissage 3)", reps, [&] {
        seg = *segmentation::segment(img,
                                     {.max_colors = 8, .min_region_px = 16, .smoothing_radius_px = 3});
    });
    std::printf("  régions : %zu (défauts desktop)\n", seg.region_count());
    measure("segment (16 coul, min 16, lissage 3)", reps, [&] {
        (void)segmentation::segment(img,
                                    {.max_colors = 16, .min_region_px = 16, .smoothing_radius_px = 3});
    });

    measure("region_adjacency", reps, [&] { (void)segmentation::region_adjacency(seg); });
    {
        const vectorization::VectorizeOptions vo{Millimeters{25.4 / dpi}, Micrometers{100}};
        std::size_t n = 0;
        measure("vectorize_region (toutes régions)", reps, [&] {
            n = 0;
            for (const auto& slot : seg.region_slots) {
                if (slot) {
                    auto r = vectorization::vectorize_region(seg, slot->id, vo);
                    n += r ? r->size() : 0;
                }
            }
        });
        std::printf("  morceaux vectorisés : %zu\n", n);
    }

    project.mm_per_px = Millimeters{25.4 / dpi};
    project.original = img;
    project.segmentation = seg;
    autodigitize::AutoOptions opts;
    opts.mm_per_px = project.mm_per_px;
    const auto cand = segmentation::background_candidate(seg);
    opts.skip_largest_region = cand && cand->recommended;

    autodigitize::AutoResult auto_result;
    measure("auto_digitize", std::max(1, reps / 2), [&] {
        IdGenerator<ObjectId> ids;
        auto r = autodigitize::auto_digitize(seg, ids, opts);
        if (!r) {
            std::fprintf(stderr, "auto_digitize : %s\n", r.error().message.c_str());
            std::exit(1);
        }
        auto_result = std::move(*r);
        project.object_ids = ids;
    });
    project.vector_objects = auto_result.vectors;
    project.embroidery_objects = auto_result.embroideries;
    std::printf("  objets vectoriels %zu | objets brodés %zu\n", project.vector_objects.size(),
                project.embroidery_objects.size());
    std::printf("  empreinte segmentation %016llx\n",
                static_cast<unsigned long long>(fnv(seg.labels)));
    if (!saveOsp.empty() && !project_io::save_project(saveOsp, project)) {
        std::fprintf(stderr, "écriture de %s impossible\n", saveOsp.c_str());
        return 1;
    }
    } // fin du pipeline image -> projet

    stitch::StitchSequence raw;
    measure("generate_sequence (brut)", reps,
            [&] { raw = *stitch_generation::generate_sequence(project); });
    stitch::StitchSequence eff;
    measure("effective_sequence", reps,
            [&] { eff = *stitch_generation::effective_sequence(project); });
    const auto st = stitch::compute_stats(eff);
    std::printf("  commandes %zu | points %zu | sauts %zu | coupes %zu\n", eff.commands.size(),
                st.stitches, st.jumps, st.trims);
    report_travel(project, eff);
    std::printf("  empreinte séquence effective %016llx\n",
                static_cast<unsigned long long>(stitch_generation::fingerprint(eff.commands)));

    // Projet avec retouches manuelles : un point déplacé dans les trois
    // premiers objets qui ont des points TopStitch.
    document::Project edited = project;
    int editedCount = 0;
    std::optional<ObjectId> target;
    for (auto& obj : edited.embroidery_objects) {
        if (editedCount >= 3) {
            break;
        }
        const auto slice = stitch_generation::raw_slice(raw, obj.id);
        for (std::size_t i = 0; i < slice.size(); ++i) {
            if (slice[i].pass == stitch::StitchPass::TopStitch &&
                stitch_generation::is_movable_point(slice[i])) {
                document::StitchOverride o;
                o.base_index = i;
                o.moved_to = slice[i].pos + Vec2um{Micrometers{150}, Micrometers{0}};
                obj.overrides.push_back(o);
                obj.edited_fingerprint = stitch_generation::fingerprint(slice);
                obj.edited_point_count = static_cast<std::uint32_t>(slice.size());
                ++editedCount;
                target = obj.id;
                break;
            }
        }
    }
    std::printf("  objets retouchés : %d\n", editedCount);
    measure("effective_sequence (avec retouches)", reps,
            [&] { (void)stitch_generation::effective_sequence(edited); });
    measure("refresh_context (cible retouchée)", reps,
            [&] { (void)stitch_generation::refresh_context(edited, target); });
    // Ce que MainWindow::refreshImage recalcule après CHAQUE mutation (glisser
    // de nœud, bascule de visibilité, paramètre…), hors rendu Qt.
    edited.ops = {image::QuantizeOp{8}};
    measure("refreshImage-equivalent (quantize + ctx)", reps, [&] {
        (void)image::apply_pipeline(edited.original, edited.ops);
        (void)stitch_generation::refresh_context(edited, target);
    });

    std::vector<std::uint8_t> dst;
    measure("encode_dst", reps, [&] { dst = *formats::encode_dst(eff); });
    measure("decode_dst", reps, [&] { (void)formats::decode_dst(dst); });

    const auto osp = std::filesystem::temp_directory_path() / "openstitch-bench.osp";
    measure("save_project (.osp)", reps, [&] {
        if (!project_io::save_project(osp, edited)) {
            std::abort();
        }
    });
    std::printf("  taille .osp : %.1f Ko\n",
                static_cast<double>(std::filesystem::file_size(osp)) / 1024.0);
    measure("load_project (.osp)", reps, [&] {
        if (!project_io::load_project(osp)) {
            std::abort();
        }
    });
    std::filesystem::remove(osp);
    measure("copie de Project (deep copy)", reps, [&] {
        const document::Project copy = edited;
        (void)copy;
    });
    std::printf("mémoire finale %.1f Mo | pic %.1f Mo\n", current_mb(), peak_mb());
    return 0;
}
