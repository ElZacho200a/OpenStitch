// SPDX-License-Identifier: Apache-2.0
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

#include <filesystem>

#include "action_controller.hpp"
#include "analysis_controller.hpp"
#include "digitize_pipeline.hpp"
#include "edit_controller.hpp"
#include "file_controller.hpp"
#include "gpu_canvas.hpp"
#include "image_controller.hpp"
#include "image_space_controller.hpp"
#include "inspector_controller.hpp"
#include "region_controller.hpp"
#include "studio_controller.hpp"
#include "thread_controller.hpp"
#include "view_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

// Vraie image PNG : quatre aplats de couleur, bruit déterministe léger, passée par la chaîne
// réelle d'import/numérisation du moteur.
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

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    QTemporaryDir dir;
    ImageController images{studio};
    RegionController regions{studio, images};
    Rig() {
        session.replaceProject(digitizedProject(writeTestImage(dir.path())), true);
        QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && images.imageWidth() > 0, 20'000);
    }
};

} // namespace

class StudioImageTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void prepIsOneUndoStepAndPreviewLeavesDocumentIntact();
    void prepRefusalsAndBeforeAfterImages();
    void imageSpaceDrivesThePrepJourney();
};

void StudioImageTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
    qmlRegisterType<GpuCanvas>("OpenStitch.Studio", 1, 0, "GpuCanvas");
    qmlRegisterUncreatableType<StudioController>("OpenStitch.Studio", 1, 0, "StudioController",
                                                 QStringLiteral("Provided by the application"));
}

