// SPDX-License-Identifier: Apache-2.0
// Espace « Image » du Studio : le vrai Main.qml, la vraie image (PNG passé par run_digitize), le
// vrai WorkspaceController. Parcours : l'espace affiche l'image, avant/après, rideau, pile
// d'opérations (ajout, suppression, réordonnancement, annulation), recadrage, état publié.
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <filesystem>

#include "action_controller.hpp"
#include "digitize_pipeline.hpp"
#include "edit_controller.hpp"
#include "gpu_canvas.hpp"
#include "image_controller.hpp"
#include "image_space_controller.hpp"
#include "openstitch/stitch_generation/overrides.hpp"
#include "studio_controller.hpp"
#include "workspace_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

// Quatre aplats (haut-gauche rouge, haut-droite vert, bas-gauche bleu, bas-droite jaune) + bruit.
QString writeImage(const QString& dir) {
    QImage img(160, 120, QImage::Format_RGB32);
    QPainter painter(&img);
    painter.fillRect(0, 0, 80, 60, QColor(200, 30, 40));
    painter.fillRect(80, 0, 80, 60, QColor(30, 160, 60));
    painter.fillRect(0, 60, 80, 60, QColor(40, 60, 200));
    painter.fillRect(80, 60, 80, 60, QColor(240, 220, 40));
    painter.end();
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

QQuickItem* named(QQuickWindow* window, const QString& name) {
    return findVisual(window->contentItem(), name);
}

bool press(QQuickWindow* window, const QString& name) {
    auto* item = named(window, name);
    return item && item->isEnabled() && QMetaObject::invokeMethod(item, "click");
}

} // namespace

class StudioSpaceImageTest : public QObject {
    Q_OBJECT
private:
    QColor pixelAt(QQuickWindow* window, QPointF screenInCanvas, QQuickItem* canvas) {
        const QImage grab = window->grabWindow();
        const QPointF p = canvas->mapToScene(screenInCanvas) * grab.devicePixelRatio();
        return grab.pixelColor(static_cast<int>(p.x()), static_cast<int>(p.y()));
    }
    static bool nearColor(const QColor& a, const QColor& b, int tolerance = 14) {
        return std::abs(a.red() - b.red()) <= tolerance &&
               std::abs(a.green() - b.green()) <= tolerance &&
               std::abs(a.blue() - b.blue()) <= tolerance;
    }
    static bool isGray(const QColor& c, int tolerance = 6) {
        return std::abs(c.red() - c.green()) <= tolerance &&
               std::abs(c.green() - c.blue()) <= tolerance;
    }

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

    void geometryOfTheLayerIsExact() {
        auto data = std::make_shared<SourceImageData>();
        data->after = QImage(100, 50, QImage::Format_RGB32);
        data->before = QImage(100, 50, QImage::Format_RGB32);
        data->mmPerPx = 0.5;
        SourceImageView view;
        view.data = data;
        const auto rect = sourceImageRectMm(data->after, 0.5);
        QCOMPARE(rect, QRectF(-25.0, -12.5, 50.0, 25.0)); // centrée sur l'origine
        QCOMPARE(sourceImagePieces(view).size(), std::size_t{1});
        view.mode = SourceCompare::Curtain;
        view.curtain = 0.25;
        const auto pieces = sourceImagePieces(view);
        QCOMPARE(pieces.size(), std::size_t{2});
        QVERIFY(pieces[0].before);
        QCOMPARE(pieces[0].destMm.width(), 12.5);
        QCOMPARE(pieces[0].srcPx.width(), 25.0);
        QVERIFY(!pieces[1].before);
        QCOMPARE(pieces[1].destMm.left(), -12.5);
        QCOMPARE(pieces[1].srcPx.left(), 25.0);
        view.mode = SourceCompare::Before;
        QCOMPARE(sourceImagePieces(view).size(), std::size_t{1});
    }

