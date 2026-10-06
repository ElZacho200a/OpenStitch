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

    void rectangleInsideAHoleSelectsOnlyInWindowIfContainsNothing() {
        openstitch::document::Project project;
        // Carré 20 x 20 mm (0..20) avec un trou 8..12 (physique).
        const ObjectId id = addSquare(project, 0, 0, 20);
        openstitch::geometry::Path hole;
        hole.closed = true;
        hole.nodes = {corner(8000, 8000), corner(12000, 8000), corner(12000, 12000),
                      corner(8000, 12000)};
        project.findObject(id)->paths.front().holes.push_back(hole);
        const QRectF insideHole(-0.0 + 9.0, -11.0, 2.0, 2.0); // scène : x 9..11, y -11..-9
        // Croisement : le rectangle ne touche ni le remplissage ni un bord -> pas retenu.
        QVERIFY(objectsInRectangleMm(project, insideHole, true).empty());
        // Un rectangle qui mord sur le bord du trou croise l'objet.
        QCOMPARE(objectsInRectangleMm(project, QRectF(7.0, -11.0, 3.0, 2.0), true).size(),
                 std::size_t{1});
        // Fenêtre : la boîte englobante de l'objet doit tenir dans le cadre (le trou n'y change
        // rien).
        QVERIFY(objectsInRectangleMm(project, insideHole, false).empty());
        QCOMPARE(objectsInRectangleMm(project, QRectF(-1.0, -21.0, 22.0, 22.0), false).size(),
                 std::size_t{1});
    }

    void openPathIsSelectedByItsStrokeOnlyNotItsImplicitFill() {
        openstitch::document::Project project;
        openstitch::document::VectorObject object;
        object.id = project.object_ids.next();
        object.name = "ouvert";
        openstitch::geometry::Path path;
        path.closed = false; // « U » ouvert : 0,0 -> 0,10 -> 10,10 -> 10,0 (physique)
        path.nodes = {corner(0, 0), corner(0, 10000), corner(10000, 10000), corner(10000, 0)};
        object.paths.push_back(openstitch::geometry::PathSet{path, {}});
        project.vector_objects.push_back(object);
        // Au milieu de la zone fermée implicitement (entre les branches) : rien en croisement.
        QVERIFY(objectsInRectangleMm(project, QRectF(4.0, -6.0, 2.0, 2.0), true).empty());
        // Sur le trait (branche gauche) : retenu.
        QCOMPARE(objectsInRectangleMm(project, QRectF(-1.0, -6.0, 2.0, 2.0), true).size(),
                 std::size_t{1});
        // Fenêtre englobante : retenu.
        QCOMPARE(objectsInRectangleMm(project, QRectF(-1.0, -11.0, 12.0, 12.0), false).size(),
                 std::size_t{1});
    }

    void rotatedShapeUsesItsRealOutlineInCrossingMode() {
        openstitch::document::Project project;
        openstitch::document::VectorObject object;
        object.id = project.object_ids.next();
        object.name = "losange";
        openstitch::geometry::Path path;
        path.closed = true; // losange centré (10,10) de demi-diagonale 10 (physique)
        path.nodes = {corner(10000, 0), corner(20000, 10000), corner(10000, 20000),
                      corner(0, 10000)};
        object.paths.push_back(openstitch::geometry::PathSet{path, {}});
        project.vector_objects.push_back(object);
        // Coin du cadre englobant, hors du losange : la boîte l'inclurait, pas la forme.
        QVERIFY(objectsInRectangleMm(project, QRectF(0.0, -3.0, 2.0, 2.0), true).empty());
        QCOMPARE(objectsInRectangleMm(project, QRectF(9.0, -11.0, 2.0, 2.0), true).size(),
                 std::size_t{1});
    }

    void curvedSegmentIsHitByTheRealCurveNotTheControlPolygon() {
        openstitch::document::Project project;
        openstitch::document::VectorObject object;
        object.id = project.object_ids.next();
        object.name = "courbe";
        openstitch::geometry::Path path;
        path.closed = true; // base 0..10 ; arête haute en arche (tangentes +/-8 mm vers le haut)
        auto n0 = corner(0, 0);
        auto n1 = corner(10000, 0);
        auto n2 = corner(10000, 10000);
        auto n3 = corner(0, 10000);
        n2.tan_out = Vec2um{Micrometers{-3000}, Micrometers{8000}};
        n3.tan_in = Vec2um{Micrometers{3000}, Micrometers{8000}};
        path.nodes = {n0, n1, n2, n3};
        object.paths.push_back(openstitch::geometry::PathSet{path, {}});
        project.vector_objects.push_back(object);
        // Le sommet de l'arche (t = 0,5) culmine à y = 10 + 0,75 * 8 = 16 mm (physique) :
        // un rectangle autour y 15..17 la croise réellement.
        QCOMPARE(objectsInRectangleMm(project, QRectF(4.0, -17.0, 2.0, 2.0), true).size(),
                 std::size_t{1});
        // Un rectangle au-dessus de la courbe réelle (y 17,5..19) ne la croise pas.
        QVERIFY(objectsInRectangleMm(project, QRectF(4.0, -19.0, 2.0, 1.5), true).empty());
        // Fenêtre : la boîte englobante réelle (sommet 16) tient dans un cadre jusqu'à y = 17.
        QCOMPARE(objectsInRectangleMm(project, QRectF(-1.0, -17.0, 12.0, 18.0), false).size(),
                 std::size_t{1});
    }

    void emptyObjectIsNeverSelected() {
        openstitch::document::Project project;
        openstitch::document::VectorObject object;
        object.id = project.object_ids.next();
        object.name = "vide";
        project.vector_objects.push_back(object); // aucun chemin
        openstitch::document::VectorObject object2 = object;
        object2.id = project.object_ids.next();
        object2.paths.push_back(openstitch::geometry::PathSet{}); // chemin sans nœud
        project.vector_objects.push_back(object2);
        const QRectF big(-100.0, -100.0, 200.0, 200.0);
        QVERIFY(objectsInRectangleMm(project, big, true).empty());
        QVERIFY(objectsInRectangleMm(project, big, false).empty());
        QVERIFY(objectsAtPointMm(project, QPointF(0.0, 0.0)).empty());
    }

    void emptyRectangleSelectsNothingInWindowMode() {
        openstitch::document::Project project;
        addSquare(project, 0, 0, 10);
        QVERIFY(objectsInRectangleMm(project, QRectF(), false).empty());
    }
};

QTEST_APPLESS_MAIN(SelectionHitTest)
#include "test_selection_hit_test.moc"
