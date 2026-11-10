// SPDX-License-Identifier: Apache-2.0
// Espace « Segmentation » : vrai Main.qml, vrai SegmentationSpace.qml, vrais contrôleurs et un
// vrai document numérisé. Reprend les parcours segmentation / régions / IA de l'ancien préparateur
// (test_studio_image) en les faisant passer par l'espace, puis vérifie l'affichage des régions, la
// sélection au clic, l'état publié et la mise à jour de l'aval.
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>
#include <filesystem>
#include <memory>

#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "digitize_pipeline.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "image_controller.hpp"
#include "inspector_controller.hpp"
#include "layers/region_layer.hpp"
#include "region_controller.hpp"
#include "segmentation_space_controller.hpp"
#include "studio_controller.hpp"
#include "thread_controller.hpp"
#include "view_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

// Quatre aplats (160 x 120) avec un léger bruit déterministe, importés par la vraie chaîne.
QString writeTestImage(const QString& dir) {
    QImage img(160, 120, QImage::Format_RGB32);
    QPainter painter(&img);
    painter.fillRect(0, 0, 80, 60, QColor(200, 30, 40));
    painter.fillRect(80, 0, 80, 60, QColor(30, 160, 60));
    painter.fillRect(0, 60, 80, 60, QColor(40, 60, 200));
    painter.fillRect(80, 60, 80, 60, QColor(240, 220, 40));
    painter.end();
    std::uint32_t seed = 12345;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            seed = seed * 1664525u + 1013904223u;
            const int n = static_cast<int>((seed >> 24) % 9) - 4;
            QColor c = QColor::fromRgb(img.pixel(x, y));
            img.setPixelColor(x, y,
                              QColor(std::clamp(c.red() + n, 0, 255),
                                     std::clamp(c.green() + n, 0, 255),
                                     std::clamp(c.blue() + n, 0, 255)));
        }
    }
    const QString path = dir + QStringLiteral("/blocks.png");
    img.save(path);
    return path;
}

document::Project digitizedProject(const QString& imagePath) {
    DigitizeRequest request;
    request.image = std::filesystem::path(imagePath.toStdWString());
    request.settings.max_colors = 4;
    request.settings.min_region_px = 16;
    auto out = run_digitize(request, TaskContext{});
    if (!out) {
        qFatal("run_digitize failed: %s", out.error().message.c_str());
    }
    return std::move(out->project);
}

QQuickItem* findVisual(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* found = findVisual(child, name)) {
            return found;
        }
    }
    return nullptr;
}

int highlightAlphaAt(const RegionLayerData& layer, int x, int y) {
    if (layer.highlight.isNull() || layer.highlightRectMm.isEmpty() || layer.rectMm.isEmpty()) {
        return 0;
    }
    const double mm = layer.rectMm.width() / layer.fill.width();
    const double dx = (layer.highlightRectMm.left() - layer.rectMm.left()) / mm;
    const double dy = (layer.highlightRectMm.top() - layer.rectMm.top()) / mm;
    const int ox = static_cast<int>(std::lround(dx));
    const int oy = static_cast<int>(std::lround(dy));
    const int localX = x - ox;
    const int localY = y - oy;
    if (localX < 0 || localY < 0 || localX >= layer.highlight.width() ||
        localY >= layer.highlight.height()) {
        return 0;
    }
    return layer.highlight.pixelColor(localX, localY).alpha();
}

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    QTemporaryDir dir;
    std::filesystem::path cfg{dir.path().toStdWString()};
    FileController files{studio, &actions, dir.path()};
    InspectorController inspector{studio};
    ViewController view{studio, cfg};
    ThreadController threads{studio, cfg};
    AnalysisController analysis{studio, view, cfg};
    ImageController images{studio};
    RegionController regions{studio, images};
    SegmentationSpaceController space{studio, images, regions};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    WorkspaceController* workspace{nullptr};

    QQuickItem* named(const char* name) const {
        return findVisual(window->contentItem(), QString::fromLatin1(name));
    }
    bool press(const char* name) const {
        auto* item = named(name);
        return item && item->isEnabled() && QMetaObject::invokeMethod(item, "click");
    }
    GpuCanvas* canvas() const {
        return window->findChild<GpuCanvas*>(QStringLiteral("segmentationCanvas"));
    }
    QVariantMap stage() const { return workspace->space(QStringLiteral("segmentation")); }
    // Position (coordonnées fenêtre) du pixel (px, py) de l'image de travail.
    QPointF windowPos(int px, int py) const {
        auto* area = named("segmentationPickArea");
        const auto view = canvas()->viewState();
        const double mm = session.project().mm_per_px.value;
        const auto& seg = *session.project().segmentation;
        const double sx = (px + 0.5 - seg.width / 2.0) * mm;
        const double sy = (py + 0.5 - seg.height / 2.0) * mm;
        const double zoom = view.value("zoom").toDouble();
        const QPointF item(area->width() / 2.0 + (sx - view.value("centerX").toDouble()) * zoom,
                           area->height() / 2.0 + (sy - view.value("centerY").toDouble()) * zoom);
        return area->mapToScene(item);
    }
};

} // namespace

class StudioSpaceSegmentationTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig) {
        connect(&rig.engine, &QQmlApplicationEngine::warnings, this,
                [&rig](const QList<QQmlError>& errors) {
                    for (const auto& e : errors) {
                        rig.warnings.push_back(e.toString());
                    }
                });
        rig.engine.addImageProvider(QStringLiteral("prepare"), rig.images.createProvider());
        auto* ctx = rig.engine.rootContext();
        ctx->setContextProperty(QStringLiteral("studio"), &rig.studio);
        ctx->setContextProperty(QStringLiteral("actions"), &rig.actions);
        ctx->setContextProperty(QStringLiteral("editor"), &rig.editor);
        ctx->setContextProperty(QStringLiteral("files"), &rig.files);
        ctx->setContextProperty(QStringLiteral("inspector"), &rig.inspector);
        ctx->setContextProperty(QStringLiteral("view"), &rig.view);
        ctx->setContextProperty(QStringLiteral("threads"), &rig.threads);
        ctx->setContextProperty(QStringLiteral("analysis"), &rig.analysis);
        ctx->setContextProperty(QStringLiteral("imageTools"), &rig.images);
        ctx->setContextProperty(QStringLiteral("regionTools"), &rig.regions);
        ctx->setContextProperty(QStringLiteral("segmentationSpace"), &rig.space);
        rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
        rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
        QVERIFY(rig.window);
        rig.workspace = rig.window->findChild<WorkspaceController*>();
        QVERIFY(rig.workspace);
        rig.session.replaceProject(digitizedProject(writeTestImage(rig.dir.path())), true);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.studio.busy() && rig.images.imageWidth() > 0, 20'000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
        rig.workspace->setCurrent(QStringLiteral("segmentation"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.named("segmentationPickArea") != nullptr, 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(rig.regions.regions().size() >= 2, 5'000);
        // Cadre l'image segmentée dans le canevas (vue en mm de scène).
        const auto rect = regionLayerRect(rig.session.project());
        rig.canvas()->zoomToSceneRect(rect.left(), rect.top(), rect.right(), rect.bottom());
        QTRY_VERIFY_WITH_TIMEOUT(rig.canvas()->regionLayer() != nullptr, 5'000);
    }

private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
        qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                     QStringLiteral("Provided by the application"));
    }

    // --- couche « regions » ---------------------------------------------------------------
    void regionsAreDrawnOnTheCanvas() {
        Rig rig;
        open(rig);
        auto* canvas = rig.canvas();
        QVERIFY(canvas->layers().value("regions").toBool());
        QVERIFY(!canvas->layers().value("stitches").toBool());
        const auto layer = canvas->regionLayer();
        QVERIFY(layer);
        const auto& seg = *rig.session.project().segmentation;
        QCOMPARE(layer->fill.width(), seg.width);
        QCOMPARE(layer->fill.height(), seg.height);
        // Le centre du bloc rouge porte la couleur de sa région ; le rectangle est en mm.
        const QColor red = layer->fill.pixelColor(20, 20);
        QVERIFY(red.red() > red.green() && red.red() > red.blue());
        const double mm = rig.session.project().mm_per_px.value;
        QCOMPARE(layer->rectMm.width(), seg.width * mm);
        QCOMPARE(layer->rectMm.center().x(), 0.0);
        // Le rendu logiciel réel affiche bien cette couleur à l'écran (aucun objet QML par région).
        QTest::qWait(150);
        const auto shot = rig.window->grabWindow();
        const auto pos = rig.windowPos(20, 20);
        const QColor onScreen =
            shot.pixelColor(static_cast<int>(pos.x()), static_cast<int>(pos.y()));
        QVERIFY2(onScreen.red() > onScreen.green() + 20 && onScreen.red() > onScreen.blue() + 20,
                 qPrintable(onScreen.name()));
        // Une seule reconstruction pour la révision courante ; pan/zoom n'en provoquent aucune.
        const int fill = rig.space.fillRebuilds();
        const int high = rig.space.highlightRebuilds();
        const auto before = canvas->regionLayer();
        canvas->panBy(15, -9);
        canvas->zoomAt(1.4, 100, 100);
        QTest::qWait(150);
        QCOMPARE(rig.space.fillRebuilds(), fill);
        QCOMPARE(rig.space.highlightRebuilds(), high);
        QCOMPARE(canvas->regionLayer().get(), before.get());
        // L'opacité ne reconstruit pas les textures (même image, révision inchangée).
        rig.space.setOpacity(0.4);
        QTRY_VERIFY(canvas->regionLayer().get() != before.get());
        QCOMPARE(canvas->regionLayer()->fillRevision, before->fillRevision);
        QCOMPARE(rig.space.fillRebuilds(), fill);
        // Contours : une reconstruction du remplissage.
        rig.space.setContours(false);
        QTRY_COMPARE(rig.space.fillRebuilds(), fill + 1);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // --- sélection au clic ----------------------------------------------------------------
    void clickSelectsRegionsAndSyncsTheList() {
        Rig rig;
        open(rig);
        const int fill = rig.space.fillRebuilds();
        QCOMPARE(rig.regions.selectedCount(), 0);
        QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier,
                          rig.windowPos(20, 20).toPoint());
        QTRY_COMPARE(rig.regions.selectedCount(), 1);
        const auto first = rig.regions.selected().at(0).toDouble();
        QCOMPARE(static_cast<std::uint64_t>(first),
                 regionAtPixel(rig.session.project(), 20, 20).value());
        QTRY_VERIFY(rig.canvas()->regionLayer()->highlight.isNull() == false);
        QCOMPARE(highlightAlphaAt(*rig.canvas()->regionLayer(), 20, 20) > 0, true);
        QCOMPARE(highlightAlphaAt(*rig.canvas()->regionLayer(), 120, 90), 0);
        // Le masque ne doit plus mesurer toute l'image pour une petite région.
        QVERIFY(rig.canvas()->regionLayer()->highlight.width() <
                rig.session.project().segmentation->width);
        // Ctrl : ajoute une deuxième région (multi-sélection) ; Ctrl sur la même la retire.
        QTest::mouseClick(rig.window, Qt::LeftButton, Qt::ControlModifier,
                          rig.windowPos(120, 90).toPoint());
        QTRY_COMPARE(rig.regions.selectedCount(), 2);
        QTRY_VERIFY(highlightAlphaAt(*rig.canvas()->regionLayer(), 120, 90) > 0);
        QTest::mouseClick(rig.window, Qt::LeftButton, Qt::ControlModifier,
                          rig.windowPos(120, 90).toPoint());
        QTRY_COMPARE(rig.regions.selectedCount(), 1);
        // Clic simple ailleurs : remplace ; hors image : efface. Le fond n'est jamais reconstruit.
        QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier,
                          rig.windowPos(120, 90).toPoint());
        QTRY_COMPARE(rig.regions.selected().size(), 1);
        QVERIFY(rig.regions.selected().at(0).toDouble() != first);
        QVERIFY(!rig.space.pickAt(1e6, 1e6, false));
        QTRY_COMPARE(rig.regions.selectedCount(), 0);
        QCOMPARE(rig.space.fillRebuilds(), fill);
        // La sélection faite depuis la liste (RegionController) est mise en évidence sur le
        // canevas.
        rig.regions.selectRegion(first);
        QTRY_VERIFY(!rig.canvas()->regionLayer()->highlight.isNull());
        QCOMPARE(rig.space.selectedCount(), 1);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void selectionAndRecolorDoNotRegenerateStitches() {
        Rig rig;
        open(rig);
        const auto scene = rig.studio.scene();
        QVERIFY(scene);
        const auto mapRevision = rig.regions.mapRevision();
        const int fillRebuilds = rig.space.fillRebuilds();
        const int highlightRebuilds = rig.space.highlightRebuilds();
        QSignalSpy geometry(&rig.studio, &StudioController::sceneChanged);

        // A simple click updates the lightweight selection overlay, not the region data.
        rig.regions.selectAtPixel(20, 20);
        QTRY_COMPARE(rig.regions.selectedCount(), 1);
        QTRY_VERIFY(rig.space.highlightRebuilds() > highlightRebuilds);
        QCOMPARE(rig.regions.mapRevision(), mapRevision);
        QCOMPARE(rig.space.fillRebuilds(), fillRebuilds);
        QVERIFY(rig.studio.scene() == scene);

        // Recoloring changes the segmented source only. The generated stitch scene
        // must remain available until an explicit downstream regeneration.
        QCOMPARE(rig.regions.recolorSelected(QStringLiteral("#00aa55")), QString());
        QTRY_VERIFY(rig.regions.mapRevision() > mapRevision);
        QVERIFY(rig.studio.scene() == scene);
        QVERIFY(rig.session.undo());
        QVERIFY(rig.studio.scene() == scene);
        QVERIFY(rig.session.redo());
        QVERIFY(rig.studio.scene() == scene);
        QCOMPARE(geometry.count(), 0);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // --- état publié et mise à jour de l'aval ----------------------------------------------
    void stagePublishedAndDownstreamUpdate() {
        Rig rig;
        open(rig);
        QCOMPARE(rig.stage().value("state").toString(), QStringLiteral("upToDate"));
        QCOMPARE(rig.stage().value("actionLabel").toString(), QStringLiteral("Segmenter l'image"));
        // Une correction de région : étape obsolète (formes non à jour).
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.deleteSelected(), QString());
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("stale"));
        QVERIFY(rig.space.canUpdateShapes());
        // « Mettre à jour les formes » : une seule étape d'annulation, étape de nouveau à jour.
        const auto objects = rig.session.project().embroidery_objects.size();
        const auto depth = rig.session.undoNames().size();
        QSignalSpy done(&rig.regions, &RegionController::regenerated);
        QVERIFY(rig.press("updateShapesButton"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60'000);
        QCOMPARE(rig.session.undoNames().size(), depth + 1);
        QVERIFY(rig.session.project().embroidery_objects.size() < objects);
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("upToDate"));
        rig.session.undo();
        QCOMPARE(rig.session.project().embroidery_objects.size(), objects);
        // Une opération d'image invalide la segmentation : l'étape devient obsolète et l'action du
        // bandeau (workspace.updateRequested) resegmente en une seule étape d'annulation.
        QVERIFY(rig.images.applyPrep(QStringLiteral("grayscale"), {}).isEmpty());
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("stale"));
        QVERIFY(!rig.session.project().segmentation.has_value());
        QVERIFY(rig.named("segmentationEmpty")->isVisible());
        const auto stepsBefore = rig.session.undoNames().size();
        QSignalSpy applied(&rig.images, &ImageController::segmentationApplied);
        rig.workspace->requestUpdate(QStringLiteral("segmentation"));
        QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, 30'000);
        QCOMPARE(rig.session.undoNames().size(), stepsBefore + 1);
        QTRY_VERIFY(rig.canvas()->regionLayer() && !rig.canvas()->regionLayer()->fill.isNull());
        // Les formes sont désormais en retard sur les régions : étape obsolète jusqu'à la mise à
        // jour.
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("stale"));
        QSignalSpy regenerated(&rig.regions, &RegionController::regenerated);
        QTRY_VERIFY(rig.press("updateShapesButton"));
        QTRY_COMPARE_WITH_TIMEOUT(regenerated.count(), 1, 60'000);
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("upToDate"));
        // Quitter l'espace : la publication est retirée (le calcul de base reprend).
        rig.workspace->setCurrent(QStringLiteral("points"));
        QTRY_VERIFY(rig.stage().value("actionLabel").toString().isEmpty());
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // --- parcours repris de l'ancien préparateur, par les panneaux de l'espace -------------
    void panelsDriveSegmentationRegionsAndAi() {
        Rig rig;
        open(rig);
        auto* panels = rig.named("segmentationPanels");
        QVERIFY(panels);
        // Segmentation : réglages saisis, aperçu annulable, application en une étape.
        auto* segmentationSettings = rig.named("segmentationSection");
        QVERIFY(segmentationSettings);
        QVERIFY(segmentationSettings->setProperty("maxColors", 3));
        QVERIFY(segmentationSettings->setProperty("minRegionPx", 25));
        QVERIFY(segmentationSettings->setProperty("smoothing", 2));
        QVERIFY(rig.press("segPreviewButton"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.images.segPreviewActive(), 20'000);
        // F08-01: editing segmentation colours invalidates a completed preview
        // without mutating the project or leaving the old image on screen.
        const auto priorPreviewRevision = rig.session.revision();
        QVERIFY(segmentationSettings->setProperty("maxColors", 4));
        QTRY_VERIFY(!rig.images.segPreviewActive());
        QCOMPARE(rig.session.revision(), priorPreviewRevision);
        QVERIFY(segmentationSettings->setProperty("maxColors", 3));
        QVERIFY(rig.press("segPreviewButton"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.images.segPreviewActive(), 20'000);
        QVERIFY(rig.press("segCancelButton"));
        QVERIFY(!rig.images.segPreviewActive());
        QVERIFY(rig.press("segPreviewButton"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.images.segPreviewActive(), 20'000);
        const auto revision = rig.session.revision();
        const auto depth = rig.session.undoNames().size();
        QCOMPARE(rig.session.revision(), revision); // aperçu : document intact
        QVERIFY(rig.press("segApplyButton"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.session.undoNames().size(), depth + 1, 20'000);
        QVERIFY(rig.regions.stale()); // formes obsolètes après re-segmentation
        QTRY_VERIFY_WITH_TIMEOUT(rig.regions.regions().size() >= 2, 5'000);
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("stale"));
        // Régions : sélection, recoloration, fusion, suppression, régénération.
        panels->setProperty("tab", 1);
        QVERIFY(rig.named("regionStaleBanner"));
        // F08-02: unlike the old fixed 60/true values, the palette preview
        // follows the editable colour-distance and adjacency settings.
        auto* palette = rig.named("regionTools");
        // F08-01: the region editor inherits the *actual* segmentation values,
        // not ImageController's default values. Reconfirm uses the frozen copy.
        QVERIFY(palette);
        const auto liveOptions = palette->property("segmentationOptions").toMap();
        QCOMPARE(liveOptions.value("max_colors").toInt(), 3);
        QCOMPARE(liveOptions.value("min_region_px").toInt(), 25);
        QCOMPARE(liveOptions.value("smoothing_radius_px").toInt(), 2);
        auto* distance = rig.named("regionPaletteDistance");
        auto* adjacent = rig.named("regionMergeAdjacent");
        auto* preview = rig.named("regionPalettePreview");
        QVERIFY(palette && distance && adjacent && preview);
        QVERIFY(palette->setProperty("paletteDistance", 25));
        QVERIFY(palette->setProperty("paletteMergeAdjacent", false));
        QTRY_COMPARE(distance->property("value").toInt(), 25);
        QTRY_VERIFY(!adjacent->property("checked").toBool());
        const auto expectedPreview = rig.regions.previewReduction(
            palette->property("paletteColors").toInt(), 25, false);
        QTRY_COMPARE(preview->property("p").toMap().value("after").toInt(),
                     expectedPreview.value("after").toInt());
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        // F08-02: a user-chosen destination replaces the automatic first
        // neighbour. One real document command, one undo, and target ID kept.
        const auto sourceId = rig.regions.selected().front().toDouble();
        double destinationId = 0;
        for (const auto& item : rig.regions.regions()) {
            const double id = item.toMap().value("id").toDouble();
            if (id != sourceId) {
                destinationId = id;
                break;
            }
        }
        QVERIFY(destinationId > 0);
        const auto beforeTargetMerge = rig.session.undoNames().size();
        QVERIFY(rig.press("regionChooseMergeTarget"));
        QTRY_VERIFY(palette->property("choosingMergeTarget").toBool());
        QVERIFY(QMetaObject::invokeMethod(
            palette, "tappedRegion", Qt::DirectConnection,
            Q_ARG(QVariant, QVariant(destinationId)),
            Q_ARG(QVariant, QVariant(false))));
        QCOMPARE(rig.session.undoNames().size(), beforeTargetMerge + 1);
        QTRY_VERIFY(!palette->property("choosingMergeTarget").toBool());
        QVERIFY(rig.session.undo());
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        rig.named("regionColorField")->setProperty("text", QStringLiteral("#00aa55"));
        QTRY_VERIFY(rig.named("regionRecolor")->isEnabled());
        QVERIFY(rig.press("regionRecolor"));
        QVERIFY(rig.regions.message().contains(QStringLiteral("recolor")));
        // La couche affiche la nouvelle couleur (reconstruction pour cette révision).
        QTRY_VERIFY(rig.canvas()->regionLayer()->fill.pixelColor(20, 20).green() > 120);
        const auto mergeDepth = rig.session.undoNames().size();
        QVERIFY(rig.press("regionMergeNeighbour"));
        QCOMPARE(rig.session.undoNames().size(), mergeDepth + 1);
        QTRY_VERIFY_WITH_TIMEOUT(rig.regions.regions().size() >= 2, 5'000);
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        QVERIFY(rig.press("regionDelete"));
        QVERIFY(rig.press("regionCleanIslands"));
        QVERIFY(rig.press("regionRegenerate"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.regions.busy() && !rig.regions.stale(), 60'000);
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("upToDate"));
        QVERIFY(rig.press("regionTool_brush"));
        QCOMPARE(rig.regions.tool(), QStringLiteral("brush"));
        QVERIFY(rig.press("regionTool_select"));
        // IA : sondage des backends, repli classique explicite.
        panels->setProperty("tab", 2);
        QVERIFY(rig.press("aiProbeButton"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.images.aiBusy() && !rig.images.aiSummary().isEmpty(), 30'000);
        // Request a re-segmentation and verify the exact user settings are
        // captured for the confirmation path even if the UI changes afterward.
        panels->setProperty("tab", 1);
        QVERIFY(rig.press("regionResegment"));
        const auto pending = palette->property("pendingOptions").toMap();
        QCOMPARE(pending.value("max_colors").toInt(), 3);
        QCOMPARE(pending.value("min_region_px").toInt(), 25);
        QCOMPARE(pending.value("smoothing_radius_px").toInt(), 2);
        if (rig.regions.busy())
            rig.regions.cancelTask();
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // Sans contrôleurs d'espace ni d'image (hôte minimal) : l'espace s'ouvre sans erreur.
    void spaceOpensWithoutOptionalControllers() {
        DocumentSession session;
        StudioController studio(session);
        ActionController actions(studio);
        EditController editor(studio);
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this,
                [&](const QList<QQmlError>& errors) {
                    for (const auto& e : errors) {
                        warnings.push_back(e.toString());
                    }
                });
        engine.rootContext()->setContextProperty(QStringLiteral("studio"), &studio);
        engine.rootContext()->setContextProperty(QStringLiteral("actions"), &actions);
        engine.rootContext()->setContextProperty(QStringLiteral("editor"), &editor);
        engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        auto* workspace = window->findChild<WorkspaceController*>();
        QVERIFY(workspace);
        workspace->setCurrent(QStringLiteral("segmentation"));
        QTRY_VERIFY(window->findChild<QObject*>(QStringLiteral("segmentationSpace")));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};

QTEST_MAIN(StudioSpaceSegmentationTest)
#include "test_studio_space_segmentation.moc"
