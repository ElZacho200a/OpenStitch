// SPDX-License-Identifier: Apache-2.0
// V0 « charpente des quatre espaces » : le vrai Main.qml, le vrai WorkspaceController, un vrai
// document (image, segmentation, formes, objets de broderie).
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest/QtTest>

#include <cmath>
#include <memory>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "studio_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
constexpr std::uint64_t kRegion = 1;
constexpr std::uint64_t kVector = (1ULL << 54) + 16;
constexpr std::uint64_t kObject = (1ULL << 54) + 17;

document::Project pipelineProject() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    segmentation::Segmentation seg;
    seg.width = 2;
    seg.height = 2;
    seg.labels.assign(4, 1);
    segmentation::Region region;
    region.id = RegionId{kRegion};
    region.pixel_count = 4;
    seg.region_slots.push_back(region);
    project.segmentation = seg;

    document::VectorObject vector;
    vector.id = ObjectId{kVector};
    vector.source_region = RegionId{kRegion};
    geometry::Path path;
    path.closed = false;
    path.nodes = {{{Micrometers{-15'000}, Micrometers{0}}},
                  {{Micrometers{15'000}, Micrometers{0}}}};
    vector.paths.push_back({path, {}});
    project.vector_objects.push_back(vector);
    document::EmbroideryObject object;
    object.id = ObjectId{kObject};
    object.source_vector = vector.id;
    object.name = "Thread";
    object.rgb = {180, 17, 65};
    object.params = document::RunningStitchParams{Micrometers{2'000}};
    project.embroidery_objects.push_back(object);
    project.object_ids.reset(kObject);
    return project;
}

// Parcours de l'arbre VISUEL (les contenus d'un bouton ne sont pas des enfants QObject).
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
    QQmlApplicationEngine engine;
    QStringList warnings;
    QQuickWindow* window{nullptr};
    WorkspaceController* workspace{nullptr};
};
} // namespace

class StudioWorkspacesTest : public QObject {
    Q_OBJECT
private:
    void open(Rig& rig, bool withProject = true) {
        connect(&rig.engine, &QQmlApplicationEngine::warnings, this,
                [&rig](const QList<QQmlError>& errors) {
                    for (const auto& error : errors) {
                        rig.warnings.push_back(error.toString());
                    }
                });
        rig.engine.rootContext()->setContextProperty(QStringLiteral("studio"), &rig.controller);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("actions"), &rig.actions);
        rig.engine.rootContext()->setContextProperty(QStringLiteral("editor"), &rig.editor);
        if (withProject) {
            rig.session.replaceProject(pipelineProject(), false);
        }
        rig.engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!rig.engine.rootObjects().isEmpty(), qPrintable(rig.warnings.join('\n')));
        rig.window = qobject_cast<QQuickWindow*>(rig.engine.rootObjects().front());
        QVERIFY(rig.window);
        rig.workspace = rig.window->findChild<WorkspaceController*>();
        QVERIFY(rig.workspace);
        QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
        if (withProject) {
            QTRY_VERIFY_WITH_TIMEOUT(!rig.controller.busy() && rig.controller.scene() != nullptr,
                                     15'000);
        }
    }
    // Les canevas déjà visités sont mis en cache, mais UN SEUL reste visible.
    GpuCanvas* onlyCanvas(Rig& rig) {
        const auto visible = [&] {
            QList<GpuCanvas*> out;
            for (auto* canvas : rig.window->findChildren<GpuCanvas*>()) {
                if (canvas->isVisible())
                    out.push_back(canvas);
            }
            return out;
        };
        [&] { QTRY_COMPARE_WITH_TIMEOUT(visible().size(), 1, 5'000); }();
        const auto all = visible();
        return all.size() == 1 ? all.front() : nullptr;
    }
    QVariantMap spaceMap(Rig& rig, const QString& id) { return rig.workspace->space(id); }

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

    void fourSpacesAndOneVisibleCanvas() {
        Rig rig;
        open(rig);
        const auto spaces = rig.workspace->spaces();
        QCOMPARE(spaces.size(), 4);
        const QStringList ids{"image", "segmentation", "vector", "points"};
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(spaces[i].toMap().value("id").toString(), ids[i]);
            QVERIFY(!spaces[i].toMap().value("label").toString().isEmpty());
            QVERIFY(!spaces[i].toMap().value("detail").toString().isEmpty());
        }
        QCOMPARE(rig.workspace->current(), QStringLiteral("points")); // comportement historique
        QVERIFY(onlyCanvas(rig));
        QCOMPARE(onlyCanvas(rig)->objectName(), QStringLiteral("studioCanvas"));
        GpuCanvas* initialPoints = onlyCanvas(rig);
        for (const auto& id : {QStringLiteral("image"), QStringLiteral("segmentation"),
                               QStringLiteral("vector"), QStringLiteral("points")}) {
            rig.workspace->setCurrent(id);
            auto* canvas = onlyCanvas(rig);
            QVERIFY2(canvas, qPrintable(id));
            QVERIFY(rig.window->findChild<QObject*>(id + "Space") != nullptr ||
                    id == QStringLiteral("points"));
        }
        QVERIFY(rig.window->findChild<QObject*>(QStringLiteral("pointsSpace")));
        // Les quatre scènes visitées sont gardées en mémoire, mais une seule est visible.
        QCOMPARE(rig.window->findChildren<GpuCanvas*>().size(), 4);
        rig.workspace->setCurrent(QStringLiteral("points"));
        QCOMPARE(onlyCanvas(rig), initialPoints);
        // Le canevas de l'espace Vecteurs est vide de points (couche « stitches » éteinte).
        rig.workspace->setCurrent(QStringLiteral("vector"));
        QVERIFY(!onlyCanvas(rig)->layers().value("stitches").toBool());
        QVERIFY(onlyCanvas(rig)->layers().value("vectors").toBool());
        QVERIFY(rig.window->findChild<QObject*>(QStringLiteral("vectorSpace")));
        // Le canevas des points garde la couche des points par défaut.
        rig.workspace->setCurrent(QStringLiteral("points"));
        QVERIFY(onlyCanvas(rig)->layers().value("stitches").toBool());
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void cameraAndSelectionSurviveSpaceSwitches() {
        Rig rig;
        open(rig);
        auto* points = onlyCanvas(rig);
        QVERIFY(points);
        points->zoomToScale(7.5);
        points->panBy(23.0, -11.0);
        const auto before = points->viewState();
        rig.controller.selectObject(QString::number(kObject), false);
        QVERIFY(rig.controller.hasSelection());

        for (const auto& id : {QStringLiteral("segmentation"), QStringLiteral("points"),
                               QStringLiteral("image"), QStringLiteral("points")}) {
            rig.workspace->setCurrent(id);
            auto* canvas = onlyCanvas(rig);
            QVERIFY(canvas);
            QTRY_VERIFY_WITH_TIMEOUT(canvas->width() > 0 && canvas->height() > 0, 5'000);
            const auto after = canvas->viewState();
            QVERIFY2(std::abs(after.value("zoom").toDouble() - before.value("zoom").toDouble()) <
                         1e-6,
                     qPrintable(id));
            QVERIFY2(std::abs(after.value("centerX").toDouble() -
                              before.value("centerX").toDouble()) < 1e-3,
                     qPrintable(id));
            QVERIFY2(std::abs(after.value("centerY").toDouble() -
                              before.value("centerY").toDouble()) < 1e-3,
                     qPrintable(id));
            QVERIFY(rig.controller.hasSelection());
            QCOMPARE(static_cast<int>(rig.session.selection().targets().size()), 1);
        }
        QCOMPARE(rig.workspace->view().value("selectionCount").toInt(), 1);
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void ctrlOneToFourSwitchSpaces() {
        Rig rig;
        open(rig);
        const QStringList ids{"image", "segmentation", "vector", "points"};
        for (const int order : {2, 0, 3, 1}) {
            QTest::keyClick(rig.window, Qt::Key_1 + order, Qt::ControlModifier);
            QTRY_COMPARE(rig.workspace->current(), ids[order]);
            QVERIFY(onlyCanvas(rig));
        }
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void badgesAndBannerFollowAnInvalidatedSegmentation() {
        Rig rig;
        open(rig);
        // Document cohérent : tout est à jour, aucun bandeau.
        QTRY_COMPARE(spaceMap(rig, "segmentation").value("state").toString(),
                     QStringLiteral("upToDate"));
        for (const auto* id : {"image", "segmentation", "vector", "points"}) {
            QCOMPARE(spaceMap(rig, id).value("state").toString(), QStringLiteral("upToDate"));
        }
        rig.workspace->setCurrent(QStringLiteral("vector"));
        auto* banner = deepItem(rig.window->contentItem(), QStringLiteral("upstreamBanner"));
        QVERIFY(banner);
        QVERIFY(!banner->isVisible());

        // Des régions sont corrigées sans répercussion : la segmentation devient obsolète.
        rig.workspace->setProperty("dirtyReason", QStringLiteral("3 régions corrigées"));
        rig.workspace->setProperty("segmentationDirty", true);
        QTRY_COMPARE(spaceMap(rig, "segmentation").value("state").toString(),
                     QStringLiteral("stale"));
        QCOMPARE(spaceMap(rig, "vector").value("state").toString(), QStringLiteral("stale"));
        QCOMPARE(spaceMap(rig, "points").value("state").toString(), QStringLiteral("stale"));
        QCOMPARE(spaceMap(rig, "image").value("state").toString(), QStringLiteral("upToDate"));

        // Pastille (couleur d'état + nom accessible) de la barre d'onglets.
        QTRY_VERIFY(deepItem(rig.window->contentItem(), QStringLiteral("spaceBadge_segmentation")));
        auto* badge =
            deepItem(rig.window->contentItem(), QStringLiteral("spaceBadge_segmentation"));
        QTRY_COMPARE(badge->property("badgeState").toString(), QStringLiteral("stale"));
        auto* tab = deepItem(rig.window->contentItem(), QStringLiteral("spaceTab_segmentation"));
        QVERIFY(tab);

        // Bandeau de l'espace Vecteurs : état de l'étape AMONT (segmentation) + bouton d'action.
        QTRY_VERIFY(banner->isVisible());
        auto* text = deepItem(rig.window->contentItem(), QStringLiteral("upstreamBannerText"));
        QVERIFY(text);
        QVERIFY(
            text->property("text").toString().contains(QStringLiteral("Segmentation obsolète")));
        QVERIFY(text->property("text").toString().contains(QStringLiteral("3 régions corrigées")));
        auto* action = deepItem(rig.window->contentItem(), QStringLiteral("upstreamBannerAction"));
        QVERIFY(action);
        QVERIFY(!action->isVisible()); // l'espace n'a pas encore fourni son action
        rig.workspace->setStageAction(QStringLiteral("vector"),
                                      QStringLiteral("Mettre à jour les formes"));
        QTRY_VERIFY(action->isVisible());
        QSignalSpy requested(rig.workspace, &WorkspaceController::updateRequested);
        QVERIFY(QMetaObject::invokeMethod(action, "clicked"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.front().front().toString(), QStringLiteral("vector"));

        // L'espace Image n'a pas d'amont : pas de bandeau.
        rig.workspace->setCurrent(QStringLiteral("image"));
        QTRY_VERIFY(!banner->isVisible());
        // Une étape publiée par son espace l'emporte sur le calcul de base.
        rig.workspace->setStageStatus(QStringLiteral("image"), QStringLiteral("blocked"),
                                      QStringLiteral("test"));
        QCOMPARE(spaceMap(rig, "image").value("state").toString(), QStringLiteral("blocked"));
        rig.workspace->clearStageStatus(QStringLiteral("image"));
        QCOMPARE(spaceMap(rig, "image").value("state").toString(), QStringLiteral("upToDate"));

        // Corrections répercutées : tout redevient à jour.
        rig.workspace->setProperty("segmentationDirty", false);
        QTRY_COMPARE(spaceMap(rig, "vector").value("state").toString(), QStringLiteral("upToDate"));
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }

    void emptyDocumentReportsEmptyStages() {
        Rig rig;
        open(rig, false);
        QCOMPARE(spaceMap(rig, "image").value("state").toString(), QStringLiteral("empty"));
        QCOMPARE(spaceMap(rig, "segmentation").value("state").toString(),
                 QStringLiteral("blocked"));
        QCOMPARE(spaceMap(rig, "points").value("state").toString(), QStringLiteral("empty"));
        QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
    }
};

QTEST_MAIN(StudioWorkspacesTest)
#include "test_studio_workspaces.moc"
