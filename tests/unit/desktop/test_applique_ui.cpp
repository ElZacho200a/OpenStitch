// SPDX-License-Identifier: Apache-2.0
// Objet appliqué (HP-SPEC-001) : câblage bureau (menu Broderie), conversion annulable et export
// du contour de découpe.
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "main_window.hpp"

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static document::Project oneShape() {
        document::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(2 * 2 * 4, 255);
        document::VectorObject vec;
        vec.id = project.object_ids.next();
        geometry::PathSet set;
        for (const Vec2um v :
             {Vec2um{Micrometers{0}, Micrometers{0}}, Vec2um{Micrometers{20'000}, Micrometers{0}},
              Vec2um{Micrometers{20'000}, Micrometers{20'000}},
              Vec2um{Micrometers{0}, Micrometers{20'000}}}) {
            set.outer.nodes.push_back({v, geometry::NodeType::Corner, {}, {}});
        }
        set.outer.closed = true;
        vec.paths.push_back(set);
        project.vector_objects.push_back(vec);
        document::EmbroideryObject emb;
        emb.id = project.object_ids.next();
        emb.name = "forme";
        emb.source_vector = vec.id;
        emb.params = document::TatamiParams{};
        project.embroidery_objects.push_back(emb);
        return project;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("AppliqueUiTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void convertActionIsUndoable() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(oneShape());
        QVERIFY(window.appliqueConvertAct_ != nullptr);

        // Sans sélection : rien ne change.
        window.appliqueConvertAct_->trigger();
        QVERIFY(window.project_.embroidery_objects[0].is_tatami());

        window.selectedEmbroidery_ = window.project_.embroidery_objects[0].id;
        window.appliqueConvertAct_->trigger();
        QVERIFY(window.project_.embroidery_objects[0].is_applique());

        QVERIFY(window.undoStack_.undo(window.project_));
        QVERIFY(window.project_.embroidery_objects[0].is_tatami());
        QVERIFY(window.undoStack_.redo(window.project_));
        QVERIFY(window.project_.embroidery_objects[0].is_applique());
    }

    void cutContourExportsSvgAndRefusesWithoutApplique() {
        MainWindow window;
        window.show();
        window.applyLoadedProject(oneShape());
        QTemporaryDir out;
        QVERIFY(out.isValid());
        const QString svg = out.filePath(QStringLiteral("cut.svg"));

        QVERIFY(!window.exportAppliqueCutTo(svg)); // aucun appliqué
        QVERIFY(!QFile::exists(svg));

        window.selectedEmbroidery_ = window.project_.embroidery_objects[0].id;
        QVERIFY(window.convertSelectionToApplique());
        QVERIFY(window.exportAppliqueCutTo(svg));
        QVERIFY(QFile::exists(svg));
        QFile file(svg);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("<path"));
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_applique_ui.moc"
