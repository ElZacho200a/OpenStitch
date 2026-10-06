// SPDX-License-Identifier: Apache-2.0
// Invariants généraux de la fenêtre principale (audit UI du 2026-10-06,
// docs/ui-audit-2026-10.md). Ces tests ne dépendent d'aucune fonctionnalité : ils
// verrouillent des propriétés de câblage qui doivent rester vraies quand l'interface
// évolue. Un défaut connu est déclaré avec QEXPECT_FAIL(Continue) : la suite reste
// verte, et dès que le défaut est corrigé le test échoue en « XPASS » jusqu'à ce que
// le marqueur soit retiré — il ne peut donc pas rester périmé en silence.
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QShortcut>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <map>
#include <set>

#include "interaction_map.hpp"
#include "main_window.hpp"

using openstitch::desktop::MainWindow;

namespace {

// Toutes les actions des menus, sous-menus compris, hors séparateurs et menus eux-mêmes.
void collectMenuActions(const QMenu* menu, QList<QAction*>& out) {
    for (QAction* action : menu->actions()) {
        if (action->isSeparator()) {
            continue;
        }
        if (action->menu() != nullptr) {
            collectMenuActions(action->menu(), out);
        } else {
            out.append(action);
        }
    }
}

// Mnémonique d'un titre de menu ou d'action (« &Fichier » -> 'f'), 0 s'il n'y en a pas.
QChar mnemonicOf(const QString& text) {
    for (int i = 0; i + 1 < text.size(); ++i) {
        if (text[i] == QLatin1Char('&')) {
            if (text[i + 1] == QLatin1Char('&')) {
                ++i; // « && » = esperluette littérale
                continue;
            }
            return text[i + 1].toLower();
        }
    }
    return QChar();
}


// Séquences de touches réellement câblées sur la fenêtre (QAction + QShortcut de contexte non
// local), comme windowShortcutsAreUnique.
std::set<QString> actualWindowShortcuts(const MainWindow& window) {
    std::set<QString> keys;
    for (const QAction* action : window.findChildren<QAction*>()) {
        if (action->shortcutContext() == Qt::WidgetShortcut) {
            continue;
        }
        for (const QKeySequence& seq : action->shortcuts()) {
            keys.insert(seq.toString());
        }
    }
    for (const QShortcut* shortcut : window.findChildren<QShortcut*>()) {
        if (shortcut->context() != Qt::WidgetShortcut) {
            keys.insert(shortcut->key().toString());
        }
    }
    return keys;
}

// Séquence d'une ligne `Key` de la table ; vide pour une ligne « modificateur seul » (touche 0).
QString keySequenceOf(const openstitch::desktop::Row& row) {
    if (row.gesture.key == Qt::Key(0)) {
        return {};
    }
    return QKeySequence(QKeyCombination(row.gesture.mods, row.gesture.key)).toString();
}

} // namespace

