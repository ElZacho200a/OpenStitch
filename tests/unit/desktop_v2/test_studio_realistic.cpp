// SPDX-License-Identifier: Apache-2.0
// Famille K : rendu réaliste accéléré par le GPU.
// - logique sans GPU : runs ordonnés, tranches, sprites, cache par couleur, invalidation ;
// - repli : en backend logiciel, la bascule reste sur le raster CPU de référence ;
// - chemin RHI (Direct3D 11 WARP sous Windows) : sauté proprement quand le backend n'est pas RHI.
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <array>
#include <cmath>
#include <filesystem>

#include "analysis_controller.hpp"
#include "gpu_canvas.hpp"
#include "preview_service.hpp"
#include "realistic_scene.hpp"
#include "studio_controller.hpp"
#include "thread_textures.hpp"
#include "view_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
constexpr std::uint64_t firstId = (1ULL << 54) + 17;
constexpr std::uint64_t secondId = (1ULL << 54) + 19;

// Deux objets en points droits, de couleurs différentes, qui se croisent (l'ordre compte).
document::Project fixture(std::int32_t halfWidthUm = 15'000) {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    for (const auto id : {firstId, secondId}) {
        const std::int32_t y = id == firstId ? 4'000 : -4'000;
        document::VectorObject vector;
        vector.id = ObjectId{id - 1};
        geometry::Path path;
        path.closed = false;
        path.nodes = {{{Micrometers{-halfWidthUm}, Micrometers{y}}},
                      {{Micrometers{halfWidthUm}, Micrometers{-y}}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{id};
        object.source_vector = vector.id;
        object.name = id == firstId ? "Raspberry thread" : "Mint thread";
        object.rgb = id == firstId ? std::array<std::uint8_t, 3>{180, 17, 65}
                                   : std::array<std::uint8_t, 3>{21, 180, 151};
        object.params = document::RunningStitchParams{Micrometers{1'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(secondId);
    return project;
}

StudioSegment segment(std::array<std::uint8_t, 3> rgb, float ax = 0, float ay = 0, float bx = 1,
                      float by = 0) {
    StudioSegment s;
    s.thread.ax = ax;
    s.thread.ay = ay;
    s.thread.bx = bx;
    s.thread.by = by;
    s.thread.rgb = rgb;
    return s;
}

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    QTemporaryDir dir;
    ViewController view{studio, std::filesystem::path(dir.path().toStdWString())};
    AnalysisController analysis{studio, view, std::filesystem::path(dir.path().toStdWString())};
    GpuCanvas canvas;
    Rig() {
        canvas.setWidth(480);
        canvas.setHeight(320);
        canvas.setController(&studio);
        view.attachCanvas(&canvas);
        session.replaceProject(fixture(), true);
        wait();
    }
    void wait() {
        QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && studio.scene() != nullptr, 15'000);
        QTRY_VERIFY_WITH_TIMEOUT(view.simReady() && view.simTotal() > 0, 15'000);
    }
};

// Statistiques d'encre : pixels qui s'écartent nettement du fond de tissu uni.
struct Ink {
    long pixels{0};
    double r{0}, g{0}, b{0};
};
Ink inkStats(const QImage& image, const QRect& area, std::array<std::uint8_t, 3> fabric) {
    Ink ink;
    const auto rgb = image.convertToFormat(QImage::Format_RGB32);
    for (int y = std::max(0, area.top()); y < std::min(rgb.height(), area.bottom()); ++y) {
        for (int x = std::max(0, area.left()); x < std::min(rgb.width(), area.right()); ++x) {
            const QColor c = rgb.pixelColor(x, y);
            const int d = std::abs(c.red() - fabric[0]) + std::abs(c.green() - fabric[1]) +
                          std::abs(c.blue() - fabric[2]);
            if (d > 90) {
                ++ink.pixels;
                ink.r += c.red();
                ink.g += c.green();
                ink.b += c.blue();
            }
        }
    }
    if (ink.pixels > 0) {
        ink.r /= static_cast<double>(ink.pixels);
        ink.g /= static_cast<double>(ink.pixels);
        ink.b /= static_cast<double>(ink.pixels);
    }
    return ink;
}
} // namespace

class StudioRealisticTest : public QObject {
    Q_OBJECT
private slots:
    void runsKeepSewingOrderAndCoverEverySegment();
    void chunksAreBoundedAndOrdered();
    void quadsAreDeterministicAndBinnedByOrientation();
    void spriteCacheIsPerColourAndInvalidatedByStyle();
    void spriteLightingFollowsReliefAndOrientation();
    void shadowSpriteAndFabricTile();
    void sceneRunsMatchTheEffectiveSequence();
    void toggleKeepsDocumentUntouchedAndFallsBackToCpuInSoftware();
    void gpuPathBuildsOncePerRevisionAndMatchesCpuReference();
};

void StudioRealisticTest::runsKeepSewingOrderAndCoverEverySegment() {
    const std::array<std::uint8_t, 3> a{200, 0, 0};
    const std::array<std::uint8_t, 3> b{0, 0, 200};
    const std::vector<StudioSegment> segments{segment(a), segment(a), segment(b),
                                              segment(a), segment(a), segment(a)};
    const auto runs = buildRealisticRuns(segments);
    // Le même fil revient après un autre : c'est un NOUVEAU run (le recouvrement dépend de
    // l'ordre).
    QCOMPARE(runs.size(), std::size_t{3});
    QCOMPARE(runs[0].count, std::size_t{2});
    QCOMPARE(runs[1].count, std::size_t{1});
    QCOMPARE(runs[2].count, std::size_t{3});
    QCOMPARE(runs[0].rgb, a);
    QCOMPARE(runs[1].rgb, b);
    QCOMPARE(runs[2].rgb, a);
    std::size_t next = 0;
    for (const auto& run : runs) {
        QCOMPARE(run.first, next); // contigus : ni trou ni recouvrement
        next += run.count;
    }
    QCOMPARE(next, segments.size());
    QVERIFY(buildRealisticRuns({}).empty());
}

void StudioRealisticTest::chunksAreBoundedAndOrdered() {
    const std::vector<RealisticRun> runs{{{1, 2, 3}, 0, 12'000}, {{4, 5, 6}, 12'000, 3}};
    const auto chunks = buildRealisticChunks(runs, 5'000);
    QCOMPARE(chunks.size(), std::size_t{4});
    const std::vector<std::size_t> expectedCounts{5'000, 5'000, 2'000, 3};
    std::size_t next = 0;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        QCOMPARE(chunks[i].count, expectedCounts[i]);
        QCOMPARE(chunks[i].first, next);
        QVERIFY(chunks[i].count * kVerticesPerSegment <= 30'000);
        next += chunks[i].count;
    }
    QCOMPARE(chunks[3].run, std::size_t{1});
}

void StudioRealisticTest::quadsAreDeterministicAndBinnedByOrientation() {
    const RealisticStyle style;
    const std::vector<StudioSegment> segments{segment({1, 2, 3}, 0, 0, 2, 0),
                                              segment({1, 2, 3}, 0, 0, 0, 2),
                                              segment({1, 2, 3}, 5, 5, 5, 5)}; // segment nul
    std::vector<RealisticVertex> first(segments.size() * kVerticesPerSegment);
    std::vector<RealisticVertex> second(first.size());
    QCOMPARE(writeThreadQuads(segments, style, first.data()), first.size());
    writeThreadQuads(segments, style, second.data());
    QCOMPARE(std::memcmp(first.data(), second.data(), first.size() * sizeof(RealisticVertex)), 0);
    // Premier segment (le long de +x) : étendu d'une demi-largeur de chaque bout, largeur du fil.
    const float half = static_cast<float>(style.threadWidthMm) / 2;
    float minX = 1e9F, maxX = -1e9F, minY = 1e9F, maxY = -1e9F;
    for (std::size_t i = 0; i < kVerticesPerSegment; ++i) {
        minX = std::min(minX, first[i].x);
        maxX = std::max(maxX, first[i].x);
        minY = std::min(minY, first[i].y);
        maxY = std::max(maxY, first[i].y);
        QVERIFY(first[i].v >= 0.0F && first[i].v <= 1.0F);
    }
    QVERIFY(std::abs(minX - (-half)) < 1e-5F);
    QVERIFY(std::abs(maxX - (2 + half)) < 1e-5F);
    QVERIFY(std::abs((maxY - minY) - 2 * half) < 1e-5F);
    // Deux orientations différentes tombent dans des cellules d'atlas différentes.
    QVERIFY(std::abs(first[0].u - first[kVerticesPerSegment].u) > 1e-4F);
    for (const auto& v : first) {
        QVERIFY(v.u >= 0.0F && v.u <= 1.0F);
        QVERIFY(std::isfinite(v.x) && std::isfinite(v.y)); // le segment nul reste fini
    }
    QCOMPARE(orientationBin(1, 0), orientationBin(1, 0));
    QVERIFY(orientationBin(1, 0) != orientationBin(-1, 0));
    // L'ombre est décalée vers le bas-droite (lumière haut-gauche) et plus large.
    std::vector<RealisticVertex> shadow(first.size());
    writeShadowQuads(segments, style, shadow.data());
    QVERIFY(shadow[0].x > first[0].x - half); // décalage positif en x
    QVERIFY(shadowOffset(style.threadWidthMm).dx > 0 && shadowOffset(style.threadWidthMm).dy > 0);
}

void StudioRealisticTest::spriteCacheIsPerColourAndInvalidatedByStyle() {
    ThreadSpriteCache cache;
    RealisticStyle style;
    cache.setStyle(style);
    const std::array<std::uint8_t, 3> red{200, 20, 20};
    const std::array<std::uint8_t, 3> blue{20, 20, 200};
    const auto* first = &cache.sprite(red);
    QCOMPARE(cache.generatedCount(), std::size_t{1});
    for (int i = 0; i < 5; ++i) {
        QCOMPARE(&cache.sprite(red), first); // même objet, rien de régénéré
    }
    (void)cache.sprite(blue);
    QCOMPARE(cache.generatedCount(), std::size_t{2});
    QCOMPARE(cache.size(), std::size_t{2});
    // Un réglage qui n'affecte pas le fil (tissu, ombre) ne régénère rien.
    auto other = style;
    other.fabricRgb = {1, 2, 3};
    other.shadow = 0.9;
    QVERIFY(!cache.setStyle(other));
    (void)cache.sprite(red);
    QCOMPARE(cache.generatedCount(), std::size_t{2});
    // La brillance change l'éclairage : le cache est vidé puis régénéré une fois par couleur.
    other.sheen = 0.1;
    QVERIFY(cache.setStyle(other));
    QCOMPARE(cache.size(), std::size_t{0});
    (void)cache.sprite(red);
    QCOMPARE(cache.generatedCount(), std::size_t{3});
    QCOMPARE(cache.sprite(red).width, kSpriteCellWidth * kOrientationBins);
    QCOMPARE(cache.sprite(red).height, kSpriteHeight);
}

void StudioRealisticTest::spriteLightingFollowsReliefAndOrientation() {
    RealisticStyle flat;
    flat.relief = 0.0;
    flat.sheen = 0.0;
    flat.twist = 0.0;
    const auto flatSprite = generateThreadSprite({200, 100, 50}, flat);
    // Sans relief : couleur unie au centre du fil (alpha plein au milieu de la cellule).
    const int mid = kSpriteCellWidth / 2;
    const int row = kSpriteHeight / 2;
    for (int bin = 0; bin < kOrientationBins; ++bin) {
        const auto* p = flatSprite.pixel(bin * kSpriteCellWidth + mid, row);
        QVERIFY(std::abs(p[0] - 200) <= 2 && std::abs(p[1] - 100) <= 2 && std::abs(p[2] - 50) <= 2);
        QCOMPARE(static_cast<int>(p[3]), 255);
    }
    RealisticStyle lit;
    lit.relief = 0.9;
    lit.sheen = 0.0;
    lit.twist = 0.0;
    const auto sprite = generateThreadSprite({200, 100, 50}, lit);
    // Avec relief : le profil en travers n'est plus uniforme (flanc éclairé / flanc sombre) ...
    int minLuma = 999, maxLuma = 0;
    for (int cx = 4; cx < kSpriteCellWidth - 4; ++cx) {
        minLuma = std::min<int>(minLuma, sprite.pixel(cx, row)[0]);
        maxLuma = std::max<int>(maxLuma, sprite.pixel(cx, row)[0]);
    }
    QVERIFY(maxLuma - minLuma > 20);
    // ... et dépend de l'orientation du fil par rapport à la lumière : deux cellules opposées
    // ne sont pas identiques.
    bool differs = false;
    for (int cx = 0; cx < kSpriteCellWidth; ++cx) {
        differs |= sprite.pixel(cx, row)[0] !=
                   sprite.pixel(kSpriteCellWidth * (kOrientationBins / 2) + cx, row)[0];
    }
    QVERIFY(differs);
    // Les bouts sont effacés en alpha, le cœur est opaque ; bords doux en travers.
    QVERIFY(sprite.pixel(mid, 0)[3] < sprite.pixel(mid, row)[3]);
    QVERIFY(sprite.pixel(0, row)[3] < sprite.pixel(mid, row)[3]);
    // Le reflet éclaircit : sheen > 0 ne peut pas assombrir.
    RealisticStyle shiny = lit;
    shiny.sheen = 1.0;
    const auto shinySprite = generateThreadSprite({200, 100, 50}, shiny);
    int brighter = 0;
    for (int cx = 0; cx < kSpriteCellWidth; ++cx) {
        const int a = shinySprite.pixel(cx, row)[1];
        const int b = sprite.pixel(cx, row)[1];
        QVERIFY(a >= b);
        brighter += a > b;
    }
    QVERIFY(brighter > 0);
}

void StudioRealisticTest::shadowSpriteAndFabricTile() {
    RealisticStyle style;
    style.shadow = 0.0;
    const auto none = generateShadowSprite(style);
    style.shadow = 1.0;
    const auto strong = generateShadowSprite(style);
    const int mid = kSpriteCellWidth / 2;
    const int row = kSpriteHeight / 2;
    QCOMPARE(static_cast<int>(none.pixel(mid, row)[3]), 0);
    QVERIFY(strong.pixel(mid, row)[3] > 60);
    QCOMPARE(static_cast<int>(strong.pixel(mid, row)[0]), 0); // noir
    // Tissu : opaque ; toile unie = couleur unie ; armure = relief visible.
    RealisticStyle plain;
    plain.fabricWeave = false;
    const auto flat = generateFabricTile(plain);
    QCOMPARE(flat.width, kFabricTilePixels);
    for (int i = 0; i < 50; ++i) {
        const auto* p = flat.pixel((i * 7) % flat.width, (i * 13) % flat.height);
        QCOMPARE(static_cast<int>(p[3]), 255);
        QVERIFY(std::abs(p[0] - plain.fabricRgb[0]) <= 1);
    }
    RealisticStyle weave = plain;
    weave.fabricWeave = true;
    weave.fabricRelief = 1.0;
    const auto woven = generateFabricTile(weave);
    int lo = 255, hi = 0;
    for (int y = 0; y < woven.height; ++y) {
        for (int x = 0; x < woven.width; ++x) {
            lo = std::min<int>(lo, woven.pixel(x, y)[0]);
            hi = std::max<int>(hi, woven.pixel(x, y)[0]);
        }
    }
    QVERIFY(hi - lo > 15);
    // Déterminisme : mêmes entrées, mêmes octets.
    QCOMPARE(generateFabricTile(weave).rgba, woven.rgba);
    QCOMPARE(generateThreadSprite({9, 8, 7}, style).rgba,
             generateThreadSprite({9, 8, 7}, style).rgba);
}

void StudioRealisticTest::sceneRunsMatchTheEffectiveSequence() {
    Rig rig;
    const auto scene = rig.studio.scene();
    QVERIFY(scene && !scene->segments.empty());
    const auto runs = buildRealisticRuns(scene->segments);
    // Deux objets de couleurs différentes cousus l'un après l'autre : exactement deux runs.
    QCOMPARE(runs.size(), std::size_t{2});
    QCOMPARE(runs[0].count + runs[1].count, scene->segments.size());
    QVERIFY(runs[0].rgb != runs[1].rgb);
    // L'ordre des runs est l'ordre de couture de la séquence effective.
    QCOMPARE(scene->segments[runs[0].first].thread.rgb, runs[0].rgb);
    QCOMPARE(scene->segments[runs[1].first].thread.rgb, runs[1].rgb);
    QCOMPARE(scene->segments[runs[1].first].objectId != scene->segments[runs[0].first].objectId,
             true);
}

void StudioRealisticTest::toggleKeepsDocumentUntouchedAndFallsBackToCpuInSoftware() {
    Rig rig;
    const auto revision = rig.session.revision();
    const auto commands = rig.studio.commandCount();
    QVERIFY(!rig.view.realistic());
    QVERIFY(rig.view.realisticMode().isEmpty());
    // Sans fenêtre / en backend logiciel : pas de chemin GPU, repli sur le raster CPU.
    QVERIFY(!rig.canvas.rhiBackend());
    QSignalSpy toggled(&rig.view, &ViewController::realisticChanged);
    rig.view.setRealistic(true);
    QCOMPARE(toggled.count(), 1);
    QVERIFY(rig.view.realisticMode().contains(QStringLiteral("CPU")));
    QVERIFY(!rig.view.realisticOnGpu());
    // Le repli calcule l'image de référence (moteur CPU render_preview).
    QTRY_VERIFY_WITH_TIMEOUT(rig.analysis.realisticReady(), 30'000);
    // Réglages de qualité : n'affectent pas le document.
    QVERIFY(rig.analysis.applyRenderPreset(QStringLiteral("draft")));
    QCOMPARE(rig.view.realisticStyle(), realisticStyleFrom(*renderPreset("draft")));
    rig.view.toggleRealistic(); // coupe : l'image de référence est retirée
    QVERIFY(!rig.view.realistic());
    QVERIFY(!rig.analysis.realisticReady());
    QVERIFY(rig.view.realisticMode().isEmpty());
    QCOMPARE(rig.session.revision(), revision);
    QCOMPARE(rig.studio.commandCount(), commands);
    // Aucun calque GPU n'a été construit dans ce backend.
    QCOMPARE(rig.canvas.realisticBuildCount(), 0);
}

void StudioRealisticTest::gpuPathBuildsOncePerRevisionAndMatchesCpuReference() {
    if (qgetenv("OPENSTITCH_RHI_TEST") != "1") {
        QSKIP("RHI test not requested (set OPENSTITCH_RHI_TEST=1 with QSG_RHI_BACKEND=d3d11 and "
              "QSG_RHI_PREFER_SOFTWARE_RENDERER=1)");
    }
    QQuickWindow window;
    window.resize(480, 320);
    Rig rig;
    rig.canvas.setParentItem(window.contentItem());
    rig.canvas.setWidth(480);
    rig.canvas.setHeight(320);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (!rig.canvas.rhiBackend()) {
        QSKIP("Qt Quick backend is not RHI (software or unavailable adapter)");
    }
    qInfo() << "backend:" << rig.canvas.backendName();
    rig.view.attachCanvas(&rig.canvas); // republie avec la fenêtre : le chemin est maintenant connu
    QVERIFY(rig.view.realisticOnGpu());
    rig.canvas.fitDesign();
    // Rendu plat de référence pour comparer la couverture.
    QTRY_VERIFY_WITH_TIMEOUT(rig.canvas.geometryBuildCount() >= 1, 15'000);
    QCOMPARE(rig.canvas.realisticBuildCount(),
             0); // rien de réaliste tant que la bascule est coupée

    rig.view.setRealistic(true);
    QTRY_VERIFY_WITH_TIMEOUT(rig.canvas.realisticActive(), 15'000);
    QCOMPARE(rig.canvas.realisticBuildCount(), 1);
    QCOMPARE(rig.view.realisticMode(), QStringLiteral("Réaliste · GPU"));

    // Pan / zoom : aucune reconstruction (la géométrie vit en mm, la caméra est une matrice).
    const int geometry = rig.canvas.geometryBuildCount();
    for (int i = 0; i < 6; ++i) {
        rig.canvas.panBy(7, -3);
        rig.canvas.zoomAt(1.2, 240, 160);
        QVERIFY(!window.grabWindow().isNull());
    }
    QCOMPARE(rig.canvas.realisticBuildCount(), 1);
    QCOMPARE(rig.canvas.geometryBuildCount(), geometry);
    // Bascule coupée puis remise : la révision est inchangée, rien n'est recalculé.
    rig.view.setRealistic(false);
    QVERIFY(!window.grabWindow().isNull());
    rig.view.setRealistic(true);
    QVERIFY(!window.grabWindow().isNull());
    QTRY_VERIFY_WITH_TIMEOUT(rig.canvas.realisticActive(), 15'000);
    QCOMPARE(rig.canvas.realisticBuildCount(), 1);
    // Réglage de qualité qui touche le fil : exactement une reconstruction.
    QVERIFY(rig.analysis.setRenderSetting(QStringLiteral("sheen"), QStringLiteral("0.9")));
    QTRY_COMPARE_WITH_TIMEOUT(rig.canvas.realisticBuildCount(), 2, 15'000);
    QVERIFY(!window.grabWindow().isNull());
    QCOMPARE(rig.canvas.realisticBuildCount(), 2);

    // Le document change (nouvelle révision de scène) : exactement une reconstruction de plus.
    rig.session.replaceProject(fixture(12'000), true);
    rig.wait();
    QTRY_COMPARE_WITH_TIMEOUT(rig.canvas.realisticBuildCount(), 3, 15'000);

    // Comparaison de statistiques avec la référence CPU (render_preview).
    rig.canvas.fitDesign();
    QTest::qWait(100);
    const QImage gpu = window.grabWindow();
    QVERIFY(!gpu.isNull());
    const auto settings = rig.analysis.renderSettings();
    const RenderSettings render = *renderPreset("balanced");
    (void)settings;
    auto cpu =
        render_preview(rig.session.snapshot(), rig.session.revision(), render, TaskContext{});
    QVERIFY(cpu.has_value());
    QImage cpuImage;
    QVERIFY(cpuImage.loadFromData(cpu->png.data(), static_cast<uint>(cpu->png.size()), "PNG"));
    QVERIFY(gpu.save(QStringLiteral("realistic_gpu_frame.png")));
    QVERIFY(cpuImage.save(QStringLiteral("realistic_cpu_reference.png")));
    const auto& camera = rig.canvas.camera();
    const double gpuScale = camera.scale() * window.effectiveDevicePixelRatio();
    const auto sceneBounds = rig.studio.scene()->bounds;
    const auto a = camera.worldToScreen({sceneBounds.x0, sceneBounds.y0});
    const auto b = camera.worldToScreen({sceneBounds.x1, sceneBounds.y1});
    const QRect area(QPoint(static_cast<int>(a.x), static_cast<int>(a.y)),
                     QPoint(static_cast<int>(b.x), static_cast<int>(b.y)));
    const auto fabric = render.fabricRgb;
    const Ink gpuInk = inkStats(gpu, area, fabric);
    const Ink cpuInk = inkStats(cpuImage, QRect(QPoint(0, 0), cpuImage.size()), fabric);
    QVERIFY(gpuInk.pixels > 50);
    QVERIFY(cpuInk.pixels > 50);
    const double gpuAreaMm2 = static_cast<double>(gpuInk.pixels) / (gpuScale * gpuScale);
    const double cpuAreaMm2 =
        static_cast<double>(cpuInk.pixels) / (cpu->px_per_mm * cpu->px_per_mm);
    qInfo() << "ink area mm2 gpu/cpu:" << gpuAreaMm2 << cpuAreaMm2 << "mean rgb gpu:" << gpuInk.r
            << gpuInk.g << gpuInk.b << "cpu:" << cpuInk.r << cpuInk.g << cpuInk.b;
    // Tolérances documentées (docs/desktop-v2/19-rendu-realiste-gpu.md) : l'aire d'encre entre 0,25 et 2,5 fois la
    // référence (mesuré 0,36 sur WARP : fil de 0,35 mm très fin, seuil d'encre strict) (bouts, ombre portée et anticrénelage diffèrent), la couleur moyenne à
    // 70 niveaux par canal près.
    const double ratio = gpuAreaMm2 / cpuAreaMm2;
    QVERIFY2(ratio > 0.25 && ratio < 2.5, qPrintable(QString::number(ratio)));
    QVERIFY(std::abs(gpuInk.r - cpuInk.r) < 70);
    QVERIFY(std::abs(gpuInk.g - cpuInk.g) < 70);
    QVERIFY(std::abs(gpuInk.b - cpuInk.b) < 70);
}

QTEST_MAIN(StudioRealisticTest)
#include "test_studio_realistic.moc"
