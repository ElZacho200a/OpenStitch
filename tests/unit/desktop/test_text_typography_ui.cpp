// SPDX-License-Identifier: Apache-2.0
// Crenage manuel par paire (HP-TXT-007) et ligatures (HP-TXT-012) dans le dialogue texte et
// l'application d'un texte : saisie, conversion en options de mise en page, annulation.
#include <QApplication>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "main_window.hpp"
#include "text_dialog.hpp"

using openstitch::desktop::formatKerningPairs;
using openstitch::desktop::layoutOptionsFor;
using openstitch::desktop::MainWindow;
using openstitch::desktop::parseKerningPairs;
using openstitch::desktop::TextDialog;

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
    // Abscisse du premier noeud de la deuxieme lettre : bouge quand le crenage change.
    static int secondLetterX(const document::Project& project) {
        return project.vector_objects.at(1).paths.at(0).outer.nodes.at(0).pos.x.value;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("TextTypographyTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void parsesPairsInMillimetres() {
        const auto parsed =
            parseKerningPairs(QStringLiteral("AV: -0.3 mm\n\n To:0,15\nAV : -0.5mm\n"));
        QVERIFY(parsed.errors.isEmpty());
        // Paire repetee : la derniere ligne gagne, position de la premiere conservee.
        QCOMPARE(parsed.pairs.size(), std::size_t{2});
        QVERIFY(parsed.pairs[0].left == U'A');
        QVERIFY(parsed.pairs[0].right == U'V');
        QCOMPARE(parsed.pairs[0].adjust.value, -500);
        QVERIFY(parsed.pairs[1].left == U'T');
        QCOMPARE(parsed.pairs[1].adjust.value, 150);
    }

    void rejectsBadLinesWithAMessageAndKeepsTheOthers() {
        const auto parsed =
            parseKerningPairs(QStringLiteral("AV: -0.3\nABC: 1\nXY: beaucoup\nWA: 99 mm\nTo: 0.1"));
        QCOMPARE(parsed.pairs.size(), std::size_t{2});
        QCOMPARE(parsed.errors.size(), 3);
        QVERIFY(parsed.errors[0].contains(QStringLiteral("Ligne 2")));
    }

    void formatsAndReparsesIdentically() {
        const std::vector<document::KerningPair> pairs{{U'A', U'V', Micrometers{-300}},
                                                       {U'T', U'o', Micrometers{1'250}}};
        const QString text = formatKerningPairs(pairs);
        QCOMPARE(text, QStringLiteral("AV: -0.3 mm\nTo: 1.25 mm"));
        QVERIFY(parseKerningPairs(text).pairs == pairs);
    }

    void defaultTextGivesDefaultLayoutOptions() {
        const auto options = layoutOptionsFor(document::TextObject{});
        QVERIFY(options.kerning_overrides.empty());
        QVERIFY(!options.ligatures);
        QVERIFY(options.compose_marks);
        QVERIFY(options.bidi);
    }

    void fieldsBecomeLayoutOptions() {
        document::TextObject t;
        t.kerning_pairs = {{U'A', U'V', Micrometers{-300}}};
        t.ligatures = true;
        const auto options = layoutOptionsFor(t);
        QCOMPARE(options.kerning_overrides.size(), std::size_t{1});
        QCOMPARE(options.kerning_overrides[0].adjust.value, -300);
        QVERIFY(options.ligatures);
    }

    void dialogEditsPairsAndLigatures() {
        auto initial = sample("AV");
        initial.kerning_pairs = {{U'A', U'V', Micrometers{-300}}};
        initial.ligatures = true;
        TextDialog dialog(initial, false);
        auto* edit = dialog.findChild<QPlainTextEdit*>(QStringLiteral("kerning_pairs_edit"));
        auto* ligatures = dialog.findChild<QCheckBox*>(QStringLiteral("ligatures_check"));
        QVERIFY(edit != nullptr);
        QVERIFY(ligatures != nullptr);
        QCOMPARE(edit->toPlainText(), QStringLiteral("AV: -0.3 mm"));
        QVERIFY(ligatures->isChecked());
        QVERIFY(dialog.textObject().kerning_pairs == initial.kerning_pairs);

        edit->setPlainText(QStringLiteral("AV: -0.6 mm\nTo: 0.2 mm"));
        ligatures->setChecked(false);
        const auto edited = dialog.textObject();
        QCOMPARE(edited.kerning_pairs.size(), std::size_t{2});
        QCOMPARE(edited.kerning_pairs[0].adjust.value, -600);
        QVERIFY(!edited.ligatures);
    }

    void dialogReportsInvalidPairLines() {
        TextDialog dialog(sample("AV"), true);
        auto* edit = dialog.findChild<QPlainTextEdit*>(QStringLiteral("kerning_pairs_edit"));
        edit->setPlainText(QStringLiteral("n'importe quoi"));
        QMetaObject::invokeMethod(&dialog, "refresh");
        QVERIFY(!dialog.warnings().isEmpty());
        QVERIFY(dialog.warnings().first().contains(QStringLiteral("Ligne 1")));
        QVERIFY(dialog.textObject().kerning_pairs.empty());
    }

    void applyingPairsMovesLettersAndIsOneUndoStep() {
        MainWindow window;
        window.applyLoadedProject(blankProject());
        QVERIFY(window.applyText(sample("AV"), true));
        const int plainX = secondLetterX(window.project_);

        auto edited = window.project_.text_objects[0];
        edited.kerning_pairs = {{U'A', U'V', Micrometers{-1'000}}};
        QVERIFY(window.applyText(edited, false));
        QVERIFY(secondLetterX(window.project_) != plainX);
        QCOMPARE(window.project_.text_objects[0].kerning_pairs.size(), std::size_t{1});

        window.undo();
        QCOMPARE(secondLetterX(window.project_), plainX);
        QVERIFY(window.project_.text_objects[0].kerning_pairs.empty());
        window.redo();
        QVERIFY(secondLetterX(window.project_) != plainX);
        QCOMPARE(window.project_.text_objects[0].kerning_pairs.size(), std::size_t{1});
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_text_typography_ui.moc"
