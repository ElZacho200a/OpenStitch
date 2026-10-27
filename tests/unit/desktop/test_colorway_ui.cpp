// SPDX-License-Identifier: Apache-2.0
// Coloris (HP-THR-006), tissu (HP-THR-008), fusion de fils (HP-THR-011) : câblage bureau.
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "canvas_view.hpp"
#include "colorway_dialog.hpp"
#include "main_window.hpp"
#include "openstitch/commands/colorway_commands.hpp"
#include "thread_merge_dialog.hpp"

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
        for (std::uint8_t i = 1; i <= 2; ++i) {
            document::EmbroideryObject emb;
            emb.id = project.object_ids.next();
            emb.name = "obj";
            emb.rgb = {static_cast<std::uint8_t>(i * 50), 0, 0};
            emb.params = document::RunningStitchParams{};
            project.embroidery_objects.push_back(emb);
        }
        return project;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("ColorwayUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void actionsExistAndDialogOpens() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoObjects());
        QVERIFY(window.colorwayAct_ != nullptr);
        QVERIFY(window.threadMergeAct_ != nullptr);
        window.colorwayAct_->trigger();
        QVERIFY(window.colorwayDialog_ != nullptr);
        QVERIFY(window.colorwayDialog_->isVisible());
    }

    void addAndActivateColorwayIsUndoable() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoObjects());
        window.showColorwayDialog();
        auto* add =
            window.colorwayDialog_->findChild<QPushButton*>(QStringLiteral("btn_colorway_add"));
        auto* activate = window.colorwayDialog_->findChild<QPushButton*>(
            QStringLiteral("btn_colorway_activate"));
        auto* list =
            window.colorwayDialog_->findChild<QListWidget*>(QStringLiteral("list_colorways"));
        QVERIFY(add != nullptr && activate != nullptr && list != nullptr);
        add->click();
        QCOMPARE(window.project_.colorways.size(), std::size_t{2});
        QCOMPARE(window.project_.active_colorway, 1);
        QCOMPARE(list->count(), 2);

        window.project_.embroidery_objects[0].rgb = {9, 9, 9}; // retouche dans le coloris 2
        list->setCurrentRow(0);
        QVERIFY(activate->isEnabled());
        activate->click();
        QCOMPARE(window.project_.active_colorway, 0);
        QCOMPARE(window.project_.embroidery_objects[0].rgb[0], std::uint8_t{50});
        window.undoStack_.undo(window.project_);
        QCOMPARE(window.project_.active_colorway, 1);
    }

    void fabricPresetChangesProjectAndCanvasAndIsUndoable() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoObjects());
        window.showColorwayDialog();
        auto* combo =
            window.colorwayDialog_->findChild<QComboBox*>(QStringLiteral("combo_fabric_preset"));
        QVERIFY(combo != nullptr);
        QCOMPARE(window.view_->fabricColor(), QColor(Qt::white));
        combo->setCurrentIndex(4); // « Noir »
        emit combo->activated(4);
        QCOMPARE(window.project_.fabric.color, (std::array<std::uint8_t, 3>{25, 25, 28}));
        QCOMPARE(window.view_->fabricColor(), QColor(25, 25, 28));
        window.undoStack_.undo(window.project_);
        window.updateActions();
        QCOMPARE(window.view_->fabricColor(), QColor(Qt::white));
    }

    void fabricMaterialAndStabilizerAreUndoable() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(twoObjects());
        window.showColorwayDialog();
        const auto* dialog = window.colorwayDialog_;
        auto* material = dialog->findChild<QComboBox*>(QStringLiteral("combo_fabric_material"));
        auto* stabilizer = dialog->findChild<QLineEdit*>(QStringLiteral("edit_fabric_stabilizer"));
        QVERIFY(material != nullptr && stabilizer != nullptr);
        material->setCurrentIndex(material->findData(QStringLiteral("jersey")));
        QCOMPARE(window.project_.fabric.material, std::string("jersey"));
        stabilizer->setText(QStringLiteral("cut-away"));
        emit stabilizer->editingFinished();
        QCOMPARE(window.project_.fabric.stabilizer, std::string("cut-away"));
        QVERIFY(window.undoStack_.undo(window.project_));
        QCOMPARE(window.project_.fabric.stabilizer, std::string());
        QCOMPARE(window.project_.fabric.material, std::string("jersey"));
    }

    void mergeDialogPreviewsAndBuildsPlan() {
        std::vector<stitch_analysis::ThreadUsage> usage(3);
        usage[0].identity.rgb = {200, 0, 0};
        usage[0].stitch_count = 900;
        usage[0].objects = {ObjectId{1}};
        usage[1].identity.rgb = {205, 2, 2};
        usage[1].stitch_count = 100;
        usage[1].objects = {ObjectId{2}};
        usage[2].identity.rgb = {0, 0, 200};
        usage[2].stitch_count = 500;
        usage[2].objects = {ObjectId{3}};
        ThreadMergeDialog dialog(usage);
        // Par defaut : reduire a N-1 = 2 fils.
        QCOMPARE(dialog.plan().after, std::size_t{2});
        QCOMPARE(dialog.plan().reassignments.size(), std::size_t{1});
        auto* manual = dialog.findChild<QWidget*>(QStringLiteral("radio_merge_manual"));
        QVERIFY(manual != nullptr);
        QMetaObject::invokeMethod(manual, "click");
        QCOMPARE(dialog.plan().after, std::size_t{2}); // fil 0 -> fil 1 par defaut
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_colorway_ui.moc"
