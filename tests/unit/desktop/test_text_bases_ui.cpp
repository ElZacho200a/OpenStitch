// SPDX-License-Identifier: Apache-2.0
// Bases de texte (HP-TXT-005), edition par lettre (HP-TXT-008) et connecteurs (HP-TXT-014)
// dans le dialogue texte et l'application d'un texte : saisie, conversion, annulation.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "main_window.hpp"
#include "text_advanced_panel.hpp"
#include "text_dialog.hpp"

using openstitch::desktop::formatBasePath;
using openstitch::desktop::formatGlyphEdits;
using openstitch::desktop::MainWindow;
using openstitch::desktop::parseBasePath;
using openstitch::desktop::parseGlyphEdits;
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
    // Troisieme lettre (malgre le nom) : en arc centre, la lettre du milieu reste au sommet.
    static int secondLetterY(const document::Project& project) {
        return project.vector_objects.at(2).paths.at(0).outer.nodes.at(0).pos.y.value;
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("TextBasesTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void parsesGlyphEditLines() {
        const auto parsed = parseGlyphEdits(QStringLiteral("3: 0,5 -1 15 1,2 #ff0000\n\n1: 2"));
        QVERIFY(parsed.errors.isEmpty());
        QCOMPARE(parsed.edits.size(), std::size_t{2});
        // Tri par rang, rang saisi a partir de 1 -> index 0-based.
        QCOMPARE(parsed.edits[0].index, 0U);
        QCOMPARE(parsed.edits[0].dx.value, 2000);
        QCOMPARE(parsed.edits[1].index, 2U);
        QCOMPARE(parsed.edits[1].dx.value, 500);
        QCOMPARE(parsed.edits[1].dy.value, -1000);
        QCOMPARE(parsed.edits[1].scale, 1.2);
        QVERIFY(parsed.edits[1].rgb.has_value());
        QCOMPARE((*parsed.edits[1].rgb)[0], std::uint8_t{255});
    }

    void rejectsInvalidGlyphEditLines() {
        const auto parsed = parseGlyphEdits(QStringLiteral("abc\n0: 1\n2: 1 2 3 99\n4: x"));
        QCOMPARE(parsed.edits.size(), std::size_t{0});
        QCOMPARE(parsed.errors.size(), 4);
    }

    void glyphEditsRoundTripThroughText() {
        const auto parsed = parseGlyphEdits(QStringLiteral("2: 0.5 -1 15 1.5 #00FF00"));
        const auto again = parseGlyphEdits(formatGlyphEdits(parsed.edits));
        QVERIFY(again.errors.isEmpty());
        QVERIFY(again.edits == parsed.edits);
    }

    void parsesPathPoints() {
        const auto parsed = parseBasePath(QStringLiteral("0 0\n10,5 3\nnope"));
        QCOMPARE(parsed.points.size(), std::size_t{2});
        QCOMPARE(parsed.points[1].x.value, 10'500);
        QCOMPARE(parsed.errors.size(), 1);
        QCOMPARE(formatBasePath(parsed.points), QStringLiteral("0 0\n10.5 3"));
    }

    void dialogEditsBaseConnectorsAndLetters() {
        auto initial = sample("AB CD");
        TextDialog dialog(initial, false);
        auto* base = dialog.findChild<QComboBox*>(QStringLiteral("text_base_combo"));
        auto* radius = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("text_base_radius_spin"));
        auto* inside = dialog.findChild<QCheckBox*>(QStringLiteral("text_base_inside_check"));
        auto* letters = dialog.findChild<QComboBox*>(QStringLiteral("text_letter_connector_combo"));
        auto* words = dialog.findChild<QComboBox*>(QStringLiteral("text_word_connector_combo"));
        auto* glyphs = dialog.findChild<QPlainTextEdit*>(QStringLiteral("text_glyph_edits"));
        QVERIFY(base && radius && inside && letters && words && glyphs);
        QVERIFY(!radius->isEnabled()); // ligne droite : pas de rayon
        QVERIFY(dialog.textObject().base.kind == document::TextBaseKind::Line);

        base->setCurrentIndex(base->findData(static_cast<int>(document::TextBaseKind::Arc)));
        QVERIFY(radius->isEnabled());
        radius->setValue(45.0);
        inside->setChecked(true);
        letters->setCurrentIndex(
            letters->findData(static_cast<int>(document::ConnectorMode::NeverTrim)));
        words->setCurrentIndex(
            words->findData(static_cast<int>(document::ConnectorMode::AlwaysTrim)));
        glyphs->setPlainText(QStringLiteral("2: 1 0 0 1"));

        const auto edited = dialog.textObject();
        QVERIFY(edited.base.kind == document::TextBaseKind::Arc);
        QCOMPARE(edited.base.radius.value, 45'000);
        QVERIFY(edited.base.inside);
        QVERIFY(edited.letter_connector == document::ConnectorMode::NeverTrim);
        QVERIFY(edited.word_connector == document::ConnectorMode::AlwaysTrim);
        QCOMPARE(edited.glyph_edits.size(), std::size_t{1});
        QCOMPARE(edited.glyph_edits[0].index, 1U);
    }

    void dialogReportsInvalidGlyphLines() {
        TextDialog dialog(sample("AB"), true);
        auto* glyphs = dialog.findChild<QPlainTextEdit*>(QStringLiteral("text_glyph_edits"));
        glyphs->setPlainText(QStringLiteral("n'importe quoi"));
        QMetaObject::invokeMethod(&dialog, "refresh");
        QVERIFY(!dialog.warnings().isEmpty());
        QVERIFY(dialog.textObject().glyph_edits.empty());
    }

    void applyingArcMovesLettersAndIsOneUndoStep() {
        MainWindow window;
        window.applyLoadedProject(blankProject());
        QVERIFY(window.applyText(sample("AVA"), true));
        const int lineY = secondLetterY(window.project_);

        auto edited = window.project_.text_objects[0];
        edited.base.kind = document::TextBaseKind::Arc;
        edited.base.radius = Micrometers{30'000};
        edited.align = document::TextAlign::Center;
        QVERIFY(window.applyText(edited, false));
        QVERIFY(secondLetterY(window.project_) != lineY);
        QVERIFY(window.project_.text_objects[0].base.kind == document::TextBaseKind::Arc);

        window.undo();
        QCOMPARE(secondLetterY(window.project_), lineY);
        QVERIFY(window.project_.text_objects[0].base.kind == document::TextBaseKind::Line);
        window.redo();
        QVERIFY(window.project_.text_objects[0].base.kind == document::TextBaseKind::Arc);
    }

    void applyingGlyphEditRecoloursOnlyThatLetter() {
        MainWindow window;
        window.applyLoadedProject(blankProject());
        QVERIFY(window.applyText(sample("AB"), true));
        auto edited = window.project_.text_objects[0];
        edited.glyph_edits.push_back({1, Micrometers{0}, Micrometers{0}, Angle{0.0}, 1.0,
                                      std::array<std::uint8_t, 3>{200, 10, 10}});
        QVERIFY(window.applyText(edited, false));
        const auto& vectors = window.project_.vector_objects;
        QVERIFY(vectors.size() >= 2);
        QCOMPARE(vectors.front().rgb[0], edited.rgb[0]);
        QCOMPARE(vectors.back().rgb[0], std::uint8_t{200});
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_text_bases_ui.moc"
