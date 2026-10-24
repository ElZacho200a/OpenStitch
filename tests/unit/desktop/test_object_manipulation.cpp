// SPDX-License-Identifier: Apache-2.0
// Manipulation d'objets (HP-OBJ-001/004) : cadre commun et poignée de rotation d'une sélection
// multiple, pivot déplaçable, une seule étape d'annulation.
#include <QApplication>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "canvas_view.hpp"
#include "main_window.hpp"
#include "node_handle.hpp"
#include "rotation_handle_items.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static void addBlock(document::Project& project, int x0, int y0, int x1, int y1) {
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        vec.name = "Bloc";
        geometry::Path path;
        path.closed = true;
        const auto node = [](int x, int y) {
            return geometry::PathNode{Vec2um{Micrometers{x * 1000}, Micrometers{y * 1000}},
                                      geometry::NodeType::Corner, std::nullopt, std::nullopt};
        };
        path.nodes = {node(x0, y0), node(x1, y0), node(x1, y1), node(x0, y1)};
        vec.paths.push_back(geometry::PathSet{path, {}});
        project.vector_objects.push_back(vec);
    }

    static document::Project twoBlocks() {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        addBlock(project, 0, 0, 10, 10);
        addBlock(project, 20, 0, 30, 10);
        return project;
    }

    template <typename T> static int countItems(const MainWindow& window) {
        int n = 0;
        for (QGraphicsItem* item : window.scene_->items()) {
            n += dynamic_cast<T*>(item) != nullptr ? 1 : 0;
        }
        return n;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("ObjectManipulationTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void multiSelectionHasACommonBoxAndOneRotationHandle() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoBlocks());
        window.showVectorsAct_->setChecked(true);
        const ObjectId a = window.project_.vector_objects[0].id;
        const ObjectId b = window.project_.vector_objects[1].id;

        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a}});
        window.refreshImage();
        QVERIFY(window.commonBox_ == nullptr); // une seule forme : pas de cadre commun
        QCOMPARE(countItems<ResizeHandleItem>(window), 5);
        QCOMPARE(countItems<PivotHandleItem>(window), 1);

        window.setSelection(
            {.region = std::nullopt, .embroidery = std::nullopt, .objects = {a, b}});
        window.refreshImage();
        QVERIFY(window.commonBox_ != nullptr);
        // Cadre commun = boîte des deux formes (0..30 mm en X, 0..10 mm en Y, scène Y inversé).
        const QRectF box = window.commonBox_->rect();
        QCOMPARE(box.left(), 0.0);
        QCOMPARE(box.right(), 30.0);
        QCOMPARE(box.top(), -10.0);
        QCOMPARE(box.bottom(), 0.0);
        // Pas de coins de redimensionnement en multi (HP-OBJ-005), mais la rotation et le pivot.
        QCOMPARE(countItems<ResizeHandleItem>(window), 1);
        QCOMPARE(countItems<PivotHandleItem>(window), 1);
    }

    void rotatingTwoShapesAroundACustomPivotIsOneUndoStep() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoBlocks());
        const ObjectId a = window.project_.vector_objects[0].id;
        const ObjectId b = window.project_.vector_objects[1].id;
        const auto beforeA = window.project_.vector_objects[0].paths;
        const auto beforeB = window.project_.vector_objects[1].paths;

        const Vec2um origin{Micrometers{0}, Micrometers{0}};
        window.rotateObjects({a, b}, 90.0, origin);
        // (x, y) -> (-y, x) autour de l'origine : le coin (10, 0) de la forme A passe en (0, 10).
        QCOMPARE(window.project_.vector_objects[0].paths[0].outer.nodes[1].pos,
                 (Vec2um{Micrometers{0}, Micrometers{10'000}}));
        QCOMPARE(window.project_.vector_objects[1].paths[0].outer.nodes[0].pos,
                 (Vec2um{Micrometers{0}, Micrometers{20'000}}));

        window.undo(); // un seul pas pour les deux formes
        QVERIFY(window.project_.vector_objects[0].paths == beforeA);
        QVERIFY(window.project_.vector_objects[1].paths == beforeB);
        QVERIFY(!window.undoStack_.canUndo());
    }

    void customPivotAppliesOnlyToTheSelectionItWasSetFor() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoBlocks());
        const ObjectId a = window.project_.vector_objects[0].id;
        const ObjectId b = window.project_.vector_objects[1].id;
        const auto box = commands::objects_bounds(window.project_, {a, b});
        QVERIFY(box.has_value());

        QCOMPARE(window.rotationPivotFor({a, b}, *box), box->center());
        const Vec2um pivot{Micrometers{1'000}, Micrometers{2'000}};
        window.rotationPivot_ = pivot;
        window.rotationPivotTargets_ = {a, b};
        QCOMPARE(window.rotationPivotFor({a, b}, *box), pivot);
        QCOMPARE(window.rotationPivotFor({a}, *box), box->center()); // autre sélection : centre
    }

    void rotationPreviewShowsTheRotatedBoxAndIsRemovable() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoBlocks());
        const auto box =
            commands::objects_bounds(window.project_, {window.project_.vector_objects[0].id});
        QVERIFY(box.has_value());
        window.showRotationPreview(*box, box->center(), 90.0);
        QVERIFY(window.rotationPreview_ != nullptr);
        // Un carré de 10 mm tourné de 90° autour de son centre garde la même boîte.
        const QRectF bounds = window.rotationPreview_->polygon().boundingRect();
        QVERIFY(qAbs(bounds.width() - 10.0) < 1e-6);
        QVERIFY(qAbs(bounds.height() - 10.0) < 1e-6);
        window.hideRotationPreview();
        QVERIFY(window.rotationPreview_ == nullptr);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_object_manipulation.moc"
