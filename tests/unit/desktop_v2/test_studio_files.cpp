// SPDX-License-Identifier: Apache-2.0
// Famille A (fichiers, imports, exports) de Studio : parcours complets sur de vraies données, avec
// la vraie interface QML (QT_QPA_PLATFORM=offscreen), sans simulation du moteur.
#include <QFile>
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
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cmath>

#include <filesystem>
#include <fstream>
#include <memory>

#include "action_controller.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "image_controller.hpp"
#include "image_space_controller.hpp"
#include "openstitch/project_io/project_io.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "region_controller.hpp"
#include "segmentation_space_controller.hpp"
#include "studio_controller.hpp"
#include "vector_space_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {
constexpr std::uint64_t firstId = (1ULL << 54) + 17;
constexpr std::uint64_t secondId = (1ULL << 54) + 19;

document::Project studioFixture() {
    document::Project project;
    project.finishing = document::SequenceFinishing::legacy();
    for (const auto id : {firstId, secondId}) {
        const std::int32_t y = id == firstId ? 10'000 : -10'000;
        document::VectorObject vector;
        vector.id = ObjectId{id - 1};
        geometry::Path path;
        path.closed = false;
        path.nodes = {{{Micrometers{-15'000}, Micrometers{y}}},
                      {{Micrometers{15'000}, Micrometers{y}}}};
        vector.paths.push_back({path, {}});
        project.vector_objects.push_back(vector);
        document::EmbroideryObject object;
        object.id = ObjectId{id};
        object.source_vector = vector.id;
        object.name = id == firstId ? "Raspberry thread" : "Mint thread";
        object.rgb = id == firstId ? std::array<std::uint8_t, 3>{180, 17, 65}
                                   : std::array<std::uint8_t, 3>{21, 180, 151};
        object.params = document::RunningStitchParams{Micrometers{2'000}};
        project.embroidery_objects.push_back(object);
    }
    project.object_ids.reset(secondId);
    return project;
}

QString osp(const QTemporaryDir& dir, const char* name, const document::Project& project) {
    const QString path = dir.filePath(QString::fromLatin1(name));
    const auto saved =
        project_io::save_project(std::filesystem::path(path.toStdWString()), project);
    return saved ? path : QString();
}

QUrl url(const QTemporaryDir& dir, const char* name) {
    return QUrl::fromLocalFile(dir.filePath(QString::fromLatin1(name)));
}

QByteArray bytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QQuickItem* findVisualItem(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* found = findVisualItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

std::vector<ObjectId> sewnLayerOrder(const document::Project& project) {
    std::vector<ObjectId> order;
    if (const auto sequence = stitch_generation::effective_sequence(project); sequence) {
        for (const auto& command : sequence->commands) {
            const auto* object = project.findEmbroidery(command.source);
            const auto* shape =
                object == nullptr ? nullptr : project.findObject(object->source_vector);
            if (command.type == stitch::CommandType::Stitch && shape &&
                shape->source_image.valid() && (order.empty() || order.back() != shape->calque)) {
                order.push_back(shape->calque);
            }
        }
    }
    return order;
}

// Session + contrôleurs sur la vraie scène QML.
struct Rig {
    DocumentSession session;
    StudioController studio{session};
    ActionController actions{studio};
    EditController editor{studio};
    std::unique_ptr<FileController> files;
    QQmlApplicationEngine engine;
    QQuickWindow* window{nullptr};
    QStringList warnings;

    explicit Rig(const QString& stateDir = {}, bool withQml = false) {
        files = std::make_unique<FileController>(studio, &actions, stateDir);
        if (!withQml) {
            return;
        }
        QObject::connect(&engine, &QQmlApplicationEngine::warnings,
                         [this](const QList<QQmlError>& errors) {
                             for (const auto& e : errors) {
                                 warnings.push_back(e.toString());
                             }
                         });
        engine.rootContext()->setContextProperty(QStringLiteral("studio"), &studio);
        engine.rootContext()->setContextProperty(QStringLiteral("actions"), &actions);
        engine.rootContext()->setContextProperty(QStringLiteral("editor"), &editor);
        engine.rootContext()->setContextProperty(QStringLiteral("files"), files.get());
        engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        window = engine.rootObjects().isEmpty()
                     ? nullptr
                     : qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    }

    void load(const QString& path) {
        files->openPath(path);
        QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && !files->busy() && studio.scene() != nullptr,
                                 15'000);
    }
    void makeDirty() {
        studio.selectObject(QString::number(firstId));
        QVERIFY(studio.moveSelection(3, 1));
        QVERIFY(studio.dirty());
    }
    bool waitIdle() {
        return QTest::qWaitFor([this] { return !files->busy(); }, 30'000);
    }
};
} // namespace

class StudioFilesTest : public QObject {
    Q_OBJECT
private slots:
    void photoRenderRunsTheExternalEngineOnTheThreadScene();
    void initTestCase();
    void openDialogListsOpenRaster();
    void openingAnotherProjectClearsLayerAndObjectSelection();
    void fileMenuEntriesCloseAfterRealMouseClick();
    void fileMenuStaysReachableInAShortWindow();
    void comboPopupOpensAboveAtWindowBottom();
    void comboPopupFollowsMovedFieldAndShowsCppRowLabels();
    void unsavedGuardNewOpenAndQuit();
    void guardSaveWithAndWithoutPath();
    void guardDialogIsShownByTheRealQml();
    void discardFromCloseDialogClosesWindowWithoutSaving();
    void recentFilesPersistPruneAndReopen();
    void openingMessageIsReplacedOnceLoadedInsteadOfLingering();
    void dropAndCommandLineDispatchByExtension();
    void machineExportImportRoundTrip();
    void insertDesignPreviewThenOneUndoStep();
    void exportOptionsShowThePlanBeforeWriting();
    void overflowExportRequiresExplicitConfirmation();
    void exportsAreDeterministicAndRepeatable();
    void documentExportsWriteRealFiles();
    void exportOptionsAreOneUndoStepAndRefusedWhenInvalid();
    void imageAndVectorImports();
    void imagesStayIndependentThroughStitchesAndSave();
    void openRasterThroughStudioPoints();
    void batchSegmentationSkipsTransparentImage();
    void vectorizeAllLayersIsIdempotent();
    void autosaveRecoveryAndDiscard();
    void hoopMachineAndUserHoops();
    void hoopQuickSettingsAreOneUndoStepEach();
};

void StudioFilesTest::imagesStayIndependentThroughStitchesAndSave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(80, 80, QImage::Format_ARGB32);
    const auto red = url(dir, "fond.png");
    const auto blue = url(dir, "dessus.png");
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.fillRect(12, 10, 50, 42, Qt::red);
    }
    QVERIFY(image.save(red.toLocalFile()));
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.fillRect(17, 30, 45, 38, Qt::blue);
    }
    QVERIFY(image.save(blue.toLocalFile()));

    Rig rig;
    const auto existing = osp(dir, "existant.osp", studioFixture());
    QVERIFY(!existing.isEmpty());
    rig.load(existing);
    ImageController images(rig.studio);
    ImageSpaceController imageSpace(rig.studio, images);
    VectorSpaceController vectors(rig.studio);
    QQmlApplicationEngine engine;
    auto* context = engine.rootContext();
    context->setContextProperty(QStringLiteral("studio"), &rig.studio);
    context->setContextProperty(QStringLiteral("editor"), &rig.editor);
    context->setContextProperty(QStringLiteral("actions"), &rig.actions);
    context->setContextProperty(QStringLiteral("files"), rig.files.get());
    context->setContextProperty(QStringLiteral("imageTools"), &images);
    context->setContextProperty(QStringLiteral("imageSpace"), &imageSpace);
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY(!engine.rootObjects().empty());
    auto* window = engine.rootObjects().front();
    QSignalSpy imported(rig.files.get(), &FileController::imageImported);
    const auto broken = url(dir, "corrompu.png");
    {
        QFile file(broken.toLocalFile());
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("not a png"), qint64{9});
    }
    rig.files->importImageLayers({red, broken});
    QVERIFY(rig.waitIdle());
    QVERIFY(rig.files->messageIsError());
    QVERIFY(rig.session.project().image_sources.empty());
    QCOMPARE(rig.session.historySize(), std::size_t{0});
    QCOMPARE(imported.count(), 0);
    rig.files->importImageLayers({red, blue});
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 15'000);
    const auto& project = rig.session.project();
    QCOMPARE(rig.studio.path(), existing);
    QCOMPARE(project.image_sources.size(), std::size_t{2});
    QCOMPARE(project.embroidery_objects.size(), std::size_t{2});
    const auto lower = project.image_sources[0].calque;
    const auto upper = project.image_sources[1].calque;
    QCOMPARE(rig.session.historySize(), std::size_t{1});
    QVERIFY(rig.session.undo());
    QVERIFY(project.image_sources.empty());
    QVERIFY(rig.session.redo());
    QCOMPARE(project.image_sources.size(), std::size_t{2});
    QVERIFY(rig.session.selectImageSource(lower));

    QCOMPARE(images.applyPrep(QStringLiteral("flip_h"), {}), QString());
    QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QObject*>(QStringLiteral("imageLayerSelector")),
                             5'000);
    auto* picker = window->findChild<QObject*>(QStringLiteral("imageLayerSelector"));
    QTRY_COMPARE_WITH_TIMEOUT(picker->property("currentText").toString(),
                              QStringLiteral("fond.png"), 5'000);
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 1)));
    QCOMPARE(picker->property("model").toStringList().size(), 2);
    QVERIFY(project.imageSourceId() == upper);
    QVERIFY(rig.session.undo());
    QVERIFY(project.findImageSource(lower)->ops.empty());
    QVERIFY(rig.session.redo());
    QCOMPARE(project.findImageSource(lower)->ops.size(), std::size_t{1});
    QVERIFY(project.findImageSource(upper)->ops.empty());
    QVERIFY(project.imageSourceId() == upper);

    for (const ObjectId layer : {lower, upper}) {
        QVERIFY(rig.session.selectImageSource(layer));
        images.applySegmentation({});
        QTRY_VERIFY_WITH_TIMEOUT(images.hasSegmentation() && !images.busy(), 15'000);
        QVERIFY(vectors.updateFromSegmentation());
    }
    QVERIFY(vectors.convertToEmbroidery());
    QTRY_VERIFY_WITH_TIMEOUT(!vectors.converting(), 15'000); // calcul hors du fil d'interface
    for (const ObjectId layer : {lower, upper}) {
        QVERIFY(std::ranges::any_of(project.embroidery_objects, [&](const auto& object) {
            const auto* shape = project.findObject(object.source_vector);
            return shape && shape->source_image == layer && shape->calque == layer;
        }));
    }
    QVERIFY(sewnLayerOrder(project) == (std::vector<ObjectId>{lower, upper}));

    const auto path = osp(dir, "calques.osp", project);
    QVERIFY(!path.isEmpty());
    const auto saved = project_io::load_project(std::filesystem::path(path.toStdWString()));
    QVERIFY(saved.has_value());
    QCOMPARE(saved->image_sources.size(), std::size_t{2});
    QCOMPARE(saved->findImageSource(lower)->ops.size(), std::size_t{1});
    QVERIFY(sewnLayerOrder(*saved) == (std::vector<ObjectId>{lower, upper}));
    for (const ObjectId layer : {lower, upper}) {
        const auto* source = saved->findImageSource(layer);
        QVERIFY(source && source->segmentation);
        QVERIFY(source->original.rgba == project.findImageSource(layer)->original.rgba);
        QVERIFY(source->segmentation->labels ==
                project.findImageSource(layer)->segmentation->labels);
    }
}

void StudioFilesTest::openRasterThroughStudioPoints() {
    const auto path = QFINDTESTDATA("../../fixtures/motif-calques.ora");
    QVERIFY(!path.isEmpty());
    Rig rig;
    ImageController images(rig.studio);
    ImageSpaceController imageSpace(rig.studio, images);
    RegionController regions(rig.studio, images);
    SegmentationSpaceController segmentation(rig.studio, images, regions);
    VectorSpaceController vectors(rig.studio);
    QQmlApplicationEngine engine;
    auto* context = engine.rootContext();
    context->setContextProperty(QStringLiteral("studio"), &rig.studio);
    context->setContextProperty(QStringLiteral("editor"), &rig.editor);
    context->setContextProperty(QStringLiteral("actions"), &rig.actions);
    context->setContextProperty(QStringLiteral("files"), rig.files.get());
    context->setContextProperty(QStringLiteral("imageTools"), &images);
    context->setContextProperty(QStringLiteral("imageSpace"), &imageSpace);
    context->setContextProperty(QStringLiteral("regionTools"), &regions);
    context->setContextProperty(QStringLiteral("segmentationSpace"), &segmentation);
    context->setContextProperty(QStringLiteral("vectorStage"), &vectors);
    engine.addImageProvider(QStringLiteral("prepare"), images.createProvider());
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY(!engine.rootObjects().empty());
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    auto* workspace = window->findChild<WorkspaceController*>();
    QVERIFY(workspace);
    const auto ready = [window](const QString& name) {
        auto* item = findVisualItem(window->contentItem(), name);
        return item && item->isVisible() && item->isEnabled();
    };
    const auto click = [window](const QString& name) {
        auto* item = findVisualItem(window->contentItem(), name);
        return item && item->isVisible() && item->isEnabled() &&
               QMetaObject::invokeMethod(item, "click");
    };

    QSignalSpy imported(rig.files.get(), &FileController::imageImported);
    rig.files->openUrl(QUrl::fromLocalFile(path));
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 15'000);
    QCOMPARE(workspace->current(), QStringLiteral("image"));
    const auto& project = rig.session.project();
    QCOMPARE(images.imageLayers(), (QStringList{"Feuilles", "Fleur"}));
    QCOMPARE(project.image_sources.size(), std::size_t{2});
    const auto leaves = project.image_sources[0].calque;
    const auto flower = project.image_sources[1].calque;
    QVERIFY(!project.findImageSource(leaves)->segmentation);
    QVERIFY(!project.findImageSource(flower)->segmentation);
    QVERIFY(project.vector_objects.empty());
    QVERIFY(project.embroidery_objects.empty());

    workspace->setCurrent(QStringLiteral("segmentation"));
    const auto beforeSegmentation = rig.session.historyPosition();
    // Le bouton se désactive dès que le clic démarre la tâche (imageTools.busy) : attendre qu'il
    // soit cliquable puis cliquer une seule fois, sans réévaluer l'action elle-même (un aperçu en
    // direct automatique peut aussi occuper imageTools.busy quelques secondes au premier accès).
    QTRY_VERIFY_WITH_TIMEOUT(ready(QStringLiteral("segmentAllLayersButton")), 15'000);
    QVERIFY(click(QStringLiteral("segmentAllLayersButton")));
    QTRY_VERIFY_WITH_TIMEOUT(!images.busy() && project.findImageSource(leaves)->segmentation &&
                                 project.findImageSource(flower)->segmentation,
                             15'000);
    QCOMPARE(project.imageSourceId(), leaves);
    QCOMPARE(rig.session.historyPosition(), beforeSegmentation + 1);
    QVERIFY(project.vector_objects.empty());
    QVERIFY(project.embroidery_objects.empty());
    QVERIFY(project.findImageSource(leaves)->segmentation->region_count() > 0);
    QVERIFY(project.findImageSource(flower)->segmentation->region_count() > 0);
    auto* regionPicker = window->findChild<QObject*>(QStringLiteral("segmentationLayerSelector"));
    QVERIFY(regionPicker);
    QVERIFY(QMetaObject::invokeMethod(regionPicker, "activated", Q_ARG(int, 1)));
    QCOMPARE(project.imageSourceId(), flower);
    QVERIFY(rig.session.undo());
    QVERIFY(!project.findImageSource(leaves)->segmentation);
    QVERIFY(!project.findImageSource(flower)->segmentation);
    QVERIFY(rig.session.redo());
    QCOMPARE(project.imageSourceId(), flower);
    QVERIFY(project.findImageSource(leaves)->segmentation);
    QVERIFY(project.findImageSource(flower)->segmentation);

    workspace->setCurrent(QStringLiteral("vector"));
    QTRY_VERIFY_WITH_TIMEOUT(click(QStringLiteral("vectorUpdateButton")), 5'000);
    QVERIFY(!project.vector_objects.empty());
    QVERIFY(std::ranges::all_of(project.vector_objects, [&](const auto& shape) {
        return shape.calque == flower && shape.source_image == flower;
    }));
    auto* vectorPicker = window->findChild<QObject*>(QStringLiteral("vectorImageLayerSelector"));
    QVERIFY(vectorPicker);
    QVERIFY(QMetaObject::invokeMethod(vectorPicker, "activated", Q_ARG(int, 0)));
    QCOMPARE(project.imageSourceId(), leaves);
    QTRY_VERIFY_WITH_TIMEOUT(click(QStringLiteral("vectorUpdateButton")), 5'000);
    // Un seul clic (QTRY réévalue l'expression) ; la conversion tourne hors du fil d'interface.
    auto* convert = findVisualItem(window->contentItem(), QStringLiteral("vectorConvertButton"));
    QTRY_VERIFY_WITH_TIMEOUT(convert && convert->isVisible() && convert->isEnabled(), 5'000);
    QVERIFY(QMetaObject::invokeMethod(convert, "click"));
    // Le clic ouvre les réglages de conversion ; la conversion part à la confirmation.
    auto* confirm =
        findVisualItem(window->contentItem(), QStringLiteral("vectorConvertConfirmButton"));
    QVERIFY(confirm);
    QTRY_VERIFY_WITH_TIMEOUT(confirm->isVisible() && confirm->isEnabled(), 5'000);
    QVERIFY(QMetaObject::invokeMethod(confirm, "click"));
    QTRY_VERIFY_WITH_TIMEOUT(!project.embroidery_objects.empty(), 15'000);

    workspace->setCurrent(QStringLiteral("points"));
    QTRY_VERIFY_WITH_TIMEOUT(rig.studio.scene() && !rig.studio.busy(), 15'000);
    const std::vector<ObjectId> expected{leaves, flower};
    QVERIFY(sewnLayerOrder(project) == expected);
    QCOMPARE(rig.editor.layers().size(), 2);
    QVERIFY(rig.editor.setLayerVisible(QString::number(leaves.value), false));
    QVERIFY(sewnLayerOrder(project) == std::vector<ObjectId>{flower});
    QVERIFY(rig.editor.setLayerVisible(QString::number(leaves.value), true));

    const QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto savedPath = osp(dir, "openraster.osp", project);
    QVERIFY(!savedPath.isEmpty());
    const auto saved = project_io::load_project(std::filesystem::path(savedPath.toStdWString()));
    QVERIFY(saved.has_value());
    QVERIFY(sewnLayerOrder(*saved) == expected);
}

void StudioFilesTest::batchSegmentationSkipsTransparentImage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage source(32, 32, QImage::Format_ARGB32);
    const auto solid = url(dir, "plein.png");
    const auto transparent = url(dir, "transparent.png");
    source.fill(Qt::red);
    QVERIFY(source.save(solid.toLocalFile()));
    source.fill(Qt::transparent);
    QVERIFY(source.save(transparent.toLocalFile()));

    Rig rig;
    ImageController images(rig.studio);
    QSignalSpy imported(rig.files.get(), &FileController::imageImported);
    rig.files->importImageLayers({solid, transparent});
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 10'000);
    const auto& project = rig.session.project();
    QCOMPARE(project.image_sources.size(), std::size_t{2});
    const auto before = rig.session.historyPosition();
    images.applySegmentationToAllLayers({});
    images.cancelTask();
    QCOMPARE(rig.session.historyPosition(), before);

    images.applySegmentationToAllLayers({});
    QTRY_VERIFY_WITH_TIMEOUT(!images.busy() && project.image_sources.front().source.segmentation,
                             10'000);
    QCOMPARE(rig.session.historyPosition(), before + 1);
    QVERIFY(!project.image_sources.back().source.segmentation);
    QVERIFY(!images.messageIsError());
    QVERIFY(images.message().contains(QStringLiteral("transparent")));
}

void StudioFilesTest::vectorizeAllLayersIsIdempotent() {
    // « Vectoriser les N calques » : chaque calque reçoit les formes de SES régions, une seule
    // fois ; une seconde passe sur la même segmentation n'ajoute rien (pas de doublon).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage source(48, 48, QImage::Format_ARGB32);
    const auto first = url(dir, "rouge.png");
    const auto second = url(dir, "bleu.png");
    source.fill(Qt::white);
    {
        QPainter painter(&source);
        painter.fillRect(8, 8, 24, 24, Qt::red);
    }
    QVERIFY(source.save(first.toLocalFile()));
    source.fill(Qt::white);
    {
        QPainter painter(&source);
        painter.fillRect(16, 16, 24, 24, Qt::blue);
    }
    QVERIFY(source.save(second.toLocalFile()));

    Rig rig;
    ImageController images(rig.studio);
    QSignalSpy imported(rig.files.get(), &FileController::imageImported);
    rig.files->importImageLayers({first, second});
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 10'000);
    const auto& project = rig.session.project();
    QCOMPARE(project.image_sources.size(), std::size_t{2});
    images.applySegmentationToAllLayers({});
    QTRY_VERIFY_WITH_TIMEOUT(!images.busy() && project.image_sources[0].source.segmentation &&
                                 project.image_sources[1].source.segmentation,
                             10'000);

    VectorSpaceController vectors(rig.studio);
    QCOMPARE(vectors.segmentedLayerCount(), 2);
    QVERIFY(vectors.updateAllLayersFromSegmentation());
    const auto shapes = project.vector_objects.size();
    QVERIFY(shapes > 0);
    for (const auto& layer : project.image_sources) {
        std::size_t regions = 0;
        for (const auto& slot : layer.source.segmentation->region_slots) {
            regions += slot ? 1 : 0;
        }
        const auto owned = std::ranges::count_if(project.vector_objects, [&](const auto& shape) {
            return shape.source_region && shape.source_image == layer.calque;
        });
        QVERIFY(owned > 0);
        QVERIFY(static_cast<std::size_t>(owned) <= regions);
    }
    QVERIFY(!vectors.updateAllLayersFromSegmentation());
    QCOMPARE(project.vector_objects.size(), shapes);
}

void StudioFilesTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
}

void StudioFilesTest::openDialogListsOpenRaster() {
    Rig rig({}, true);
    QVERIFY(rig.window);
    auto* dialog = rig.window->findChild<QObject*>(QStringLiteral("openAnyDialog"));
    QVERIFY(dialog);
    const auto filters = dialog->property("nameFilters").toStringList();
    QVERIFY(!filters.isEmpty());
    QVERIFY(filters.front().contains(QStringLiteral("*.ora")));
    QVERIFY(filters.contains(QStringLiteral("OpenRaster (*.ora)")));
}

void StudioFilesTest::openingAnotherProjectClearsLayerAndObjectSelection() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto first = studioFixture();
    first.calques = {{ObjectId{201}, "Premier fond"}, {ObjectId{202}, "Premier dessus"}};
    auto second = studioFixture();
    second.calques = {{ObjectId{301}, "Nouveau fond"}, {ObjectId{302}, "Nouveau dessus"}};
    const auto firstPath = osp(dir, "un.osp", first);
    const auto secondPath = osp(dir, "deux.osp", second);
    QVERIFY(!firstPath.isEmpty() && !secondPath.isEmpty());

    Rig rig({}, true);
    QVERIFY(rig.window);
    rig.load(firstPath);
    auto* layerPanel = rig.window->findChild<QObject*>(QStringLiteral("layerPanel"));
    QVERIFY(layerPanel);
    auto* tree = layerPanel->findChild<QObject*>(QStringLiteral("objectTree"));
    auto* rename = layerPanel->findChild<QObject*>(QStringLiteral("layerRenameField"));
    QVERIFY(tree && rename);
    // Le calque courant de l'arbre est le calque actif du document (même notion partout).
    QVERIFY(rig.session.selectCalque(ObjectId{201}));
    QTRY_COMPARE(tree->property("currentLayer").toString(), QStringLiteral("201"));
    QVERIFY(rename->setProperty("text", QStringLiteral("Ancien brouillon")));
    QVERIFY(rename->setProperty("visible", true));
    const Target selected{TargetKind::EmbroideryObject, firstId};
    rig.studio.selectObject(QString::number(firstId));
    QVERIFY(rig.session.selection().contains(selected));
    QVERIFY(second.findEmbroidery(ObjectId{firstId}));

    auto* dialog = rig.window->findChild<QObject*>(QStringLiteral("openAnyDialog"));
    QVERIFY(dialog);
    QTest::keyClick(rig.window, Qt::Key_O, Qt::ControlModifier);
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 5'000);
    QVERIFY(dialog->setProperty("selectedFile", QUrl::fromLocalFile(secondPath)));
    QCOMPARE(dialog->property("selectedFile").toUrl(), QUrl::fromLocalFile(secondPath));
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY_WITH_TIMEOUT(
        rig.studio.path() == secondPath && !rig.studio.busy() && rig.studio.scene(), 15'000);
    QVERIFY(rig.session.selection().targets().empty());
    // Nouveau document : aucun calque actif enregistré, c'est le fond qui reçoit les formes.
    QTRY_COMPARE(tree->property("currentLayer").toString(), QStringLiteral("301"));
    QCOMPARE(layerPanel->property("layerId").toString(), QStringLiteral("301"));
    QVERIFY(!rename->property("visible").toBool());
    QVERIFY(rename->property("text").toString().isEmpty());
}

void StudioFilesTest::fileMenuEntriesCloseAfterRealMouseClick() {
    Rig rig({}, true);
    QVERIFY(rig.window);
    QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
    auto* button = rig.window->findChild<QQuickItem*>(QStringLiteral("fileMenuButton"));
    auto* menu = rig.window->findChild<QObject*>(QStringLiteral("fileMenu"));
    auto* entry = rig.window->findChild<QQuickItem*>(QStringLiteral("newFileMenuEntry"));
    QVERIFY(button);
    QVERIFY(menu);
    QVERIFY(entry);
    const auto center = [](QQuickItem* item) {
        return item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
    };
    QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, center(button));
    QTRY_VERIFY_WITH_TIMEOUT(menu->property("visible").toBool(), 3'000);
    // A physical click must both execute the action and release the modal popup;
    // otherwise the invisible/leftover overlay swallows clicks on the rest of Studio.
    QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier, center(entry));
    QTRY_VERIFY_WITH_TIMEOUT(!menu->property("visible").toBool(), 3'000);
    QVERIFY(rig.files->pendingReason().isEmpty());
}

void StudioFilesTest::fileMenuStaysReachableInAShortWindow() {
    // QA-STUDIO-001 CTL-013/015, POL-001/023: at the minimum height the File menu ran past the
    // window bottom and EXPORTER / PROJET could not be reached.
    Rig rig({}, true);
    QVERIFY(rig.window);
    QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
    rig.window->resize(rig.window->width(), 640);
    QTRY_COMPARE_WITH_TIMEOUT(rig.window->height(), 640, 3'000);
    auto* button = rig.window->findChild<QQuickItem*>(QStringLiteral("fileMenuButton"));
    auto* menu = rig.window->findChild<QObject*>(QStringLiteral("fileMenu"));
    QVERIFY(button);
    QVERIFY(menu);
    QTest::mouseClick(
        rig.window, Qt::LeftButton, Qt::NoModifier,
        button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(menu->property("visible").toBool(), 3'000);
    QTest::qWait(100);
    const double top = menu->property("y").toDouble();
    const double height = menu->property("height").toDouble();
    QVERIFY2(top + height <= rig.window->height() - 4.0,
             "The File menu must fit inside the window (scrolling for the rest)");
    auto* scroll = rig.window->findChild<QQuickItem*>(QStringLiteral("fileMenuScroll"));
    QVERIFY2(scroll, "The File menu entries must be in a ScrollView");
    auto* flick = scroll->property("contentItem").value<QQuickItem*>();
    QVERIFY(flick);
    // The last entries exist and the content can be scrolled down to them.
    QVERIFY(flick->property("contentHeight").toDouble() > flick->height());
    flick->setProperty("contentY", flick->property("contentHeight").toDouble() - flick->height());
    auto* hoop = rig.window->findChild<QQuickItem*>(QStringLiteral("hoopMenuEntry"));
    QVERIFY2(hoop, "The 'Cadre et machine' entry needs an objectName");
    const auto rect = hoop->mapRectToScene(hoop->boundingRect());
    QVERIFY2(rect.bottom() <= rig.window->height() && rect.top() >= top,
             "The last entry must be visible after scrolling");
}

void StudioFilesTest::comboPopupOpensAboveAtWindowBottom() {
    QQmlApplicationEngine engine;
    const QByteArray qml = R"qml(
        import QtQuick
        import QtQuick.Controls
        import "."
        ApplicationWindow {
            width: 400
            height: 300
            visible: true
            StudioComboBox {
                objectName: "edgeCombo"
                x: 230
                y: 250
                width: 155
                model: ["Premier", "Deuxième", "Troisième", "Quatrième", "Cinquième"]
                currentIndex: 4
            }
        }
    )qml";
    engine.loadData(qml, QUrl(QStringLiteral("qrc:/studio/qml/studio_combo_edge_test.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "The StudioComboBox fixture must load");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5'000);
    auto* combo = window->findChild<QQuickItem*>(QStringLiteral("edgeCombo"));
    QVERIFY(combo);
    auto* popup = combo->property("popup").value<QObject*>();
    QVERIFY(popup);

    const QPoint pos =
        combo->mapToScene(QPointF(combo->width() / 2.0, combo->height() / 2.0)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pos);
    QTRY_VERIFY_WITH_TIMEOUT(popup->property("visible").toBool(), 3'000);
    // The ListView only receives its delegate model once the popup is visible: measuring or
    // clicking before the five delegates exist races the layout (QA-STUDIO-001, CTL/POP).
    auto* list = popup->property("contentItem").value<QQuickItem*>();
    QVERIFY(list);
    QTRY_COMPARE_WITH_TIMEOUT(list->property("count").toInt(), 5, 3'000);
    QTRY_VERIFY_WITH_TIMEOUT(popup->property("height").toDouble() >= 100.0, 3'000);
    QTest::qWait(50); // let the y binding (above/below) settle on the final height
    const double x = popup->property("x").toDouble();
    const double y = popup->property("y").toDouble();
    const double w = popup->property("width").toDouble();
    const double h = popup->property("height").toDouble();
    QVERIFY2(y >= 7 && y + h <= window->height() - 7,
             "Drop-down extends beyond vertical window bounds");
    QVERIFY2(x >= 7 && x + w <= window->width() - 7,
             "Drop-down extends beyond horizontal window bounds");
    QVERIFY(w >= combo->width());
    QVERIFY(h >= 32.0);

    // A ListView initially highlights currentIndex=4. On Windows its auto-positioning
    // may scroll the first visible row away from index 0; explicitly scroll to the
    // beginning before testing a physical click on the first row.
    QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtBeginning"));
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(list->property("contentY").toDouble()) < 0.5, 3'000);
    // The item is displayed above the field, and it must remain physically clickable.
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      list->mapToScene(QPointF(24.0, 14.0)).toPoint());
    QTRY_COMPARE_WITH_TIMEOUT(combo->property("currentIndex").toInt(), 0, 3'000);
    QTRY_VERIFY_WITH_TIMEOUT(!popup->property("visible").toBool(), 3'000);
}

void StudioFilesTest::comboPopupFollowsMovedFieldAndShowsCppRowLabels() {
    // QA-STUDIO-001 CTL-011/012/013/016, POP-006: in the Points inspector the choice combo
    // is laid out after creation and fed by a C++ QVariantList of maps. The drop-down must open
    // next to the field (not at the window corner) and every row must carry its label.
    QQmlApplicationEngine engine;
    QVariantList choices;
    for (const auto* label : {"Tatami", "Satin", "Directionnel", "Contour"}) {
        choices.push_back(QVariantMap{{QStringLiteral("label"), QString::fromUtf8(label)}});
    }
    engine.rootContext()->setContextProperty(QStringLiteral("cppChoices"), choices);
    const QByteArray qml = R"qml(
        import QtQuick
        import QtQuick.Window
        import "."
        Window {
            width: 500
            height: 700
            visible: true
            Item { id: spacer; width: 10; height: 0 }
            StudioComboBox {
                objectName: "movedCombo"
                x: 200
                y: spacer.height
                width: 180
                model: cppChoices
                textRole: "label"
                currentIndex: 2
            }
            Component.onCompleted: spacer.height = 240   // the field moves after creation
        }
    )qml";
    engine.loadData(qml, QUrl(QStringLiteral("qrc:/studio/qml/studio_combo_moved_test.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "The StudioComboBox fixture must load");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5'000);
    auto* combo = window->findChild<QQuickItem*>(QStringLiteral("movedCombo"));
    QVERIFY(combo);
    QTRY_COMPARE_WITH_TIMEOUT(combo->y(), 240.0, 3'000);
    QCOMPARE(combo->property("displayText").toString(), QStringLiteral("Directionnel"));

    const QPoint pos =
        combo->mapToScene(QPointF(combo->width() / 2.0, combo->height() / 2.0)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pos);
    auto* popup = combo->property("popup").value<QObject*>();
    QVERIFY(popup);
    QTRY_VERIFY_WITH_TIMEOUT(popup->property("visible").toBool(), 3'000);
    auto* list = popup->property("contentItem").value<QQuickItem*>();
    QVERIFY(list);
    QTRY_COMPARE_WITH_TIMEOUT(list->property("count").toInt(), 4, 3'000);
    QTest::qWait(50);

    const double popupY = popup->property("y").toDouble();
    const double popupH = popup->property("height").toDouble();
    const double popupX = popup->property("x").toDouble();
    // Adjacent to the field (below or above it), never stranded at the window origin.
    const bool below = std::abs(popupY - (combo->y() + combo->height())) <= 8.0;
    const bool above = std::abs((popupY + popupH) - combo->y()) <= 8.0;
    QVERIFY2(below || above, "The drop-down must open next to its field");
    QVERIFY2(std::abs(popupX - combo->x()) <= 8.0 || popupX >= 8.0, "Unexpected popup x");
    QVERIFY2(popupX > 100.0, "The drop-down opened at the window's left corner");

    const QStringList expected{QStringLiteral("Tatami"), QStringLiteral("Satin"),
                               QStringLiteral("Directionnel"), QStringLiteral("Contour")};
    for (int i = 0; i < expected.size(); ++i) {
        QQuickItem* row = nullptr;
        QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row),
                                          Q_ARG(int, i)));
        QVERIFY2(row, qPrintable(QStringLiteral("Row %1 must exist").arg(i)));
        QCOMPARE(row->property("text").toString(), expected.at(i));
    }
}

void StudioFilesTest::unsavedGuardNewOpenAndQuit() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    const auto b = osp(dir, "b.osp", studioFixture());
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    Rig rig;
    rig.load(a);
    QVERIFY(!rig.studio.dirty());
    // Document propre : « Nouveau » s'exécute tout de suite, sans question.
    QSignalSpy guard(rig.files.get(), &FileController::guardChanged);
    rig.makeDirty();
    rig.files->requestNew();
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("nouveau"));
    QVERIFY(rig.studio.dirty()); // rien n'a été remplacé tant qu'on n'a pas répondu
    QVERIFY(!rig.session.project().embroidery_objects.empty());
    // Annuler : on garde le document modifié.
    rig.files->resolveGuard(QStringLiteral("cancel"));
    QVERIFY(rig.files->pendingReason().isEmpty());
    QVERIFY(rig.studio.dirty());
    QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});
    // Ignorer : les modifications sont perdues, le nouveau projet est installé.
    rig.files->requestNew();
    rig.files->resolveGuard(QStringLiteral("discard"));
    QVERIFY(rig.session.project().embroidery_objects.empty());
    QVERIFY(!rig.studio.dirty());
    // Ouvrir un autre fichier est protégé de la même façon.
    rig.load(a);
    rig.makeDirty();
    rig.files->openPath(b);
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("ouvrir"));
    rig.files->resolveGuard(QStringLiteral("cancel"));
    QCOMPARE(rig.studio.path(), a);
    // Quitter : propre -> immédiat ; modifié -> boîte, puis quitApproved.
    QSignalSpy quit(rig.files.get(), &FileController::quitApproved);
    QVERIFY(!rig.files->requestQuit());
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("quitter"));
    QVERIFY(quit.isEmpty());
    rig.files->resolveGuard(QStringLiteral("cancel"));
    QVERIFY(quit.isEmpty());
    QVERIFY(!rig.files->requestQuit());
    rig.files->resolveGuard(QStringLiteral("discard"));
    QCOMPARE(quit.count(), 1);
    rig.studio.undo();
    QVERIFY(!rig.studio.dirty());
    QVERIFY(rig.files->requestQuit());
    QVERIFY(guard.count() >= 4);
}

void StudioFilesTest::guardSaveWithAndWithoutPath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    Rig rig;
    rig.load(a);
    rig.makeDirty();
    const auto before = bytes(a);
    // Avec un chemin : « Enregistrer » écrit puis poursuit.
    rig.files->requestNew();
    rig.files->resolveGuard(QStringLiteral("save"));
    QVERIFY(rig.files->pendingReason().isEmpty());
    QVERIFY(bytes(a) != before);                               // le projet modifié a été écrit
    QVERIFY(rig.session.project().embroidery_objects.empty()); // puis « Nouveau » exécuté
    // Sans chemin : l'enregistrement sous est demandé, la suite attend le fichier.
    rig.session.replaceProject(studioFixture(), true);
    rig.makeDirty();
    QSignalSpy saveAs(rig.files.get(), &FileController::saveAsRequested);
    rig.files->requestNew();
    rig.files->resolveGuard(QStringLiteral("save"));
    QCOMPARE(saveAs.count(), 1);
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("nouveau"));
    rig.files->guardSaveAs(url(dir, "nouveau.osp"));
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("nouveau.osp"))));
    QVERIFY(rig.files->pendingReason().isEmpty());
    QVERIFY(rig.session.project().embroidery_objects.empty());
    // Une sauvegarde qui échoue n'efface rien : l'action est abandonnée.
    rig.session.replaceProject(studioFixture(), true);
    rig.makeDirty();
    rig.files->requestNew();
    rig.files->resolveGuard(QStringLiteral("save"));
    rig.files->guardSaveAs(url(dir, "a.osp/dossier/x.osp"));
    QVERIFY(rig.files->messageIsError());
    QVERIFY(rig.studio.dirty());
    QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});
}

void StudioFilesTest::guardDialogIsShownByTheRealQml() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    Rig rig({}, true);
    QVERIFY2(rig.window != nullptr, qPrintable(rig.warnings.join('\n')));
    rig.load(a);
    auto* guard = rig.window->findChild<QObject*>(QStringLiteral("guardDialog"));
    QVERIFY(guard);
    QVERIFY(!guard->property("visible").toBool());
    rig.makeDirty();
    rig.files->requestNew();
    QTRY_VERIFY(guard->property("visible").toBool());
    // Le bouton « Annuler » de la boîte répond vraiment au contrôleur.
    auto* cancel = rig.window->findChild<QObject*>(QStringLiteral("guardCancel"));
    QVERIFY(cancel);
    QMetaObject::invokeMethod(cancel, "clicked");
    QTRY_VERIFY(!guard->property("visible").toBool());
    QVERIFY(rig.studio.dirty());
    // « Ignorer » remplace le document.
    rig.files->requestNew();
    QTRY_VERIFY(guard->property("visible").toBool());
    QMetaObject::invokeMethod(rig.window->findChild<QObject*>(QStringLiteral("guardDiscard")),
                              "clicked");
    QTRY_VERIFY(rig.session.project().embroidery_objects.empty());
    QVERIFY(rig.window->findChild<QObject*>(QStringLiteral("fileDropArea")));
    QVERIFY(rig.window->findChild<QObject*>(QStringLiteral("fileMenuButton")));
    QVERIFY2(rig.warnings.isEmpty(), qPrintable(rig.warnings.join('\n')));
}

void StudioFilesTest::discardFromCloseDialogClosesWindowWithoutSaving() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = osp(dir, "a.osp", studioFixture());
    QVERIFY(!path.isEmpty());
    const auto original = bytes(path);
    Rig rig({}, true);
    QVERIFY2(rig.window, qPrintable(rig.warnings.join('\n')));
    rig.load(path);
    rig.makeDirty();
    QTRY_VERIFY(rig.window->isExposed());

    auto* guard = rig.window->findChild<QObject*>(QStringLiteral("guardDialog"));
    auto* cancel = rig.window->findChild<QQuickItem*>(QStringLiteral("guardCancel"));
    auto* discard = rig.window->findChild<QQuickItem*>(QStringLiteral("guardDiscard"));
    QVERIFY(guard);
    QVERIFY(cancel);
    QVERIFY(discard);
    const auto click = [&](QQuickItem* button) {
        const auto center = QPointF(button->width() / 2, button->height() / 2);
        QTest::mouseClick(rig.window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(center).toPoint());
    };

    rig.window->close();
    QTRY_VERIFY(guard->property("visible").toBool());
    click(cancel);
    QTRY_VERIFY(!guard->property("visible").toBool());
    QVERIFY(rig.window->isVisible());
    QVERIFY(rig.studio.dirty());

    QSignalSpy approved(rig.files.get(), &FileController::quitApproved);
    rig.window->close();
    QTRY_VERIFY(guard->property("visible").toBool());
    click(discard);
    QCOMPARE(approved.count(), 1);
    QTRY_VERIFY(!rig.window->isVisible());
    QCOMPARE(bytes(path), original);
}

void StudioFilesTest::recentFilesPersistPruneAndReopen() {
    QTemporaryDir dir;
    QTemporaryDir state;
    QVERIFY(dir.isValid() && state.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    const auto b = osp(dir, "b.osp", studioFixture());
    {
        Rig rig(state.path());
        QVERIFY(rig.files->recent().isEmpty());
        rig.load(a);
        rig.load(b);
        const auto recent = rig.files->recent();
        QCOMPARE(recent.size(), 2);
        QCOMPARE(recent.front().toMap().value(QStringLiteral("path")).toString(), b);
        QCOMPARE(recent.front().toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("b.osp"));
        // Rouvrir un récent le remonte sans doublon.
        rig.files->requestOpenRecent(1);
        QTRY_VERIFY_WITH_TIMEOUT(rig.studio.path() == a && !rig.studio.busy(), 15'000);
        QCOMPARE(rig.files->recent().size(), 2);
        QCOMPARE(rig.files->recent().front().toMap().value(QStringLiteral("path")).toString(), a);
    }
    {
        // Persistance entre deux lancements.
        Rig rig(state.path());
        QCOMPARE(rig.files->recent().size(), 2);
        QVERIFY(QFile::remove(b));
        Rig again(state.path()); // fichier disparu : retiré à la lecture
        QCOMPARE(again.files->recent().size(), 1);
        again.files->requestOpenRecent(0);
        QTRY_VERIFY_WITH_TIMEOUT(again.studio.path() == a && !again.studio.busy(), 15'000);
        again.files->clearRecent();
        QVERIFY(again.files->recent().isEmpty());
    }
    Rig reread(state.path());
    QVERIFY(reread.files->recent().isEmpty());
    // Un fichier introuvable est signalé sans toucher au document.
    reread.files->openPath(dir.filePath(QStringLiteral("fantome.osp")));
    QVERIFY(reread.files->messageIsError());
}

// QA-STUDIO-001 POP-015/UX-11: "Ouverture de X…" must not linger as if still loading once the
// document is visible -- loadFinished(true) replaces it instead of leaving it for the toast's
// 8 s timer.
void StudioFilesTest::openingMessageIsReplacedOnceLoadedInsteadOfLingering() {
    QTemporaryDir dir;
    QTemporaryDir state;
    QVERIFY(dir.isValid() && state.isValid());
    const auto path = osp(dir, "boat.osp", studioFixture());
    Rig rig(state.path());
    rig.files->openPath(path);
    QVERIFY(rig.files->message().startsWith(QStringLiteral("Ouverture de")));
    QTRY_VERIFY_WITH_TIMEOUT(
        !rig.studio.busy() && !rig.files->busy() && rig.studio.scene() != nullptr, 15'000);
    QCOMPARE(rig.files->message(), QStringLiteral("Projet ouvert."));
    QVERIFY(!rig.files->messageIsError());
}

void StudioFilesTest::dropAndCommandLineDispatchByExtension() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto a = osp(dir, "a.osp", studioFixture());
    Rig rig;
    // Glisser-déposer d'un projet.
    rig.files->openDropped({QUrl::fromLocalFile(a)});
    QTRY_VERIFY_WITH_TIMEOUT(rig.studio.path() == a && rig.studio.scene() != nullptr, 15'000);
    // Dropping an image loads ONLY the source, with no digitization dialog.
    QImage image(40, 40, QImage::Format_RGB32);
    image.fill(Qt::white);
    const auto png = dir.filePath(QStringLiteral("dessin.png"));
    QVERIFY(image.save(png));
    QSignalSpy imported(rig.files.get(), &FileController::imageImported);
    rig.files->openDropped({QUrl::fromLocalFile(png)});
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, 15'000);
    QVERIFY(rig.waitIdle());
    QCOMPARE(rig.studio.path(), QString());
    QVERIFY(rig.session.project().hasImage());
    QVERIFY(!rig.session.project().segmentation.has_value());
    QVERIFY(rig.session.project().vector_objects.empty());
    QVERIFY(rig.session.project().embroidery_objects.empty());
    // Format inconnu et URL distante : refus lisible.
    QFile unknown(dir.filePath(QStringLiteral("x.xyz")));
    QVERIFY(unknown.open(QIODevice::WriteOnly));
    unknown.close();
    rig.files->openPath(unknown.fileName());
    QVERIFY(rig.files->messageIsError());
    rig.files->openDropped({QUrl(QStringLiteral("https://exemple.test/a.osp"))});
    QVERIFY(rig.files->messageIsError());
}

void StudioFilesTest::machineExportImportRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    QVERIFY(!rig.files->machineFormats().isEmpty());
    for (const auto* name : {"sortie.dst", "sortie.pes", "sortie.jef", "sortie.exp"}) {
        rig.files->exportMachine(url(dir, name));
        QVERIFY2(rig.waitIdle(), name);
        QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
        QVERIFY2(QFileInfo(dir.filePath(QString::fromLatin1(name))).size() > 0, name);
    }
    QVERIFY(rig.files->hasLastExport());
    // Réimport du DST : broderie machine, projet non enregistré, document propre.
    Rig second;
    second.files->openPath(dir.filePath(QStringLiteral("sortie.dst")));
    QTRY_VERIFY_WITH_TIMEOUT(!second.studio.busy() && !second.files->busy() &&
                                 second.session.project().imported_design.has_value(),
                             15'000);
    QCOMPARE(second.studio.title(), QStringLiteral("sortie.dst"));
    QVERIFY(!second.studio.hasPath());
    QVERIFY(!second.studio.dirty());
    // Une erreur d'écriture est rapportée, jamais silencieuse.
    rig.files->exportMachine(url(dir, "sortie.dst/x.dst"));
    QVERIFY(rig.waitIdle());
    QVERIFY(rig.files->messageIsError());
    // Un format inconnu est refusé avant tout calcul.
    rig.files->exportMachine(url(dir, "x.abc"));
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("x.abc"))));
}

// CX08 : Fichier > Insérer un design. Aperçu dans la vraie boîte QML, insertion en une étape
// d'annulation avec des identifiants neufs ; un fichier illisible ne peut pas être validé.
void StudioFilesTest::insertDesignPreviewThenOneUndoStep() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig({}, true);
    QVERIFY(rig.window);
    QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
    const QString source = osp(dir, "motif.osp", studioFixture());
    QVERIFY(!source.isEmpty());
    rig.session.replaceProject(studioFixture(), true);

    auto* panel = rig.window->findChild<QObject*>(QStringLiteral("filePanel"));
    auto* dialog = rig.window->findChild<QObject*>(QStringLiteral("insertDesignDialog"));
    auto* summary = rig.window->findChild<QObject*>(QStringLiteral("insertDesignSummary"));
    auto* accept = rig.window->findChild<QQuickItem*>(QStringLiteral("acceptInsertDesign"));
    auto* cancel = rig.window->findChild<QQuickItem*>(QStringLiteral("cancelInsertDesign"));
    QVERIFY(panel);
    QVERIFY(dialog);
    QVERIFY(summary);
    QVERIFY(accept);
    QVERIFY(cancel);
    QVERIFY(rig.window->findChild<QQuickItem*>(QStringLiteral("insertDesignMenuEntry")));

    // Aperçu : rien n'est modifié tant que l'utilisateur n'a pas validé.
    const auto depth = rig.session.undoNames().size();
    QVERIFY(QMetaObject::invokeMethod(
        panel, "previewInsert", Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(source)))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 3'000);
    QVERIFY2(summary->property("text").toString().contains(QStringLiteral("2 objets")),
             qPrintable(summary->property("text").toString()));
    QVERIFY(accept->isEnabled());
    QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});
    QVERIFY(QMetaObject::invokeMethod(accept, "click"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 3'000);

    const auto& project = rig.session.project();
    QCOMPARE(project.embroidery_objects.size(), std::size_t{4});
    QCOMPARE(project.vector_objects.size(), std::size_t{4});
    QCOMPARE(rig.session.undoNames().size(), depth + 1);
    for (std::size_t i = 2; i < 4; ++i) {
        QVERIFY(project.embroidery_objects[i].id.value > secondId); // ids jamais réutilisés
    }
    QVERIFY(!rig.files->messageIsError());
    QVERIFY(rig.session.undo());
    QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});

    // Fichier illisible : refus affiché, bouton Insérer désactivé, document intact.
    const QString junk = dir.filePath(QStringLiteral("abime.osp"));
    {
        QFile file(junk);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("pas une archive");
    }
    QVERIFY(QMetaObject::invokeMethod(
        panel, "previewInsert", Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(junk)))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 3'000);
    QVERIFY(!accept->isEnabled());
    QVERIFY(!summary->property("text").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 3'000);
    QVERIFY(!rig.files->insertDesign(QUrl::fromLocalFile(junk)));
    QVERIFY(rig.files->messageIsError());
    QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});
    QVERIFY(rig.warnings.isEmpty());
}

// CX07 : Fichier > Exporter > Image / Gabarit ouvre les réglages ; l'aperçu annonce la taille en
// pixels ou le nombre de pages AVANT le choix du fichier, et suit les réglages.
void StudioFilesTest::exportOptionsShowThePlanBeforeWriting() {
    Rig rig({}, true);
    QVERIFY(rig.window);
    QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);
    rig.session.replaceProject(studioFixture(), true);
    auto* panel = rig.window->findChild<QObject*>(QStringLiteral("filePanel"));
    auto* dialog = rig.window->findChild<QObject*>(QStringLiteral("exportOptionsDialog"));
    auto* summary = rig.window->findChild<QObject*>(QStringLiteral("exportPlanSummary"));
    auto* confirm = rig.window->findChild<QQuickItem*>(QStringLiteral("confirmExportOptions"));
    auto* cancel = rig.window->findChild<QQuickItem*>(QStringLiteral("cancelExportOptions"));
    QVERIFY(panel && dialog && summary && confirm && cancel);
    QSignalSpy plans(rig.files.get(), &FileController::exportPlanReady);

    QVERIFY(QMetaObject::invokeMethod(panel, "openExportOptions",
                                      Q_ARG(QVariant, QStringLiteral("image"))));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 3'000);
    QTRY_COMPARE_WITH_TIMEOUT(plans.count(), 1, 15'000);
    const auto image = plans.takeFirst().at(1).toMap();
    QVERIFY2(image.value(QStringLiteral("ok")).toBool(),
             qPrintable(image.value(QStringLiteral("message")).toString()));
    const int width6 = image.value(QStringLiteral("width")).toInt();
    QVERIFY(width6 > 0);
    QTRY_VERIFY_WITH_TIMEOUT(summary->property("text").toString().contains(QString::number(width6)),
                             3'000);
    QVERIFY(confirm->isEnabled());

    // Doubler la résolution double (à un pixel près) la largeur annoncée.
    dialog->setProperty("pxPerMm", 12.0);
    QVERIFY(QMetaObject::invokeMethod(dialog, "refresh"));
    QTRY_COMPARE_WITH_TIMEOUT(plans.count(), 1, 15'000);
    const int width12 = plans.takeFirst().at(1).toMap().value(QStringLiteral("width")).toInt();
    QVERIFY2(std::abs(width12 - 2 * width6) <= 2,
             qPrintable(QStringLiteral("%1 vs %2").arg(width12).arg(width6)));
    QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 3'000);

    // Gabarit : nombre de pages et papier, recalculés au changement d'orientation.
    QVERIFY(QMetaObject::invokeMethod(panel, "openExportOptions",
                                      Q_ARG(QVariant, QStringLiteral("template"))));
    QTRY_COMPARE_WITH_TIMEOUT(plans.count(), 1, 15'000);
    auto tpl = plans.takeFirst();
    QCOMPARE(tpl.at(0).toString(), QStringLiteral("template"));
    QVERIFY(tpl.at(1).toMap().value(QStringLiteral("pages")).toInt() >= 1);
    QCOMPARE(tpl.at(1).toMap().value(QStringLiteral("pageWidthMm")).toDouble(), 210.0);
    dialog->setProperty("landscape", true);
    QVERIFY(QMetaObject::invokeMethod(dialog, "refresh"));
    QTRY_COMPARE_WITH_TIMEOUT(plans.count(), 1, 15'000);
    QCOMPARE(plans.takeFirst().at(1).toMap().value(QStringLiteral("pageWidthMm")).toDouble(),
             297.0);
    QTRY_VERIFY_WITH_TIMEOUT(summary->property("text").toString().contains(QStringLiteral("page")),
                             3'000);
    QVERIFY(QMetaObject::invokeMethod(cancel, "click"));

    // Document sans point : le gabarit est refusé, le bouton Enregistrer reste inactif.
    rig.session.replaceProject(document::Project{}, true);
    QVERIFY(QMetaObject::invokeMethod(panel, "openExportOptions",
                                      Q_ARG(QVariant, QStringLiteral("template"))));
    QTRY_COMPARE_WITH_TIMEOUT(plans.count(), 1, 15'000);
    QVERIFY(!plans.takeFirst().at(1).toMap().value(QStringLiteral("ok")).toBool());
    QVERIFY(!confirm->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
    QVERIFY(rig.warnings.isEmpty());
}

void StudioFilesTest::overflowExportRequiresExplicitConfirmation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig({}, true);
    QVERIFY(rig.window);
    QTRY_VERIFY_WITH_TIMEOUT(rig.window->isExposed(), 5'000);

    auto project = studioFixture();
    project.canvas.width = Micrometers{10'000};
    project.canvas.height = Micrometers{10'000};
    rig.session.replaceProject(project, true);

    const auto file = dir.filePath(QStringLiteral("hors_cadre.dst"));
    const auto output = QUrl::fromLocalFile(file);
    QSignalSpy requested(rig.files.get(), &FileController::exportConfirmationRequested);
    auto* dialog = rig.window->findChild<QObject*>(QStringLiteral("exportOverflowDialog"));
    auto* cancel = rig.window->findChild<QQuickItem*>(QStringLiteral("cancelOverflowExport"));
    auto* accept = rig.window->findChild<QQuickItem*>(QStringLiteral("acceptOverflowExport"));
    QVERIFY(dialog);
    QVERIFY(cancel);
    QVERIFY(accept);

    rig.files->exportMachine(output);
    QTRY_COMPARE_WITH_TIMEOUT(requested.count(), 1, 3'000);
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 3'000);
    QVERIFY(!QFileInfo::exists(file)); // no premature write
    QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 3'000);
    QVERIFY(!QFileInfo::exists(file)); // cancellation never writes

    rig.files->exportMachine(output);
    QTRY_COMPARE_WITH_TIMEOUT(requested.count(), 2, 3'000);
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("visible").toBool(), 3'000);
    QVERIFY(!QFileInfo::exists(file));
    QVERIFY(QMetaObject::invokeMethod(accept, "click"));
    QVERIFY(rig.waitIdle());
    QVERIFY2(QFileInfo(file).size() > 0, qPrintable(rig.files->message()));

    // Consent is revision-scoped. Editing after the warning invalidates it.
    QVERIFY(QFile::remove(file));
    rig.files->exportMachine(output);
    QTRY_COMPARE_WITH_TIMEOUT(requested.count(), 3, 3'000);
    rig.studio.selectObject(QString::number(firstId));
    QVERIFY(rig.studio.moveSelection(1, 0));
    QVERIFY(QMetaObject::invokeMethod(accept, "click"));
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!QFileInfo::exists(file));
}

void StudioFilesTest::exportsAreDeterministicAndRepeatable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    rig.files->exportAgain();
    QVERIFY(rig.files->messageIsError()); // aucun export précédent
    rig.files->exportMachine(url(dir, "un.dst"));
    QVERIFY(rig.waitIdle());
    const auto first = bytes(dir.filePath(QStringLiteral("un.dst")));
    QVERIFY(!first.isEmpty());
    QFile::remove(dir.filePath(QStringLiteral("un.dst")));
    rig.files->exportAgain(); // « avec les derniers réglages »
    QVERIFY(rig.waitIdle());
    QCOMPARE(rig.files->lastExportName(), QStringLiteral("un.dst"));
    QCOMPARE(bytes(dir.filePath(QStringLiteral("un.dst"))), first); // octet pour octet
    // L'export ne modifie jamais le document.
    QVERIFY(!rig.studio.dirty());
    QVERIFY(!rig.studio.canUndo());
    // Sans extension, le format demandé est ajouté.
    rig.files->exportMachine(url(dir, "sans"), {{QStringLiteral("format"), QStringLiteral("pes")}});
    QVERIFY(rig.waitIdle());
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("sans.pes"))));
}

void StudioFilesTest::photoRenderRunsTheExternalEngineOnTheThreadScene() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    // Interpréteur absent : message d'erreur clair, aucun fichier.
    qputenv("OPENSTITCH_PYTHON", "openstitch-python-absent-xyz");
    rig.files->exportPhotoRender(url(dir, "absent.png"));
    QVERIFY(rig.waitIdle());
    QVERIFY(rig.files->messageIsError());
    QVERIFY(rig.files->message().contains(QStringLiteral("Python")));
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("absent.png"))));
    qunsetenv("OPENSTITCH_PYTHON");
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (python.isEmpty()) {
        QSKIP("python absent : moteur factice non exécutable");
    }
    // Moteur factice : vérifie la scène reçue puis écrit un PNG 1 x 1 (pas de Mitsuba en test).
    const QString fake = dir.filePath(QStringLiteral("fake_engine.py"));
    {
        QFile f(fake);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("import json, sys, base64\n"
                "scene = json.load(open(sys.argv[1], encoding='utf-8'))\n"
                "assert scene['format'] == 'openstitch-thread-scene' and scene['segments']\n"
                "a = sys.argv\n"
                "assert a[a.index('--spp') + 1] == '4' and a[a.index('--tilt') + 1] == '12'\n"
                "assert a[a.index('--px-per-mm') + 1] == '40'\n"
                "assert a[a.index('--fabric') + 1] == '10,20,30'\n"
                "assert a[a.index('--fabric-type') + 1] == 'jersey'\n"
                "assert a[a.index('--background') + 1] == '200,200,204'\n" // 999 refusé
                "assert scene['thread_width_mm'] == 0.5\n"
                "open(sys.argv[2], 'wb').write(base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB"
                "CAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFBQIAX8jx0gAAAABJRU5ErkJggg=='))\n"
                "print('rendu factice', len(scene['segments']))\n");
    }
    qputenv("OPENSTITCH_RAYTRACE_SCRIPT", fake.toUtf8());
    rig.files->exportPhotoRender(url(dir, "photo.png"),
                                 {{QStringLiteral("tiltDeg"), 12},
                                  {QStringLiteral("samples"), 4},
                                  {QStringLiteral("pxPerMm"), 99}, // borné à 40
                                  {QStringLiteral("threadWidthMm"), 0.5},
                                  {QStringLiteral("fabric"), QStringLiteral("10,20,30")},
                                  {QStringLiteral("fabricType"), QStringLiteral("jersey")},
                                  {QStringLiteral("background"), QStringLiteral("1,2,999")}});
    QVERIFY(rig.waitIdle());
    qunsetenv("OPENSTITCH_RAYTRACE_SCRIPT");
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(rig.files->message().contains(QStringLiteral("Rendu photo écrit")));
    QImage png(dir.filePath(QStringLiteral("photo.png")));
    QCOMPARE(png.size(), QSize(1, 1));
}

void StudioFilesTest::documentExportsWriteRealFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    rig.files->exportThreadList(url(dir, "fils.csv"));
    QVERIFY(rig.waitIdle()); // la génération des points tourne sur un worker
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    const auto csv = bytes(dir.filePath(QStringLiteral("fils.csv")));
    QVERIFY(csv.contains("ordre;"));
    QCOMPARE(csv.count('\n'), 3); // en-tête + deux fils
    rig.files->exportDxf(url(dir, "contours"));
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(bytes(dir.filePath(QStringLiteral("contours.dxf"))).contains("SECTION"));
    rig.files->exportSvg(url(dir, "points.svg"));
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(bytes(dir.filePath(QStringLiteral("points.svg"))).contains("<svg"));
    rig.files->exportImage(url(dir, "rendu.png"), 4.0);
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QImage png(dir.filePath(QStringLiteral("rendu.png")));
    QVERIFY(!png.isNull() && png.width() > 10);
    rig.files->exportImage(url(dir, "rendu.jpg"), 4.0, 80);
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(bytes(dir.filePath(QStringLiteral("rendu.jpg"))).startsWith("\xFF\xD8"));
    rig.files->exportImage(url(dir, "mauvais.png"), 99.0); // résolution hors bornes
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("mauvais.png"))));
    rig.files->exportProductionSheet(url(dir, "fiche.pdf"));
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(bytes(dir.filePath(QStringLiteral("fiche.pdf"))).startsWith("%PDF"));
    rig.files->exportPlacementTemplate(url(dir, "gabarit.pdf"),
                                       {{QStringLiteral("paper"), QStringLiteral("A4")}});
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    const auto first = bytes(dir.filePath(QStringLiteral("gabarit.pdf")));
    QVERIFY(first.startsWith("%PDF"));
    rig.files->exportPlacementTemplate(url(dir, "gabarit2.pdf"));
    QVERIFY(rig.waitIdle());
    QCOMPARE(bytes(dir.filePath(QStringLiteral("gabarit2.pdf"))), first); // déterministe
    rig.files->exportPlacementTemplate(url(dir, "mauvais.pdf"),
                                       {{QStringLiteral("paper"), QStringLiteral("B7")}});
    QVERIFY(rig.files->messageIsError());
    // Projet vide : les exports refusent avec un message, sans fichier.
    rig.files->requestNew();
    rig.files->exportThreadList(url(dir, "vide.csv"));
    QVERIFY(rig.waitIdle());
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("vide.csv"))));
    QVERIFY(!rig.studio.dirty());
}

void StudioFilesTest::exportOptionsAreOneUndoStepAndRefusedWhenInvalid() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    const auto before = rig.files->exportOptions();
    QVERIFY(rig.files
                ->setExportOptions({{QStringLiteral("trimThresholdMm"), 6.0},
                                    {QStringLiteral("lockKind"), QStringLiteral("triangle")}})
                .isEmpty());
    QVERIFY(rig.studio.canUndo());
    QCOMPARE(rig.files->exportOptions().value(QStringLiteral("trimThresholdMm")).toDouble(), 6.0);
    QCOMPARE(rig.files->exportOptions().value(QStringLiteral("lockKind")).toString(),
             QStringLiteral("triangle"));
    rig.studio.undo();
    QCOMPARE(rig.files->exportOptions(), before);
    QVERIFY(!rig.files->setExportOptions({{QStringLiteral("trimThresholdMm"), -4.0}}).isEmpty());
    QVERIFY(!rig.files->setExportOptions({{QStringLiteral("lockKind"), QStringLiteral("zz")}})
                 .isEmpty());
    QVERIFY(!rig.studio.canUndo());
    // Les réglages passés à l'export sont appliqués au document avant l'écriture (annulable).
    rig.files->exportMachine(url(dir, "reglages.dst"), {{QStringLiteral("trimThresholdMm"), 5.5}});
    QVERIFY(rig.waitIdle());
    QVERIFY(rig.studio.canUndo());
    QCOMPARE(rig.files->exportOptions().value(QStringLiteral("trimThresholdMm")).toDouble(), 5.5);
    // La vérification tourne sur un worker : le texte arrive après « Vérification… ».
    rig.files->requestExportCheck(QStringLiteral("dst"));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.files->exportCheckText().isEmpty() &&
                                 rig.files->exportCheckText() != QStringLiteral("Vérification…"),
                             10'000);
}

void StudioFilesTest::imageAndVectorImports() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(120, 120, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(200, 30, 40));
        painter.drawEllipse(20, 20, 80, 80);
        painter.setBrush(QColor(20, 40, 200));
        painter.drawRect(45, 45, 30, 30);
    }
    const auto png = dir.filePath(QStringLiteral("logo.png"));
    QVERIFY(image.save(png));
    Rig rig;
    rig.session.replaceProject(studioFixture(), true);
    rig.makeDirty();
    // Physical scale is checked before loading the source.
    rig.files->importImage(png, {{QStringLiteral("heightMm"), 2.0}});
    QVERIFY(rig.files->messageIsError());
    QVERIFY(!rig.files->busy());
    // L'import remplace le document : protégé par la question « Enregistrer ? ».
    auto options = rig.files->imageImportDefaults();
    QVERIFY(options.contains(QStringLiteral("heightMm")));
    QVERIFY(!options.contains(QStringLiteral("maxColors")));
    QVERIFY(!options.contains(QStringLiteral("smoothingPx")));
    options[QStringLiteral("heightMm")] = 40.0;
    rig.files->importImage(png, options);
    QCOMPARE(rig.files->pendingReason(), QStringLiteral("importer"));
    rig.files->resolveGuard(QStringLiteral("discard"));
    QVERIFY(rig.waitIdle());
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    const auto& loaded = rig.session.project();
    QVERIFY(loaded.hasImage());
    QCOMPARE(loaded.original.width, 120);
    QCOMPARE(loaded.original.height, 120);
    QCOMPARE(loaded.mm_per_px.value, 40.0 / 120.0);
    QVERIFY(loaded.ops.empty());
    QVERIFY(!loaded.segmentation.has_value());
    QVERIFY(loaded.vector_objects.empty());
    QVERIFY(loaded.embroidery_objects.empty());
    QVERIFY(!loaded.imported_design.has_value());
    QTRY_VERIFY_WITH_TIMEOUT(!rig.studio.busy(), 15'000);
    QCOMPARE(rig.studio.commandCount(), 0);
    QCOMPARE(rig.studio.title(), QStringLiteral("logo.png"));
    QVERIFY(!rig.studio.hasPath());
    QVERIFY(rig.studio.dirty()); // à enregistrer
    // SVG et DXF s'ajoutent au projet courant, en une étape annulable.
    const auto svgPath = dir.filePath(QStringLiteral("forme.svg"));
    QFile svg(svgPath);
    QVERIFY(svg.open(QIODevice::WriteOnly));
    svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40mm\" height=\"30mm\" "
              "viewBox=\"0 0 40 30\"><rect x=\"5\" y=\"5\" width=\"30\" height=\"20\" "
              "fill=\"#ff0000\"/></svg>");
    svg.close();
    const auto before = rig.session.project().vector_objects.size();
    rig.files->importVector(svgPath);
    QVERIFY2(!rig.files->messageIsError(), qPrintable(rig.files->message()));
    QVERIFY(rig.session.project().vector_objects.size() > before);
    rig.studio.undo();
    QCOMPARE(rig.session.project().vector_objects.size(), before);
    rig.files->importVector(dir.filePath(QStringLiteral("logo.png")));
    QVERIFY(rig.files->messageIsError()); // une image ne s'importe pas comme tracé
}

