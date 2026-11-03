// SPDX-License-Identifier: Apache-2.0
// HP-UX-007 / HP-UX-008 / HP-FILE-007 : préférences générales, unités d'affichage, insertion d'un
// design dans le projet ouvert (headless, ni pixels ni sleep).
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <filesystem>

#include "general_preferences_dialog.hpp"
#include "length_units.hpp"
#include "main_window.hpp"
#include "openstitch/project_io/project_io.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static document::Project oneSquare() {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        geometry::Path path;
        path.closed = true;
        const auto node = [](int x, int y) {
            return geometry::PathNode{Vec2um{Micrometers{x * 1000}, Micrometers{y * 1000}},
                                      geometry::NodeType::Corner, std::nullopt, std::nullopt};
        };
        path.nodes = {node(0, 0), node(10, 0), node(10, 10), node(0, 10)};
        vec.paths.push_back(geometry::PathSet{path, {}});
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.source_vector = vec.id;
        emb.params = document::TatamiParams{};
        project.vector_objects.push_back(std::move(vec));
        project.embroidery_objects.push_back(std::move(emb));
        return project;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("GeneralPreferencesTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void unitConversionsAreExactAtTheBoundary() {
        using namespace length_units;
        QCOMPARE(toMm(1.0, Unit::Inches), 25.4);
        QCOMPARE(toMicrometers(1.0, Unit::Inches).value, 25'400);
        QCOMPARE(toMicrometers(10.0, Unit::Millimeters).value, 10'000);
        QCOMPARE(fromMicrometers(Micrometers{25'400}, Unit::Inches), 1.0);
        QCOMPARE(formatMm(25.4, Unit::Inches), QStringLiteral("1.000 in"));
        QCOMPARE(formatMm(12.5, Unit::Millimeters), QStringLiteral("12.5 mm"));
        QCOMPARE(formatMicrometers(Micrometers{12'700}, Unit::Inches, false),
                 QStringLiteral("0.500"));
        QCOMPARE(formatPoint(25.4, 0.0, Unit::Inches),
                 QStringLiteral("x : 1.000 in   y : 0.000 in"));
    }

    void preferencesPersistAndClamp() {
        GeneralPreferences prefs;
        prefs.unit = length_units::Unit::Inches;
        prefs.gridStepMm = 2.5;
        prefs.autosaveMinutes = 99; // hors bornes
        saveGeneralPreferences(prefs);
        const GeneralPreferences loaded = loadGeneralPreferences();
        QCOMPARE(loaded.unit, length_units::Unit::Inches);
        QCOMPARE(loaded.gridStepMm, 2.5);
        QCOMPARE(loaded.autosaveMinutes, 30);
        QCOMPARE(length_units::current(), length_units::Unit::Inches);
        length_units::setCurrent(length_units::Unit::Millimeters);
    }

    void dialogRoundTripKeepsGridStepAcrossUnitSwitch() {
        GeneralPreferencesDialog dialog;
        GeneralPreferences prefs;
        prefs.gridStepMm = 1.0;
        prefs.autosaveMinutes = 5;
        dialog.setPreferences(prefs);
        QCOMPARE(dialog.preferences().gridStepMm, 1.0);
        // Aller-retour mm -> pouces -> mm sans dérive d'arrondi.
        auto* unit = dialog.findChild<QComboBox*>(QStringLiteral("prefsUnit"));
        QVERIFY(unit != nullptr);
        unit->setCurrentIndex(1);
        QCOMPARE(dialog.preferences().unit, length_units::Unit::Inches);
        QCOMPARE(dialog.preferences().gridStepMm, 1.0);
        unit->setCurrentIndex(0);
        QCOMPARE(dialog.preferences().gridStepMm, 1.0);
        QCOMPARE(dialog.preferences().autosaveMinutes, 5);
    }

    void insertDesignIsOneUndoStepWithFreshIds() {
        const QString file = settingsDir_.filePath(QStringLiteral("insert.osp"));
        document::Project other = oneSquare();
        QVERIFY(project_io::save_project(std::filesystem::path(file.toStdWString()), other)
                    .has_value());

        MainWindow window;
        window.show();
        window.applyLoadedProject(oneSquare());
        QVERIFY(window.findChild<QAction*>(QStringLiteral("action_insertDesign")) != nullptr);
        QVERIFY(window.findChild<QAction*>(QStringLiteral("action_generalPreferences")) != nullptr);
        const auto lastId = window.project_.object_ids.last();

        QVERIFY(window.insertDesignFile(file));
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{2});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{2});
        QVERIFY(window.project_.vector_objects.back().id.value > lastId);
        QVERIFY(window.project_.embroidery_objects.back().source_vector ==
                window.project_.vector_objects.back().id);

        QVERIFY(window.undoStack_.undo(window.project_));
        QCOMPARE(window.project_.vector_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{1});
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_general_preferences.moc"
