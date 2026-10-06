// SPDX-License-Identifier: Apache-2.0
// Dialogues d'aide (lot L5, T3) : « Gestes souris et clavier » et « Guide de prise en main ».
// Headless (QT_QPA_PLATFORM=offscreen), sans sleep ni comparaison de pixels.
#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "help_dialogs.hpp"
#include "interaction_map.hpp"

namespace openstitch::desktop {

namespace {

QAction* addAction(QMainWindow& window, const QString& name, const QString& text,
                   const QKeySequence& shortcut = QKeySequence()) {
    auto* action = new QAction(text, &window);
    action->setObjectName(name);
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    window.addAction(action);
    return action;
}

QString cell(const GesturesDialog& dialog, int row, int column) {
    const QAbstractItemModel* model = dialog.table()->model();
    return model->data(model->index(row, column)).toString();
}

// Prochain widget focalisable de la chaîne de focus, hors enfants internes de `w`.
QWidget* nextOutside(QWidget* w) {
    QWidget* next = w->nextInFocusChain();
    while (next != w && (w->isAncestorOf(next) || next->focusPolicy() == Qt::NoFocus)) {
        next = next->nextInFocusChain();
    }
    return next;
}

// Restaure le préréglage même si un test échoue en cours de route.
struct PresetGuard {
    Preset saved{InteractionMap::preset()};
    ~PresetGuard() { InteractionMap::setPreset(saved); }
};

} // namespace

class HelpDialogsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("HelpDialogsTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
        InteractionMap::setPreset(Preset::OpenStitch);
    }

    void gesturesDialogRowsMatchAllRowsExactly() {
        PresetGuard guard;
        InteractionMap::setPreset(Preset::OpenStitch);
        QObject noActions;
        GesturesDialog dialog(&noActions);
        const auto rows = InteractionMap::allRows(false);
        QVERIFY(!rows.isEmpty());
        // Cardinalité : une ligne de dialogue par ligne de table non planned.
        QCOMPARE(dialog.totalRowCount(), static_cast<int>(rows.size()));
        QCOMPARE(dialog.visibleRowCount(), static_cast<int>(rows.size()));
        // Et chaque ligne reprend contexte / geste / libellé de la table, dans l'ordre.
        for (int i = 0; i < rows.size(); ++i) {
            const Row& row = *rows[i];
            QCOMPARE(cell(dialog, i, 0), InteractionMap::contextName(row.context));
            QCOMPARE(cell(dialog, i, 1), InteractionMap::describe(row.gesture));
            QCOMPARE(cell(dialog, i, 2), InteractionMap::label(row));
        }
        // Aucune ligne planned n'est affichée.
        const auto withPlanned = InteractionMap::allRows(true);
        if (withPlanned.size() > rows.size()) {
            QVERIFY(dialog.totalRowCount() < static_cast<int>(withPlanned.size()));
        }
    }

    void gesturesDialogListsEveryActionShortcut() {
        PresetGuard guard;
        InteractionMap::setPreset(Preset::OpenStitch);
        QMainWindow window;
        addAction(window, QStringLiteral("action_open"), QStringLiteral("&Ouvrir une image…"),
                  QKeySequence(Qt::CTRL | Qt::Key_O));
        addAction(window, QStringLiteral("action_analyze"), QStringLiteral("&Analyser le motif"),
                  QKeySequence(Qt::Key_F5));
        addAction(window, QStringLiteral("action_noShortcut"), QStringLiteral("Sans raccourci"));
        addAction(window, QStringLiteral("action_noText"), QString(), QKeySequence(Qt::Key_F9));
        addAction(window, QStringLiteral("action_amp"), QStringLiteral("Tom && Jerry"),
                  QKeySequence(Qt::Key_F7));

        GesturesDialog dialog(&window);
        const int tableRows = static_cast<int>(InteractionMap::allRows(false).size());
        QCOMPARE(dialog.totalRowCount(), tableRows + 3);

        const QString group = GesturesDialog::commandsGroupName();
        QStringList actionLabels;
        QStringList actionKeys;
        for (int r = 0; r < dialog.visibleRowCount(); ++r) {
            if (cell(dialog, r, 0) == group) {
                actionKeys << cell(dialog, r, 1);
                actionLabels << cell(dialog, r, 2);
            }
        }
        QVERIFY(actionLabels.contains(QStringLiteral("Ouvrir une image…")));
        QVERIFY(actionLabels.contains(QStringLiteral("Analyser le motif")));
        QVERIFY(actionLabels.contains(QStringLiteral("Tom & Jerry")));
        QVERIFY(!actionLabels.contains(QStringLiteral("Sans raccourci")));
        for (const QString& label : actionLabels) {
            QVERIFY2(!label.contains(QLatin1Char('&')) || label == QStringLiteral("Tom & Jerry"),
                     qPrintable(label));
        }
        QVERIFY(actionKeys.contains(
            QKeySequence(Qt::CTRL | Qt::Key_O).toString(QKeySequence::NativeText)));
        QVERIFY(actionKeys.contains(QKeySequence(Qt::Key_F5).toString(QKeySequence::NativeText)));
    }

