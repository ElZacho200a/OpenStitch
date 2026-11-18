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
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "document_session.hpp"
#include "node_edit.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "openstitch/stitch_generation/generate.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "preview_service.hpp"
#include "shape_batch.hpp"
#include "shape_resolver.hpp"
#include "studio_scene.hpp"
#include "transform_batch.hpp"
#include "warp_brush.hpp"

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

// LOT 2 de la proposition « la forme, unité d'édition » : où part le temps quand on édite une forme
// dans un gros document ? Une seule forme est poussée au pinceau (trait de 60 touches), puis on
// mesure ce que paient la fenêtre et les consommateurs après le geste. Rien n'est plafonné : ces
// chiffres décident des lots suivants (invalidations ciblées, aperçu léger). Lancer en Release :
// `test_desktop_v2_core "[warp]"` ne les inclut pas (étiquette cachée), utiliser
// `"[perf][editcost]"`.
TEST_CASE("perf: cout d'une edition de forme (pinceau, suppression) par nombre d'objets",
          "[.perf][editcost]") {
    std::printf("\n%7s | %9s | %12s | %12s | %11s | %10s | %10s | %10s | %10s | %10s | %10s\n",
                "objets", "snap (ms)", "rejeu 60 (ms)", "reprise 60", "fin (ms)", "execute",
                "seq tiede", "seq froid", "scene", "supprimer", "annuler");
    for (const int count : {1, 100, 500, 1000}) {
        DocumentSession session;
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        session.replaceProject(std::move(*created), true);
        REQUIRE(session.applyBatch(gridBatch(count), "Grille").committed);
        const Target target{TargetKind::EmbroideryObject,
                            session.project().embroidery_objects.front().id.value};

        // Appui : l'outil prend un instantane du document (copie complete).
        const auto snapStart = Clock::now();
        const auto snapshot = session.snapshot();
        const double snapMs = msSince(snapStart);

        // Premiere sequence : remplit le cache de regeneration par objet.
        TaskContext task;
        REQUIRE(stitch_generation::effective_sequence(*snapshot, task).has_value());

        // Trait de 60 touches de la forme (centre -150,-150 mm, 8 x 8) vers l'exterieur.
        std::vector<WarpTouch> touches;
        for (int i = 0; i < 60; ++i) {
            touches.push_back(
                {Vec2um{Micrometers{-150000}, Micrometers{-150000 + i * 200}}, false});
        }
        // Avant le lot pinceau reactif : chaque mise a jour rejouait tout le trait.
        const auto replayStart = Clock::now();
        for (std::size_t k = 1; k <= touches.size(); ++k) {
            const std::vector<WarpTouch> prefix(touches.begin(),
                                                touches.begin() + static_cast<std::ptrdiff_t>(k));
            REQUIRE(previewWarp(*snapshot, target, {}, prefix, false, task).has_value());
        }
        const double replayMs = msSince(replayStart);
        // Maintenant : seules les nouvelles touches sont jouees.
        WarpStroke state;
        const auto resumeStart = Clock::now();
        for (std::size_t k = 1; k <= touches.size(); ++k) {
            const std::vector<WarpTouch> prefix(touches.begin(),
                                                touches.begin() + static_cast<std::ptrdiff_t>(k));
            auto step = previewWarp(*snapshot, target, {}, prefix, false, task, std::move(state));
            REQUIRE(step.has_value());
            state = std::move(step->stroke);
        }
        const double resumeMs = msSince(resumeStart);

        // Relachement : preparation de la commande (validation du contour).
        const auto finishStart = Clock::now();
        auto finished = previewWarp(*snapshot, target, {}, touches, true, task, state);
        const double finishMs = msSince(finishStart);
        REQUIRE(finished.has_value());
        REQUIRE(finished->command);

        // Application sur le fil d'interface (commande + notifications de la session).
        const auto executeStart = Clock::now();
        session.execute(std::move(finished->command));
        const double executeMs = msSince(executeStart);

        // Ce que paient les consommateurs apres l'edition : sequence avec cache tiede (un seul
        // objet a change), sequence a froid, reconstruction de la scene Studio.
        const auto after = session.snapshot();
        const auto warmStart = Clock::now();
        REQUIRE(stitch_generation::effective_sequence(*after, task).has_value());
        const double warmMs = msSince(warmStart);
        stitch_generation::clear_chunk_cache();
        const auto coldStart = Clock::now();
        REQUIRE(stitch_generation::effective_sequence(*after, task).has_value());
        const double coldMs = msSince(coldStart);
        const auto sceneStart = Clock::now();
        REQUIRE(build_studio_scene(after, 2, task).has_value());
        const double sceneMs = msSince(sceneStart);

        // Suppression de forme (une etape) puis annulation.
        const auto removeStart = Clock::now();
        auto removal = prepareShapeRemoval(
            session.project(),
            {Target{TargetKind::VectorObject,
                    session.project().embroidery_objects.back().source_vector.value}});
        REQUIRE(removal.command);
        session.execute(std::move(removal.command));
        const double removeMs = msSince(removeStart);
        const auto undoStart = Clock::now();
        REQUIRE(session.undo());
        const double undoMs = msSince(undoStart);

        std::printf("%7d | %9.2f | %12.1f | %12.1f | %11.2f | %10.2f | %10.1f | %10.1f | %10.1f | "
                    "%10.2f | %10.2f\n",
                    count, snapMs, replayMs, resumeMs, finishMs, executeMs, warmMs, coldMs, sceneMs,
                    removeMs, undoMs);
    }
}