namespace openstitch::desktop {

class UiInvariantsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(settingsDir_.isValid());
        // Même isolation des QSettings que test_main_window : jamais le profil réel.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitchUITest"));
        QCoreApplication::setApplicationName(QStringLiteral("UiInvariantsTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    }

    // Deux actions de la même fenêtre avec la même touche rendent le raccourci
    // ambigu : Qt ne déclenche ni l'une ni l'autre.
    void windowShortcutsAreUnique() {
        MainWindow window;
        std::map<QString, QStringList> owners; // touche -> noms des propriétaires
        for (const QAction* action : window.findChildren<QAction*>()) {
            if (action->shortcutContext() == Qt::WidgetShortcut) {
                continue;
            }
            for (const QKeySequence& seq : action->shortcuts()) {
                owners[seq.toString()]
                    << (action->objectName().isEmpty() ? action->text() : action->objectName());
            }
        }
        for (const QShortcut* shortcut : window.findChildren<QShortcut*>()) {
            if (shortcut->context() == Qt::WidgetShortcut) {
                continue;
            }
            owners[shortcut->key().toString()] << QStringLiteral("QShortcut");
        }
        QStringList clashes;
        for (const auto& [key, names] : owners) {
            if (!key.isEmpty() && names.size() > 1) {
                clashes << key + QStringLiteral(" : ") + names.join(QStringLiteral(", "));
            }
        }
        // Audit UI 2026-10-06 : la touche G est liée au mode remodelage satin ET à l'outil
        // polygone régulier (main_window.cpp, createToolPalette / satinEditModeAct_).
        QVERIFY2(clashes.isEmpty(), qPrintable(clashes.join(QStringLiteral(" | "))));
    }

    // Chaque panneau doit avoir un nom d'objet (restoreState) et pouvoir être rouvert
    // depuis un menu une fois fermé via sa croix.
    void everyDockHasAnObjectNameAndIsReopenableFromAMenu() {
        MainWindow window;
        const auto docks = window.findChildren<QDockWidget*>();
        QVERIFY(!docks.isEmpty());

        QList<QAction*> menuActions;
        for (const QAction* top : window.menuBar()->actions()) {
            if (top->menu() != nullptr) {
                collectMenuActions(top->menu(), menuActions);
            }
        }
        QStringList unreachable;
        for (const QDockWidget* dock : docks) {
            QVERIFY2(!dock->objectName().isEmpty(), qPrintable(dock->windowTitle()));
            if (!menuActions.contains(dock->toggleViewAction())) {
                unreachable << dock->windowTitle();
            }
        }
        // Audit UI 2026-10-06 : aucun dock n'expose toggleViewAction() dans un menu ;
        // un panneau fermé ne peut pas être rouvert dans la session.
        QVERIFY2(unreachable.isEmpty(), qPrintable(unreachable.join(QStringLiteral(", "))));
    }

    // Une même lettre de mnémonique deux fois dans un menu : Alt+lettre devient ambigu.
    void menuMnemonicsAreUniquePerMenu() {
        MainWindow window;
        QStringList clashes;
        for (const QAction* top : window.menuBar()->actions()) {
            const QMenu* menu = top->menu();
            if (menu == nullptr) {
                continue;
            }
            std::map<QChar, QStringList> byLetter;
            for (const QAction* action : menu->actions()) {
                if (action->isSeparator()) {
                    continue;
                }
                const QChar letter = mnemonicOf(action->text());
                if (!letter.isNull()) {
                    byLetter[letter] << action->text();
                }
            }
            for (const auto& [letter, texts] : byLetter) {
                if (texts.size() > 1) {
                    clashes << menu->title() + QStringLiteral(" [") + letter +
                                   QStringLiteral("] : ") + texts.join(QStringLiteral(" | "));
                }
            }
        }
        QVERIFY2(clashes.isEmpty(), qPrintable(clashes.join(QStringLiteral("\n"))));
    }

    // Les menus de premier niveau doivent eux-mêmes avoir des mnémoniques distincts.
    void topLevelMenuMnemonicsAreUnique() {
        MainWindow window;
        std::map<QChar, QStringList> byLetter;
        for (const QAction* top : window.menuBar()->actions()) {
            const QChar letter = mnemonicOf(top->text());
            if (!letter.isNull()) {
                byLetter[letter] << top->text();
            }
        }
        for (const auto& [letter, texts] : byLetter) {
            QVERIFY2(texts.size() == 1, qPrintable(QString(letter) + QStringLiteral(" : ") +
                                                   texts.join(QStringLiteral(" | "))));
        }
    }

    // Règle 5 de la table des gestes : une ligne Key ROUTÉE par le canevas ne doit coïncider avec
    // aucun raccourci de la fenêtre (sinon le canevas ne la verrait jamais).
    void routedKeyRowsDoNotClashWithWindowShortcuts() {
        MainWindow window;
        const std::set<QString> actual = actualWindowShortcuts(window);
        QStringList clashes;
        for (const Row& row : InteractionMap::rawRows()) {
            if (row.gesture.kind != GestureKind::Key || !row.routed || row.planned) {
                continue;
            }
            const QString seq = keySequenceOf(row);
            if (!seq.isEmpty() && actual.count(seq) != 0) {
                clashes << QString::fromLatin1(row.id) + QStringLiteral(" : ") + seq;
            }
        }
        QVERIFY2(clashes.isEmpty(), qPrintable(clashes.join(QStringLiteral(" | "))));
    }

    // Dans l'autre sens : une ligne Key « Existing » non routée documente un raccourci qui existe
    // vraiment (QAction/QShortcut), sauf les flèches, lues par keyPressEvent du canevas.
    void existingKeyRowsMatchAnActualShortcut() {
        MainWindow window;
        const std::set<QString> actual = actualWindowShortcuts(window);
        const std::set<Qt::Key> canvasKeys = {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up,
                                              Qt::Key_Down};
        QStringList missing;
        int checked = 0;
        for (const Row& row : InteractionMap::rawRows()) {
            if (row.gesture.kind != GestureKind::Key || row.routed || row.planned ||
                row.origin != Origin::Existing) {
                continue;
            }
            if (canvasKeys.count(row.gesture.key) != 0) {
                continue;
            }
            const QString seq = keySequenceOf(row);
            if (seq.isEmpty()) {
                continue; // modificateur seul : pas une touche câblée
            }
            ++checked;
            if (actual.count(seq) == 0) {
                missing << QString::fromLatin1(row.id) + QStringLiteral(" : ") + seq;
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QStringLiteral(" | "))));
        QVERIFY(checked >= 5); // Suppr, Échap, F, Entrée, Retour arrière au moins
    }

    // Aide : trois entrées nommées, F1 libre (windowShortcutsAreUnique le garde déjà).
    void helpActionsHaveStableObjectNames() {
        MainWindow window;
        for (const char* name :
             {"action_help_quickstart", "action_help_gestures", "action_about"}) {
            QVERIFY2(window.findChild<QAction*>(QString::fromLatin1(name)) != nullptr, name);
        }
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::UiInvariantsTest)
#include "test_ui_invariants.moc"