    void plainActionTextStripsMnemonics() {
        QCOMPARE(plainActionText(QStringLiteral("&Fichier")), QStringLiteral("Fichier"));
        QCOMPARE(plainActionText(QStringLiteral("Aid&e")), QStringLiteral("Aide"));
        QCOMPARE(plainActionText(QStringLiteral("A && B")), QStringLiteral("A & B"));
        QCOMPARE(plainActionText(QStringLiteral("Sans")), QStringLiteral("Sans"));
    }

    void gesturesDialogSearchFiltersRows() {
        PresetGuard guard;
        InteractionMap::setPreset(Preset::OpenStitch);
        QMainWindow window;
        addAction(window, QStringLiteral("action_analyze"), QStringLiteral("&Analyser le motif"),
                  QKeySequence(Qt::Key_F5));
        GesturesDialog dialog(&window);
        const int total = dialog.totalRowCount();

        dialog.searchField()->setText(QStringLiteral("analyser"));
        QCOMPARE(dialog.visibleRowCount(), 1);
        QCOMPARE(cell(dialog, 0, 2), QStringLiteral("Analyser le motif"));

        // Insensible à la casse et aux accents (« Échap » dans la table).
        dialog.searchField()->setText(QStringLiteral("echap"));
        const int plain = dialog.visibleRowCount();
        QVERIFY(plain > 0);
        QVERIFY(plain < total);
        dialog.searchField()->setText(QStringLiteral("ÉCHAP"));
        QCOMPARE(dialog.visibleRowCount(), plain);
        dialog.searchField()->setText(QStringLiteral("analyser le motif"));
        QCOMPARE(dialog.visibleRowCount(), 1);
        // Plusieurs mots : tous doivent correspondre, dans n'importe quel ordre.
        dialog.searchField()->setText(QStringLiteral("motif analyser"));
        QCOMPARE(dialog.visibleRowCount(), 1);
        dialog.searchField()->setText(QStringLiteral("zzzz-introuvable"));
        QCOMPARE(dialog.visibleRowCount(), 0);

        dialog.searchField()->clear();
        QCOMPARE(dialog.visibleRowCount(), total);
    }

