// SPDX-License-Identifier: Apache-2.0
// Organiser les objets (HP-OBJ-005/008/010/012/022) : repartition, alignement sur le cadre,
// verrou d'edition, redimensionnement par facteurs, decalage de duplication.
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "main_window.hpp"

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

    static document::Project blocks() {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        addBlock(project, 0, 0, 10, 10);
        addBlock(project, 12, 0, 22, 10);
        addBlock(project, 90, 0, 100, 10);
        return project;
    }

    static int minX(const MainWindow& w, int index) {
        return w.project_.vector_objects[static_cast<std::size_t>(index)]
            .paths[0]
            .outer.nodes[0]
            .pos.x.value;
    }

private slots:
    void initTestCase() {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("ArrangeUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void distributeIsOneUndoStep() {
        MainWindow window;
        window.applyLoadedProject(blocks());
        std::vector<ObjectId> ids;
        for (const auto& v : window.project_.vector_objects) {
            ids.push_back(v.id);
        }
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = ids});
        window.distributeShapes(true);
        QCOMPARE(minX(window, 1), 45'000);
        window.undo();
        QCOMPARE(minX(window, 1), 12'000);
    }

    void alignToFrameUsesTheHoopBox() {
        MainWindow window;
        window.applyLoadedProject(blocks());
        const ObjectId a = window.project_.vector_objects[0].id;
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a}});
        window.alignReference_ = 2;
        window.alignShapes(0); // a gauche du cadre
        QCOMPARE(minX(window, 0), -window.project_.canvas.usable_width().value / 2);
    }

    void lockedShapeIsNotPickedOnTheCanvas() {
        MainWindow window;
        window.applyLoadedProject(blocks());
        window.showVectorsAct_->setChecked(true);
        const ObjectId a = window.project_.vector_objects[0].id;
        QVERIFY(window.objectAt(QPointF(5.0, -5.0)).has_value());
        window.setShapesLocked({a}, true);
        QVERIFY(!window.objectAt(QPointF(5.0, -5.0)).has_value());
        window.undo();
        QVERIFY(window.objectAt(QPointF(5.0, -5.0)).has_value());
    }

    void resizeByFactorsKeepsTheCentre() {
        MainWindow window;
        window.applyLoadedProject(blocks());
        const ObjectId a = window.project_.vector_objects[0].id;
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a}});
        window.resizeSelectionByFactors(2.0, 1.0, true);
        QCOMPARE(minX(window, 0), -5'000);
        window.undo();
        QCOMPARE(minX(window, 0), 0);
    }

    void duplicateRepeatsTheRememberedOffset() {
        MainWindow window;
        window.applyLoadedProject(blocks());
        const ObjectId a = window.project_.vector_objects[0].id;
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {a}});
        window.duplicateOffset_ = Vec2um{Micrometers{5'000}, Micrometers{0}};
        window.duplicateSelectionWithStitches();
        window.duplicateSelectionWithStitches(); // la copie est selectionnee : meme decalage
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{5});
        QCOMPARE(minX(window, 4), 10'000);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_arrange_ui.moc"
