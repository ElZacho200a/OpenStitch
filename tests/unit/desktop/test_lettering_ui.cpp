// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "font_catalog.hpp"
#include "main_window.hpp"
#include "properties_panel.hpp"
#include "text_dialog.hpp"

using openstitch::desktop::FontCatalog;
using openstitch::desktop::MainWindow;
using openstitch::desktop::PropertiesPanel;
using openstitch::desktop::TextDialog;
using openstitch::desktop::Tool;

namespace openstitch::desktop {

class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static document::Project blankProject() {
        document::Project p;
        p.original.width = 2;
        p.original.height = 2;
        p.original.rgba.assign(2 * 2 * 4, 255);
        return p;
    }
    static document::TextObject sample(const char* text) {
        document::TextObject t;
        t.text = text;
        t.font.builtin = "vera-sans-bold";
        t.cap_height = Micrometers{12'000};
        return t;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("LetteringTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void catalogListsBuiltinFontsAndLoadsThem() {
        auto& cat = FontCatalog::instance();
        cat.setSystemFontDirsForTesting({});
        QVERIFY(cat.entries().size() >= 2);
        QVERIFY(!cat.entries()[0].builtin.isEmpty());
        QString error;
        QVERIFY(cat.load(FontCatalog::refOf(cat.entries()[0]), &error) != nullptr);
        document::TextFontRef unknown;
        unknown.builtin = "nope";
        QVERIFY(cat.load(unknown, &error) == nullptr);
        QVERIFY(!error.isEmpty());
        document::TextFontRef gone;
        gone.family = "Police Disparue";
        gone.file = "/nowhere/x.ttf";
        QCOMPARE(cat.indexOf(gone), -1);
    }

    void catalogScansSystemDirectoriesAndFallsBackByFamily() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QFile::copy(QStringLiteral(":/fonts/Vera.ttf"),
                            dir.filePath(QStringLiteral("Mon Vera.ttf"))));
        auto& cat = FontCatalog::instance();
        cat.setSystemFontDirsForTesting({dir.path()});
        bool found = false;
        for (const auto& e : cat.entries()) {
            found = found || (!e.path.isEmpty() && e.path.contains(QStringLiteral("Mon Vera")));
        }
        QVERIFY(found);
        // Fichier déplacé : repli par famille sur la police installée.
        document::TextFontRef moved;
        moved.family = "Bitstream Vera Sans";
        moved.file = "/ancien/chemin/Vera.ttf";
        QVERIFY(cat.indexOf(moved) >= 0);
        QVERIFY(cat.load(moved) != nullptr);
        cat.setSystemFontDirsForTesting({});
    }

    void dialogReflectsSettingsAndBlocksEmptyText() {
        TextDialog dialog(sample("Hi"), true);
        auto* ok = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok->isEnabled());
        auto* edit = dialog.findChild<QPlainTextEdit*>(QStringLiteral("text_edit"));
        edit->setPlainText(QString());
        QMetaObject::invokeMethod(&dialog, "refresh");
        QVERIFY(!ok->isEnabled());
        edit->setPlainText(QStringLiteral("Salut\nMonde"));
        auto* fill = dialog.findChild<QComboBox*>(QStringLiteral("fill_combo"));
        fill->setCurrentIndex(fill->findData(static_cast<int>(document::TextFill::Tatami)));
        QMetaObject::invokeMethod(&dialog, "refresh");
        QVERIFY(ok->isEnabled());
        const auto t = dialog.textObject();
        QCOMPARE(QString::fromStdString(t.text), QStringLiteral("Salut\nMonde"));
        QCOMPARE(t.fill, document::TextFill::Tatami);
        QCOMPARE(t.cap_height.value, 12'000);
    }

    void dialogWarnsAboutTinyText() {
        auto t = sample("Hi");
        t.cap_height = Micrometers{3'500};
        TextDialog dialog(t, true);
        QVERIFY(!dialog.warnings().isEmpty());
    }

    void dialogWithMissingFontKeepsOkDisabled() {
        auto t = sample("Hi");
        t.font = {};
        t.font.family = "Police Disparue";
        t.font.file = "/nowhere/x.ttf";
        TextDialog dialog(t, false);
        QVERIFY(!dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->isEnabled());
        QVERIFY(!dialog.warnings().isEmpty());
    }

    void applyTextIsOneUndoStepAndEditsInPlace() {
        MainWindow window;
        window.applyLoadedProject(blankProject());
        QVERIFY(window.applyText(sample("ABC"), true));
        QCOMPARE(window.project_.text_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{3});
        QVERIFY(window.selectedTextId().has_value());
        QVERIFY(window.editTextAct_->isEnabled());
        auto* panel = window.findChild<PropertiesPanel*>();
        QVERIFY(panel->textInfoVisible());

        auto edited = window.project_.text_objects[0];
        edited.text = "ABCDE";
        QVERIFY(window.applyText(edited, false));
        QCOMPARE(window.project_.text_objects.size(), std::size_t{1});
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{5});

        window.undo();
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{3});
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{1});
        window.undo();
        QCOMPARE(window.undoStack_.undoNames().size(), std::size_t{0});
        QCOMPARE(window.project_.text_objects.size(), std::size_t{0});
        QVERIFY(window.project_.vector_objects.empty());
        window.redo();
        window.redo();
        QCOMPARE(window.project_.embroidery_objects.size(), std::size_t{5});

        window.setSelection({.region = std::nullopt,
                             .embroidery = std::nullopt,
                             .objects = {window.project_.vector_objects.front().id}});
        window.removeSelectedText();
        QVERIFY(window.project_.text_objects.empty());
        window.undo();
        QCOMPARE(window.project_.text_objects.size(), std::size_t{1});
    }

    void unreadableFontDoesNotTouchTheDocument() {
        MainWindow window;
        window.applyLoadedProject(blankProject());
        QCOMPARE(window.project_.text_objects.size(), std::size_t{0});
        QVERIFY(window.selectedTextId() == std::nullopt);
        QVERIFY(!window.editTextAct_->isEnabled());
    }

    void textToolClickOpensDialogAndCreatesText() {
        MainWindow window;
        window.applyLoadedProject(blankProject());
        window.setTool(Tool::Text);
        QVERIFY(window.toolTextAct_->isChecked());
        QTimer::singleShot(0, [] {
            auto* d = qobject_cast<TextDialog*>(QApplication::activeModalWidget());
            if (d == nullptr) {
                return;
            }
            d->findChild<QPlainTextEdit*>(QStringLiteral("text_edit"))
                ->setPlainText(QStringLiteral("Ok"));
            d->accept();
        });
        window.onCanvasClicked(QPointF(10.0, -5.0));
        QCOMPARE(window.project_.text_objects.size(), std::size_t{1});
        // Origine = clic converti en repère modèle (Y vers le haut).
        QCOMPARE(window.project_.text_objects[0].origin.x.value, 10'000);
        QCOMPARE(window.project_.text_objects[0].origin.y.value, 5'000);
        QVERIFY(window.currentTool_ == Tool::Select);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_lettering_ui.moc"