void StudioFilesTest::autosaveRecoveryAndDiscard() {
    QTemporaryDir state;
    QTemporaryDir dir;
    QVERIFY(state.isValid() && dir.isValid());
    {
        Rig rig(state.path());
        QVERIFY(!rig.files->hasRecovery());
        QVERIFY(!rig.files->autosaveNow()); // document propre : rien à sauvegarder
        rig.session.replaceProject(studioFixture(), true);
        rig.makeDirty();
        QVERIFY(rig.files->autosaveNow());
        // Arrêt anormal : le contrôleur disparaît sans requestQuit().
    }
    {
        Rig rig(state.path());
        QVERIFY(rig.files->hasRecovery());
        QVERIFY(!rig.files->autosaveNow()); // ne pas écraser avant que l'utilisateur ait tranché
        QSignalSpy changed(rig.files.get(), &FileController::recoveryChanged);
        rig.files->recoverAutosave();
        QVERIFY(!rig.files->hasRecovery());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(rig.session.project().embroidery_objects.size(), std::size_t{2});
        QVERIFY(rig.studio.dirty()); // récupéré : reste à enregistrer
        QVERIFY(!rig.studio.hasPath());
        // Enregistrer efface la sauvegarde : plus rien à proposer au lancement suivant.
        QVERIFY(rig.studio.saveProjectAs(url(dir, "recupere.osp")));
    }
    {
        Rig rig(state.path());
        QVERIFY(!rig.files->hasRecovery());
        rig.session.replaceProject(studioFixture(), true);
        rig.makeDirty();
        QVERIFY(rig.files->autosaveNow());
    }
    {
        Rig rig(state.path());
        QVERIFY(rig.files->hasRecovery());
        rig.files->discardRecovery(); // « Ignorer »
        QVERIFY(!rig.files->hasRecovery());
    }
    Rig last(state.path());
    QVERIFY(!last.files->hasRecovery());
    // Quitter proprement ne laisse pas de sauvegarde.
    last.session.replaceProject(studioFixture(), true);
    last.makeDirty();
    QVERIFY(last.files->autosaveNow());
    QVERIFY(!last.files->requestQuit());
    last.files->resolveGuard(QStringLiteral("discard"));
    Rig after(state.path());
    QVERIFY(!after.files->hasRecovery());
}