// Meme mesure sur UNE grande forme a nombre de sommets croissant (contour trace d'une image) : le
// pinceau rejoue des dabs sur tout le contour, la regeneration tatami couvre 120 mm de large.
TEST_CASE("perf: cout du pinceau sur une grande forme par nombre de sommets", "[.perf][editcost]") {
    std::printf("\n%7s | %9s | %12s | %12s | %11s | %10s | %10s | %10s | %10s | %10s\n", "sommets",
                "snap (ms)", "rejeu 60 (ms)", "reprise 60", "fin (ms)", "execute", "seq tiede",
                "seq froid", "scene", "aplatir");
    for (const int vertices : {8, 100, 500, 2000}) {
        std::vector<PointMm> outline;
        for (int i = 0; i < vertices; ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * i / vertices;
            const double radius = 60.0 * (1.0 + 0.05 * std::sin(5.0 * angle));
            outline.push_back({radius * std::cos(angle), radius * std::sin(angle)});
        }
        DocumentSession session;
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        session.replaceProject(std::move(*created), true);
        REQUIRE(session.applyBatch(batchAddPolygon(outline), "Forme").committed);
        const Target target{TargetKind::EmbroideryObject,
                            session.project().embroidery_objects.front().id.value};

        const auto snapStart = Clock::now();
        const auto snapshot = session.snapshot();
        const double snapMs = msSince(snapStart);
        TaskContext task;
        REQUIRE(stitch_generation::effective_sequence(*snapshot, task).has_value());

        std::vector<WarpTouch> touches;
        for (int i = 0; i < 60; ++i) {
            touches.push_back({Vec2um{Micrometers{50000 + i * 300}, Micrometers{0}}, false});
        }
        const auto replayStart = Clock::now();
        for (std::size_t k = 1; k <= touches.size(); ++k) {
            const std::vector<WarpTouch> prefix(touches.begin(),
                                                touches.begin() + static_cast<std::ptrdiff_t>(k));
            REQUIRE(previewWarp(*snapshot, target, {}, prefix, false, task).has_value());
        }
        const double replayMs = msSince(replayStart);
        WarpStroke state;
        const auto resumeStart = Clock::now();
        for (std::size_t k = 1; k <= touches.size(); ++k) {
            const std::vector<WarpTouch> prefix(touches.begin(),
                                                touches.begin() + static_cast<std::ptrdiff_t>(k));
            auto step = previewWarp(*snapshot, target, {}, prefix, false, task, std::move(state));
            REQUIRE(step.has_value());
            state = std::move(step->stroke);
        }
        const double resumeMs = msSince(resumeStart);

        // Cout, sur le fil d'interface, de la conversion de l'apercu en ligne a chaque mise a jour
        // (ToolController::rebuild aplatit chaque contour avant de le donner au canevas).
        const auto flattenStart = Clock::now();
        std::size_t flatPoints = 0;
        for (const auto& set : state.paths) {
            flatPoints += geometry::flatten(set.outer, Micrometers{100}).points.size();
            for (const auto& hole : set.holes) {
                flatPoints += geometry::flatten(hole, Micrometers{100}).points.size();
            }
        }
        const double flattenMs = msSince(flattenStart);
        REQUIRE(flatPoints > 0);

        const auto finishStart = Clock::now();
        auto finished = previewWarp(*snapshot, target, {}, touches, true, task, state);
        const double finishMs = msSince(finishStart);
        REQUIRE(finished.has_value());
        REQUIRE(finished->command);
        const auto executeStart = Clock::now();
        session.execute(std::move(finished->command));
        const double executeMs = msSince(executeStart);

        const auto after = session.snapshot();
        const auto warmStart = Clock::now();
        REQUIRE(stitch_generation::effective_sequence(*after, task).has_value());
        const double warmMs = msSince(warmStart);
        stitch_generation::clear_chunk_cache();
        const auto coldStart = Clock::now();
        REQUIRE(stitch_generation::effective_sequence(*after, task).has_value());
        const double coldMs = msSince(coldStart);
        const auto sceneStart = Clock::now();
        REQUIRE(build_studio_scene(after, 2, task).has_value());
        const double sceneMs = msSince(sceneStart);

        std::printf(
            "%7d | %9.2f | %12.1f | %12.1f | %11.2f | %10.2f | %10.1f | %10.1f | %10.1f | %10.3f\n",
            vertices, snapMs, replayMs, resumeMs, finishMs, executeMs, warmMs, coldMs, sceneMs,
            flattenMs);
    }
}

