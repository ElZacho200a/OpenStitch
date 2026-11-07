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
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest/QtTest>

#include <memory>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "openstitch/commands/project_commands.hpp"
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
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
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
        QVERIFY(rig.canvas->vectorImage() != nullptr);
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

    void clickSelectsAShapeAndShowsItsOrigin() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 2.5, 0.0));
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
        // La sélection est redessinée (nœuds visibles) : une reconstruction de plus.
        QVERIFY(rig.stage.layerBuildCount() >= 2);
        // Clic dans le vide : plus de sélection.
        QTest::mouseClick(rig.window, Qt::LeftButton, {}, at(rig, 0.0, 6.0));
        QTRY_VERIFY_WITH_TIMEOUT(rig.session.selection().empty(), 3'000);
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
        // Correction de région : fusion de la région 2 dans la 1 (la forme de la 2 devient
        // orpheline).
        rig.session.execute(
            std::make_unique<commands::MergeRegionsCommand>(RegionId{1}, RegionId{2}));
        QTRY_COMPARE_WITH_TIMEOUT(
            rig.workspace->space(QStringLiteral("vector")).value("state").toString(),
            QStringLiteral("stale"), 3'000);
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

    void convertToEmbroideryIsOneUndoStep() {
        Rig rig;
        open(rig);
        enterSpace(rig);
        QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{0});
        const auto undoBefore = steps(rig);
        QVERIFY(rig.stage.convertToEmbroidery());
        QVERIFY(rig.session.project().embroidery_objects.size() >= 1);
        QCOMPARE(steps(rig), undoBefore + 1);
        QVERIFY(rig.session.undo());
        QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{0});
    }

    // Recette : ecrit le projet de l'espace pour la capture (OPENSTITCH_VECTOR_SAMPLE=chemin.osp).
    void writeSampleForCapture() {
        const auto path = qEnvironmentVariable("OPENSTITCH_VECTOR_SAMPLE");
        if (path.isEmpty()) {
            QSKIP("recette seulement");
        }
        QVERIFY(project_io::save_project(path.toStdWString(), shapesProject()).has_value());
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