void StudioFilesTest::hoopMachineAndUserHoops() {
    QTemporaryDir state;
    QVERIFY(state.isValid());
    {
        Rig rig(state.path());
        rig.session.replaceProject(studioFixture(), true);
        QVERIFY(!rig.files->hoops().isEmpty());
        QVERIFY(!rig.files->machines().isEmpty());
        const auto hoop = rig.files->hoops().front().toMap();
        QVERIFY(rig.files->applyHoop(hoop.value(QStringLiteral("id")).toString(), 3.0).isEmpty());
        QCOMPARE(rig.files->currentHoop().value(QStringLiteral("id")).toString(),
                 hoop.value(QStringLiteral("id")).toString());
        QVERIFY(rig.studio.canUndo());
        QVERIFY(!rig.files->applyHoop(QStringLiteral("inconnu"), 0.0).isEmpty());
        QVERIFY(!rig.files->applyCustomHoop(5.0, 5.0, 0.0).isEmpty());
        QVERIFY(rig.files->applyCustomHoop(120.0, 80.0, 2.0).isEmpty());
        QVERIFY(rig.files->currentHoop().value(QStringLiteral("id")).toString().isEmpty());
        const auto machine = rig.files->machines().front().toMap();
        QVERIFY(rig.files->applyMachine(machine.value(QStringLiteral("id")).toString()).isEmpty());
        QVERIFY(!rig.files->applyMachine(QStringLiteral("inconnue")).isEmpty());
        QVERIFY(rig.files->saveCurrentHoopAs(QStringLiteral("Mon cadre")).isEmpty());
        QCOMPARE(rig.files->userHoops().size(), 1);
        QVERIFY(!rig.files->saveCurrentHoopAs(QStringLiteral("a;b")).isEmpty());
    }
    Rig rig(state.path()); // « Mes cadres » survit au redémarrage
    QCOMPARE(rig.files->userHoops().size(), 1);
    rig.session.replaceProject(studioFixture(), true);
    QVERIFY(rig.files->applyUserHoop(QStringLiteral("Mon cadre")).isEmpty());
    QVERIFY(rig.files->removeUserHoop(QStringLiteral("Mon cadre")).isEmpty());
    QVERIFY(rig.files->userHoops().isEmpty());
}

