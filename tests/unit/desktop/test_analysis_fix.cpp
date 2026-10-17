// SPDX-License-Identifier: Apache-2.0
// Panneau « Analyse » : constats issus de stitch_analysis::analyze_project et bouton
// « Corriger » (un Fix = une commande annulable). Headless, sans pixel ni sleep.
#include <QAction>
#include <QDockWidget>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "font_catalog.hpp"
#include "main_window.hpp"
#include "openstitch/document/project.hpp"
#include "openstitch/stitch_analysis/analyze.hpp"

using openstitch::Micrometers;
using openstitch::ObjectId;
using openstitch::Vec2um;
using openstitch::desktop::MainWindow;

namespace {

namespace doc = openstitch::document;
namespace geo = openstitch::geometry;
namespace sa = openstitch::stitch_analysis;

// Mêmes rôles que main_window_analysis.cpp (valeurs d'implémentation du panneau).
constexpr int kObjectRole = Qt::UserRole + 2;
constexpr int kFixKindRole = Qt::UserRole + 10;
constexpr int kFixLabelRole = Qt::UserRole + 15;

// Un carré de 10 mm décalé de `offsetX`, cousu en point courant de longueur `stitchLength`.
doc::Project squareProject(std::int32_t offsetX, std::int32_t stitchLength) {
    doc::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    doc::VectorObject vector;
    vector.id = project.object_ids.next();
    vector.name = "Carre";
    geo::Path path;
    path.closed = true;
    for (const auto& [x, y] :
         {std::pair{0, 0}, std::pair{10'000, 0}, std::pair{10'000, 10'000}, std::pair{0, 10'000}}) {
        path.nodes.push_back({Vec2um{Micrometers{offsetX + x}, Micrometers{y}},
                              geo::NodeType::Corner, std::nullopt, std::nullopt});
    }
    vector.paths.push_back({std::move(path), {}});
    doc::EmbroideryObject emb;
    emb.id = project.object_ids.next();
    emb.source_vector = vector.id;
    emb.name = "Contour";
    doc::RunningStitchParams params;
    params.stitch_length = Micrometers{stitchLength};
    emb.params = params;
    project.vector_objects.push_back(std::move(vector));
    project.embroidery_objects.push_back(std::move(emb));
    return project;
}

int fixKindOf(const QListWidgetItem* item) {
    return item->data(kFixKindRole).toInt();
}

int rowWithFix(const QListWidget& list, sa::FixKind kind) {
    for (int row = 0; row < list.count(); ++row) {
        if (fixKindOf(list.item(row)) == static_cast<int>(kind)) {
            return row;
        }
    }
    return -1;
}

} // namespace

namespace openstitch::desktop {

// Nom imposé par `friend class MainWindowTest` (main_window.hpp).
class MainWindowTest : public QObject {
    Q_OBJECT

private:
    QTemporaryDir settingsDir_;

    static QPushButton* fixButton(MainWindow& window) {
        return window.analysisDock_->findChild<QPushButton*>(QStringLiteral("analysisFixButton"));
    }

    // Sélectionne le constat portant `kind` et rend le bouton ; null si absent.
    static QPushButton* selectFix(MainWindow& window, sa::FixKind kind) {
        const int row = rowWithFix(*window.analysisList_, kind);
        if (row < 0) {
            return nullptr;
        }
        window.analysisList_->setCurrentRow(row);
        return fixButton(window);
    }

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("AnalysisFixTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    void panelListsTheFindingsOfAnalyzeProject() {
        MainWindow window;
        window.applyLoadedProject(squareProject(100'000, 1'000));
        QVERIFY(window.sequence_.has_value());
        const auto expected = sa::analyze_project(window.project_, *window.sequence_);
        QVERIFY(!expected.findings.empty());
        window.runAnalysis();
        std::size_t rows = 0;
        for (int i = 0; i < window.analysisList_->count(); ++i) {
            rows += window.analysisList_->item(i)->data(Qt::UserRole + 1).toInt() >= 0 ? 1 : 0;
        }
        QCOMPARE(rows, expected.findings.size());
        // Le constat « hors cadre » porte l'objet fautif et sa correction.
        QVERIFY(rowWithFix(*window.analysisList_, sa::FixKind::MoveIntoHoop) >= 0);
    }

    void fixButtonIsDisabledWithoutFixAndEnabledWithFix() {
        MainWindow window;
        window.applyLoadedProject(squareProject(100'000, 1'000));
        window.runAnalysis();
        auto* button = fixButton(window);
        QVERIFY(button != nullptr);
        QVERIFY(!button->isEnabled()); // rien de sélectionné

        // Constat sans correction : point trop court dans la séquence.
        int noFixRow = -1;
        const int fixRow = rowWithFix(*window.analysisList_, sa::FixKind::MoveIntoHoop);
        QVERIFY(fixRow >= 0);
        for (int i = 0; i < window.analysisList_->count(); ++i) {
            const auto* item = window.analysisList_->item(i);
            if (item->data(Qt::UserRole + 1).toInt() >= 0 && fixKindOf(item) == 0) {
                noFixRow = i;
                break;
            }
        }
        if (noFixRow >= 0) {
            window.analysisList_->setCurrentRow(noFixRow);
            QVERIFY(!button->isEnabled());
        }
        window.analysisList_->setCurrentRow(fixRow);
        QVERIFY(button->isEnabled());
        QVERIFY2(button->text().startsWith(QStringLiteral("Corriger : ")),
                 qPrintable(button->text()));
        QVERIFY(button->text().contains(
            window.analysisList_->item(fixRow)->data(kFixLabelRole).toString()));
    }

