// SPDX-License-Identifier: Apache-2.0
// Espace « Vectorisation » (V01) : le vrai Main.qml, le vrai WorkspaceController, un vrai projet
// dont les formes viennent des régions d'une segmentation (vectorisation réelle). Couche vectors,
// clic de sélection, outil de forme + annulation, lien vers la région d'origine, état publié, mise
// à jour depuis la segmentation après une correction de région.
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest/QtTest>

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <vector>

#include "action_catalog.hpp"
#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "layers/vector_layer.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/commands/replace_project_command.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/vectorization/vectorize.hpp"
#include "studio_controller.hpp"
#include "tool_controller.hpp"
#include "vector_controller.hpp"
#include "vector_space_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

// Image 40 x 20 px : moitié gauche rouge (région 1), moitié droite bleue (région 2) ; 0,25 mm/px.
document::Project shapesProject() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    project.mm_per_px = Millimeters{0.25};
    project.original.width = 40;
    project.original.height = 20;
    segmentation::Segmentation seg;
    seg.width = 40;
    seg.height = 20;
    seg.labels.assign(40 * 20, 0);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x) {
            seg.labels[static_cast<std::size_t>(y * 40 + x)] = x < 20 ? 1 : 2;
        }
    }
    for (std::uint64_t id = 1; id <= 2; ++id) {
        segmentation::Region region;
        region.id = RegionId{id};
        region.rgb = id == 1 ? std::array<std::uint8_t, 3>{200, 30, 30}
                             : std::array<std::uint8_t, 3>{30, 40, 200};
        region.pixel_count = 400;
        seg.region_slots.push_back(region);
    }
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x) {
            const bool left = x < 20;
            project.original.rgba.insert(project.original.rgba.end(),
                                         {static_cast<std::uint8_t>(left ? 200 : 30),
                                          static_cast<std::uint8_t>(left ? 30 : 40),
                                          static_cast<std::uint8_t>(left ? 30 : 200), 255});
        }
    }
    project.segmentation = seg;
    const vectorization::VectorizeOptions options{.mm_per_px = project.mm_per_px};
    const auto sets = vectorization::vectorize_regions(*project.segmentation,
                                                       {RegionId{1}, RegionId{2}}, options);
    project.object_ids.reset(100);
    for (std::uint64_t id = 1; id <= 2; ++id) {
        document::VectorObject shape;
        shape.id = project.object_ids.next();
        shape.name = "Region " + std::to_string(id);
        shape.source_region = RegionId{id};
        shape.rgb = project.segmentation->find(RegionId{id})->rgb;
        shape.paths = *sets[id - 1];
        project.vector_objects.push_back(std::move(shape));
    }
    return project;
}

// N carrés de 8 mm (pas 10 mm) sans segmentation : de quoi dépasser kSyncShapeLimit.
document::Project manyShapesProject(int count) {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    project.object_ids.reset(1000);
    for (int i = 0; i < count; ++i) {
        document::VectorObject shape;
        shape.id = project.object_ids.next();
        shape.name = "Forme " + std::to_string(i);
        shape.rgb = {static_cast<std::uint8_t>(40 + i % 200), 90, 160};
        const int x0 = (i % 32) * 10000 - 150000;
        const int y0 = (i / 32) * 10000 - 150000;
        geometry::PathSet set;
        for (const auto& [dx, dy] : {std::pair{0, 0}, {8000, 0}, {8000, 8000}, {0, 8000}}) {
            set.outer.nodes.push_back({Vec2um{Micrometers{x0 + dx}, Micrometers{y0 + dy}}});
        }
        shape.paths.push_back(std::move(set));
        project.vector_objects.push_back(std::move(shape));
    }
    return project;
}

