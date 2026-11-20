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
#include "detail_zone_controller.hpp"
#include "digitize_pipeline.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "image_controller.hpp"
#include "inspector_controller.hpp"
#include "layers/region_layer.hpp"
#include "point_edit.hpp"
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

// 240 x 120 : moitie gauche en degrade lent (brique -> bleu gris), moitie droite en aplat vert.
QString writeGradientImage(const QString& dir) {
    QImage img(240, 120, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (x < 120) {
                const double t = x / 119.0;
                img.setPixelColor(x, y,
                                  QColor(static_cast<int>(200 - 80 * t),
                                         static_cast<int>(120 + 10 * t),
                                         static_cast<int>(90 + 100 * t)));
            } else {
                img.setPixelColor(x, y, QColor(60, 160, 60));
            }
        }
    }
    const QString path = dir + QStringLiteral("/gradient.png");
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
    DetailZoneController detail{studio, images};
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
        const auto viewState = canvas()->viewState();
        const double mm = session.project().mm_per_px.value;
        const auto& seg = *session.project().segmentation;
        const double sx = (px + 0.5 - seg.width / 2.0) * mm;
        const double sy = (py + 0.5 - seg.height / 2.0) * mm;
        const double zoom = viewState.value("zoom").toDouble();
        const QPointF item(
            area->width() / 2.0 + (sx - viewState.value("centerX").toDouble()) * zoom,
            area->height() / 2.0 + (sy - viewState.value("centerY").toDouble()) * zoom);
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
        ctx->setContextProperty(QStringLiteral("detailZones"), &rig.detail);
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
        // A completed operation must not leave a misleading processing badge.
        QVERIFY(!rig.space.busy());
        auto* processing = rig.named("segmentationBusyBadge");
        QVERIFY(processing);
        QVERIFY(!processing->isVisible());
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
        // Le bandeau est un statut, jamais un déclencheur : aucune étiquette d'action (audit UX
        // panneaux Segmentation — « Segmenter l'image »/« Régénérer les objets » vivent
        // uniquement dans le panneau de droite).
        QVERIFY(rig.stage().value("actionLabel").toString().isEmpty());
        // Une correction de région : étape obsolète (formes non à jour).
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.deleteSelected(), QString());
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("stale"));
        QVERIFY(rig.space.canUpdateShapes());
        // « Régénérer les objets » (onglet Régions) : une seule étape d'annulation, étape de
        // nouveau à jour.
        const auto objects = rig.session.project().embroidery_objects.size();
        const auto depth = rig.session.undoNames().size();
        QSignalSpy done(&rig.regions, &RegionController::regenerated);
        QVERIFY(rig.press("regionRegenerate"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60'000);
        QCOMPARE(rig.session.undoNames().size(), depth + 1);
        QVERIFY(rig.session.project().embroidery_objects.size() < objects);
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("upToDate"));
        rig.session.undo();
        QCOMPARE(rig.session.project().embroidery_objects.size(), objects);
        // Une opération d'image invalide la segmentation : l'étape devient obsolète et une
        // demande de mise à jour (workspace.updateRequested, ex. depuis un autre espace)
        // resegmente en une seule étape d'annulation.
        QVERIFY(rig.images.applyPrep(QStringLiteral("grayscale"), {}).isEmpty());
        QTRY_COMPARE(rig.stage().value("state").toString(), QStringLiteral("stale"));
        QVERIFY(!rig.session.project().segmentation.has_value());
        // L'état vide n'a plus de carte dédiée sur le canevas (audit UX panneaux Segmentation,
        // retrait de la bulle d'info redondante avec l'onglet Segmentation) : seul l'état réel
        // compte, pas un élément visuel.
        QVERIFY(!rig.space.hasSegmentation());
        // L'aperçu en direct de l'onglet Segmentation peut recalculer après l'opération d'image :
        // la demande n'est honorée qu'une fois les calculs terminés (garde canSegment).
        QTRY_VERIFY_WITH_TIMEOUT(rig.space.canSegment(), 30'000);
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
        // QTRY_VERIFY réévalue son expression une fois vraie : attendre le bouton actif, puis
        // cliquer une seule fois (le clic lance la régénération et désactive le bouton).
        QTRY_VERIFY(rig.named("regionRegenerate") && rig.named("regionRegenerate")->isEnabled());
        QVERIFY(rig.press("regionRegenerate"));
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
        // Ce parcours teste l'aperçu explicite : l'aperçu en direct a son propre test.
        QVERIFY(segmentationSettings->setProperty("liveEnabled", false));
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
        const auto expectedPreview =
            rig.regions.previewReduction(palette->property("paletteColors").toInt(), 25, false);
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
        QVERIFY(QMetaObject::invokeMethod(palette, "tappedRegion", Qt::DirectConnection,
                                          Q_ARG(QVariant, QVariant(destinationId)),
                                          Q_ARG(QVariant, QVariant(false))));
        QCOMPARE(rig.session.undoNames().size(), beforeTargetMerge + 1);
        QTRY_VERIFY(!palette->property("choosingMergeTarget").toBool());
        QVERIFY(rig.session.undo());
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        // F08-02: colour picker is available and malformed hex never reaches
        // the document mutation. A valid hex colour enables recolouring.
        QVERIFY(rig.named("regionPickColor"));
        auto* recolourField = rig.named("regionColorField");
        QVERIFY(recolourField);
        QVERIFY(recolourField->setProperty("text", QStringLiteral("#not-a-colour")));
        QTRY_VERIFY(!rig.named("regionRecolor")->isEnabled());
        QVERIFY(recolourField->property("invalid").toBool());
        QVERIFY(recolourField->setProperty("text", QStringLiteral("#00aa55")));
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
        QTRY_VERIFY(rig.named("regionToolOptions")->isVisible());
        // Échap quitte le pinceau (Shortcut de Main.qml relayé à l'espace).
        QTest::keyClick(rig.window, Qt::Key_Escape);
        QCOMPARE(rig.regions.tool(), QStringLiteral("select"));
        QVERIFY(rig.press("regionTool_erase"));
        QCOMPARE(rig.regions.tool(), QStringLiteral("erase"));
        QVERIFY(rig.press("selectToolButton"));
        QCOMPARE(rig.regions.tool(), QStringLiteral("select"));
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

private slots:
    // regionMergeColorIntoUndo : selectionner 2 regions, appeler mergeColorInto(idSource, idCible)
    // -> QString vide ; le nombre de regions diminue ; undo restaure.
    void regionMergeColorIntoUndo() {
        Rig rig;
        open(rig);
        const auto beforeCount = rig.regions.regions().size();
        QVERIFY(beforeCount >= 2);
        // Deux aplats VOISINS (haut gauche et haut droit) designes par pixel : la numerotation
        // des regions suit l'ordre des classes du k-means, qui ne garantit pas que les deux
        // premiers identifiants soient adjacents (sinon la fusion recolore sans fusionner).
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        const double sourceId = rig.regions.selected().front().toDouble();
        rig.regions.selectAtPixel(100, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        const double targetId = rig.regions.selected().front().toDouble();
        QVERIFY(sourceId > 0 && targetId > 0 && sourceId != targetId);
        QCOMPARE(rig.regions.mergeColorInto(sourceId, targetId), QString());
        QTRY_COMPARE(rig.regions.regions().size(), beforeCount - 1);
        QVERIFY(rig.session.undo());
        QTRY_COMPARE(rig.regions.regions().size(), beforeCount);
    }

    // regionReducePaletteUndo : previewReduction(2, 60.0, true) renvoie "after" <= "before" ;
    // reducePalette vide ; undo la retire.
    void regionReducePaletteUndo() {
        Rig rig;
        open(rig);
        const auto beforeDepth = rig.session.undoNames().size();
        QVariantMap preview = rig.regions.previewReduction(2, 60.0, true);
        QVERIFY(preview.value("after").toInt() <= preview.value("before").toInt());
        QCOMPARE(rig.regions.reducePalette(2, 60.0, true), QString());
        QCOMPARE(rig.session.undoNames().size(), beforeDepth + 1);
        QVERIFY(rig.session.undo());
        QCOMPARE(rig.session.undoNames().size(), beforeDepth);
    }

    // regionCleanIslandsUndo : cleanIslands(5) renvoie QString vide ou un message sans erreur ; si
    // le projet change, undo restaure.
    void regionCleanIslandsUndo() {
        // Image de test sans petit îlot : message informatif, aucune étape d'annulation créée.
        Rig rig;
        open(rig);
        const auto depth = rig.session.undoNames().size();
        const QString msg = rig.regions.cleanIslands(5);
        QVERIFY(msg.isEmpty() || msg.contains(QStringLiteral("rien")));
        QCOMPARE(rig.session.undoNames().size(), depth);
    }

    // regionFillHolesUndo : même principe avec fillHoles(5).
    void regionFillHolesUndo() {
        Rig rig;
        open(rig);
        const auto depth = rig.session.undoNames().size();
        const QString msg = rig.regions.fillHoles(5);
        QVERIFY(msg.isEmpty() || msg.contains(QStringLiteral("Aucun")));
        QCOMPARE(rig.session.undoNames().size(), depth);
    }

    // regionDeleteSelectedUndo : selectAtPixel(20,20) puis deleteSelected() vide ; un undo restaure
    // le nombre d'objets embroidery_objects.
    void regionDeleteSelectedUndo() {
        Rig rig;
        open(rig);
        const auto beforeObjects = rig.session.project().embroidery_objects.size();
        rig.regions.selectAtPixel(20, 20);
        QTRY_COMPARE(rig.regions.selectedCount(), 1);
        QCOMPARE(rig.regions.deleteSelected(), QString());
        QVERIFY(rig.session.undoNames().size() > 0);
        QVERIFY(rig.session.undo());
        QCOMPARE(rig.session.project().embroidery_objects.size(), beforeObjects);
    }

    // regionPaintStrokeNeedsOneRegion : sans selection, paintStroke({}, false) renvoie un message
    // NON vide (erreur "choisissez d'abord UNE region").
    void regionPaintStrokeNeedsOneRegion() {
        Rig rig;
        open(rig);
        const QString msg = rig.regions.paintStroke(QVariantList{}, false);
        QVERIFY(!msg.isEmpty());
        QVERIFY(msg.contains("choisissez"));
    }

    // regionSelectAllAndInvert : selectAll() puis invertSelection() ; apres inversion la selection
    // est vide.
    void regionSelectAllAndInvert() {
        Rig rig;
        open(rig);
        rig.regions.selectAll();
        QVERIFY(rig.regions.selectedCount() >= 2);
        rig.regions.invertSelection();
        QCOMPARE(rig.regions.selectedCount(), 0);
    }
    // Fusion de la sélection : refusée avec une seule région, une étape d'annulation sinon.
    void regionMergeSelectedNeedsTwoAndUndoes() {
        Rig rig;
        open(rig);
        const auto regions = rig.regions.regions();
        QVERIFY(regions.size() >= 3);
        const auto before = regions.size();
        rig.regions.selectRegion(regions[0].toMap().value("id").toDouble());
        QVERIFY(!rig.regions.mergeSelected().isEmpty());
        rig.regions.selectRegion(regions[1].toMap().value("id").toDouble(), QStringLiteral("add"));
        QCOMPARE(rig.regions.selectedCount(), 2);
        const auto depth = rig.session.undoNames().size();
        QCOMPARE(rig.regions.mergeSelected(), QString());
        QCOMPARE(rig.session.undoNames().size(), depth + 1);
        QTRY_VERIFY(rig.regions.regions().size() < before);
        rig.session.undo();
        QTRY_COMPARE(rig.regions.regions().size(), before);
    }

    // Fusion d'une région source dans une cible choisie : la cible garde son identité.
    void regionMergeSelectedIntoKeepsTheTarget() {
        Rig rig;
        open(rig);
        const auto regions = rig.regions.regions();
        QVERIFY(regions.size() >= 2);
        const auto before = regions.size();
        const double source = regions[0].toMap().value("id").toDouble();
        const double target = regions[1].toMap().value("id").toDouble();
        rig.regions.selectAll();
        QVERIFY(!rig.regions.mergeSelectedInto(target).isEmpty()); // plusieurs sources : refus
        rig.regions.selectRegion(source);
        QCOMPARE(rig.regions.mergeSelectedInto(target), QString());
        QTRY_COMPARE(rig.regions.regions().size(), before - 1);
        bool targetKept = false;
        for (const auto& region : rig.regions.regions()) {
            targetKept = targetKept || region.toMap().value("id").toDouble() == target;
        }
        QVERIFY(targetKept);
        rig.session.undo();
        QTRY_COMPARE(rig.regions.regions().size(), before);
    }

    // Fusion dans la voisine : une région du quadrillage a toujours une voisine.
    void regionMergeWithNeighbourUndoes() {
        Rig rig;
        open(rig);
        const auto regions = rig.regions.regions();
        QVERIFY(regions.size() >= 2);
        const auto before = regions.size();
        QCOMPARE(rig.regions.mergeWithNeighbour(regions[0].toMap().value("id").toDouble()),
                 QString());
        QTRY_COMPARE(rig.regions.regions().size(), before - 1);
        rig.session.undo();
        QTRY_COMPARE(rig.regions.regions().size(), before);
    }

    // Sélection des voisines : sans sélection rien ne change, sinon la sélection s'étend.
    void regionSelectNeighboursExpandsTheSelection() {
        Rig rig;
        open(rig);
        rig.regions.clearSelection();
        rig.regions.selectNeighbours();
        QCOMPARE(rig.regions.selectedCount(), 0);
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        rig.regions.selectNeighbours();
        QVERIFY(rig.regions.selectedCount() >= 2);
    }
    // Parcours des régions (suivante / précédente, avec bouclage) et pas du rayon du pinceau.
    void regionStepAndBrushRadiusSteps() {
        Rig rig;
        open(rig);
        const auto regions = rig.regions.regions();
        const int n = static_cast<int>(regions.size());
        QVERIFY(n >= 2);
        auto idAt = [&](int i) { return regions[i].toMap().value("id").toDouble(); };
        rig.regions.clearSelection();
        QVERIFY(rig.regions.selectNextRegion()); // sans sélection : la première
        QCOMPARE(rig.regions.selectedCount(), 1);
        QVERIFY(rig.regions.selectNextRegion());
        rig.regions.selectRegion(idAt(n - 1));
        QVERIFY(rig.regions.selectNextRegion());     // bouclage vers la première
        QVERIFY(rig.regions.selectPreviousRegion()); // bouclage vers la dernière
        QCOMPARE(rig.regions.selectedCount(), 1);
        rig.regions.clearSelection();
        QVERIFY(rig.regions.selectPreviousRegion()); // sans sélection : la dernière
        QCOMPARE(rig.regions.selectedCount(), 1);
        // Boutons de l'espace : mêmes actions.
        QVERIFY(rig.press("regionNext"));
        QCOMPARE(rig.regions.selectedCount(), 1);

        const double radius = rig.regions.brushRadius();
        rig.regions.stepBrushRadius(1);
        QCOMPARE(rig.regions.brushRadius(), radius + 1.0);
        rig.regions.stepBrushRadius(-2);
        QCOMPARE(rig.regions.brushRadius(), radius - 1.0);
        rig.regions.stepBrushRadius(-1000);
        QCOMPARE(rig.regions.brushRadius(), 1.0);
        rig.regions.stepBrushRadius(1000);
        QCOMPARE(rig.regions.brushRadius(), 200.0);
    }
    // Verrouillage (état de session) : une région verrouillée ne peut pas être sélectionnée, est
    // exclue de "Toutes" et ne peut pas être la cible d'une fusion ; déverrouiller la rend à
    // nouveau disponible pour les deux.
    void regionLockPreventsSelectionAndMergeTarget() {
        Rig rig;
        open(rig);
        const auto regions = rig.regions.regions();
        QVERIFY(regions.size() >= 3);
        const double a = regions[0].toMap().value("id").toDouble();
        const double b = regions[1].toMap().value("id").toDouble();

        QVERIFY(!rig.regions.isRegionLocked(a));
        rig.regions.selectRegion(a);
        rig.regions.toggleLockRegion(a);
        QVERIFY(rig.regions.isRegionLocked(a));
        QCOMPARE(rig.regions.selectedCount(), 0); // verrouiller désélectionne

        rig.regions.selectRegion(a);
        QCOMPARE(rig.regions.selectedCount(), 0); // toujours refusée tant que verrouillée

        rig.regions.selectAll();
        for (const auto& id : rig.regions.selected()) {
            QVERIFY(id.toDouble() != a);
        }

        rig.regions.clearSelection();
        rig.regions.selectRegion(b);
        QVERIFY(!rig.regions.mergeSelectedInto(a).isEmpty()); // cible verrouillée : refus

        rig.regions.toggleLockRegion(a);
        QVERIFY(!rig.regions.isRegionLocked(a));
        rig.regions.selectRegion(a);
        QCOMPARE(rig.regions.selectedCount(), 1); // déverrouillée : de nouveau sélectionnable
    }
    // Aperçu en direct (V1) : un changement de réglage relance l'aperçu tout seul, sans toucher au
    // document ; la case le désactive.
    void segmentationPreviewIsLiveWhenSettingsChange() {
        Rig rig;
        open(rig);
        auto* settings = rig.named("segmentationSection");
        QVERIFY(settings);
        QVERIFY(settings->property("liveEnabled").toBool());
        QVERIFY(rig.named("segLiveBox"));
        const auto revision = rig.session.revision();
        QVERIFY(settings->setProperty("maxColors", 3));
        QTRY_VERIFY_WITH_TIMEOUT(rig.images.segPreviewActive(), 20'000);
        QVERIFY(settings->setProperty("maxColors", 4)); // invalide puis relance
        QTRY_VERIFY_WITH_TIMEOUT(rig.images.segPreviewActive(), 20'000);
        QCOMPARE(rig.session.revision(), revision);
        // L'aperçu est posé SUR le canevas, à l'échelle de la caméra ; les régions du projet sont
        // masquées pendant ce temps.
        auto* overlay = rig.named("segmentationLivePreview");
        QVERIFY(overlay);
        QTRY_VERIFY_WITH_TIMEOUT(overlay->isVisible(), 5'000);
        const auto view = rig.canvas()->viewState();
        const double expectedWidth =
            rig.images.imageWidth() * rig.images.mmPerPx() * view.value("zoom").toDouble();
        QVERIFY(std::abs(overlay->width() - expectedWidth) < 1.0);
        auto* space = rig.named("segmentationSpace");
        QVERIFY(space);
        QVERIFY(!space->property("canvasLayers").toMap().value("regions").toBool());
        QVERIFY(rig.press("segCancelButton"));
        QTRY_VERIFY(!rig.images.segPreviewActive());
        QTRY_VERIFY(space->property("canvasLayers").toMap().value("regions").toBool());
        QVERIFY(!overlay->isVisible());
        QVERIFY(rig.press("segLiveBox"));
        QVERIFY(!settings->property("liveEnabled").toBool());
        QVERIFY(settings->setProperty("maxColors", 5));
        auto* timer = rig.window->findChild<QObject*>(QStringLiteral("segLiveTimer"));
        QVERIFY(timer);
        QVERIFY(!timer->property("running").toBool()); // plus d'aperçu automatique
        QCOMPARE(rig.session.revision(), revision);
    }

    // Suppr dans l'espace Segmentation supprime les régions sélectionnées (une étape d'annulation),
    // comme le bouton Supprimer.
    void deleteKeyRemovesTheSelectedRegions() {
        Rig rig;
        open(rig);
        const auto before = rig.regions.regions().size();
        const auto depth = rig.session.undoNames().size();
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        rig.window->requestActivate();
        QTest::keyClick(rig.window, Qt::Key_Delete);
        QTRY_COMPARE(rig.session.undoNames().size(), depth + 1);
        QTRY_VERIFY(rig.regions.regions().size() < before);
        QCOMPARE(rig.regions.selectedCount(), 0);
        QVERIFY(rig.session.undo());
        QTRY_COMPARE(rig.regions.regions().size(), before);
    }
    // Le nombre de couleurs distinctes de la segmentation ne dépasse jamais « Couleurs maximum »
    // et il est affiché (aperçu et projet).
    void segmentationColourCountRespectsTheMaximumAndIsShown() {
        Rig rig;
        open(rig);
        for (const int maximum : {2, 3}) {
            QSignalSpy applied(&rig.images, &ImageController::segmentationApplied);
            rig.images.applySegmentation({{QStringLiteral("max_colors"), maximum},
                                          {QStringLiteral("min_region_px"), 16},
                                          {QStringLiteral("smoothing_radius_px"), 1}});
            QTRY_VERIFY_WITH_TIMEOUT(applied.count() >= 1, 30'000);
            QVERIFY(rig.images.colorCount() >= 1);
            QVERIFY2(rig.images.colorCount() <= maximum,
                     qPrintable(QStringLiteral("%1 couleurs pour un maximum de %2")
                                    .arg(rig.images.colorCount())
                                    .arg(maximum)));
        }
        rig.images.previewSegmentation({{QStringLiteral("max_colors"), 3}});
        QTRY_VERIFY_WITH_TIMEOUT(rig.images.segPreviewActive(), 20'000);
        QVERIFY(rig.images.segPreviewInfo().contains(QStringLiteral("couleurs")));
    }

    // --- audit par zone : coquilles de l'espace Segmentation ---------------------------------
    // « Re-segmenter » applique le lissage du panneau : la cle envoyee par le QML
    // (smoothing_radius_px) est bien celle que lit RegionController.
    void resegmentHonoursThePanelSmoothing() {
        Rig rig;
        open(rig);
        const auto options = [](int smoothing) {
            return QVariantMap{{QStringLiteral("max_colors"), 16},
                               {QStringLiteral("min_region_px"), 2},
                               {QStringLiteral("smoothing_radius_px"), smoothing}};
        };
        const auto countAfterApply = [&](int smoothing) {
            QSignalSpy applied(&rig.images, &ImageController::segmentationApplied);
            rig.images.applySegmentation(options(smoothing));
            if (!QTest::qWaitFor([&] { return applied.count() >= 1; }, 30'000)) {
                return -1;
            }
            return rig.images.regionCount();
        };
        const int rough = countAfterApply(0);
        const int smooth = countAfterApply(10);
        QVERIFY(rough > 0 && smooth > 0);
        // Sans ecart entre les deux lissages, le test ne prouverait rien.
        QVERIFY2(rough != smooth, "l'image de test ne distingue pas les lissages 0 et 10");
        // Document a 10 de lissage ; « Re-segmenter » a 0 doit retrouver la segmentation brute.
        QSignalSpy done(&rig.regions, &RegionController::regenerated);
        rig.regions.regenerate(true, true, options(0));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60'000);
        QCOMPARE(rig.images.regionCount(), rough);
    }

    // Une seule demande de confirmation, portee par RegionController et affichee par l'onglet
    // Regions (la carte bas-gauche n'a plus que des reglages d'affichage, audit UX panneaux
    // Segmentation) : confirmer execute la demande affichee (ici une re-segmentation).
    void overwriteConfirmationIsSharedAndRunsTheRequestedAction() {
        Rig rig;
        open(rig);
        // Une retouche de point sur un objet genere : la regeneration doit demander confirmation.
        std::uint64_t generated = 0;
        for (const auto& e : rig.session.project().embroidery_objects) {
            const auto* v = rig.session.project().findObject(e.source_vector);
            if (v != nullptr && v->source_region) {
                generated = e.id.value;
                break;
            }
        }
        QVERIFY(generated != 0);
        const auto view = stitchPoints(rig.session.project(), generated);
        QVERIFY(view.has_value());
        bool moved = false;
        for (const auto& p : view->points) {
            if (p.movable) {
                moved = moveStitchPoints(rig.session, generated,
                                         {{p.index, PointMm{p.pos.x + 1.0, p.pos.y}}})
                            .ok;
                break;
            }
        }
        QVERIFY(moved);
        auto* panels = rig.named("segmentationPanels");
        QVERIFY(panels);
        panels->setProperty("tab", 1);
        const auto depth = rig.session.undoNames().size();
        QVERIFY(rig.press("regionResegment"));
        // Rien n'est modifie tant que la question n'est pas tranchee.
        QTRY_VERIFY(!rig.regions.confirmText().isEmpty());
        QCOMPARE(rig.space.confirmText(), rig.regions.confirmText());
        QTRY_VERIFY(rig.named("regionConfirm")->isVisible());
        QCOMPARE(rig.session.undoNames().size(), depth);
        QVERIFY(!rig.regions.busy());
        // « Garder mes retouches » referme la question sans rien executer.
        QVERIFY(rig.press("regionConfirmNo"));
        QTRY_VERIFY(rig.regions.confirmText().isEmpty());
        QTRY_VERIFY(!rig.named("regionConfirm")->isVisible());
        QCOMPARE(rig.session.undoNames().size(), depth);
        // Nouvelle demande de re-segmentation, confirmee : c'est bien une re-segmentation qui
        // s'execute (la segmentation est remplacee), pas une simple regeneration des objets.
        const auto regionsBefore = rig.regions.regions().size();
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.deleteSelected(), QString()); // la segmentation differe de l'image
        QTRY_COMPARE(rig.regions.regions().size(), regionsBefore - 1);
        QVERIFY(rig.press("regionResegment"));
        QTRY_VERIFY(rig.named("regionConfirm")->isVisible());
        QSignalSpy done(&rig.regions, &RegionController::regenerated);
        QVERIFY(rig.press("regionConfirmYes"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60'000);
        QTRY_VERIFY(rig.regions.confirmText().isEmpty());
        QTRY_VERIFY(!rig.named("regionConfirm")->isVisible());
        // L'image a bien ete re-segmentee (avec les reglages du panneau, differents de ceux du
        // projet d'origine) : la region supprimee n'est plus manquante.
        QTRY_VERIFY(rig.regions.regions().size() >= regionsBefore);
    }

    // « Choisir la cible » : un clic sur le canevas designe la region qui absorbe la source, au
    // lieu de remplacer la selection.
    void mergeTargetCanBeChosenOnTheCanvas() {
        Rig rig;
        open(rig);
        rig.regions.selectAtPixel(20, 20);
        QCOMPARE(rig.regions.selectedCount(), 1);
        const auto before = rig.regions.regions().size();
        const auto depth = rig.session.undoNames().size();
        rig.regions.setMergeTargetMode(true);
        QVERIFY(rig.regions.mergeTargetMode());
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, rig.windowPos(120, 20).toPoint());
        QTRY_COMPARE(rig.session.undoNames().size(), depth + 1);
        QTRY_COMPARE(rig.regions.regions().size(), before - 1);
        QVERIFY(!rig.regions.mergeTargetMode());
        QVERIFY(rig.session.undo());
        // Sans region source unique, le mode est refuse.
        rig.regions.clearSelection();
        rig.regions.setMergeTargetMode(true);
        QVERIFY(!rig.regions.mergeTargetMode());
    }

    // Lien amont « Aller a la region » de l'espace Vecteurs : la region posee dans la selection
    // partagee est selectionnee a l'entree dans l'espace (liste et canevas).
    void sharedRegionSelectionIsAdoptedOnEntry() {
        Rig rig;
        open(rig);
        const auto id =
            static_cast<std::uint64_t>(rig.regions.regions().back().toMap().value("id").toDouble());
        rig.workspace->setCurrent(QStringLiteral("points"));
        QTRY_VERIFY(rig.stage().value("actionLabel").toString().isEmpty());
        rig.regions.clearSelection();
        rig.session.selection().replace({Target{TargetKind::Region, id}});
        rig.workspace->setCurrent(QStringLiteral("segmentation"));
        QTRY_COMPARE(rig.regions.selectedCount(), 1);
        QCOMPARE(static_cast<std::uint64_t>(rig.regions.selected().front().toDouble()), id);
        // Une selection locale faite ensuite n'est pas ecrasee par la meme cible au retour.
        rig.regions.clearSelection();
        rig.workspace->setCurrent(QStringLiteral("points"));
        QTRY_VERIFY(rig.stage().value("actionLabel").toString().isEmpty());
        rig.workspace->setCurrent(QStringLiteral("segmentation"));
        // Le bandeau reste un statut, jamais une action (audit UX panneaux Segmentation).
        QTRY_VERIFY(rig.stage().value("actionLabel").toString().isEmpty());
        QCOMPARE(rig.regions.selectedCount(), 0);
    }

    // L'apercu de segmentation ne masque pas les regions hors de son onglet ni apres avoir quitte
    // l'espace.
    void previewIsDroppedOutsideTheSegmentationTab() {
        Rig rig;
        open(rig);
        auto* panels = rig.named("segmentationPanels");
        auto* settings = rig.named("segmentationSection");
        QVERIFY(panels && settings);
        QVERIFY(settings->setProperty("liveEnabled", false));
        QVERIFY(rig.press("segPreviewButton"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.images.segPreviewActive(), 20'000);
        panels->setProperty("tab", 1);
        QTRY_VERIFY(!rig.images.segPreviewActive());
        panels->setProperty("tab", 0);
        QVERIFY(rig.press("segPreviewButton"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.images.segPreviewActive(), 20'000);
        rig.workspace->setCurrent(QStringLiteral("points"));
        QTRY_VERIFY(!rig.images.segPreviewActive());
    }

    // Zones de fondu (experimental) : la case du panneau porte le mode jusqu'au moteur ;
    // « Mettre a jour les formes » coud la zone en degrade en fondu, en un pas d'annulation.
    void blendZonesExperimentSewsGradientZoneAsFade() {
        Rig rig;
        open(rig);
        rig.session.replaceProject(digitizedProject(writeGradientImage(rig.dir.path())), true);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.studio.busy() && rig.images.imageWidth() == 240, 20'000);
        auto* settings = rig.named("segmentationSection");
        QVERIFY(settings);
        QVERIFY(settings->setProperty("liveEnabled", false));
        QVERIFY(rig.named("segBlendZones") != nullptr);
        QVERIFY(!settings->property("blendZones").toBool());
        QVERIFY(settings->setProperty("blendZones", true));
        QVERIFY(settings->setProperty("blendThreads", 4));
        const auto options = settings->property("options").toMap();
        QCOMPARE(options.value(QStringLiteral("blend_zones")).toBool(), true);
        QCOMPARE(options.value(QStringLiteral("blend_threads")).toInt(), 4);

        QSignalSpy applied(&rig.images, &ImageController::segmentationApplied);
        QVERIFY(rig.press("segApplyButton"));
        QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, 60'000);
        // Degrade a gauche, aplat a droite : deux zones, pas des bandes.
        QCOMPARE(rig.session.project().imageSource().segmentation->region_count(), 2u);
        QVERIFY(rig.images.lastSegmentationOptions().value(QStringLiteral("blend_zones")).toBool());

        const auto depth = rig.session.undoNames().size();
        QSignalSpy regenerated(&rig.regions, &RegionController::regenerated);
        rig.space.updateShapes(true);
        QTRY_COMPARE_WITH_TIMEOUT(regenerated.count(), 1, 60'000);
        QCOMPARE(rig.session.undoNames().size(), depth + 1);
        const auto fades = [&rig] {
            return std::count_if(rig.session.project().embroidery_objects.begin(),
                                 rig.session.project().embroidery_objects.end(),
                                 [](const auto& e) { return e.is_multicolor_gradient(); });
        };
        QVERIFY(fades() >= 1);
        rig.session.undo();
        QCOMPARE(fades(), 0);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // --- zones de détail --------------------------------------------------------------------
    void detailBrushPaintsUndoablyAndOnlyOneToolIsActive() {
        Rig rig;
        open(rig);
        QVERIFY(!rig.detail.hasDetail());
        QVERIFY(rig.named("regionTool_detail") != nullptr);
        QVERIFY(rig.named("regionTool_detailErase") != nullptr);
        // Un seul outil modal à la fois : le pinceau de détail éteint celui des régions.
        QVERIFY(rig.press("regionTool_brush"));
        QCOMPARE(rig.regions.tool(), QStringLiteral("brush"));
        QVERIFY(rig.press("regionTool_detail"));
        QCOMPARE(rig.detail.tool(), QStringLiteral("paint"));
        QCOMPARE(rig.regions.tool(), QStringLiteral("select"));
        QVERIFY(rig.press("regionTool_brush"));
        QCOMPARE(rig.detail.tool(), QString());
        QVERIFY(rig.press("regionTool_detail"));

        // Un trait de 6 mm de long, au centre de l'image : UNE étape d'annulation.
        const auto depth = rig.session.undoNames().size();
        rig.detail.setRadiusMm(2.0);
        QVariantList stroke;
        for (const double x : {-3.0, 0.0, 3.0}) {
            stroke.push_back(QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), 0.0}});
        }
        QCOMPARE(rig.detail.paintStroke(stroke), QString());
        QCOMPARE(rig.session.undoNames().size(), depth + 1);
        QTRY_VERIFY_WITH_TIMEOUT(rig.detail.hasDetail(), 5'000);
        QVERIFY(rig.detail.coveragePercent() > 0.0);
        QVERIFY(rig.detail.needsSegmentation());
        const auto mask = rig.session.project().imageSource().detail_mask;
        QVERIFY(mask.any_detail());
        QVERIFY(mask.matches(rig.images.imageWidth(), rig.images.imageHeight()));
        QVERIFY(!rig.images.images()->get(QStringLiteral("detail")).isNull());

        // Gomme : le même trait retire la zone ; un rayon absurde est refusé sans étape.
        rig.detail.setTool(QStringLiteral("erase"));
        QCOMPARE(rig.detail.paintStroke(stroke), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.detail.hasDetail(), 5'000);
        rig.detail.setRadiusMm(1000.0); // borné, jamais refusé : la borne est celle du service
        QCOMPARE(rig.detail.radiusMm(), 50.0);

        // Annuler remet la zone peinte.
        QVERIFY(rig.session.undo());
        QTRY_VERIFY_WITH_TIMEOUT(rig.detail.hasDetail(), 5'000);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void detailStrokeOutsideTheImageIsRefusedWithoutAnUndoStep() {
        Rig rig;
        open(rig);
        const auto depth = rig.session.undoNames().size();
        rig.detail.setTool(QStringLiteral("paint"));
        const auto refusal = rig.detail.paintStroke(
            {QVariantMap{{QStringLiteral("x"), 1e6}, {QStringLiteral("y"), 1e6}}});
        QVERIFY(!refusal.isEmpty());
        QVERIFY(rig.detail.messageIsError());
        QCOMPARE(rig.session.undoNames().size(), depth);
        QVERIFY(!rig.detail.hasDetail());
    }
};

QTEST_MAIN(StudioSpaceSegmentationTest)
#include "test_studio_space_segmentation.moc"
