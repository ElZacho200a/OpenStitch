// SPDX-License-Identifier: Apache-2.0
// Mesures de réactivité de la V2 avec 1, 100, 500 et 1 000 objets. Étiquette cachée [.perf] : ces
// mesures ne tournent pas dans la suite ordinaire (elles dépendent de la machine et de la
// configuration) ; lancer `test_desktop_v2_core "[perf]"` de préférence en Release. Elles ne
// remplacent aucune assertion fonctionnelle : elles produisent les chiffres cités dans
// `docs/desktop-v2/10-performances.md`. Seul le retour visuel LOCAL (aperçu d'un glisser de nœud)
// a un plafond contrôlé, avec une marge large pour ne pas dépendre de la machine.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "document_session.hpp"
#include "node_edit.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "preview_service.hpp"
#include "shape_batch.hpp"
#include "studio_scene.hpp"
#include "transform_batch.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

[[nodiscard]] double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1));
    return values[index];
}

// N rectangles de 8 x 8 mm sur une grille, ajoutés par UN lot (une seule étape d'annulation).
[[nodiscard]] std::string gridBatch(int count) {
    std::string batch = R"({"schema":"openstitch.batch/1","operations":[)";
    const int perRow = 32;
    for (int i = 0; i < count; ++i) {
        const double x = (i % perRow) * 10.0 - 150.0;
        const double y = (i / perRow) * 10.0 - 150.0;
        if (i > 0) {
            batch += ",";
        }
        char op[160];
        std::snprintf(op, sizeof op,
                      R"({"op":"add_shape","shape":"rect","center_mm":[%.1f,%.1f],)"
                      R"("width_mm":8.0,"height_mm":8.0})",
                      x, y);
        batch += op;
    }
    return batch + "]}";
}

} // namespace

TEST_CASE("responsiveness with 1, 100, 500 and 1000 objects", "[.perf][desktop_v2]") {
    std::printf("\n%8s | %10s | %10s | %12s | %10s | %14s | %14s | %13s | %13s\n", "objets",
                "ajout (ms)", "copie (ms)", "séquence (ms)", "aperçu (ms)", "nœud p95 (ms)",
                "nœud max (ms)", "+1 objet (ms)", "déplacer (ms)");
    for (const int count : {1, 100, 500, 1000}) {
        DocumentSession session;
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        session.replaceProject(std::move(*created), true);

        const auto addStart = Clock::now();
        REQUIRE(session.applyBatch(gridBatch(count), "Grille").committed);
        const double addMs = msSince(addStart);
        REQUIRE(static_cast<int>(session.project().embroidery_objects.size()) == count);

        const auto copyStart = Clock::now();
        const auto snapshot = session.snapshot();
        const double copyMs = msSince(copyStart);

        TaskContext task;
        const auto seqStart = Clock::now();
        const auto sequence = stitch_generation::effective_sequence(*snapshot, task);
        const double seqMs = msSince(seqStart);
        REQUIRE(sequence.has_value());

        const auto previewStart = Clock::now();
        const auto preview = render_preview(snapshot, session.revision(), 4.0, task);
        const double previewMs = msSince(previewStart);
        REQUIRE(preview.has_value());

        // Retour visuel LOCAL : aperçu d'un glisser de nœud, répété comme à chaque mouvement.
        const std::uint64_t id = session.project().embroidery_objects.front().id.value;
        const auto view = nodeView(session.project(), id);
        REQUIRE(view.has_value());
        std::vector<double> samples;
        for (int i = 0; i < 60; ++i) {
            const auto start = Clock::now();
            (void)previewNodeMove(session.project(), id, {view->nodes.front().ref},
                                  {0.02 * i, 0.01 * i}, false);
            samples.push_back(msSince(start));
        }
        const double p95 = percentile(samples, 0.95);
        const double max = *std::max_element(samples.begin(), samples.end());

        // Gestes ordinaires sur le fil d'interface dans un document déjà chargé.
        const auto oneStart = Clock::now();
        REQUIRE(session.applyBatch(batchAddRect({400, 400}, 8, 8), "Un objet").committed);
        const double oneMs = msSince(oneStart);
        const auto moveStart = Clock::now();
        REQUIRE(session.applyBatch(batchTranslate({id}, 1.0, 1.0), "Déplacer").committed);
        const double moveMs = msSince(moveStart);

        std::printf("%8d | %10.1f | %10.1f | %12.1f | %10.1f | %14.3f | %14.3f | %13.1f | %13.1f\n",
                    count, addMs, copyMs, seqMs, previewMs, p95, max, oneMs, moveMs);
        // Plafond du retour local : 50 ms au 95e centile, même en Debug sur une machine lente.
        CHECK(p95 < 50.0);
    }
}