    void presetSwitchChangesRowsConsistentlyWithAllRows() {
        PresetGuard guard;
        InteractionMap::setPreset(Preset::OpenStitch);
        QObject noActions;
        GesturesDialog dialog(&noActions);
        const int openStitchRows = static_cast<int>(InteractionMap::allRows(false).size());
        QCOMPARE(dialog.totalRowCount(), openStitchRows);
        QCOMPARE(dialog.presetCombo()->currentIndex(), 0);

        qRegisterMetaType<openstitch::desktop::Preset>();
        QSignalSpy spy(&dialog, &GesturesDialog::presetChanged);
        dialog.presetCombo()->setCurrentIndex(1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(InteractionMap::preset(), Preset::Touchpad);
        const auto touchpadRows = InteractionMap::allRows(false);
        QVERIFY(static_cast<int>(touchpadRows.size()) != openStitchRows);
        QCOMPARE(dialog.totalRowCount(), static_cast<int>(touchpadRows.size()));
        for (int i = 0; i < touchpadRows.size(); ++i) {
            QCOMPARE(cell(dialog, i, 2), InteractionMap::label(*touchpadRows[i]));
        }

        dialog.presetCombo()->setCurrentIndex(0);
        QCOMPARE(InteractionMap::preset(), Preset::OpenStitch);
        QCOMPARE(dialog.totalRowCount(), openStitchRows);
    }

    void gesturesDialogHasAccessibleNamesAndTabOrder() {
        QObject noActions;
        GesturesDialog dialog(&noActions);
        QVERIFY(!dialog.accessibleName().isEmpty());
        QVERIFY(!dialog.searchField()->accessibleName().isEmpty());
        QCOMPARE(dialog.searchField()->accessibleName(),
                 QStringLiteral("Rechercher un geste ou un raccourci"));
        QVERIFY(!dialog.presetCombo()->accessibleName().isEmpty());
        QVERIFY(!dialog.table()->accessibleName().isEmpty());
        QVERIFY(!dialog.closeButton()->accessibleName().isEmpty());
        QCOMPARE(dialog.table()->selectionBehavior(), QAbstractItemView::SelectRows);
        QVERIFY(!dialog.table()->tabKeyNavigation());
        QVERIFY(dialog.minimumSize().width() > 0 && dialog.minimumSize().height() > 0);
        QVERIFY(dialog.width() >= dialog.minimumWidth());
        QVERIFY(dialog.height() >= dialog.minimumHeight());

        // Ordre de tabulation : recherche -> préréglage -> tableau -> Fermer.
        // (le bouton d'effacement interne du QLineEdit est ignoré.)
        QCOMPARE(nextOutside(dialog.searchField()), dialog.presetCombo());
        QCOMPARE(nextOutside(dialog.presetCombo()), dialog.table());
        QCOMPARE(nextOutside(dialog.table()), dialog.closeButton());
    }

    void quickStartHasSixStepsAndRealFlow() {
        QObject noActions;
        QuickStartDialog dialog(&noActions);
        QCOMPARE(dialog.stepCount(), 6);
        QVERIFY(!dialog.accessibleName().isEmpty());
        QVERIFY(!dialog.closeButton()->accessibleName().isEmpty());
        const auto steps = QuickStartDialog::defaultSteps();
        QCOMPARE(static_cast<int>(steps.size()), 6);
        for (const QuickStartStep& step : steps) {
            QVERIFY(!step.title.isEmpty());
            QVERIFY(!step.body.isEmpty());
        }
    }

    void quickStartButtonTriggersAction() {
        QMainWindow window;
        QAction* edit = addAction(window, QStringLiteral("action_stitchEditMode"),
                                  QStringLiteral("&Éditer les points…"));
        QSignalSpy spy(edit, &QAction::triggered);

        QuickStartDialog dialog(&window);
        // Étape 5 (index 4) : édition -> action_stitchEditMode.
        QPushButton* button = dialog.stepButton(4);
        QVERIFY(button != nullptr);
        QVERIFY(button->isEnabled());
        QCOMPARE(button->text(), QStringLiteral("Éditer les points…"));
        QVERIFY(!button->accessibleName().isEmpty());
        button->click();
        QCOMPARE(spy.count(), 1);

        // Désactivée -> bouton grisé avec la raison en infobulle, pas de déclenchement.
        edit->setEnabled(false);
        QVERIFY(!button->isEnabled());
        QVERIFY(!button->toolTip().isEmpty());
        button->click();
        QCOMPARE(spy.count(), 1);
        edit->setEnabled(true);
        QVERIFY(button->isEnabled());
        button->click();
        QCOMPARE(spy.count(), 2);
    }

    void quickStartMissingActionDisablesButton() {
        QMainWindow window;
        std::vector<QuickStartStep> steps{
            {QStringLiteral("Un"), QStringLiteral("Corps"), {QStringLiteral("action_absent")}},
            {QStringLiteral("Deux"), QStringLiteral("Info seule"), {}},
            {QStringLiteral("Trois"),
             QStringLiteral("Deux boutons"),
             {QStringLiteral("action_a"), QStringLiteral("action_b")}},
        };
        QAction* a = addAction(window, QStringLiteral("action_a"), QStringLiteral("A"));
        QAction* b = addAction(window, QStringLiteral("action_b"), QStringLiteral("B"));
        b->setEnabled(false);
        QSignalSpy spyA(a, &QAction::triggered);
        QSignalSpy spyB(b, &QAction::triggered);

        QuickStartDialog dialog(&window, steps);
        QCOMPARE(dialog.stepCount(), 3);
        QPushButton* missing = dialog.stepButton(0);
        QVERIFY(missing != nullptr);
        QVERIFY(!missing->isEnabled());
        QVERIFY(!missing->toolTip().isEmpty());
        QCOMPARE(dialog.stepButtonCount(1), 0);
        QVERIFY(dialog.stepButton(1) == nullptr);
        QCOMPARE(dialog.stepButtonCount(2), 2);
        dialog.stepButton(2, 0)->click();
        dialog.stepButton(2, 1)->click();
        QCOMPARE(spyA.count(), 1);
        QCOMPARE(spyB.count(), 0);
        QVERIFY(!dialog.stepButton(2, 1)->isEnabled());
    }

    void quickStartWorksWithoutActionRoot() {
        QuickStartDialog dialog(nullptr);
        QCOMPARE(dialog.stepCount(), 6);
        QPushButton* edit = dialog.stepButton(4);
        QVERIFY(edit != nullptr);
        QVERIFY(!edit->isEnabled());
    }

    void dialogsAreNonModalAndConstructWithoutMainWindow() {
        QObject noActions;
        GesturesDialog gestures(&noActions);
        QuickStartDialog quick(&noActions);
        QVERIFY(!gestures.isModal());
        QVERIFY(!quick.isModal());
        QVERIFY(gestures.parentWidget() == nullptr);
        QVERIFY(quick.parentWidget() == nullptr);
        GesturesDialog nullRoot(nullptr);
        QVERIFY(nullRoot.totalRowCount() > 0);
        gestures.show();
        quick.show();
        QVERIFY(gestures.isVisible());
        QVERIFY(quick.isVisible());
    }

    void aboutTextHasNameVersionLicenseAndRepository() {
        const QString text = aboutText();
        QVERIFY(text.contains(QStringLiteral("OpenStitch")));
        QVERIFY(text.contains(QStringLiteral("Apache-2.0")));
        QVERIFY(text.contains(QStringLiteral("0.1.0")));
        QVERIFY(text.contains(QStringLiteral("https://github.com/")));
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::HelpDialogsTest)
#include "test_help_dialogs.moc"
