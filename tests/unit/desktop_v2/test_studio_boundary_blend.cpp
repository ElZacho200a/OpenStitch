// SPDX-License-Identifier: Apache-2.0
// Fondu à la frontière dans Studio (lot L3, docs/studio-fondu-frontiere-spec.md §8) : vrai
// moteur, QTest headless. Création depuis deux zones sélectionnées, réédition, retrait, refus
// lisibles sans mutation, un pas d'annulation par action, panneau hébergé par l'inspecteur.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "boundary_blend_controller.hpp"
#include "openstitch/project_ops/factory.hpp"
#include "shape_batch.hpp"
#include "studio_controller.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;
using namespace openstitch::desktop_v2::studio;

namespace {

struct Rig {
    DocumentSession session;
    StudioController studio{session};
    BoundaryBlendController blend{studio};
    std::uint64_t left{0};
    std::uint64_t right{0};
    std::uint64_t far{0};

    Rig() {
        auto created = project_ops::create_empty_project({});
        if (created) {
            session.replaceProject(std::move(*created), true);
        }
    }
    std::uint64_t add(double x, const char* rgb) {
        const auto r = session.applyBatch(batchAddRect({x, 0}, 20, 20), "Zone");
        const auto id = createdObjectId(r.report).value_or(0);
        const std::string color = std::string(R"({"schema":"openstitch.batch/1","operations":[)") +
                                  R"({"op":"set_color","object":)" + std::to_string(id) +
                                  R"(,"rgb":")" + rgb + R"("}]})";
        (void)session.applyBatch(color, "Couleur");
        return id;
    }
    void build() {
        left = add(-10, "#1E50C8");
        right = add(10, "#C81E1E");
        far = add(60, "#1EC850");
    }
    void select(std::vector<std::uint64_t> ids) {
        std::vector<Target> targets;
        for (const auto id : ids) {
            targets.push_back({TargetKind::EmbroideryObject, id});
        }
        session.selection().replace(std::move(targets));
    }
    [[nodiscard]] std::size_t steps() const { return session.undoNames().size(); }
};

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

} // namespace

class StudioBoundaryBlendTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void createEditRemoveJourney();
    void refusalsDoNotMutate();
    void panelIsHostedAndDrivesTheController();
};

void StudioBoundaryBlendTest::initTestCase() {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
}

void StudioBoundaryBlendTest::createEditRemoveJourney() {
    Rig rig;
    rig.build();
    rig.select({rig.left, rig.right});
    QVERIFY(rig.blend.canCreate());
    QVERIFY(!rig.blend.hasBlend());

    const auto before = rig.steps();
    QVERIFY(rig.blend.create(2.0, 3.0, 1));
    QCOMPARE(rig.steps(), before + 1);
    QVERIFY(rig.blend.hasBlend());
    QVERIFY(rig.blend.active());
    // Largeurs vues depuis la zone principale (la dernière sélectionnée : `right`).
    QCOMPARE(rig.blend.widthMm(), 2.0);
    QCOMPARE(rig.blend.partnerWidthMm(), 3.0);
    QCOMPARE(rig.blend.curve(), 1);
    QVERIFY(rig.blend.frontierMm() > 15.0);
    QVERIFY(!rig.blend.frontier().isEmpty());
    QVERIFY(rig.blend.summary().contains(QStringLiteral("Fondu avec")));

    rig.select({rig.left});
    QVERIFY(rig.blend.hasBlend());
    QCOMPARE(rig.blend.widthMm(), 3.0);
    QVERIFY(rig.blend.edit(4.0, 3.0, 2));
    QCOMPARE(rig.steps(), before + 2);
    QCOMPARE(rig.blend.widthMm(), 4.0);
    QCOMPARE(rig.blend.curve(), 2);

    QVERIFY(rig.blend.remove());
    QCOMPARE(rig.steps(), before + 3);
    QVERIFY(!rig.blend.hasBlend());
    QVERIFY(!rig.session.project().findEmbroidery(ObjectId{rig.right})->boundary_blend);

    rig.session.undo();
    QVERIFY(rig.blend.hasBlend());
}

void StudioBoundaryBlendTest::refusalsDoNotMutate() {
    Rig rig;
    rig.build();
    rig.select({rig.left, rig.far});
    QVERIFY(!rig.blend.canCreate());
    QVERIFY(!rig.blend.refusal().isEmpty());
    const auto before = rig.steps();
    QVERIFY(!rig.blend.create(2.5, 2.5, 0));
    QCOMPARE(rig.steps(), before);
    QVERIFY(!rig.blend.lastOk());

    rig.select({rig.left});
    QVERIFY(!rig.blend.canCreate());
    QVERIFY(!rig.blend.remove());
    QCOMPARE(rig.steps(), before);
}

void StudioBoundaryBlendTest::panelIsHostedAndDrivesTheController() {
    Rig rig;
    rig.build();
    rig.select({rig.left, rig.right});
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("boundaryBlend"), &rig.blend);
    engine.loadData(R"(
        import QtQuick
        import QtQuick.Window
        import "qrc:/studio/qml"
        Window {
            width: 360; height: 400; visible: true
            BoundaryBlendPanel { anchors.fill: parent; controller: boundaryBlend }
        })");
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    auto* create = findVisual(window->contentItem(), QStringLiteral("boundaryBlendCreate"));
    QVERIFY(create);
    QVERIFY(create->isVisible());
    const auto before = rig.steps();
    QVERIFY(QMetaObject::invokeMethod(create, "click"));
    QCOMPARE(rig.steps(), before + 1);
    QVERIFY(rig.blend.hasBlend());
    auto* remove = findVisual(window->contentItem(), QStringLiteral("boundaryBlendRemove"));
    QVERIFY(remove);
    QTRY_VERIFY(remove->isVisible());
}

QTEST_MAIN(StudioBoundaryBlendTest)
#include "test_studio_boundary_blend.moc"