namespace {

// Serpentin de N segments de ~1 mm (comme les scenes du banc Studio), 1 000 segments par objet.
[[nodiscard]] StudioSceneSnapshot syntheticPickScene(std::size_t count) {
    StudioSceneSnapshot scene;
    scene.threadWidthMm = 0.22;
    const int perRow = 200;
    scene.segments.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const int row = static_cast<int>(i) / perRow;
        const int col = static_cast<int>(i) % perRow;
        const float x = static_cast<float>(row % 2 == 0 ? col : perRow - col);
        const float y = static_cast<float>(row) * 0.5F;
        const float dx = row % 2 == 0 ? 1.0F : -1.0F;
        scene.segments.push_back({{x, y, x + dx, y, {10, 20, 30}}, 1 + i / 1000});
    }
    return scene;
}

} // namespace

// Survol Studio : pick() est appele a chaque mouvement de souris sur le fil d'interface.
TEST_CASE("perf: StudioSceneSnapshot pick au survol", "[.perf][pick]") {
    for (const std::size_t count : {1000UL, 10000UL, 100000UL, 500000UL}) {
        const auto linear = syntheticPickScene(count);
        auto indexed = linear;
        const auto gridStart = Clock::now();
        indexed.pickGrid = build_pick_grid(indexed.segments);
        std::printf("[perf-pick] n=%zu grid build=%.2fms\n", count, msSince(gridStart));
        REQUIRE(indexed.pickGrid);
        const double height = static_cast<double>(count / 200) * 0.5 + 1.0;
        for (const auto& [mode, scene] :
             {std::pair<const char*, const StudioSceneSnapshot*>{"linear", &linear},
              std::pair<const char*, const StudioSceneSnapshot*>{"grid  ", &indexed}}) {
            std::vector<double> hitUs;
            std::vector<double> missUs;
            std::uint32_t state = 12345;
            const auto next = [&state] {
                state = state * 1664525U + 1013904223U;
                return static_cast<double>(state >> 8) / static_cast<double>(1U << 24);
            };
            for (int i = 0; i < 2000; ++i) {
                const double x = next() * 200.0;
                const double y = next() * height;
                const auto start = Clock::now();
                const auto found = scene->pick(x, y, 0.3);
                const double us = msSince(start) * 1000.0;
                (found ? hitUs : missUs).push_back(us);
            }
            const auto report = [&](const char* label, const std::vector<double>& v) {
                if (!v.empty()) {
                    std::printf(
                        "[perf-pick] n=%zu %s %s count=%zu p50=%.1fus p95=%.1fus p99=%.1fus\n",
                        count, mode, label, v.size(), percentile(v, 0.50), percentile(v, 0.95),
                        percentile(v, 0.99));
                }
            };
            report("hit ", hitUs);
            report("miss", missUs);
            CHECK(hitUs.size() + missUs.size() == 2000);
        }
    }
}

// PERF-005 / PERF-011 : coût d'UNE reconstruction de scène complète (celle que déclenche toute
// édition qui touche la broderie), par nombre d'objets. Hors fil d'interface en production : ce
// chiffre dit combien de temps la géométrie reste périmée après chaque édition.
TEST_CASE("perf: build_studio_scene par nombre d'objets", "[.perf][scene]") {
    std::printf("\n%8s | %10s | %10s | %10s | %10s | %10s | %10s | %10s\n", "objets", "segments",
                "total p50", "total max", "sequence", "segments", "sommets", "grille pick");
    for (const int count : {1, 100, 500, 1000}) {
        DocumentSession session;
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        session.replaceProject(std::move(*created), true);
        REQUIRE(session.applyBatch(gridBatch(count), "Grille").committed);
        const auto snapshot = session.snapshot();
        std::vector<double> totals;
        StudioSceneBuildTiming timing;
        std::size_t segments = 0;
        for (int run = 0; run < 5; ++run) {
            const auto start = Clock::now();
            const auto built = build_studio_scene(snapshot, 1, TaskContext{});
            totals.push_back(msSince(start));
            REQUIRE(built.has_value());
            timing = (*built)->timing;
            segments = (*built)->segments.size();
        }
        std::printf("%8d | %10zu | %8.1fms | %8.1fms | %8.1fms | %8.1fms | %8.1fms | %8.1fms\n",
                    count, segments, percentile(totals, 0.5), percentile(totals, 1.0),
                    timing.effectiveSequenceMs, timing.threadSegmentsMs, timing.verticesMs,
                    timing.pickGridMs);
    }
}
