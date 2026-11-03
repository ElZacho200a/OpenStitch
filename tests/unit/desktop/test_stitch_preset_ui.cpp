// SPDX-License-Identifier: Apache-2.0
// Préréglages de points (HP-ENG-013) : câblage bureau (menu Broderie).
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "main_window.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static document::Project twoObjects() {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        for (int i = 0; i < 2; ++i) {
            document::EmbroideryObject emb;
            emb.id = project.object_ids.next();
            emb.name = "obj";
            emb.params = document::RunningStitchParams{};
            project.embroidery_objects.push_back(emb);
        }
        document::TatamiParams tatami;
        tatami.row_spacing = Micrometers{550};
        project.embroidery_objects[1].params = tatami;
        return project;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("StitchPresetUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void saveApplyDeleteAreUndoableThroughTheMenu() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoObjects());
        QVERIFY(window.stitchPresetMenu_ != nullptr);

        // Sans sélection : enregistrer est désactivé.
        window.rebuildStitchPresetMenu();
        QVERIFY(window.stitchPresetSaveAct_ != nullptr);
        QVERIFY(!window.stitchPresetSaveAct_->isEnabled());

        // Enregistre les réglages de l'objet 2 (tatami 0,55 mm).
        window.selectedEmbroidery_ = window.project_.embroidery_objects[1].id;
        window.saveStitchPreset(QStringLiteral("tatami serviette"));
        QCOMPARE(window.project_.stitch_presets.size(), std::size_t{1});
        QCOMPARE(window.project_.stitch_presets[0].name, std::string("tatami serviette"));

        // Applique à l'objet 1 via l'action du menu : un seul pas d'annulation.
        window.selectedEmbroidery_ = window.project_.embroidery_objects[0].id;
        window.rebuildStitchPresetMenu();
        auto* apply = window.stitchPresetMenu_->findChild<QAction*>(
            QStringLiteral("action_stitchPresetApply_0"));
        QVERIFY(apply != nullptr && apply->isEnabled());
        apply->trigger();
        QCOMPARE(window.project_.embroidery_objects[0].params,
                 window.project_.embroidery_objects[1].params);
        QVERIFY(window.undoStack_.undo(window.project_));
        QVERIFY(std::holds_alternative<document::RunningStitchParams>(
            window.project_.embroidery_objects[0].params));

        // Suppression annulable.
        window.deleteStitchPreset(0);
        QVERIFY(window.project_.stitch_presets.empty());
        QVERIFY(window.undoStack_.undo(window.project_));
        QCOMPARE(window.project_.stitch_presets.size(), std::size_t{1});
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_stitch_preset_ui.moc"
