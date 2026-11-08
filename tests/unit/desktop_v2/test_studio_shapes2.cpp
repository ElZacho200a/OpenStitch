// SPDX-License-Identifier: Apache-2.0
// CX04 : clics QTest dans la galerie des formes de la V1.
// L'insertion des formes et l'annulation restent couvertes par test_studio_tools.cpp.
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include "studio_controller.hpp"
#include "tool_controller.hpp"

using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

class StudioShapes2Test : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void presetButtonsSelectRealToolsWithoutMutatingProject();
};

void StudioShapes2Test::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
}

void StudioShapes2Test::presetButtonsSelectRealToolsWithoutMutatingProject() {
    DocumentSession session;
    StudioController studio{session};
    ToolController tools{studio};
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("tools"), &tools);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/studio/qml/ShapeGallery.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    auto* gallery = qobject_cast<QQuickItem*>(component.create());
    QVERIFY2(gallery, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(720, 280);
    gallery->setParentItem(window.contentItem());
    gallery->setWidth(680);
    gallery->setHeight(260);
    window.show();

    const auto initialRevision = session.revision();
    const auto initialUndo = session.undoNames().size();
    const auto click = [&](const char* id) {
        auto* button = gallery->findChild<QQuickItem*>(
            QStringLiteral("shapePreset_") + QString::fromLatin1(id));
        if (!button || button->width() <= 0 || button->height() <= 0) {
            return false;
        }
        QTest::mouseClick(&window, Qt::LeftButton, {},
                          button->mapToScene(QPointF(button->width() / 2,
                                                     button->height() / 2)).toPoint());
        return true;
    };

    QTRY_VERIFY(click("star"));
    QCOMPARE(tools.activeTool(), QStringLiteral("draw.polygon-regular"));
    QCOMPARE(tools.sides(), 5);
    QVERIFY(tools.star());

    QVERIFY(click("triangle"));
    QCOMPARE(tools.activeTool(), QStringLiteral("draw.polygon-regular"));
    QCOMPARE(tools.sides(), 3);
    QVERIFY(!tools.star());

    QVERIFY(click("hexagon"));
    QCOMPARE(tools.sides(), 6);
    QVERIFY(!tools.star());

    QVERIFY(click("rectangle"));
    QCOMPARE(tools.activeTool(), QStringLiteral("draw.rectangle"));

    QVERIFY(click("ellipse"));
    QCOMPARE(tools.activeTool(), QStringLiteral("draw.ellipse"));

    QVERIFY(click("polygon"));
    QCOMPARE(tools.activeTool(), QStringLiteral("draw.polygon"));

    QCOMPARE(session.revision(), initialRevision);
    QCOMPARE(session.undoNames().size(), initialUndo);
}

QTEST_MAIN(StudioShapes2Test)
#include "test_studio_shapes2.moc"