void StudioImageTest::prepIsOneUndoStepAndPreviewLeavesDocumentIntact() {
    Rig rig;
    QVERIFY(rig.images.hasImage());
    const auto revision = rig.session.revision();
    const auto opsBefore = rig.session.project().ops.size();
    // Aperçu : le document ne bouge pas, l'image « preview » existe ensuite.
    rig.images.previewPrep(QStringLiteral("grayscale"), {});
    QTRY_VERIFY_WITH_TIMEOUT(rig.images.prepPreviewActive(), 20'000);
    QVERIFY(!rig.images.images()->get(QStringLiteral("preview")).isNull());
    QCOMPARE(rig.session.revision(), revision);
    QCOMPARE(rig.session.project().ops.size(), opsBefore);
    rig.images.clearPrepPreview();
    QVERIFY(!rig.images.prepPreviewActive());
    // Appliquer : UNE étape d'annulation, annulable.
    const auto undoDepth = rig.session.undoNames().size();
    QCOMPARE(rig.images.applyPrep(QStringLiteral("brightness_contrast"),
                                  {{"brightness", 20}, {"contrast", 10}}),
             QString());
    QCOMPARE(rig.session.project().ops.size(), opsBefore + 1);
    QCOMPARE(rig.session.undoNames().size(), undoDepth + 1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.images.ops().size(), static_cast<int>(opsBefore + 1), 5'000);
    rig.session.undo();
    QCOMPARE(rig.session.project().ops.size(), opsBefore);
    // Les autres traitements ajoutent chacun une opération.
    for (const char* kind : {"median", "bilateral", "flatten", "sharpen", "clahe", "quantize"}) {
        QCOMPARE(rig.images.applyPrep(QString::fromLatin1(kind),
                                      {{"strength", 1}, {"amount", 1}, {"clip", 2}, {"colors", 6}}),
                 QString());
    }
    QCOMPARE(rig.session.project().ops.size(), opsBefore + 6);
}

void StudioImageTest::prepRefusalsAndBeforeAfterImages() {
    Rig rig;
    const auto opsBefore = rig.session.project().ops.size();
    QVERIFY(!rig.images.applyPrep(QStringLiteral("brightness_contrast"), {}).isEmpty()); // 0 / 0
    QVERIFY(!rig.images.applyPrep(QStringLiteral("brightness_contrast"), {{"brightness", 500}})
                 .isEmpty());
    QVERIFY(!rig.images.applyPrep(QStringLiteral("median"), {{"strength", 9}}).isEmpty());
    QVERIFY(
        !rig.images.applyPrep(QStringLiteral("crop"), {{"x", 0}, {"y", 0}, {"w", 9999}, {"h", 50}})
             .isEmpty());
    QVERIFY(!rig.images.applyPrep(QStringLiteral("nope"), {}).isEmpty());
    QVERIFY(rig.images.messageIsError());
    QCOMPARE(rig.session.project().ops.size(), opsBefore);
    // Avant / après réels : la rotation échange les dimensions de l'image de travail.
    const int w = rig.images.imageWidth();
    const int h = rig.images.imageHeight();
    QCOMPARE(rig.images.applyPrep(QStringLiteral("rotate_right"), {}), QString());
    QTRY_COMPARE_WITH_TIMEOUT(rig.images.imageWidth(), h, 10'000);
    QCOMPARE(rig.images.imageHeight(), w);
    const auto after = rig.images.images()->get(QStringLiteral("after"));
    const auto before = rig.images.images()->get(QStringLiteral("before"));
    QCOMPARE(after.width(), h);
    QCOMPARE(before.width(), w); // l'original reste intact
    QCOMPARE(
        rig.images.applyPrep(QStringLiteral("crop"), {{"x", 10}, {"y", 10}, {"w", 40}, {"h", 30}}),
        QString());
    QTRY_COMPARE_WITH_TIMEOUT(rig.images.imageWidth(), 40, 10'000);
    QCOMPARE(rig.images.imageHeight(), 30);
}

namespace {
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
QQuickItem* named(QQuickWindow* window, const char* name) {
    return findVisual(window->contentItem(), QString::fromLatin1(name));
}
bool press(QQuickWindow* window, const char* name) {
    auto* item = named(window, name);
    return item && item->isEnabled() && QMetaObject::invokeMethod(item, "click");
}
} // namespace

void StudioImageTest::imageSpaceDrivesThePrepJourney() {
    DocumentSession session;
    StudioController studio(session);
    ActionController actions(studio);
    EditController editor(studio);
    QTemporaryDir dir;
    const auto cfg = std::filesystem::path(dir.path().toStdWString());
    FileController files(studio, &actions, dir.path());
    InspectorController inspector(studio);
    ViewController view(studio, cfg);
    ThreadController threads(studio, cfg);
    AnalysisController analysis(studio, view, cfg);
    ImageController images(studio);
    RegionController regions(studio, images);
    ImageSpaceController imageSpace(studio, images);
    QQmlApplicationEngine engine;
    QStringList warnings;
    connect(&engine, &QQmlApplicationEngine::warnings, this, [&](const QList<QQmlError>& errors) {
        for (const auto& e : errors) {
            warnings.push_back(e.toString());
        }
    });
    engine.addImageProvider(QStringLiteral("prepare"), images.createProvider());
    auto* ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("studio"), &studio);
    ctx->setContextProperty(QStringLiteral("actions"), &actions);
    ctx->setContextProperty(QStringLiteral("editor"), &editor);
    ctx->setContextProperty(QStringLiteral("files"), &files);
    ctx->setContextProperty(QStringLiteral("inspector"), &inspector);
    ctx->setContextProperty(QStringLiteral("view"), &view);
    ctx->setContextProperty(QStringLiteral("threads"), &threads);
    ctx->setContextProperty(QStringLiteral("analysis"), &analysis);
    ctx->setContextProperty(QStringLiteral("imageTools"), &images);
    ctx->setContextProperty(QStringLiteral("imageSpace"), &imageSpace);
    ctx->setContextProperty(QStringLiteral("regionTools"), &regions);
    engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    session.replaceProject(digitizedProject(writeTestImage(dir.path())), true);
    QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && images.imageWidth() > 0, 20'000);

    // Le préparateur vit dans l'espace Image (un seul espace est instancié à la fois).
    auto* workspace = window->findChild<WorkspaceController*>();
    QVERIFY(workspace);
    workspace->setCurrent(QStringLiteral("image"));
    QTRY_VERIFY_WITH_TIMEOUT(named(window, "imagePrepSection") != nullptr, 5'000);

    // Image : aperçu puis application (niveaux de gris = entrée 1 du catalogue).
    auto* prep = named(window, "imagePrepSection");
    QVERIFY(prep);
    QVERIFY(press(window, "prepApplyButton")); // luminosité 0 / contraste 0 : refus lisible
    QVERIFY(images.messageIsError());
    prep->setProperty("currentIndex", 1);
    const auto ops = session.project().ops.size();
    QVERIFY(press(window, "prepPreviewButton"));
    QTRY_VERIFY_WITH_TIMEOUT(images.prepPreviewActive(), 20'000);
    QCOMPARE(session.project().ops.size(), ops);
    QVERIFY(press(window, "prepCancelButton"));
    QVERIFY(!images.prepPreviewActive());
    QVERIFY(press(window, "prepApplyButton"));
    QCOMPARE(session.project().ops.size(), ops + 1);
    QTRY_COMPARE_WITH_TIMEOUT(images.ops().size(), static_cast<int>(ops + 1), 5'000);

    // F07-04: changing a slider after a completed preview invalidates the
    // old snapshot. It must never silently remain visible for new parameters.
    QVERIFY(prep->setProperty("currentIndex", 0));
    QVERIFY(QMetaObject::invokeMethod(
        prep, "setValue", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant(QStringLiteral("brightness"))),
        Q_ARG(QVariant, QVariant(12.0))));
    QVERIFY(press(window, "prepPreviewButton"));
    QTRY_VERIFY_WITH_TIMEOUT(images.prepPreviewActive(), 20'000);
    const auto beforeParameterChange = session.project().ops.size();
    QVERIFY(QMetaObject::invokeMethod(
        prep, "setValue", Qt::DirectConnection,
        Q_ARG(QVariant, QVariant(QStringLiteral("brightness"))),
        Q_ARG(QVariant, QVariant(22.0))));
    QTRY_VERIFY(!images.prepPreviewActive());
    QCOMPARE(session.project().ops.size(), beforeParameterChange);

    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

QTEST_MAIN(StudioImageTest)
#include "test_studio_image.moc"
