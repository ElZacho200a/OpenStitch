// SPDX-License-Identifier: Apache-2.0
// Invariants généraux de la fenêtre principale (audit UI du 2026-10-06,
// docs/ui-audit-2026-10.md). Ces tests ne dépendent d'aucune fonctionnalité : ils
// verrouillent des propriétés de câblage qui doivent rester vraies quand l'interface
// évolue. Un défaut connu est déclaré avec QEXPECT_FAIL(Continue) : la suite reste
// verte, et dès que le défaut est corrigé le test échoue en « XPASS » jusqu'à ce que
// le marqueur soit retiré — il ne peut donc pas rester périmé en silence.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSettings>
#include <QShortcut>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <map>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>

#include "app_theme.hpp"
#include "interaction_map.hpp"
#include "main_window.hpp"
#include "properties_panel.hpp"

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

// Accès à `QObject::isSignalConnected` (protégé) : une action dont aucun signal
// n'est connecté ne fait rien quand on la déclenche. Technique usuelle de test ; la
// classe dérivée n'est jamais instanciée.
struct Peek : QObject {
    static bool connected(const QObject* object, const char* signalSignature) {
        const QMetaMethod method = object->metaObject()->method(
            object->metaObject()->indexOfSignal(QMetaObject::normalizedSignature(signalSignature)));
        return static_cast<const Peek*>(object)->isSignalConnected(method);
    }
};

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

    void themeStylesKeepHoverSeparateFromSelection() {
        auto& theme = AppTheme::instance();
        const auto originalMode = theme.mode();
        const auto restore = qScopeGuard([&] { theme.setMode(originalMode); });
        theme.applyToApp(*qApp);
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            theme.setMode(mode);
            const QString qss = qApp->styleSheet();
            QVERIFY(qss.contains(QStringLiteral("QListView::item:hover:!selected")));
            QVERIFY(qss.contains(QStringLiteral("QListWidget::item:hover:!selected")));
            QVERIFY(!qss.contains(QStringLiteral("QListView::item:hover,")));
        }
    }

    void primaryButtonsHaveAnExplicitKeyboardFocusStyle() {
        auto& theme = AppTheme::instance();
        const auto originalMode = theme.mode();
        const auto restore = qScopeGuard([&] { theme.setMode(originalMode); });
        theme.applyToApp(*qApp);
        for (const auto mode : {ThemeMode::Light, ThemeMode::Dark}) {
            theme.setMode(mode);
            QVERIFY(
                qApp->styleSheet().contains(QStringLiteral("QPushButton[primary=\"true\"]:focus")));
        }
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

    // Une barre d'outils masquée doit pouvoir se rouvrir depuis un menu (comme les docks).
    void everyToolbarIsReopenableFromAMenu() {
        MainWindow window;
        QList<QAction*> menuActions;
        for (const QAction* top : window.menuBar()->actions()) {
            if (top->menu() != nullptr) {
                collectMenuActions(top->menu(), menuActions);
            }
        }
        const auto bars = window.findChildren<QToolBar*>();
        QVERIFY(!bars.isEmpty());
        QStringList unreachable;
        for (const QToolBar* bar : bars) {
            if (!menuActions.contains(bar->toggleViewAction())) {
                unreachable << bar->windowTitle();
            }
        }
        QVERIFY2(unreachable.isEmpty(), qPrintable(unreachable.join(QStringLiteral(", "))));
    }

    // Une action visible (avec un libellé) dont aucun signal n'est connecté ne fait rien
    // quand on la déclenche : câblage oublié.
    void everyLabelledActionIsConnected() {
        MainWindow window;
        QStringList dead;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->isSeparator() || action->menu() != nullptr || action->text().isEmpty()) {
                continue;
            }
            if (!Peek::connected(action, "triggered(bool)") &&
                !Peek::connected(action, "toggled(bool)")) {
                dead << action->objectName() + QStringLiteral(" \"") + action->text() +
                            QStringLiteral("\"");
            }
        }
        QVERIFY2(dead.isEmpty(), qPrintable(dead.join(QStringLiteral(" | "))));
    }

    // Aucune action de menu ou de barre n'est orpheline : chacune est atteignable
    // (présente dans un menu ou une barre) ou porte un raccourci.
    void noActionIsOrphaned() {
        MainWindow window;
        QStringList orphans;
        for (QAction* action : window.findChildren<QAction*>()) {
            if (action->isSeparator() || action->menu() != nullptr || action->text().isEmpty()) {
                continue;
            }
            if (action->associatedObjects().isEmpty() && action->shortcuts().isEmpty()) {
                orphans << action->objectName() + QStringLiteral(" \"") + action->text() +
                               QStringLiteral("\"");
            }
        }
        QVERIFY2(orphans.isEmpty(), qPrintable(orphans.join(QStringLiteral(" | "))));
    }

    // Inspecteur : chaque widget de saisie, pour chaque type d'objet, émet quelque chose
    // (un champ qui ne modifie rien est un réglage fantôme).
    void everyInspectorWidgetIsConnectedForEveryObjectType() {
        const std::vector<std::pair<QString, document::StitchParams>> types{
            {QStringLiteral("running"), document::RunningStitchParams{}},
            {QStringLiteral("tatami"), document::TatamiParams{}},
            {QStringLiteral("satin"), document::SatinParams{}},
            {QStringLiteral("directional"), document::DirectionalFillParams{}},
            {QStringLiteral("autosatin"), document::AutoSatinParams{}}};
        QStringList dead;
        int total = 0;
        for (const auto& [type, params] : types) {
            PropertiesPanel panel;
            document::EmbroideryObject object;
            object.id = ObjectId{7};
            object.params = params;
            panel.showEmbroidery(object);
            for (QWidget* w : panel.findChildren<QWidget*>()) {
                if (w->parentWidget() != nullptr &&
                    qobject_cast<QAbstractSpinBox*>(w->parentWidget()) != nullptr) {
                    continue; // QLineEdit interne d'une boîte numérique
                }
                bool ok = true;
                if (qobject_cast<QAbstractButton*>(w) != nullptr) {
                    ok = Peek::connected(w, "clicked(bool)") ||
                         Peek::connected(w, "toggled(bool)") || Peek::connected(w, "pressed()");
                } else if (qobject_cast<QSpinBox*>(w) != nullptr) {
                    ok = Peek::connected(w, "valueChanged(int)");
                } else if (qobject_cast<QDoubleSpinBox*>(w) != nullptr) {
                    ok = Peek::connected(w, "valueChanged(double)");
                } else if (qobject_cast<QComboBox*>(w) != nullptr) {
                    ok = Peek::connected(w, "currentIndexChanged(int)") ||
                         Peek::connected(w, "activated(int)") ||
                         Peek::connected(w, "currentTextChanged(QString)");
                } else {
                    continue;
                }
                ++total;
                if (!ok) {
                    dead << type + QStringLiteral(" / ") + w->objectName();
                }
            }
        }
        QVERIFY(total > 60); // 87 widgets de saisie au 2026-10-12 : le scan voit bien le panneau
        QVERIFY2(dead.isEmpty(), qPrintable(dead.join(QStringLiteral(" | "))));
    }

    // Les boutons à raccourci (Alt+Haut/Bas de l'ordre de couture) ne doivent pas
    // doubler un raccourci de la fenêtre : windowShortcutsAreUnique ne regarde que les actions.
    void buttonShortcutsDoNotClashWithWindowShortcuts() {
        MainWindow window;
        const std::set<QString> actual = actualWindowShortcuts(window);
        QStringList clashes;
        int buttons = 0;
        std::map<QString, int> seen;
        for (const QAbstractButton* button : window.findChildren<QAbstractButton*>()) {
            if (button->shortcut().isEmpty()) {
                continue;
            }
            ++buttons;
            const QString key = button->shortcut().toString();
            if (actual.count(key) != 0 || ++seen[key] > 1) {
                clashes << button->objectName() + QStringLiteral(" : ") + key;
            }
        }
        QVERIFY(buttons >= 2); // button_orderUp / button_orderDown
        QVERIFY2(clashes.isEmpty(), qPrintable(clashes.join(QStringLiteral(" | "))));
    }

    // Une infobulle qui annonce une touche « (D) » doit dire vrai : sinon l'aide ment.
    void tooltipKeyHintsMatchTheRealShortcut() {
        MainWindow window;
        static const QRegularExpression hint(
            QStringLiteral("\\(([A-Za-z0-9+ ]{1,14})\\)\\s*\\.?$"));
        const auto normalise = [](QString s) {
            s.remove(QLatin1Char(' '));
            s.replace(QStringLiteral("Maj"), QStringLiteral("Shift"));
            return s.toLower();
        };
        QStringList wrong;
        for (const QAction* action : window.findChildren<QAction*>()) {
            const auto m = hint.match(action->toolTip());
            if (!m.hasMatch() || action->shortcuts().isEmpty() ||
                m.captured(1).contains(QStringLiteral("clic"))) {
                continue;
            }
            bool matches = false;
            for (const QKeySequence& s : action->shortcuts()) {
                matches =
                    matches ||
                    normalise(s.toString(QKeySequence::PortableText)) == normalise(m.captured(1)) ||
                    normalise(s.toString(QKeySequence::NativeText)) == normalise(m.captured(1));
            }
            if (!matches) {
                wrong << action->objectName() + QStringLiteral(" : infobulle (") + m.captured(1) +
                             QStringLiteral(") / réel ") +
                             action->shortcut().toString(QKeySequence::PortableText);
            }
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QStringLiteral(" | "))));
    }

    // Dette connue (audit 2026-10-12) : ~80 actions sans objectName, donc introuvables par
    // les tests et instables pour les technologies d'assistance. XFAIL : dès que tout est
    // nommé, ce test échoue en XPASS et le marqueur doit être retiré.
    void everyMenuActionHasAnObjectName() {
        MainWindow window;
        QList<QAction*> menuActions;
        for (const QAction* top : window.menuBar()->actions()) {
            if (top->menu() != nullptr) {
                collectMenuActions(top->menu(), menuActions);
            }
        }
        QStringList unnamed;
        for (const QAction* action : menuActions) {
            if (action->objectName().isEmpty() && !action->text().isEmpty() &&
                action->parent() == &window) {
                unnamed << action->text();
            }
        }
        QEXPECT_FAIL("", "actions de menu sans objectName (audit 2026-10-12)", Continue);
        QVERIFY2(unnamed.isEmpty(),
                 qPrintable(QString::number(unnamed.size()) + QStringLiteral(" : ") +
                            unnamed.join(QStringLiteral(" | "))));
    }

private:
    QTemporaryDir settingsDir_;
};

} // namespace openstitch::desktop

QTEST_MAIN(openstitch::desktop::UiInvariantsTest)
#include "test_ui_invariants.moc"