// Réglage rapide du cadre (en-tête, clic droit des canevas) : forme, dimensions, rayon, marge en
// une commande annulable ; « Mes cadres » garde la forme ; le cadre courant est relu après annuler.
void StudioFilesTest::hoopQuickSettingsAreOneUndoStepEach() {
    QTemporaryDir state;
    QVERIFY(state.isValid());
    Rig rig(state.path(), true);
    QVERIFY2(rig.window, qPrintable(rig.warnings.join(QStringLiteral(" | "))));
    rig.session.replaceProject(studioFixture(), true);

    // API : forme réelle, refus clair d'un cadre invalide, déjà-celui-là sans erreur affichée.
    QVERIFY(rig.files->applyHoopSpec(QStringLiteral("ellipse"), 150.0, 120.0, 0.0, 4.0).isEmpty());
    auto hoop = rig.files->currentHoop();
    QCOMPARE(hoop.value(QStringLiteral("shape")).toString(), QStringLiteral("ellipse"));
    QCOMPARE(hoop.value(QStringLiteral("widthMm")).toDouble(), 150.0);
    QCOMPARE(hoop.value(QStringLiteral("heightMm")).toDouble(), 120.0);
    QCOMPARE(hoop.value(QStringLiteral("marginMm")).toDouble(), 4.0);
    QCOMPARE(rig.session.project().canvas.shape, document::HoopShape::Ellipse);
    QVERIFY(!rig.files->applyHoopSpec(QStringLiteral("ellipse"), 150.0, 120.0, 0.0, 4.0).isEmpty());
    QVERIFY(!rig.files->messageIsError());
    QVERIFY(!rig.files->applyHoopSpec(QStringLiteral("rectangle"), 5.0, 120.0, 0.0, 0.0).isEmpty());
    QCOMPARE(rig.session.project().canvas.width.value, 150'000);
    QVERIFY(rig.files->saveCurrentHoopAs(QStringLiteral("Ovale atelier")).isEmpty());
    const auto mine = rig.files->userHoops().front().toMap();
    QCOMPARE(mine.value(QStringLiteral("shape")).toString(), QStringLiteral("ellipse"));
    QCOMPARE(rig.files->currentHoop().value(QStringLiteral("label")).toString(),
             QStringLiteral("Ovale atelier"));
    QVERIFY(
        !rig.files->hoops().front().toMap().value(QStringLiteral("shape")).toString().isEmpty());

    // Vraie scène QML : le panneau s'ouvre depuis l'hôte des espaces, applique en une étape.
    auto* host = rig.window->findChild<QObject*>(QStringLiteral("spaceHost"));
    QVERIFY(host);
    QVERIFY(rig.window->findChild<QObject*>(QStringLiteral("hoopButton")));
    QVERIFY(QMetaObject::invokeMethod(host, "openHoop", Q_ARG(QVariant, QVariant()),
                                      Q_ARG(QVariant, 0), Q_ARG(QVariant, 0)));
    auto* pop = rig.window->findChild<QObject*>(QStringLiteral("hoopPopover"));
    QVERIFY(pop);
    QTRY_VERIFY(pop->property("opened").toBool());
    auto* width = rig.window->findChild<QObject*>(QStringLiteral("hoopWidthSpin"));
    auto* radius = rig.window->findChild<QObject*>(QStringLiteral("hoopRadiusSpin"));
    QVERIFY(width && radius);
    QCOMPARE(width->property("value").toInt(), 150); // relu du document à l'ouverture
    width->setProperty("value", 200);
    pop->setProperty("shape", QStringLiteral("rounded"));
    radius->setProperty("value", 15);
    QVERIFY(QMetaObject::invokeMethod(pop, "applyNow"));
    QVERIFY(rig.studio.canUndo());
    QCOMPARE(rig.session.project().canvas.width.value, 200'000);
    QCOMPARE(rig.session.project().canvas.shape, document::HoopShape::RoundedRectangle);
    QCOMPARE(rig.session.project().canvas.corner_radius.value, 15'000);

    rig.studio.undo(); // le panneau ouvert se relit sur le document
    QCOMPARE(rig.session.project().canvas.width.value, 150'000);
    QTRY_COMPARE(width->property("value").toInt(), 150);
    QCOMPARE(pop->property("shape").toString(), QStringLiteral("ellipse"));
}

QTEST_MAIN(StudioFilesTest)
#include "test_studio_files.moc"
