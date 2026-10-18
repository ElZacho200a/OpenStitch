// SPDX-License-Identifier: Apache-2.0
// Menu Forme : Lisser, Convertir en courbes, formes prédéfinies. Headless, sans pixel ni sleep.
#include <QAction>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <numbers>

#include "main_window.hpp"
#include "openstitch/document/project.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::desktop::MainWindow;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;

geo::Path polygonCircle(int radius, int sides) {
    geo::Path path;
    path.closed = true;
    for (int i = 0; i < sides; ++i) {
        const double a = 2.0 * std::numbers::pi * i / sides;
        path.nodes.push_back(
            {Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(radius * std::cos(a)))},
                    Micrometers{static_cast<std::int32_t>(std::lround(radius * std::sin(a)))}},
             geo::NodeType::Corner, std::nullopt, std::nullopt});
    }
    return path;
}

doc::Project circleProject(ObjectId& id, int sides) {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    doc::VectorObject object;
    object.id = project.object_ids.next();
    object.name = "Cercle";
    object.paths.push_back({polygonCircle(10'000, sides), {}});
    id = object.id;
    project.vector_objects.push_back(std::move(object));
    return project;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("VectorToolsTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void actionsExistAndFollowTheSelection() {
        MainWindow window;
        ObjectId id;
        window.applyLoadedProject(circleProject(id, 48));
        QVERIFY(window.smoothAct_ != nullptr);
        QVERIFY(window.convertToCurvesAct_ != nullptr);
        QVERIFY(window.presetShapesMenu_ != nullptr);
        QCOMPARE(window.presetShapesMenu_->actions().size(), 6);
        window.setSelection({});
        window.updateShapeActions();
        QVERIFY(!window.smoothAct_->isEnabled());
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {id}});
        window.updateShapeActions();
        QVERIFY(window.smoothAct_->isEnabled());
        QVERIFY(window.convertToCurvesAct_->isEnabled());
    }

    void smoothIsOneUndoStep() {
        MainWindow window;
        ObjectId id;
        window.applyLoadedProject(circleProject(id, 72));
        const auto before = window.project_.findObject(id)->paths;
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {id}});
        window.smoothSelection(Micrometers{200});
        const auto after = window.project_.findObject(id)->paths;
        QVERIFY(after.front().outer.nodes.size() < before.front().outer.nodes.size());
        QVERIFY(window.undoStack_.undo(window.project_));
        QCOMPARE(window.project_.findObject(id)->paths == before, true);
        QVERIFY(!window.undoStack_.canUndo());
        QVERIFY(window.undoStack_.redo(window.project_));
        QCOMPARE(window.project_.findObject(id)->paths == after, true);
    }

    void convertToCurvesIsOneUndoStep() {
        MainWindow window;
        ObjectId id;
        window.applyLoadedProject(circleProject(id, 24));
        const auto before = window.project_.findObject(id)->paths;
        window.setSelection({.region = std::nullopt, .embroidery = std::nullopt, .objects = {id}});
        window.convertSelectionToCurves();
        QVERIFY(window.project_.findObject(id)->paths != before);
        QVERIFY(window.undoStack_.undo(window.project_));
        QCOMPARE(window.project_.findObject(id)->paths == before, true);
    }

    void presetShapesAreInsertedAndUndoable() {
        using Shape = MainWindow::PresetShape;
        for (const Shape shape : {Shape::Star, Shape::Heart, Shape::Arrow, Shape::RoundedRectangle,
                                  Shape::Arc, Shape::Spiral}) {
            MainWindow window;
            ObjectId id;
            window.applyLoadedProject(circleProject(id, 24));
            const std::size_t before = window.project_.vector_objects.size();
            window.insertPresetShape(shape, 30.0, 6);
            QCOMPARE(window.project_.vector_objects.size(), before + 1);
            QVERIFY(!window.project_.vector_objects.back().paths.front().outer.nodes.empty());
            QVERIFY(window.undoStack_.undo(window.project_));
            QCOMPARE(window.project_.vector_objects.size(), before);
        }
    }

    void starHonoursTheBranchCount() {
        MainWindow window;
        ObjectId id;
        window.applyLoadedProject(circleProject(id, 24));
        window.insertPresetShape(MainWindow::PresetShape::Star, 30.0, 7);
        QCOMPARE(window.project_.vector_objects.back().paths.front().outer.nodes.size(),
                 size_t{14});
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_vector_tools.moc"