// Appui du pinceau sur un document qui porte une image importee : l'instantane du document est une
// copie PROFONDE, image comprise, refaite a la premiere lecture apres chaque edition. Mesure le
// cout de cette copie (sur le fil d'interface a l'appui) selon la taille de l'image.
TEST_CASE("perf: instantane du document avec une image importee", "[.perf][editcost]") {
    std::printf("\n%12s | %10s | %12s | %12s\n", "image", "Mo RGBA", "1re copie",
                "ensuite (cache)");
    for (const auto& [width, height] :
         {std::pair{0, 0}, std::pair{1000, 1000}, std::pair{2000, 2000}, std::pair{4000, 3000}}) {
        DocumentSession session;
        auto created = project_ops::create_empty_project({});
        REQUIRE(created.has_value());
        created->original.width = width;
        created->original.height = height;
        created->original.rgba.assign(static_cast<std::size_t>(width) * height * 4, 128);
        session.replaceProject(std::move(*created), true);
        REQUIRE(session.applyBatch(batchAddRect({0, 0}, 20, 10), "Forme").committed);
        const auto firstStart = Clock::now();
        const auto first = session.snapshot();
        const double firstMs = msSince(firstStart);
        const auto againStart = Clock::now();
        const auto again = session.snapshot();
        const double againMs = msSince(againStart);
        REQUIRE(first == again);
        std::printf("%5dx%-6d | %10.1f | %10.2f ms | %10.3f ms\n", width, height,
                    static_cast<double>(width) * height * 4 / 1.0e6, firstMs, againMs);
    }
}