    void moveIntoHoopMovesTheSourceVectorInOneUndoStep() {
        MainWindow window;
        window.applyLoadedProject(squareProject(100'000, 1'000));
        const doc::Project before = window.project_;
        window.runAnalysis();
        auto* button = selectFix(window, sa::FixKind::MoveIntoHoop);
        QVERIFY(button != nullptr && button->isEnabled());
        const auto undoBefore = window.undoStack_.undoNames().size();
        button->click();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore + 1);
        QVERIFY(
            window.project_.vector_objects.front().paths.front().outer.nodes.front().pos.x.value <
            100'000);
        QVERIFY(window.analysisIsStale_);
        QVERIFY(!button->isEnabled());

        // Ré-analyse : le motif rentre dans le cadre, le constat disparaît.
        window.runAnalysis();
        QCOMPARE(rowWithFix(*window.analysisList_, sa::FixKind::MoveIntoHoop), -1);

        window.undo();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        const auto& nodes = window.project_.vector_objects.front().paths.front().outer.nodes;
        const auto& nodesBefore = before.vector_objects.front().paths.front().outer.nodes;
        QCOMPARE(nodes.size(), nodesBefore.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            QCOMPARE(nodes[i].pos.x.value, nodesBefore[i].pos.x.value);
            QCOMPARE(nodes[i].pos.y.value, nodesBefore[i].pos.y.value);
        }
    }

    void clampStitchLengthBringsTheParameterBackInRange() {
        MainWindow window;
        window.applyLoadedProject(squareProject(0, 9'000));
        const ObjectId id = window.project_.embroidery_objects.front().id;
        window.runAnalysis();
        auto* button = selectFix(window, sa::FixKind::ClampStitchLength);
        QVERIFY(button != nullptr && button->isEnabled());
        const auto undoBefore = window.undoStack_.undoNames().size();
        button->click();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore + 1);
        const auto* running =
            std::get_if<doc::RunningStitchParams>(&window.project_.findEmbroidery(id)->params);
        QVERIFY(running != nullptr);
        const auto& finishing = window.project_.finishing;
        QVERIFY(running->stitch_length.value <= finishing.max_stitch_length.value);
        QVERIFY(running->stitch_length.value >= finishing.min_stitch_length.value);

        window.runAnalysis();
        QCOMPARE(rowWithFix(*window.analysisList_, sa::FixKind::ClampStitchLength), -1);

        window.undo();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        running =
            std::get_if<doc::RunningStitchParams>(&window.project_.findEmbroidery(id)->params);
        QVERIFY(running != nullptr);
        QCOMPARE(running->stitch_length.value, 9'000);
    }

    void splitLongStitchesEnablesTheFinishingOption() {
        MainWindow window;
        window.applyLoadedProject(squareProject(0, 9'000));
        QVERIFY(!window.project_.finishing.split_long_stitches);
        window.runAnalysis();
        auto* button = selectFix(window, sa::FixKind::SplitLongStitches);
        QVERIFY(button != nullptr && button->isEnabled());
        const auto undoBefore = window.undoStack_.undoNames().size();
        button->click();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore + 1);
        QVERIFY(window.project_.finishing.split_long_stitches);
        window.undo();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QVERIFY(!window.project_.finishing.split_long_stitches);
    }

    void enableFinishingTurnsTheFinishingOn() {
        MainWindow window;
        doc::Project project = squareProject(0, 1'000);
        project.finishing.enabled = false;
        window.applyLoadedProject(project);
        window.runAnalysis();
        auto* button = selectFix(window, sa::FixKind::EnableFinishing);
        QVERIFY(button != nullptr && button->isEnabled());
        const auto undoBefore = window.undoStack_.undoNames().size();
        button->click();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore + 1);
        QVERIFY(window.project_.finishing.enabled);
        window.undo();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QVERIFY(!window.project_.finishing.enabled);
    }

    void enlargeTextRaisesTheCapHeightInOneUndoStep() {
        MainWindow window;
        doc::Project project;
        project.original.width = 2;
        project.original.height = 2;
        project.original.rgba.assign(16, 255);
        window.applyLoadedProject(project);
        doc::TextObject text;
        text.text = "AB";
        text.font.builtin = "vera-sans-bold";
        text.cap_height = Micrometers{2'000}; // sous le minimum brodable
        QVERIFY(window.applyText(text, true));
        const ObjectId textId = window.project_.text_objects.front().id;
        const auto lettersBefore = window.project_.embroidery_objects.size();
        window.runAnalysis();
        auto* button = selectFix(window, sa::FixKind::EnlargeText);
        QVERIFY(button != nullptr && button->isEnabled());
        const auto undoBefore = window.undoStack_.undoNames().size();
        button->click();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore + 1);
        QVERIFY(window.project_.findText(textId)->cap_height.value > 2'000);
        QCOMPARE(window.project_.embroidery_objects.size(), lettersBefore);

        window.undo();
        QCOMPARE(window.undoStack_.undoNames().size(), undoBefore);
        QCOMPARE(window.project_.findText(textId)->cap_height.value, 2'000);
    }
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::MainWindowTest)
#include "test_analysis_fix.moc"
