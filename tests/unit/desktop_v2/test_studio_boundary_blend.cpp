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
#include "openstitch/document/boundary_blend.hpp"
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
    void drawnFrontierJourney();
    void panelIsHostedAndDrivesTheController();
    void rowsSettingJourney();
    void rowsPropertyReflectsDocument();
    void severalBlendsPerZone();
    void panelListSelectsAndRemoves();
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
    QVERIFY(rig.session.project().findEmbroidery(ObjectId{rig.right})->boundary_blends.empty());

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

void StudioBoundaryBlendTest::drawnFrontierJourney() {
    Rig rig;
    rig.build();
    rig.select({rig.left, rig.right});
    const auto before = rig.steps();
    // Une frontière tracée de biais crée le fondu (un pas d'annulation).
    const QVariantList line{QVariantMap{{QStringLiteral("x"), -1.0}, {QStringLiteral("y"), -11.0}},
                            QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 11.0}}};
    QVERIFY(rig.blend.setFrontier(line));
    QCOMPARE(rig.steps(), before + 1);
    QVERIFY(rig.blend.hasBlend());
    QVERIFY(rig.blend.manualFrontier());
    QCOMPARE(rig.blend.frontier().size(), 1);

    // Un seul point : refus lisible, aucune mutation.
    QVERIFY(!rig.blend.setFrontier(QVariantList{line.front()}));
    QCOMPARE(rig.steps(), before + 1);

    // Retour à la frontière calculée, largeurs conservées.
    const double width = rig.blend.widthMm();
    QVERIFY(rig.blend.clearFrontier());
    QCOMPARE(rig.steps(), before + 2);
    QVERIFY(!rig.blend.manualFrontier());
    QCOMPARE(rig.blend.widthMm(), width);
    QVERIFY(!rig.blend.clearFrontier());
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

void StudioBoundaryBlendTest::rowsSettingJourney() {
    Rig rig;
    rig.build();
    rig.select({rig.left, rig.right});
    QVERIFY(rig.blend.create(2.5, 2.5, 1));
    QCOMPARE(rig.blend.rows(), QStringLiteral("per_zone"));

    const auto before = rig.steps();
    QVERIFY(!rig.blend.setRows(QStringLiteral("zigzag")));
    QCOMPARE(rig.steps(), before);
    QVERIFY(!rig.blend.lastOk());

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
    auto* combo = findVisual(window->contentItem(), QStringLiteral("blendRowsCombo"));
    QVERIFY(combo);
    // Le signal activated ne change pas currentIndex : le panneau lit l'index courant.
    combo->setProperty("currentIndex", 2);
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 2)));

    QCOMPARE(rig.steps(), before + 1);
    QCOMPARE(rig.blend.rows(), QStringLiteral("rotating"));
    for (const auto id : {rig.left, rig.right}) {
        QCOMPARE(rig.session.project().findEmbroidery(ObjectId{id})->boundary_blends.front().rows,
                 document::BoundaryBlendRows::Rotating);
    }
    rig.session.undo();
    QCOMPARE(rig.blend.rows(), QStringLiteral("per_zone"));
    for (const auto id : {rig.left, rig.right}) {
        QCOMPARE(rig.session.project().findEmbroidery(ObjectId{id})->boundary_blends.front().rows,
                 document::BoundaryBlendRows::PerZone);
    }
    // Quatrième entrée : entrelacé.
    combo->setProperty("currentIndex", 3);
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 3)));
    QCOMPARE(rig.blend.rows(), QStringLiteral("interlaced"));
    QCOMPARE(rig.session.project().findEmbroidery(ObjectId{rig.left})->boundary_blends.front().rows,
             document::BoundaryBlendRows::Interlaced);
    QVERIFY(rig.blend.summary().contains(QStringLiteral("entrelacé")));
}

void StudioBoundaryBlendTest::rowsPropertyReflectsDocument() {
    Rig rig;
    rig.build();
    rig.select({rig.left, rig.right});
    // Choix en attente avant création, appliqué à la création.
    QVERIFY(rig.blend.setRows(QStringLiteral("shared")));
    QCOMPARE(rig.blend.rows(), QStringLiteral("shared"));
    QVERIFY(rig.blend.create(2.5, 2.5, 0));
    QCOMPARE(rig.blend.rows(), QStringLiteral("shared"));
    QVERIFY(rig.blend.summary().contains(QStringLiteral("angle commun")));
    rig.select({rig.left});
    QCOMPARE(rig.blend.rows(), QStringLiteral("shared"));
}

namespace {

// Trois zones A (centre), B (à gauche) et C (à droite) : A touche B et C. Fondus A-B puis A-C.
struct ThreeZones : Rig {
    std::uint64_t a{0};
    std::uint64_t b{0};
    std::uint64_t c{0};
    void buildBlends() {
        a = add(0, "#1E50C8");
        b = add(-20, "#C81E1E");
        c = add(20, "#1EC850");
        select({b, a});
        (void)blend.create(2.0, 2.0, 0);
        select({c, a});
        (void)blend.create(3.0, 3.0, 0);
        select({a});
    }
    [[nodiscard]] const document::BoundaryBlendLink* link(std::uint64_t from,
                                                          std::uint64_t to) const {
        return document::boundary_blend_link(*session.project().findEmbroidery(ObjectId{from}),
                                             ObjectId{to});
    }
};

QString partnerOf(const QVariant& entry) {
    return entry.toMap().value(QStringLiteral("partnerId")).toString();
}

} // namespace