QQuickItem* deepItem(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) {
        return root;
    }
    for (auto* child : root->childItems()) {
        if (auto* found = deepItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

void collectByShortcutLabel(QQuickItem* root, const QString& label, QList<QQuickItem*>& out) {
    if (root->property("shortcutLabel").toString() == label) {
        out.push_back(root);
    }
    for (auto* child : root->childItems()) {
        collectByShortcutLabel(child, label, out);
    }
}

struct Rig {
    DocumentSession session;
    StudioController controller{session};
    ActionController actions{controller};
    EditController editor{controller};
    ToolController tools{controller};
    VectorController vectors{controller};
    VectorSpaceController stage{controller};
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    WorkspaceController* workspace{nullptr};
    GpuCanvas* canvas{nullptr};
};

} // namespace

class StudioSpaceVectorTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig, bool withProject = true) {
        connect(&rig.engine, &QQmlApplicationEngine::warnings, this,
                [&rig](const QList<QQmlError>& errors) {
                    for (const auto& error : errors) {
                        rig.warnings.push_back(error.toString());
                    }
                });
        auto* ctx = rig.engine.rootContext();
        ctx->setContextProperty(QStringLiteral("studio"), &rig.controller);
        ctx->setContextProperty(QStringLiteral("actions"), &rig.actions);
        ctx->setContextProperty(QStringLiteral("editor"), &rig.editor);
        ctx->setContextProperty(QStringLiteral("tools"), &rig.tools);
        ctx->setContextProperty(QStringLiteral("vectors"), &rig.vectors);
        ctx->setContextProperty(QStringLiteral("vectorStage"), &rig.stage);
        if (withProject) {
            rig.session.replaceProject(shapesProject(), false);
        }
        rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
        rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
        QVERIFY(rig.window);
        rig.workspace = rig.window->findChild<WorkspaceController*>();
        QVERIFY(rig.workspace);
        QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy(), 15'000);
    }
    void enterSpace(Rig& rig) {
        rig.workspace->setCurrent(QStringLiteral("vector"));
        QTRY_VERIFY_WITH_TIMEOUT(
            rig.window->findChild<QObject*>(QStringLiteral("vectorSpace")) != nullptr, 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(
            rig.window->findChild<GpuCanvas*>(QStringLiteral("vectorCanvas")) != nullptr, 5'000);
        rig.canvas = rig.window->findChild<GpuCanvas*>(QStringLiteral("vectorCanvas"));
        QVERIFY(rig.canvas);
        QTRY_VERIFY_WITH_TIMEOUT(rig.canvas->width() > 0 && rig.canvas->height() > 0, 5'000);
        rig.canvas->zoomToScale(40.0);
        rig.canvas->centerOnScene(0.0, 0.0);
        // Changer d'espace remet les outils à la Sélection en différé (Qt.callLater, SpaceHost) :
        // vider la file d'événements avant de choisir un outil, sinon la remise à zéro tombe au
        // milieu du geste suivant.
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
    // Pixel fenêtre d'un point de scène (mm, Y vers le bas).
    QPoint at(Rig& rig, double xMm, double yMm) {
        const auto s = rig.canvas->camera().worldToScreen({xMm, yMm});
        return rig.canvas->mapToScene(QPointF(s.x, s.y)).toPoint();
    }
    static bool reddish(const QColor& c) { return c.red() > 140 && c.blue() < 100; }
    static bool bluish(const QColor& c) { return c.blue() > 140 && c.red() < 100; }
    [[nodiscard]] std::size_t steps(Rig& rig) { return rig.session.undoNames().size(); }

private slots:
    void initTestCase() {
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        // Backend logiciel par défaut (CI, offscreen). OPENSTITCH_TEST_RHI=1 joue les mêmes tests
        // de pixels sur le backend RHI natif, donc sur les triangles GPU de la couche vecteurs
        // (lancer avec QT_ENABLE_HIGHDPI_SCALING=0 : les tests visent des pixels logiques).
        if (qEnvironmentVariableIntValue("OPENSTITCH_TEST_RHI") != 1) {
            QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        }
        qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
        qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                     QStringLiteral("Provided by the application"));
        qmlRegisterUncreatableType<EditController>("OpenStitch.Studio", 1, 0, "EditController",
                                                   QStringLiteral("Provided by the application"));
    }

    void spaceDrawsTheShapesFromTheRegions() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QCOMPARE(rig.stage.shapeCount(), 2);
        QVERIFY(!rig.canvas->layers().value("stitches").toBool());
        QVERIFY(rig.canvas->layers().value("vectors").toBool());
        QVERIFY(rig.canvas->vectorLayer() != nullptr);
        QVERIFY(rig.window->findChild<QObject*>(QStringLiteral("spaceUnderConstruction")) ==
                nullptr);
        // Vrai rendu : la forme rouge à gauche, la bleue à droite, fond libre ailleurs.
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const auto image = rig.window->grabWindow();
                const auto l = at(rig, -2.5, 0.0);
                const auto r = at(rig, 2.5, 0.0);
                return reddish(image.pixelColor(l)) && bluish(image.pixelColor(r));
            }(),
            8'000);
        // Une reconstruction par révision : le pan/zoom ne reconstruit rien.
        const int builds = rig.stage.layerBuildCount();
        rig.canvas->zoomToScale(25.0);
        rig.canvas->panBy(10.0, 5.0);
        QCOMPARE(rig.stage.layerBuildCount(), builds);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void republishingImmutableCanvasLayersIsAFreeNoOp() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        const auto vectorImage = rig.canvas->vectorLayer();
        QVERIFY(vectorImage);
        const auto firstRevision = rig.canvas->overlayRevision();

        rig.canvas->setVectorLayer(vectorImage);
        QCOMPARE(rig.canvas->overlayRevision(), firstRevision);
        rig.canvas->setOverlays(std::make_shared<CanvasOverlays>());
        const auto overlaysRevision = rig.canvas->overlayRevision();
        rig.canvas->setOverlays(rig.canvas->overlays());
        QCOMPARE(rig.canvas->overlayRevision(), overlaysRevision);

        // Changer la texture vectorielle reste visible même sans incrémenter la révision
        // des overlays. Le backend logiciel détecte l'identité de la couche séparément.
        const QPoint left = at(rig, -2.5, 0.0);
        QTRY_VERIFY_WITH_TIMEOUT(reddish(rig.window->grabWindow().pixelColor(left)), 5'000);
        rig.canvas->setVectorLayer(nullptr);
        QCOMPARE(rig.canvas->overlayRevision(), overlaysRevision);
        QTRY_VERIFY_WITH_TIMEOUT(!reddish(rig.window->grabWindow().pixelColor(left)), 5'000);
        rig.canvas->setVectorLayer(vectorImage);
        QCOMPARE(rig.canvas->overlayRevision(), overlaysRevision);
        QTRY_VERIFY_WITH_TIMEOUT(reddish(rig.window->grabWindow().pixelColor(left)), 5'000);
    }

    void selectionKeepsVectorRasterAndAvoidsSecondaryTexture() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        const auto background = rig.canvas->vectorLayer();
        const int baseBuilds = rig.stage.layerBuildCount();
        QVERIFY(background);
        const QPoint left = at(rig, -2.5, 0.0);
        const QPoint right = at(rig, 2.5, 0.0);
        const QColor leftBefore = rig.window->grabWindow().pixelColor(left);
        const QColor rightBefore = rig.window->grabWindow().pixelColor(right);

        QTest::mouseClick(rig.window, Qt::LeftButton, {}, left);
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().primary().has_value(), 3'000);
        QCOMPARE(rig.stage.layerBuildCount(), baseBuilds);
        QVERIFY(rig.canvas->vectorLayer() == background);
        QCOMPARE(rig.window->grabWindow().pixelColor(left), leftBefore);
        QCOMPARE(rig.window->grabWindow().pixelColor(right), rightBefore);

        QTest::mouseClick(rig.window, Qt::LeftButton, {}, right);
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().primary().has_value(), 3'000);
        QCOMPARE(rig.session.selection().primary()->kind, TargetKind::VectorObject);
        QCOMPARE(rig.stage.layerBuildCount(), baseBuilds);
        QVERIFY(rig.canvas->vectorLayer() == background);
        QCOMPARE(rig.window->grabWindow().pixelColor(left), leftBefore);
        QCOMPARE(rig.window->grabWindow().pixelColor(right), rightBefore);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void togglingVectorVisibilityRepaintsWithoutMovingCamera() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        const QPoint left = at(rig, -2.5, 0.0);
        const QPoint right = at(rig, 2.5, 0.0);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const auto image = rig.window->grabWindow();
                return reddish(image.pixelColor(left)) && bluish(image.pixelColor(right));
            }(),
            8'000);

        const auto cameraRevision = rig.canvas->camera().revision();
        // A layer toggle is not a document or camera change. Both the software fallback
        // and the scene-graph backend must invalidate their cached image independently.
        rig.canvas->setLayers(QVariantMap{{QStringLiteral("vectors"), false}});
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const auto image = rig.window->grabWindow();
                return !reddish(image.pixelColor(left)) && !bluish(image.pixelColor(right));
            }(),
            8'000);
        QCOMPARE(rig.canvas->camera().revision(), cameraRevision);

        rig.canvas->setLayers(QVariantMap{{QStringLiteral("vectors"), true}});
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                const auto image = rig.window->grabWindow();
                return reddish(image.pixelColor(left)) && bluish(image.pixelColor(right));
            }(),
            8'000);
        QCOMPARE(rig.canvas->camera().revision(), cameraRevision);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void vectorInspectorTabsAreClickableAndScrollable() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        auto* tabs = deepItem(rig.window->contentItem(), QStringLiteral("vectorInspectorTabs"));
        auto* layers = deepItem(rig.window->contentItem(), QStringLiteral("vectorLayersTab"));
        auto* edit = deepItem(rig.window->contentItem(), QStringLiteral("vectorEditTab"));
        auto* scroll = deepItem(rig.window->contentItem(), QStringLiteral("vectorActionsScroll"));
        auto* panel = deepItem(rig.window->contentItem(), QStringLiteral("layerPanel"));
        QVERIFY(tabs && layers && edit && scroll && panel);
        QCOMPARE(tabs->property("currentIndex").toInt(), 0);
        QVERIFY(panel->isVisible());
        QVERIFY(!scroll->isVisible());

        // Exercise real input delivery: invoking AbstractButton.click() cannot detect
        // an overlapping item consuming the pointer event.
        const auto center = [](QQuickItem* item) {
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        };
        QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, center(edit));
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 1);
        QVERIFY(scroll->isVisible());
        QVERIFY(!panel->isVisible());
        QVERIFY(scroll->height() > 0);

        QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, center(layers));
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 0);
        QVERIFY(panel->isVisible());

        rig.window->resize(1000, 640);
        QTRY_COMPARE(rig.window->width(), 1000);
        QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, center(edit));
        QTRY_COMPARE(tabs->property("currentIndex").toInt(), 1);
        QVERIFY(scroll->isVisible());
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void pickHitsOpenPathBetweenNodes() {
        // AUTO-LINEWORK-01 : un trait ouvert se saisit au milieu d'un segment.
        document::VectorObject line;
        line.id = ObjectId{9001};
        line.name = "trait";
        geometry::Path path;
        path.closed = false;
        path.nodes = {
            {Vec2um{Micrometers{0}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}},
            {Vec2um{Micrometers{20'000}, Micrometers{0}}, geometry::NodeType::Corner, {}, {}}};
        line.paths.push_back(geometry::PathSet{path, {}});
        const auto layer = VectorLayer::build(std::vector<document::VectorObject>{line});
        const auto hit = layer->pick(10.0, 0.2, 0.5);
        QVERIFY(hit.has_value());
        QCOMPARE(*hit, std::uint64_t{9001});
        QVERIFY(!layer->pick(10.0, 3.0, 0.5).has_value());
    }

    void incrementalLayerBuildMatchesFullBuild() {
        Rig rig;
        open(rig);
        auto shapes = rig.session.project().vector_objects;
        QVERIFY(shapes.size() >= 2);
        std::vector<std::uint64_t> keys;
        for (std::size_t i = 0; i < shapes.size(); ++i) {
            keys.push_back(i + 1);
        }
        const auto first = VectorLayer::build(shapes, keys, nullptr);
        QVERIFY(first->triangles() == VectorLayer::build(shapes)->triangles());
        // One shape changes (colour, new key): the others reuse their triangles, and the
        // result is byte-identical to a full rebuild.
        shapes.front().rgb = {1, 2, 3};
        keys.front() = 1000;
        const auto next = VectorLayer::build(shapes, keys, first.get());
        QVERIFY(next->triangles() == VectorLayer::build(shapes)->triangles());
        QVERIFY(next->triangles() != first->triangles());
    }

    void quickBarShowsShapeOpsOnSelection() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        auto* bar = deepItem(rig.window->contentItem(), QStringLiteral("vectorQuickBar"));
        auto* unite = deepItem(rig.window->contentItem(), QStringLiteral("quickUnion"));
        auto* split = deepItem(rig.window->contentItem(), QStringLiteral("quickBreak"));
        QVERIFY(bar && unite && split);
        QVERIFY(!bar->isVisible());
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 2.5, 0.0));
        QTRY_VERIFY_WITH_TIMEOUT(bar->isVisible(), 3'000);
        // One shape, converted or not: boolean ops wait for a second one, splitting is ready.
        QCOMPARE(rig.vectors.selectedCount(), 1);
        QVERIFY(!unite->isEnabled());
        QVERIFY(split->isEnabled());
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // Lot 2 : séparer une forme d'un seul tenant dit ce qui s'est passé au lieu de répéter
    // le nom de l'action.
    void breakApartReportsItsOutcome() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 2.5, 0.0));
        QTRY_COMPARE_WITH_TIMEOUT(rig.vectors.selectedCount(), 1, 3'000);
        rig.vectors.breakApart();
        const QString message = rig.vectors.property("message").toString();
        QVERIFY2(message != QStringLiteral("Séparer en formes indépendantes"), qPrintable(message));
        QVERIFY2(message.contains(QStringLiteral("Rien à séparer")) ||
                     message.contains(QStringLiteral("Séparé en")) ||
                     message.contains(QStringLiteral("refusé")),
                 qPrintable(message));
    }

    // Lot 2 vocabulaire : « Ctrl+Maj+K » n'a qu'une seule action (séparer en formes
    // indépendantes) et le même libellé dans la barre, les options d'outil et le catalogue.
    void breakApartShortcutHasOneMeaning() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QList<QQuickItem*> carriers;
        collectByShortcutLabel(rig.window->contentItem(), QStringLiteral("Ctrl+Maj+K"), carriers);
        QVERIFY(!carriers.isEmpty());
        const auto* action = findAction("shape.break_apart");
        QVERIFY(action != nullptr);
        const QString expected = QString::fromStdString(action->label);
        QCOMPARE(expected, QStringLiteral("Séparer en formes indépendantes"));
        for (auto* item : std::as_const(carriers)) {
            QVERIFY2(item->property("text").toString() == expected,
                     qPrintable(item->objectName() + QStringLiteral(" / ") +
                                item->metaObject()->className() + QStringLiteral(" : ") +
                                item->property("text").toString()));
        }
        auto* split = deepItem(rig.window->contentItem(), QStringLiteral("quickBreak"));
        QVERIFY(split != nullptr);
        QCOMPARE(split->property("text").toString(), expected);
    }

    // Lot 2 : le libellé long reste lisible à 1000 px (barre rapide dans la fenêtre, menu des
    // formes assez large pour son entrée la plus longue).
    void breakApartLabelFitsAt1000px() {
        Rig rig;
        open(rig);
        rig.window->resize(1000, 640);
        enterSpace(rig);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 2.5, 0.0));
        auto* bar = deepItem(rig.window->contentItem(), QStringLiteral("vectorQuickBar"));
        QVERIFY(bar != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(bar->isVisible(), 3'000);
        const auto topLeft = bar->mapToScene(QPointF(0, 0));
        QVERIFY(topLeft.x() >= 0.0);
        QVERIFY2(topLeft.x() + bar->width() <= rig.window->width(),
                 qPrintable(QStringLiteral("barre %1 + %2 > fenetre %3")
                                .arg(topLeft.x())
                                .arg(bar->width())
                                .arg(rig.window->width())));
        auto* menu = rig.window->findChild<QObject*>(QStringLiteral("canvasContextMenu"));
        QVERIFY(menu != nullptr);
        auto* shapes = menu->findChild<QObject*>(QStringLiteral("contextShapesMenu"));
        QVERIFY(shapes != nullptr);
        QQmlExpression open(qmlContext(shapes), shapes, QStringLiteral("popup(300, 200)"));
        open.evaluate();
        auto* item = shapes->findChild<QQuickItem*>(QStringLiteral("contextBreakApart"));
        QVERIFY(item != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(shapes->property("width").toReal() >= item->implicitWidth(),
                                 3'000);
    }

    void clickSelectsAShapeAndShowsItsOrigin() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        const int buildsBefore = rig.stage.layerBuildCount();
        const auto base = rig.canvas->vectorLayer();
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 2.5, 0.0));
        // Selecting a vector changes the inspector, not the bitmap nor a second texture.
        QCOMPARE(rig.stage.layerBuildCount(), buildsBefore);
        QVERIFY(rig.canvas->vectorLayer() == base);
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().primary().has_value(), 3'000);
        const auto target = *rig.session.selection().primary();
        QCOMPARE(target.kind, TargetKind::VectorObject);
        const auto shape = rig.stage.selectedShape();
        QCOMPARE(shape.value("id").toString(), QString::number(target.id));
        QVERIFY(shape.value("hasSource").toBool());
        QCOMPARE(shape.value("regionId").toString(), QStringLiteral("2"));
        auto* origin = deepItem(rig.window->contentItem(), QStringLiteral("vectorShapeOrigin"));
        QVERIFY(origin);
        QVERIFY(origin->property("text").toString().contains(QStringLiteral("région 2")));
        // Clic dans le vide : plus de sélection.
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 0.0, 6.0));
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().empty(), 3'000);
        QCOMPARE(rig.stage.layerBuildCount(), buildsBefore);
    }

    // HP-OBJ-024 : la forme sélectionnée publie le nom de son calque, et ce nom permet de rendre
    // le calque actif via le même appel que ObjectTree/LayerPanel (EditController::setActiveLayer).
    void selectedShapeExposesItsLayerAndCanActivateIt() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        auto project = rig.session.project();
        document::Calque base;
        base.id = project.object_ids.next();
        base.name = "Base";
        document::Calque other;
        other.id = project.object_ids.next();
        other.name = "Détails";
        project.calques.push_back(base);
        project.calques.push_back(other);
        project.vector_objects[0].calque = base.id;  // la forme de gauche (région 1)
        project.vector_objects[1].calque = other.id; // la forme de droite (région 2)
        rig.session.replaceProject(std::move(project), false);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy(), 15'000);

        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -2.5, 0.0)); // forme de gauche
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().primary().has_value(), 3'000);
        auto shape = rig.stage.selectedShape();
        QCOMPARE(shape.value("layerName").toString(), QStringLiteral("Base"));
        QCOMPARE(shape.value("layerId").toString(), QString::number(base.id.value));

        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 2.5, 0.0)); // forme de droite
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().primary().has_value(), 3'000);
        shape = rig.stage.selectedShape();
        QCOMPARE(shape.value("layerName").toString(), QStringLiteral("Détails"));
        QCOMPARE(shape.value("layerId").toString(), QString::number(other.id.value));

        // Le même appel que ObjectTree/LayerPanel ("Rendre actif") avec l'id publié active le
        // calque de la forme sélectionnée.
        QVERIFY(rig.editor.setActiveLayer(shape.value("layerId").toString()));
        QCOMPARE(rig.editor.activeLayer(), shape.value("layerId").toString());
    }

    // HP-OBJ-024 (sécurité) : verrouiller le calque d'une sélection ne la vide pas (EditController
    // le revérifie à chaque appel), donc VectorController doit filtrer lui-même les objets
    // verrouillés plutôt que de les laisser muter via une opération de forme.
    void lockedLayerObjectsAreFilteredOutOfShapeOperations() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        auto project = rig.session.project();
        document::Calque locked;
        locked.id = project.object_ids.next();
        locked.name = "Verrouillé";
        project.calques.push_back(locked);
        for (auto& shape : project.vector_objects) {
            shape.calque = locked.id;
        }
        rig.session.replaceProject(std::move(project), false);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy(), 15'000);

        const auto leftId = rig.session.project().vector_objects[0].id.value;
        const auto rightId = rig.session.project().vector_objects[1].id.value;
        rig.session.selection().replace(
            {Target{TargetKind::VectorObject, leftId}, Target{TargetKind::VectorObject, rightId}});
        QCOMPARE(rig.session.selection().targets().size(), std::size_t{2});
        QCOMPARE(rig.vectors.selectedCount(), 2);

        QVERIFY(rig.editor.setLayerLocked(QString::number(locked.id.value), true));
        // Le verrou ne vide pas la sélection : elle reste peuplée, VectorController doit la
        // filtrer lui-même à chaque appel.
        QCOMPARE(rig.session.selection().targets().size(), std::size_t{2});
        QCOMPARE(rig.vectors.selectedCount(), 0);

        const auto before = steps(rig);
        QVERIFY(!rig.vectors.booleanOp(QStringLiteral("union")));
        QCOMPARE(steps(rig), before);
        QVERIFY(!rig.vectors.breakApart());
        QCOMPARE(steps(rig), before);
        // Jamais de mutation d'un calque verrouillé : les formes et leur calque sont inchangés.
        QCOMPARE(rig.session.project().vector_objects.size(), std::size_t{2});
        QVERIFY(rig.session.project().findCalque(locked.id)->locked);
    }

    void visualPropertiesInvalidateVectorRasterButNamesDoNot() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        const int initialBuilds = rig.stage.layerBuildCount();
        const auto originalRaster = rig.canvas->vectorLayer();

        const auto original = rig.session.snapshot();
        auto recolored = std::make_shared<document::Project>(*original);
        recolored->vector_objects.front().rgb = {10, 190, 40};
        auto recolorCommand = std::make_unique<commands::ReplaceProjectCommand>(
            "Recolorer une forme", original, recolored);
        rig.session.execute(std::move(recolorCommand));
        QCOMPARE(rig.stage.layerBuildCount(), initialBuilds + 1);
        QVERIFY(rig.canvas->vectorLayer() != originalRaster);

        // A display-only name change does not repaint vector geometry.
        const auto colorProject = rig.session.snapshot();
        auto renamed = std::make_shared<document::Project>(*colorProject);
        renamed->vector_objects.front().name = "New label";
        rig.session.execute(std::make_unique<commands::ReplaceProjectCommand>(
            "Renommer une forme", colorProject, renamed, false, false));
        QCOMPARE(rig.stage.layerBuildCount(), initialBuilds + 1);

        const auto namedProject = rig.session.snapshot();
        auto hidden = std::make_shared<document::Project>(*namedProject);
        hidden->vector_objects.front().visible = false;
        rig.session.execute(std::make_unique<commands::ReplaceProjectCommand>(
            "Masquer une forme", namedProject, hidden));
        QCOMPARE(rig.stage.layerBuildCount(), initialBuilds + 2);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // Revenir à un état déjà rasterisé (annuler, rétablir) republie l'image conservée : aucun
    // nouveau calcul, et les pixels sont ceux de l'état d'origine.
    void returningToAnAlreadyRenderedStateReusesTheCachedRaster() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        const auto originalRaster = rig.canvas->vectorLayer();
        QCOMPARE(rig.stage.layerCacheHits(), 0);

        const auto original = rig.session.snapshot();
        auto recolored = std::make_shared<document::Project>(*original);
        recolored->vector_objects.front().rgb = {10, 190, 40};
        rig.session.execute(std::make_unique<commands::ReplaceProjectCommand>("Recolorer une forme",
                                                                              original, recolored));
        QVERIFY(rig.canvas->vectorLayer() != originalRaster);
        QCOMPARE(rig.stage.layerCacheHits(), 0); // premier passage : calcul réel

        QVERIFY(rig.session.undo()); // retour à l'état d'origine : servi depuis le cache
        QCOMPARE(rig.stage.layerCacheHits(), 1);
        QVERIFY(rig.canvas->vectorLayer() == originalRaster);
        QVERIFY(rig.session.redo()); // retour à l'état recoloré : servi depuis le cache
        QCOMPARE(rig.stage.layerCacheHits(), 2);
        QVERIFY(rig.canvas->vectorLayer() != originalRaster);
    }

    void stitchParameterChangesDoNotRerasterizeVectors() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        auto project = rig.session.project();
        document::EmbroideryObject embroidery;
        embroidery.id = project.object_ids.next();
        embroidery.source_vector = project.vector_objects.front().id;
        embroidery.params = document::RunningStitchParams{};
        const auto id = embroidery.id;
        project.embroidery_objects.push_back(embroidery);
        rig.session.replaceProject(std::move(project), false);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy(), 15'000);
        const int builds = rig.stage.layerBuildCount();
        const auto base = rig.canvas->vectorLayer();
        QVERIFY(base);

        // Changing stitch length invalidates the machine sequence, not the vector texture.
        auto params = std::get<document::RunningStitchParams>(
            rig.session.project().embroidery_objects.front().params);
        params.stitch_length = Micrometers{2'000};
        rig.session.execute(
            std::make_unique<commands::SetStitchParamsCommand>(id, params, "Longueur"));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy(), 15'000);
        QCOMPARE(rig.stage.layerBuildCount(), builds);
        QVERIFY(rig.canvas->vectorLayer() == base);

        // Undo/redo have the same invalidation scope.
        QVERIFY(rig.session.undo());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy(), 15'000);
        QCOMPARE(rig.stage.layerBuildCount(), builds);
        QVERIFY(rig.session.redo());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy(), 15'000);
        QCOMPARE(rig.stage.layerBuildCount(), builds);
        QVERIFY(rig.canvas->vectorLayer() == base);

        // A real move (semantic fingerprint itself is translation invariant) must redraw.
        const auto source = rig.session.project().vector_objects.front().id;
        rig.session.execute(std::make_unique<commands::TranslateVectorObjectCommand>(
            source, Vec2um{Micrometers{1'000}, Micrometers{0}}));
        QTRY_COMPARE(rig.stage.layerBuildCount(), builds + 1);
        QVERIFY(rig.canvas->vectorLayer() != base);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void goToRegionSwitchesToSegmentationWithTheRegionSelected() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, -2.5, 0.0));
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().primary().has_value(), 3'000);
        auto* button =
            deepItem(rig.window->contentItem(), QStringLiteral("vectorGoToRegionButton"));
        QVERIFY(button);
        QTRY_VERIFY_WITH_TIMEOUT(button->isEnabled(), 3'000);
        const auto center = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, center.toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(rig.workspace->current(), QStringLiteral("segmentation"), 3'000);
        const auto primary = rig.session.selection().primary();
        QVERIFY(primary.has_value());
        QCOMPARE(primary->kind, TargetKind::Region);
        QCOMPARE(primary->id, std::uint64_t{1});
    }

    void drawToolCreatesOneShapeAndUndoRemovesIt() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QVERIFY(deepItem(rig.window->contentItem(), QStringLiteral("toolRail")));
        const auto before = rig.session.project().vector_objects.size();
        const auto undoBefore = steps(rig);
        rig.tools.setActiveTool(QStringLiteral("draw.rectangle"));
        const auto a = at(rig, -4.0, -3.0);
        const auto b = at(rig, 4.0, 3.0);
        QTest::mousePress(rig.window, Qt::LeftButton, {}, a);
        for (int i = 1; i <= 6; ++i) {
            QTest::mouseMove(rig.window, a + (b - a) * i / 6);
        }
        QTest::mouseRelease(rig.window, Qt::LeftButton, {}, b);
        QCOMPARE(rig.session.project().vector_objects.size(), before + 1);
        QCOMPARE(steps(rig), undoBefore + 1);
        QTRY_COMPARE_WITH_TIMEOUT(rig.stage.shapeCount(), static_cast<int>(before + 1), 3'000);
        // La forme dessinée à la main est distinguée : pas de région d'origine.
        const auto drawn = rig.stage.selectedShape();
        QVERIFY(!drawn.isEmpty());
        QVERIFY(!drawn.value("hasSource").toBool());
        QVERIFY(rig.session.undo());
        QCOMPARE(rig.session.project().vector_objects.size(), before);
        QCOMPARE(rig.stage.shapeCount(), static_cast<int>(before));
    }

    void stagePublishedAndUpdateFromSegmentationAfterRegionCorrection() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QTRY_COMPARE_WITH_TIMEOUT(
            rig.workspace->space(QStringLiteral("vector")).value("state").toString(),
            QStringLiteral("upToDate"), 3'000);
        QCOMPARE(rig.workspace->space(QStringLiteral("vector")).value("actionLabel").toString(),
                 QStringLiteral("Mettre à jour depuis la segmentation"));
        // Correction source : le rendu de la forme reste en cache tant que
        // l'utilisateur n'applique pas explicitement la revectorisation.
        const int initialBuilds = rig.stage.layerBuildCount();
        const auto initialImage = rig.canvas->vectorLayer();
        rig.session.execute(
            std::make_unique<commands::MergeRegionsCommand>(RegionId{1}, RegionId{2}));
        QTRY_COMPARE_WITH_TIMEOUT(
            rig.workspace->space(QStringLiteral("vector")).value("state").toString(),
            QStringLiteral("stale"), 3'000);
        QCOMPARE(rig.stage.layerBuildCount(), initialBuilds);
        QVERIFY(rig.canvas->vectorLayer() == initialImage);
        const auto plan = rig.stage.updatePlan();
        QCOMPARE(plan.value("replace").toInt(), 1);
        QCOMPARE(plan.value("orphan").toInt(), 1);
        const auto undoBefore = steps(rig);
        // Le bouton du bandeau demande la mise à jour ; des retouches vont être écrasées : la
        // confirmation s'affiche d'abord, rien n'est modifié avant.
        rig.workspace->requestUpdate(QStringLiteral("vector"));
        auto* confirm = deepItem(rig.window->contentItem(), QStringLiteral("vectorUpdateConfirm"));
        QVERIFY(confirm);
        QTRY_VERIFY_WITH_TIMEOUT(confirm->isVisible(), 3'000);
        QCOMPARE(steps(rig), undoBefore);
        auto* ok = deepItem(rig.window->contentItem(), QStringLiteral("vectorUpdateConfirmButton"));
        QVERIFY(ok);
        const auto center = ok->mapToScene(QPointF(ok->width() / 2, ok->height() / 2));
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, center.toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(steps(rig), undoBefore + 1, 3'000);
        // La forme de la région 1 couvre maintenant toute l'image (largeur ~10 mm).
        const auto& shapes = rig.session.project().vector_objects;
        double x0 = 1e9, x1 = -1e9;
        for (const auto& set : shapes.front().paths) {
            for (const auto& n : set.outer.nodes) {
                x0 = std::min(x0, n.pos.x.value / 1000.0);
                x1 = std::max(x1, n.pos.x.value / 1000.0);
            }
        }
        QVERIFY2(x1 - x0 > 8.0, qPrintable(QString::number(x1 - x0)));
        // Une seule étape d'annulation pour tout le lot.
        QVERIFY(rig.session.undo());
        QCOMPARE(steps(rig), undoBefore);
    }

    // Précision de la vectorisation (V1) : tolérance en mm bornée, courbes de Bézier avec écart
    // maximal en px ; les réglages sont visibles dans le panneau de l'espace.
    void vectorizationPrecisionIsAdjustableFromThePanel() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QSignalSpy options(&rig.stage, &VectorSpaceController::optionsChanged);
        QCOMPARE(rig.stage.precisionMm(), 0.2);
        QVERIFY(!rig.stage.curves());
        rig.stage.setPrecisionMm(0.05);
        QCOMPARE(rig.stage.precisionMm(), 0.05);
        rig.stage.setPrecisionMm(0.05); // inchangé : aucun signal
        QCOMPARE(options.count(), 1);
        rig.stage.setPrecisionMm(99.0);
        QCOMPARE(rig.stage.precisionMm(), 2.0);
        rig.stage.setPrecisionMm(-1.0);
        QCOMPARE(rig.stage.precisionMm(), 0.0); // 0 accepté : contour brut, sans simplification
        rig.stage.setPrecisionMm(0.2);

        auto* slider = deepItem(rig.window->contentItem(), QStringLiteral("vectorPrecisionSlider"));
        auto* box = deepItem(rig.window->contentItem(), QStringLiteral("vectorCurvesBox"));
        QVERIFY(slider && box);
        QTRY_VERIFY_WITH_TIMEOUT(slider->isVisible(), 3'000);
        QVERIFY(QMetaObject::invokeMethod(box, "click"));
        QVERIFY(rig.stage.curves());
        auto* curveSlider =
            deepItem(rig.window->contentItem(), QStringLiteral("vectorCurveToleranceSlider"));
        QVERIFY(curveSlider);
        QTRY_VERIFY_WITH_TIMEOUT(curveSlider->isVisible(), 3'000);
        QTRY_VERIFY_WITH_TIMEOUT(!slider->isVisible(), 3'000);
        rig.stage.setCurveTolerancePx(100.0);
        QCOMPARE(rig.stage.curveTolerancePx(), 10.0);
        rig.stage.setCurveTolerancePx(0.0);
        QCOMPARE(rig.stage.curveTolerancePx(), 0.1);
        rig.stage.setCurveTolerancePx(0.25); // subpixel : accepte tel quel
        QCOMPARE(rig.stage.curveTolerancePx(), 0.25);
        // Le plan de mise à jour utilise les réglages : le document n'est pas modifié.
        const auto depth = steps(rig);
        (void)rig.stage.updatePlan();
        QCOMPARE(steps(rig), depth);
    }

    // Mesure du parcours « retrouver un objet précis » (400 objets, fenêtre 1000x640) : parcours
    // de la liste page par page (avant la recherche), puis avec le champ de recherche. Une
    // « action » = un clic (dont une page d'ascenseur) ou une frappe.
    void searchFindsAnObjectInFewerActionsThanScrolling() {
        Rig rig;
        open(rig, false);
        rig.session.replaceProject(manyShapesProject(400), false);
        enterSpace(rig);
        rig.window->resize(1000, 640);
        QVERIFY(rig.stage.convertToEmbroidery());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.stage.converting(), 60'000);

        const auto kindOf = [](const QVariant& row) {
            return row.toMap().value(QStringLiteral("kind")).toString();
        };
        const auto centerOf = [](QQuickItem* item) {
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        };
        const auto tree = rig.editor.tree();
        std::vector<int> objectRows;
        for (int i = 0; i < tree.size(); ++i) {
            if (kindOf(tree[i]) == QStringLiteral("object")) {
                objectRows.push_back(i);
            }
        }
        QVERIFY(objectRows.size() >= 300);
        const int row = objectRows[objectRows.size() * 2 / 3];
        const auto target = tree[row].toMap();
        const QString label = target.value(QStringLiteral("label")).toString();
        const QString id = target.value(QStringLiteral("id")).toString();

        auto* list = deepItem(rig.window->contentItem(), QStringLiteral("objectTreeList"));
        QVERIFY(list != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(list->isVisible() && list->height() > 100, 5'000);
        const auto itemAt = [&](int index) {
            QQuickItem* item = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item),
                                      Q_ARG(int, index));
            return item;
        };
        QQuickItem* first = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((first = itemAt(0)) != nullptr, 5'000);
        const double pitch = first->height() + list->property("spacing").toDouble();

        // 1. Sans recherche : l'ordre de la liste est celui de la couture, la position d'un objet
        // n'est pas connue de l'utilisateur ; le seul moyen sûr est de parcourir la liste page
        // par page. Mesure sur la géométrie réelle : pages de liste jusqu'à la ligne visée (les
        // crans de molette simulés ne sont pas un compteur fiable : Qt Quick les convertit en
        // vitesse qui se dissipe avec le temps). Une page = une action (clic dans l'ascenseur).
        const double rowBottom = row * pitch + first->height();
        const int pages = static_cast<int>(std::ceil(rowBottom / list->height()));
        const int scrollActions = pages + 1; // + le clic sur la ligne
        const int visibleRows = static_cast<int>(list->height() / pitch);

        // 2. Recherche : clic dans le champ, frappe du nom, clic sur la ligne.
        auto* search = deepItem(rig.window->contentItem(), QStringLiteral("layerSearchField"));
        QVERIFY(search != nullptr);
        int searchActions = 0;
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, centerOf(search));
        ++searchActions;
        for (const QChar ch : label) {
            QTest::keyClick(rig.window, ch.toLatin1());
            ++searchActions; // une frappe = une action
        }
        const auto findRow = [&]() -> QQuickItem* {
            for (int i = 0; i < 10; ++i) {
                QQuickItem* item = itemAt(i);
                if (item == nullptr) {
                    continue;
                }
                const auto data = item->property("modelData").toMap();
                if (data.value(QStringLiteral("label")).toString() == label) {
                    return item;
                }
            }
            return nullptr;
        };
        QTRY_VERIFY_WITH_TIMEOUT(findRow() != nullptr, 5'000);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, centerOf(findRow()));
        ++searchActions;
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().primary().has_value(), 3'000);
        QCOMPARE(QString::number(rig.session.selection().primary()->id), id);

        QString report = QStringLiteral("MESURE clics : objet « %1 » parmi %2 objets, 1000x640 : ");
        report = report.arg(label).arg(objectRows.size());
        report += QStringLiteral("sans recherche %1 actions (%2 pages de %3 lignes + 1 clic) -> ");
        report = report.arg(scrollActions).arg(pages).arg(visibleRows);
        report += QStringLiteral("recherche %1 actions (1 clic + %2 frappes + 1 clic)");
        report = report.arg(searchActions).arg(label.size());
        qInfo().noquote() << report;
        const QString verdict = QStringLiteral("recherche %1 actions, parcours %2 actions");
        const QString why = verdict.arg(searchActions).arg(scrollActions);
        QVERIFY2(searchActions < scrollActions, qPrintable(why));
    }

    void convertToEmbroideryIsOneUndoStep() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{0});
        const auto undoBefore = steps(rig);
        QVERIFY(rig.stage.convertToEmbroidery());
        QVERIFY(rig.stage.converting()); // l'interface reste libre pendant le calcul
        QTRY_VERIFY_WITH_TIMEOUT(!rig.stage.converting(), 15'000);
        QVERIFY(rig.session.project().embroidery_objects.size() >= 1);
        QCOMPARE(steps(rig), undoBefore + 1);
        QVERIFY(rig.session.undo());
        QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{0});
    }

    // CX05 : « Convertir en broderie » et l'onglet Segmentation partagent UN état satin
    // (StudioController::satinOptions), mêmes libellés, et il arrive tel quel aux AutoOptions.
    void satinChoiceIsSharedAndReachesAutoOptions() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        const auto item = [&rig](const char* name) {
            return deepItem(rig.window->contentItem(), QString::fromLatin1(name));
        };
        const auto press = [&item](const char* name) {
            auto* target = item(name);
            return target && target->isEnabled() && QMetaObject::invokeMethod(target, "click");
        };
        QVERIFY(press("vectorConvertButton"));
        auto* bands = item("vectorConvertSatinBox");
        auto* compare = item("vectorConvertSatinCompareBox");
        QVERIFY(bands && compare);
        QTRY_VERIFY(bands->isVisible());
        QCOMPARE(bands->property("text").toString(), QStringLiteral("Satin sur les bandes"));
        QCOMPARE(compare->property("text").toString(),
                 QStringLiteral("Choisir le meilleur satin (expérimental)"));
        QVERIFY(!bands->property("checked").toBool());
        QVERIFY(!compare->isEnabled());
        // Coché ailleurs (onglet Segmentation = même état) : reflété ici.
        rig.controller.setSatinOptions({{"satin_bands", true}});
        QTRY_VERIFY(bands->property("checked").toBool());
        QTRY_VERIFY(compare->isEnabled());
        // Coché ici : écrit dans l'état partagé.
        QVERIFY(press("vectorConvertSatinCompareBox"));
        QTRY_VERIFY(rig.controller.satinOptions().value("satin_compare").toBool());
        QVERIFY(press("vectorConvertSatinBox"));
        QTRY_VERIFY(!rig.controller.satinOptions().value("satin_bands").toBool());
        QTRY_VERIFY(!compare->isEnabled());
        QVERIFY(press("vectorConvertSatinBox"));
        QTRY_VERIFY(rig.controller.satinOptions().value("satin_bands").toBool());
        QTRY_VERIFY(compare->property("checked").toBool()); // le choix « meilleur satin » revient
        auto options = rig.controller.satinOptions();
        options.insert("satin_compare_max_stitch_mm", 7.5);
        options.insert("satin_min_coverage", 0.8);
        rig.controller.setSatinOptions(options);
        QVERIFY(rig.stage.convertToEmbroidery());
        const auto& sent = rig.stage.lastAutoOptions();
        QVERIFY(sent.satin_bands);
        QVERIFY(sent.satin_compare);
        QCOMPARE(sent.satin_compare_max_stitch.value, 7500);
        QCOMPARE(sent.satin_min_coverage, 0.8);
        QCOMPARE(sent.satin_compact_max_width.value,
                 autodigitize::AutoOptions{}.satin_compact_max_width.value);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.stage.converting(), 15'000);
        // Satin décoché : rien de satin ne part au moteur.
        rig.controller.setSatinOptions({{"satin_compare", true}});
        QVERIFY(rig.session.undo());
        QVERIFY(rig.stage.convertToEmbroidery());
        QVERIFY(!rig.stage.lastAutoOptions().satin_bands);
        QVERIFY(!rig.stage.lastAutoOptions().satin_compare);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.stage.converting(), 15'000);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // CX05 : les avertissements d'une conversion (VectorConversion::warnings) sont résumés,
    // comptés et repliés au-delà de trois lignes ; seule la conversion suivante les vide.
    void conversionWarningsAreShown() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        const auto item = [&rig](const char* name) {
            return deepItem(rig.window->contentItem(), QString::fromLatin1(name));
        };
        rig.stage.publishConversionWarnings({"forme 1 : contour ignoré", "forme 2 : contour ignoré",
                                             "forme 3 : contour ignoré",
                                             "forme 4 : contour ignoré"});
        QCOMPARE(rig.stage.conversionWarnings().size(), 4);
        auto* summary = item("vectorConvertWarningSummary");
        QVERIFY(summary);
        QTRY_VERIFY(summary->isVisible());
        QVERIFY(summary->property("text").toString().contains(QStringLiteral("4 avertissements")));
        QTRY_VERIFY(item("vectorConvertWarningLine_2") &&
                    item("vectorConvertWarningLine_2")->isVisible());
        QVERIFY(!item("vectorConvertWarningLine_3")->isVisible());
        auto* more = item("vectorConvertWarningsMore");
        QVERIFY(more && more->isVisible());
        QVERIFY(QMetaObject::invokeMethod(more, "click"));
        QTRY_VERIFY(item("vectorConvertWarningLine_3")->isVisible());
        // La conversion suivante repart de zéro (dès son lancement).
        QVERIFY(rig.stage.convertToEmbroidery());
        QVERIFY(rig.stage.conversionWarnings().isEmpty());
        QTRY_VERIFY(!summary->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.stage.converting(), 15'000);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // Recette : ecrit le projet de l'espace pour la capture (OPENSTITCH_VECTOR_SAMPLE=chemin.osp).
    void writeSampleForCapture() {
        const auto path = qEnvironmentVariable("OPENSTITCH_VECTOR_SAMPLE");
        if (path.isEmpty()) {
            QSKIP("recette seulement");
        }
        QVERIFY(project_io::save_project(path.toStdWString(), shapesProject()).has_value());
    }

    // Mesure (PERF-024) : construction du calque vectoriel (rasterisation) et pick avec N formes.
    // Désactivé par défaut (dépend de la machine) : OPENSTITCH_PERF=1 pour l'exécuter.
    void perfVectorLayer() {
        if (qEnvironmentVariableIntValue("OPENSTITCH_PERF") != 1) {
            QSKIP("mesure désactivée : définir OPENSTITCH_PERF=1");
        }
        using Clock = std::chrono::steady_clock;
        const auto ms = [](Clock::time_point t) {
            return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
        };
        const auto pct = [](std::vector<double> v, double p) {
            std::sort(v.begin(), v.end());
            return v[static_cast<std::size_t>(p * static_cast<double>(v.size() - 1))];
        };
        for (const int count : {1, 100, 500, 1000}) {
            document::Project project;
            project.finishing = document::SequenceFinishing::legacy();
            project.object_ids.reset(1000);
            const int perRow = 32;
            for (int i = 0; i < count; ++i) {
                document::VectorObject shape;
                shape.id = project.object_ids.next();
                shape.name = "Forme " + std::to_string(i);
                shape.rgb = {static_cast<std::uint8_t>(40 + i % 200), 90, 160};
                const int x0 = (i % perRow) * 10000 - 150000; // µm : carrés de 8 mm, pas 10 mm
                const int y0 = (i / perRow) * 10000 - 150000;
                geometry::PathSet set;
                for (const auto& [dx, dy] : {std::pair{0, 0}, {8000, 0}, {8000, 8000}, {0, 8000}}) {
                    set.outer.nodes.push_back({Vec2um{Micrometers{x0 + dx}, Micrometers{y0 + dy}}});
                }
                shape.paths.push_back(std::move(set));
                project.vector_objects.push_back(std::move(shape));
            }
            std::vector<double> builds;
            std::shared_ptr<const VectorLayer> layer;
            for (int run = 0; run < 5; ++run) {
                const auto t = Clock::now();
                layer = VectorLayer::build(project);
                builds.push_back(ms(t));
            }
            QVERIFY(layer);
            QCOMPARE(static_cast<int>(layer->shapes().size()), count);
            // Rastérisation du backend logiciel, faite à la demande sur un calque neuf.
            std::vector<double> rasters;
            for (int run = 0; run < 5; ++run) {
                const auto fresh = VectorLayer::build(project);
                const auto t = Clock::now();
                (void)fresh->image();
                rasters.push_back(ms(t));
            }
            qInfo("[perf-vector] n=%d triangles=%zu raster logiciel p50=%.2fms", count,
                  layer->triangles().size() / 3, pct(rasters, 0.5));
            std::vector<double> picks;
            std::uint32_t state = 4242;
            for (int i = 0; i < 4000; ++i) {
                state = state * 1664525U + 1013904223U;
                const double x = static_cast<double>(state >> 8) / 16777216.0 * 320.0 - 160.0;
                state = state * 1664525U + 1013904223U;
                const double y = static_cast<double>(state >> 8) / 16777216.0 * 320.0 - 160.0;
                const auto t = Clock::now();
                const auto hit = layer->pick(x, y, 0.5);
                picks.push_back(ms(t) * 1000.0);
                Q_UNUSED(hit);
            }
            qInfo("[perf-vector] n=%d build p50=%.2fms max=%.2fms | pick p50=%.2fus p95=%.2fus "
                  "p99=%.2fus",
                  count, pct(builds, 0.5), pct(builds, 1.0), pct(picks, 0.5), pct(picks, 0.95),
                  pct(picks, 0.99));
        }
        // Projet réel facultatif (contours denses, courbes) : OPENSTITCH_PERF_OSP=chemin.osp.
        if (const QString osp = qEnvironmentVariable("OPENSTITCH_PERF_OSP"); !osp.isEmpty()) {
            const auto loaded = project_io::load_project(osp.toStdWString());
            QVERIFY(loaded.has_value());
            std::vector<double> builds;
            std::vector<double> rasters;
            std::shared_ptr<const VectorLayer> layer;
            for (int run = 0; run < 5; ++run) {
                auto t = Clock::now();
                layer = VectorLayer::build(*loaded);
                builds.push_back(ms(t));
                t = Clock::now();
                (void)layer->image();
                rasters.push_back(ms(t));
            }
            qInfo("[perf-vector] osp formes=%zu triangles=%zu build p50=%.2fms | raster logiciel "
                  "p50=%.2fms",
                  layer->shapes().size(), layer->triangles().size() / 3, pct(builds, 0.5),
                  pct(rasters, 0.5));
        }
        // Même surface que 1 000 formes mais 2 formes seulement : isole le coût de l'image.
        {
            document::Project sparse;
            sparse.finishing = document::SequenceFinishing::legacy();
            sparse.object_ids.reset(1000);
            for (const auto& [ox, oy] : {std::pair{-150000, -150000}, {150000, 150000}}) {
                document::VectorObject shape;
                shape.id = sparse.object_ids.next();
                shape.rgb = {40, 90, 160};
                geometry::PathSet set;
                for (const auto& [dx, dy] : {std::pair{0, 0}, {8000, 0}, {8000, 8000}, {0, 8000}}) {
                    set.outer.nodes.push_back({Vec2um{Micrometers{ox + dx}, Micrometers{oy + dy}}});
                }
                shape.paths.push_back(std::move(set));
                sparse.vector_objects.push_back(std::move(shape));
            }
            std::vector<double> builds;
            for (int run = 0; run < 5; ++run) {
                const auto t = Clock::now();
                const auto layer = VectorLayer::build(sparse);
                builds.push_back(ms(t));
                QVERIFY(layer);
            }
            qInfo("[perf-vector] sparse (2 formes, surface de 1000) build p50=%.2fms",
                  pct(builds, 0.5));
        }
    }

    // Le calque rasterisé sur un autre fil est identique, octet pour octet, à celui du fil courant.
    void layerBuildsIdenticallyOnAnotherThread() {
        const auto project = manyShapesProject(200);
        const auto here = VectorLayer::build(project);
        const auto shapes =
            std::make_shared<const std::vector<document::VectorObject>>(project.vector_objects);
        const auto there =
            std::async(std::launch::async, [shapes] { return VectorLayer::build(*shapes); }).get();
        QVERIFY(here && there);
        QCOMPARE(here->shapes().size(), there->shapes().size());
        QVERIFY(here->image().image == there->image().image); // QImage::operator== : pixels
        QCOMPARE(here->image().widthMm, there->image().widthMm);
        QCOMPARE(here->image().x0, there->image().x0);
    }

    // Triangles du chemin GPU : ils couvrent exactement le remplissage pair-impair (le trou reste
    // vide), portent la couleur prémultipliée de la forme, et l'image logicielle n'est jamais
    // rastérisée tant que seul le chemin GPU les lit.
    void layerTrianglesCoverFillAndSkipRaster() {
        document::Project project;
        project.object_ids.reset(1000);
        document::VectorObject shape;
        shape.id = project.object_ids.next();
        shape.rgb = {200, 40, 40};
        shape.source_region = RegionId{7}; // remplissage alpha 235, contour plein
        geometry::PathSet set;
        const auto square = [](geometry::Path& path, int x0, int y0, int side) {
            for (const auto& [dx, dy] : {std::pair{0, 0}, {side, 0}, {side, side}, {0, side}}) {
                path.nodes.push_back({Vec2um{Micrometers{x0 + dx}, Micrometers{y0 + dy}}});
            }
            path.closed = true;
        };
        square(set.outer, 0, 0, 10'000); // 10 x 10 mm
        geometry::Path hole;
        square(hole, 3'000, 3'000, 4'000); // trou de 4 x 4 mm
        set.holes.push_back(std::move(hole));
        shape.paths.push_back(std::move(set));
        project.vector_objects.push_back(std::move(shape));

        const auto layer = VectorLayer::build(project);
        QVERIFY(layer);
        const auto& t = layer->triangles();
        QVERIFY(!t.empty());
        QCOMPARE(t.size() % 3, std::size_t{0});
        double fillArea = 0.0;
        bool holeCovered = false;
        for (std::size_t i = 0; i + 2 < t.size(); i += 3) {
            const auto& a = t[i];
            const auto& b = t[i + 1];
            const auto& c = t[i + 2];
            if (a.a != 235) {
                continue; // contour (opaque)
            }
            QCOMPARE(int(a.r), 200 * 235 / 255);
            fillArea += std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2.0;
            const float cx = (a.x + b.x + c.x) / 3.0f;
            const float cy = (a.y + b.y + c.y) / 3.0f;
            holeCovered = holeCovered || (cx > 3.0f && cx < 7.0f && cy < -3.0f && cy > -7.0f);
        }
        QVERIFY2(std::abs(fillArea - 84.0) < 0.01, qPrintable(QString::number(fillArea)));
        QVERIFY(!holeCovered);
        QCOMPARE(layer->bytes(), static_cast<qint64>(t.size() * sizeof(StudioVertex)));
        QVERIFY(!layer->image().image.isNull()); // le backend logiciel l'obtient à la demande
        QVERIFY(layer->bytes() > static_cast<qint64>(t.size() * sizeof(StudioVertex)));
    }

    // Au-delà de kSyncShapeLimit la rasterisation quitte le fil d'interface ; une modification
    // qui en suit une autre remplace la première : jamais de calque périmé publié après coup.
    void largeDocumentsRasteriseOffTheGuiThreadAndDropStaleResults() {
        Rig rig;
        open(rig, false);
        enterSpace(rig);
        const int baseline = rig.stage.layerBuildCount();
        QVERIFY(!rig.stage.layerPending());
        rig.session.replaceProject(manyShapesProject(200), false);
        rig.session.replaceProject(manyShapesProject(201), false);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.stage.layerPending(), 10'000);
        QCOMPARE(rig.stage.shapeCount(), 201);
        QCOMPARE(rig.stage.publishedShapeCount(), 201); // le dernier état demandé, pas le premier
        QVERIFY(rig.stage.layerBuildCount() > baseline);
        QVERIFY(rig.stage.layerBuildCount() <= baseline + 2);
        // Retour à une petite scène : synchrone, sans calque en attente.
        rig.session.replaceProject(manyShapesProject(3), false);
        QVERIFY(!rig.stage.layerPending());
        QCOMPARE(rig.stage.shapeCount(), 3);
        QCOMPARE(rig.stage.publishedShapeCount(), 3);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    // Mesure : temps pendant lequel le fil d'interface est bloqué par le déclenchement d'une
    // reconstruction (replaceProject -> refresh), puis délai jusqu'au calque publié.
    void perfVectorRefreshBlocking() {
        if (qEnvironmentVariableIntValue("OPENSTITCH_PERF") != 1) {
            QSKIP("mesure désactivée : définir OPENSTITCH_PERF=1");
        }
        using Clock = std::chrono::steady_clock;
        Rig rig;
        open(rig, false);
        enterSpace(rig);
        for (const int count : {100, 140, 160, 500, 1000}) {
            std::vector<double> blocked;
            std::vector<double> total;
            for (int run = 0; run < 5; ++run) {
                // Un identifiant de couleur différent à chaque tour force une vraie reconstruction.
                auto project = manyShapesProject(count);
                project.vector_objects.front().rgb[0] = static_cast<std::uint8_t>(run * 7);
                const auto start = Clock::now();
                rig.session.replaceProject(std::move(project), false);
                blocked.push_back(
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count());
                QTRY_VERIFY_WITH_TIMEOUT(!rig.stage.layerPending(), 10'000);
                total.push_back(
                    std::chrono::duration<double, std::milli>(Clock::now() - start).count());
            }
            std::sort(blocked.begin(), blocked.end());
            std::sort(total.begin(), total.end());
            qInfo("[perf-vector-refresh] n=%d GUI bloque p50=%.2fms max=%.2fms | calque publie "
                  "p50=%.2fms",
                  count, blocked[2], blocked.back(), total[2]);
        }
    }

    void emptyDocumentPublishesTheEmptyState() {
        Rig rig;
        open(rig, false);
        enterSpace(rig);
        QCOMPARE(rig.workspace->space(QStringLiteral("vector")).value("state").toString(),
                 QStringLiteral("empty"));
        auto* empty = deepItem(rig.window->contentItem(), QStringLiteral("vectorEmpty"));
        QVERIFY(empty);
        QTRY_VERIFY_WITH_TIMEOUT(empty->isVisible(), 3'000);
    }
};

QTEST_MAIN(StudioSpaceVectorTest)
#include "test_studio_space_vector.moc"
