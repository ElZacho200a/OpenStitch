// SPDX-License-Identifier: Apache-2.0
// Détection de sélection pure (L5-T4b) : aucun QApplication requis.
#include <QTest>

#include <algorithm>

#include "openstitch/document/project.hpp"
#include "selection_hit_test.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::desktop::objectsAtPointMm;
using openstitch::desktop::objectScenePath;
using openstitch::desktop::objectsInRectangleMm;

namespace {

openstitch::geometry::PathNode corner(int xUm, int yUm) {
    return {Vec2um{Micrometers{xUm}, Micrometers{yUm}}, openstitch::geometry::NodeType::Corner,
            std::nullopt, std::nullopt};
}

// Carré de `sizeMm` mm de côté, coin bas-gauche (xMm, yMm) en repère PHYSIQUE (Y haut).
ObjectId addSquare(openstitch::document::Project& project, int xMm, int yMm, int sizeMm,
                   bool visible = true) {
    openstitch::document::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "carre";
    object.visible = visible;
    openstitch::geometry::Path path;
    path.closed = true;
    const int x0 = xMm * 1000;
    const int y0 = yMm * 1000;
    const int s = sizeMm * 1000;
    path.nodes = {corner(x0, y0), corner(x0 + s, y0), corner(x0 + s, y0 + s), corner(x0, y0 + s)};
    object.paths.push_back(openstitch::geometry::PathSet{path, {}});
    project.vector_objects.push_back(object);
    return object.id;
}

} // namespace

class SelectionHitTest : public QObject {
    Q_OBJECT

private slots:
    void scenePathFlipsYAxis() {
        openstitch::document::Project project;
        const ObjectId id = addSquare(project, 0, 0, 10);
        const QRectF bb = objectScenePath(*project.findObject(id)).boundingRect();
        // Physique y in [0, 10] -> scène y in [-10, 0].
        QCOMPARE(bb, QRectF(0.0, -10.0, 10.0, 10.0));
    }

    void pointHitReturnsTopmostFirstAndSkipsHidden() {
        openstitch::document::Project project;
        const ObjectId below = addSquare(project, 0, 0, 10);
        const ObjectId above = addSquare(project, 0, 0, 10);
        const ObjectId hidden = addSquare(project, 0, 0, 10, false);
        const auto hits = objectsAtPointMm(project, QPointF(5.0, -5.0));
        QCOMPARE(hits.size(), std::size_t{2});
        QVERIFY(hits[0] == above);
        QVERIFY(hits[1] == below);
        QVERIFY(std::find(hits.begin(), hits.end(), hidden) == hits.end());
        QVERIFY(objectsAtPointMm(project, QPointF(50.0, -50.0)).empty());
    }

    void windowRequiresFullContainmentCrossingAcceptsOverlap() {
        openstitch::document::Project project;
        const ObjectId a = addSquare(project, 0, 0, 10);  // scène x 0..10, y -10..0
        const ObjectId b = addSquare(project, 20, 0, 10); // scène x 20..30
        const QRectF coversA(-1.0, -11.0, 12.0, 12.0);    // contient A seul
        const QRectF overlapsBoth(5.0, -5.0, 20.0, 3.0);  // coupe A et B
        const auto window1 = objectsInRectangleMm(project, coversA, false);
        QCOMPARE(window1.size(), std::size_t{1});
        QVERIFY(window1[0] == a);
        QVERIFY(objectsInRectangleMm(project, overlapsBoth, false).empty());
        const auto crossing = objectsInRectangleMm(project, overlapsBoth, true);
        QCOMPARE(crossing.size(), std::size_t{2});
        QVERIFY(crossing[0] == a);
        QVERIFY(crossing[1] == b);
    }

    void crossingSelectsObjectWhenRectangleIsInsideIt() {
        openstitch::document::Project project;
        const ObjectId a = addSquare(project, 0, 0, 10);
        const auto hits = objectsInRectangleMm(project, QRectF(4.0, -6.0, 2.0, 2.0), true);
        QCOMPARE(hits.size(), std::size_t{1});
        QVERIFY(hits[0] == a);
        QVERIFY(objectsInRectangleMm(project, QRectF(4.0, -6.0, 2.0, 2.0), false).empty());
    }

    void rectangleResultIsSortedByIdAndSkipsHidden() {
        openstitch::document::Project project;
        const ObjectId first = addSquare(project, 0, 0, 2);
        const ObjectId hidden = addSquare(project, 3, 0, 2, false);
        const ObjectId third = addSquare(project, 6, 0, 2);
        // Ordre de dessin inversé : l'ordre du résultat ne doit pas en dépendre.
        std::reverse(project.vector_objects.begin(), project.vector_objects.end());
        const QRectF all(-1.0, -10.0, 20.0, 12.0);
        const auto hits = objectsInRectangleMm(project, all, false);
        QCOMPARE(hits.size(), std::size_t{2});
        QVERIFY(hits[0] == first);
        QVERIFY(hits[1] == third);
        QVERIFY(std::find(hits.begin(), hits.end(), hidden) == hits.end());
        // Deux exécutions : résultat identique.
        QVERIFY(hits == objectsInRectangleMm(project, all, false));
    }

    void emptyRectangleSelectsNothingInWindowMode() {
        openstitch::document::Project project;
        addSquare(project, 0, 0, 10);
        QVERIFY(objectsInRectangleMm(project, QRectF(), false).empty());
    }
};

QTEST_APPLESS_MAIN(SelectionHitTest)
#include "test_selection_hit_test.moc"