void StudioBoundaryBlendTest::severalBlendsPerZone() {
    ThreeZones rig;
    rig.buildBlends();
    const QString bId = QString::number(rig.b);
    const QString cId = QString::number(rig.c);
    QVERIFY(rig.link(rig.a, rig.b));
    QVERIFY(rig.link(rig.a, rig.c));

    // Une zone : ses deux fondus, le premier (plus petit partenaire) est le courant.
    QCOMPARE(rig.blend.blends().size(), 2);
    QCOMPARE(rig.blend.allFrontiers().size(), 2);
    const QString first = rig.b < rig.c ? bId : cId;
    QCOMPARE(rig.blend.currentPartner(), first);
    QVERIFY(rig.blend.hasBlend());
    int currents = 0;
    for (const auto& f : rig.blend.allFrontiers()) {
        const auto map = f.toMap();
        QVERIFY(!map.value(QStringLiteral("polylines")).toList().isEmpty());
        currents += map.value(QStringLiteral("current")).toBool() ? 1 : 0;
    }
    QCOMPARE(currents, 1);

    // Choisir A-C : les propriétés décrivent A-C ; aucune mutation.
    const auto before = rig.steps();
    QVERIFY(rig.blend.selectBlend(cId));
    QCOMPARE(rig.steps(), before);
    QCOMPARE(rig.blend.currentPartner(), cId);
    QCOMPARE(rig.blend.widthMm(), 3.0);
    QVERIFY(!rig.blend.selectBlend(QStringLiteral("999999")));
    QCOMPARE(rig.blend.currentPartner(), cId);

    // Changer la largeur de A-C : A-B inchangé, un seul pas d'annulation, choix conservé.
    QVERIFY(rig.blend.edit(4.5, 3.0, 0));
    QCOMPARE(rig.steps(), before + 1);
    QCOMPARE(rig.blend.currentPartner(), cId);
    QCOMPARE(rig.blend.widthMm(), 4.5);
    QCOMPARE(rig.link(rig.a, rig.c)->width_self.value, 4'500);
    QCOMPARE(rig.link(rig.a, rig.b)->width_self.value, 2'000);
    QCOMPARE(rig.link(rig.b, rig.a)->width_self.value, 2'000);

    // Retirer A-B depuis la liste : A-C reste, une frontière.
    QVERIFY(rig.blend.removeBlend(bId));
    QCOMPARE(rig.steps(), before + 2);
    QVERIFY(!rig.link(rig.a, rig.b));
    QVERIFY(!rig.link(rig.b, rig.a));
    QVERIFY(rig.link(rig.a, rig.c));
    QCOMPARE(rig.blend.blends().size(), 1);
    QCOMPARE(partnerOf(rig.blend.blends().front()), cId);
    QCOMPARE(rig.blend.allFrontiers().size(), 1);
    QCOMPARE(rig.blend.currentPartner(), cId);
    QVERIFY(!rig.blend.removeBlend(bId));
    QCOMPARE(rig.steps(), before + 2);

    rig.session.undo();
    QCOMPARE(rig.blend.blends().size(), 2);

    // Deux zones sélectionnées : le couple (principale, seconde) est le courant.
    rig.select({rig.c, rig.a});
    QCOMPARE(rig.blend.currentPartner(), cId);
    rig.select({rig.b, rig.c}); // pas de fondu B-C : création proposée, liste de C
    QCOMPARE(rig.blend.currentPartner(), bId);
    QVERIFY(!rig.blend.hasBlend());
    QCOMPARE(rig.blend.blends().size(), 1);
}

void StudioBoundaryBlendTest::panelListSelectsAndRemoves() {
    ThreeZones rig;
    rig.buildBlends();
    const QString bId = QString::number(rig.b);
    const QString cId = QString::number(rig.c);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("boundaryBlend"), &rig.blend);
    engine.loadData(R"(
        import QtQuick
        import QtQuick.Window
        import "qrc:/studio/qml"
        Window {
            width: 360; height: 600; visible: true
            BoundaryBlendPanel { anchors.fill: parent; controller: boundaryBlend }
        })");
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
    QVERIFY(window);
    auto* list = findVisual(window->contentItem(), QStringLiteral("boundaryBlendList"));
    QVERIFY(list);
    QTRY_VERIFY(list->isVisible());
    auto* rowC = findVisual(window->contentItem(), QStringLiteral("boundaryBlendRow_") + cId);
    QVERIFY(rowC);
    QVERIFY(findVisual(window->contentItem(), QStringLiteral("boundaryBlendRow_") + bId));

    const auto before = rig.steps();
    QVERIFY(QMetaObject::invokeMethod(rowC, "choose"));
    QCOMPARE(rig.blend.currentPartner(), cId);
    QCOMPARE(rig.steps(), before);
    // Le modèle de la liste est reconstruit : relire la ligne après le choix.
    QTRY_VERIFY([&] {
        auto* row = findVisual(window->contentItem(), QStringLiteral("boundaryBlendRow_") + cId);
        return row != nullptr && row->property("current").toBool();
    }());
    auto* width = findVisual(window->contentItem(), QStringLiteral("boundaryBlendWidthHere"));
    QVERIFY(width);
    QTRY_COMPARE(width->property("value").toInt(), 30);

    auto* removeB =
        findVisual(window->contentItem(), QStringLiteral("boundaryBlendRowRemove_") + bId);
    QVERIFY(removeB);
    QVERIFY(QMetaObject::invokeMethod(removeB, "click"));
    QCOMPARE(rig.steps(), before + 1);
    QVERIFY(!rig.link(rig.a, rig.b));
    QVERIFY(rig.link(rig.a, rig.c));
    QCOMPARE(rig.blend.allFrontiers().size(), 1);
}

QTEST_MAIN(StudioBoundaryBlendTest)
#include "test_studio_boundary_blend.moc"