    void imageSpaceJourneyOnRealMainQml() {
        DocumentSession session;
        StudioController studio(session);
        ActionController actions(studio);
        EditController editor(studio);
        ImageController images(studio);
        ImageSpaceController imageSpace(studio, images);
        QTemporaryDir dir;
        QQmlApplicationEngine engine;
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this,
                [&](const QList<QQmlError>& errors) {
                    for (const auto& e : errors) {
                        warnings.push_back(e.toString());
                    }
                });
        engine.addImageProvider(QStringLiteral("prepare"), images.createProvider());
        auto* ctx = engine.rootContext();
        ctx->setContextProperty(QStringLiteral("studio"), &studio);
        ctx->setContextProperty(QStringLiteral("actions"), &actions);
        ctx->setContextProperty(QStringLiteral("editor"), &editor);
        ctx->setContextProperty(QStringLiteral("imageTools"), &images);
        ctx->setContextProperty(QStringLiteral("imageSpace"), &imageSpace);
        session.replaceProject(digitizedProject(writeImage(dir.path())), true);
        engine.load(QUrl(QStringLiteral("qrc:/studio/qml/Main.qml")));
        QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window);
        auto* workspace = window->findChild<WorkspaceController*>();
        QVERIFY(workspace);
        QTRY_VERIFY_WITH_TIMEOUT(!studio.busy() && images.imageWidth() > 0, 20'000);
        QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5'000);

        // --- 1. l'espace affiche l'image de travail (couche sourceImage) --------------------
        workspace->setCurrent(QStringLiteral("image"));
        QTRY_VERIFY_WITH_TIMEOUT(named(window, QStringLiteral("imageCanvas")) != nullptr, 5'000);
        auto* canvas = qobject_cast<GpuCanvas*>(named(window, QStringLiteral("imageCanvas")));
        QVERIFY(canvas);
        QVERIFY(canvas->layers().value("sourceImage").toBool());
        QVERIFY(!canvas->layers().value("stitches").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(canvas->sourceImage().data != nullptr, 5'000);
        QCOMPARE(canvas->sourceImage().data->after.width(), images.imageWidth());
        QCOMPARE(canvas->sourceImage().data->after.height(), images.imageHeight());
        QVERIFY(imageSpace.layerRevision() > 0);
        // Les événements d'état/sélection, ou deux notifications pour la même image, ne
        // doivent pas déclencher de nouveau redimensionnement ni rafraîchir les bindings.
        const int originalLayerRevision = imageSpace.layerRevision();
        QSignalSpy imageStageChanged(&imageSpace, &ImageSpaceController::changed);
        QVERIFY(QMetaObject::invokeMethod(&images, "imagesChanged", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&images, "previewChanged", Qt::DirectConnection));
        QCOMPARE(imageSpace.layerRevision(), originalLayerRevision);
        QVERIFY(QMetaObject::invokeMethod(&studio, "changed", Qt::DirectConnection));
        QCOMPARE(imageStageChanged.count(), 0);
        QVERIFY(!named(window, QStringLiteral("imageEmptyState"))->isVisible());
        QVERIFY(named(window, QStringLiteral("imageSidePanel"))->isVisible());
        QVERIFY(named(window, QStringLiteral("imagePrepSection")));
        // Aucun reste de segmentation / régions / IA dans l'espace Image.
        QVERIFY(!named(window, QStringLiteral("preparePanel")));
        QVERIFY(!named(window, QStringLiteral("segmentationSection")));

        // Pixels RÉELS rendus par le backend logiciel : le bas-gauche de l'image est bleu.
        const auto w = images.imageWidth();
        const auto h = images.imageHeight();
        const QPointF blue = imageSpace.imageToScreen(w * 0.12, h * 0.85);
        const QPointF yellow = imageSpace.imageToScreen(w * 0.88, h * 0.85);
        QTRY_VERIFY_WITH_TIMEOUT(nearColor(pixelAt(window, blue, canvas), QColor(40, 60, 200)),
                                 5'000);
        // Pan / zoom ne reconstruisent aucune donnée d'image.
        const auto dataRevision = canvas->sourceImage().dataRevision;
        const int layerRevision = imageSpace.layerRevision();
        canvas->panBy(15, 10);
        canvas->zoomAt(1.2, 100, 100);
        QCOMPARE(canvas->sourceImage().dataRevision, dataRevision);
        QCOMPARE(imageSpace.layerRevision(), layerRevision);
        press(window, QStringLiteral("imageFit"));

        // --- 2. une opération d'image : aperçu non destructif puis application ----------------
        const auto opsBase = session.project().ops;
        const auto sequenceBefore = stitch_generation::effective_sequence(session.project());
        QVERIFY(sequenceBefore.has_value());
        QVERIFY(session.project().segmentation.has_value());
        QCOMPARE(workspace->space(QStringLiteral("image")).value("state").toString(),
                 QStringLiteral("upToDate"));
        const auto undoBase = session.undoNames().size();
        QCOMPARE(images.applyPrep(QStringLiteral("grayscale"), {}), QString());
        QCOMPARE(images.applyPrep(QStringLiteral("flip_h"), {}), QString());
        QCOMPARE(session.undoNames().size(), undoBase + 2);
        const int base = static_cast<int>(opsBase.size());
        QTRY_COMPARE_WITH_TIMEOUT(imageSpace.opStack().size(), base + 2, 5'000);
        // Le traitement invalide la segmentation : l'étape Image publie « à refaire ».
        QVERIFY(imageSpace.imageChanged());
        QTRY_COMPARE_WITH_TIMEOUT(
            workspace->space(QStringLiteral("image")).value("state").toString(),
            QStringLiteral("stale"), 5'000);
        QVERIFY(workspace->space(QStringLiteral("image"))
                    .value("detail")
                    .toString()
                    .contains(QStringLiteral("segmentation")));
        QVERIFY(named(window, QStringLiteral("opRow_%1").arg(base + 1)));

        // --- 3. avant / après sur le canevas ----------------------------------------------------
        // Après : l'image traitée (niveaux de gris). Avant : l'original, intact, en couleurs.
        QTRY_VERIFY_WITH_TIMEOUT(
            canvas->sourceImage().dataRevision != dataRevision &&
                isGray(pixelAt(window, imageSpace.imageToScreen(w * 0.12, h * 0.15), canvas)),
            10'000);
        QVERIFY(press(window, QStringLiteral("compareMode_1")));
        QCOMPARE(imageSpace.compareMode(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(
            nearColor(pixelAt(window, imageSpace.imageToScreen(w * 0.12, h * 0.85), canvas),
                      QColor(40, 60, 200)),
            5'000);
        // Rideau : « avant » à gauche de la ligne, « après » à droite.
        QVERIFY(press(window, QStringLiteral("compareMode_2")));
        QCOMPARE(imageSpace.compareMode(), 2);
        auto* curtainItem = named(window, QStringLiteral("compareCurtain"));
        QVERIFY(curtainItem && curtainItem->isVisible());
        imageSpace.setCurtain(0.5);
        QTRY_VERIFY_WITH_TIMEOUT(
            nearColor(pixelAt(window, imageSpace.imageToScreen(w * 0.12, h * 0.85), canvas),
                      QColor(40, 60, 200)) &&
                isGray(pixelAt(window, imageSpace.imageToScreen(w * 0.88, h * 0.85), canvas)),
            5'000);
        // Geste réel : on saisit la poignée (MouseArea) et on la tire vers la droite.
        auto* grip = named(window, QStringLiteral("curtainGrip"));
        QVERIFY(grip);
        const QPointF gripCenter = grip->mapToScene(QPointF(grip->width() / 2, grip->height() / 2));
        const QPointF target = imageSpace.imageToScreen(w * 0.75, h * 0.5);
        const QPointF targetScene = canvas->mapToScene(target);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, gripCenter.toPoint());
        QTest::mouseMove(window, ((gripCenter + targetScene) / 2).toPoint());
        QTest::mouseMove(window, QPoint(static_cast<int>(targetScene.x()), gripCenter.y()));
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
                            QPoint(static_cast<int>(targetScene.x()), gripCenter.y()));
        QVERIFY2(std::abs(imageSpace.curtain() - 0.75) < 0.05,
                 qPrintable(QString::number(imageSpace.curtain())));
        QVERIFY(press(window, QStringLiteral("compareMode_0")));
        QCOMPARE(imageSpace.compareMode(), 0);

        // --- 4. pile d'opérations : supprimer / réordonner = UNE étape d'annulation -----------
        const auto depth = session.undoNames().size();
        QVERIFY(press(window, QStringLiteral("opDown_%1").arg(base)));
        QCOMPARE(session.undoNames().size(), depth + 1);
        QVERIFY(std::holds_alternative<image::FlipOp>(session.project().ops[base]));
        QVERIFY(std::holds_alternative<image::GrayscaleOp>(session.project().ops[base + 1]));
        QVERIFY(session.undo());
        QVERIFY(std::holds_alternative<image::GrayscaleOp>(session.project().ops[base]));
        QTRY_VERIFY_WITH_TIMEOUT(named(window, QStringLiteral("opDelete_%1").arg(base)) != nullptr,
                                 5'000);
        QVERIFY(press(window, QStringLiteral("opDelete_%1").arg(base)));
        QCOMPARE(session.project().ops.size(), opsBase.size() + 1);
        QVERIFY(std::holds_alternative<image::FlipOp>(session.project().ops[base]));
        QCOMPARE(session.undoNames().size(), depth + 1);
        QVERIFY(session.undo()); // annule la suppression : la pile est revenue
        QCOMPARE(session.project().ops.size(), opsBase.size() + 2);
        // Tout annuler : document et séquence de points strictement identiques à l'origine.
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QCOMPARE(session.project().ops == opsBase, true);
        QVERIFY(session.project().segmentation.has_value());
        const auto sequenceAfter = stitch_generation::effective_sequence(session.project());
        QVERIFY(sequenceAfter.has_value());
        QVERIFY(sequenceAfter->commands == sequenceBefore->commands);
        QVERIFY(!imageSpace.imageChanged());
        QTRY_COMPARE_WITH_TIMEOUT(
            workspace->space(QStringLiteral("image")).value("state").toString(),
            QStringLiteral("upToDate"), 5'000);

        // --- 5. recadrage au canevas ------------------------------------------------------------
        QVERIFY(press(window, QStringLiteral("imageCropTool")));
        QVERIFY(imageSpace.cropActive());
        auto* cropOverlay = named(window, QStringLiteral("cropOverlay"));
        QVERIFY(cropOverlay && cropOverlay->isVisible());
        // Poignée sud-est tirée au pixel (100, 80) : le bord droit et le bord bas suivent.
        const QPointF se = imageSpace.imageToScreen(100, 80);
        QVERIFY(QMetaObject::invokeMethod(cropOverlay, "moveEdges", Q_ARG(QVariant, se.x()),
                                          Q_ARG(QVariant, se.y()), Q_ARG(QVariant, false),
                                          Q_ARG(QVariant, true), Q_ARG(QVariant, false),
                                          Q_ARG(QVariant, true)));
        QVERIFY(std::abs(imageSpace.cropX() + imageSpace.cropW() - 100) <= 1);
        QVERIFY(std::abs(imageSpace.cropY() + imageSpace.cropH() - 80) <= 1);
        imageSpace.setCrop(10, 10, 40, 30);
        const auto cropDepth = session.undoNames().size();
        QVERIFY(press(window, QStringLiteral("cropApply")));
        QCOMPARE(session.undoNames().size(), cropDepth + 1);
        QVERIFY(!imageSpace.cropActive());
        QTRY_COMPARE_WITH_TIMEOUT(images.imageWidth(), 40, 10'000);
        QCOMPARE(images.imageHeight(), 30);
        QTRY_COMPARE_WITH_TIMEOUT(canvas->sourceImage().data->after.width(), 40, 10'000);
        QVERIFY(session.undo());
        QTRY_COMPARE_WITH_TIMEOUT(images.imageWidth(), w, 10'000);
        // Échap abandonne le cadre sans toucher au document.
        QVERIFY(press(window, QStringLiteral("imageCropTool")));
        QVERIFY(imageSpace.cropActive());
        QVERIFY(press(window, QStringLiteral("imageCropTool")));
        QVERIFY(!imageSpace.cropActive());
        QCOMPARE(session.project().ops == opsBase, true);

        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void removingAnOpThatABackingCropNeedsIsRefused() {
        DocumentSession session;
        StudioController studio(session);
        ImageController images(studio);
        ImageSpaceController imageSpace(studio, images);
        QTemporaryDir dir;
        session.replaceProject(digitizedProject(writeImage(dir.path())), true);
        QTRY_VERIFY_WITH_TIMEOUT(images.imageWidth() > 0, 20'000);
        const auto opsBase = session.project().ops.size();
        QCOMPARE(images.applyPrep(QStringLiteral("rotate_right"), {}), QString());
        QCOMPARE(
            images.applyPrep(QStringLiteral("crop"), {{"x", 5}, {"y", 100}, {"w", 100}, {"h", 50}}),
            QString());
        // Retirer la rotation ferait sortir le recadrage de l'image : refus lisible, rien ne
        // change.
        QVERIFY(!imageSpace.removeOp(static_cast<int>(opsBase)).isEmpty());
        QCOMPARE(session.project().ops.size(), opsBase + 2);
        QVERIFY(!imageSpace.moveOp(static_cast<int>(opsBase), 1).isEmpty());
        QCOMPARE(session.project().ops.size(), opsBase + 2);
    }
};

QTEST_MAIN(StudioSpaceImageTest)
#include "test_studio_space_image.moc"
