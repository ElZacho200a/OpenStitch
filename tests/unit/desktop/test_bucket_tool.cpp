// SPDX-License-Identifier: Apache-2.0
// Outil Pot de peinture : un clic dans un vide fermé entre des formes crée une forme qui le remplit
// (AddVectorObjectCommand dans une commande annulable) ; un clic sur une forme ou dans un vide
// ouvert ne change rien et l'explique dans la barre d'état.
#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>

#include "canvas_view.hpp"
#include "interaction_map.hpp"
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

    // Quatre blocs (mm) autour d'un vide de 10 x 10 mm centré sur (15, 15) ; `closed` = false
    // omet le bloc du haut.
    static document::Project projectWithGap(bool closed) {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        addBlock(project, 0, 0, 10, 30);
        addBlock(project, 20, 0, 30, 30);
        addBlock(project, 10, 0, 20, 10);
        if (closed) {
            addBlock(project, 10, 20, 20, 30);
        }
        return project;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("BucketToolTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void toolIsInThePaletteWithItsInteractionContext() {
        MainWindow window;
        window.show();
        QVERIFY(window.toolBucketAct_ != nullptr);
        QVERIFY(window.toolPalette_->actions().contains(window.toolBucketAct_));
        window.toolBucketAct_->trigger();
        QCOMPARE(window.currentTool_, Tool::Bucket);
        QVERIFY(window.toolBucketAct_->isChecked());
        QCOMPARE(InteractionMap::contextFor(Tool::Bucket, false, false), Context::Bucket);
        QVERIFY(!InteractionMap::hintsFor(Context::Bucket, Qt::NoModifier).isEmpty());
        window.setTool(Tool::Select);
        QVERIFY(!window.toolBucketAct_->isChecked());
    }

    void clickInAClosedGapCreatesAnUndoableShapeAndSelectsIt() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(projectWithGap(true));
        window.setTool(Tool::Bucket);
        // Scène : mm, Y vers le bas -> le centre (15, 15) du modèle est en (15, -15).
        window.onCanvasClicked(QPointF(15.0, -15.0));
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{5});
        const auto& created = window.project_.vector_objects.back();
        QCOMPARE(created.name, std::string("Vide rempli"));
        QVERIFY(window.selectedObject_.has_value());
        QCOMPARE(*window.selectedObject_, created.id);
        QVERIFY(window.undoStack_.canUndo());

        window.findChild<QAction*>("action_undo")->trigger();
        QApplication::processEvents();
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{4});
    }

    void clickOnAShapeOrInAnOpenAreaChangesNothing() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(projectWithGap(true));
        window.setTool(Tool::Bucket);
        window.onCanvasClicked(QPointF(5.0, -15.0)); // sur le bloc de gauche
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{4});
        QVERIFY(!window.undoStack_.canUndo());

        window.applyLoadedProject(projectWithGap(false));
        window.setTool(Tool::Bucket);
        window.onCanvasClicked(QPointF(15.0, -15.0)); // le haut est ouvert
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{3});
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("fermée")));
        QVERIFY(!window.undoStack_.canUndo());
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_bucket_tool.moc"
