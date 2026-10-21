// SPDX-License-Identifier: Apache-2.0
#include <QAction>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QTest>

#include "command_palette_dialog.hpp"
#include "main_window.hpp"

using openstitch::desktop::CommandPaletteDialog;
using openstitch::desktop::MainWindow;

class CommandPaletteTest final : public QObject {
    Q_OBJECT

private slots:
    void filtersByCategoryAndSynonym() {
        QWidget owner;
        QMenuBar menus(&owner);
        auto* file = menus.addMenu(QStringLiteral("&Fichier"));
        auto* open = file->addAction(QStringLiteral("&Ouvrir un projet…"));
        open->setShortcut(QKeySequence::Open);
        file->addAction(QStringLiteral("Enregistrer le projet"));
        auto* other = menus.addMenu(QStringLiteral("&Broderie"));
        other->addAction(QStringLiteral("Créer un point droit"));

        CommandPaletteDialog palette(&menus, &owner);
        auto* search = palette.findChild<QLineEdit*>(QStringLiteral("commandPaletteSearch"));
        auto* list = palette.findChild<QListWidget*>(QStringLiteral("commandPaletteResults"));
        QVERIFY(search != nullptr);
        QVERIFY(list != nullptr);
        QCOMPARE(list->count(), 3);

        search->setText(QStringLiteral("fichier ouvrir"));
        QCOMPARE(list->count(), 1);
        QVERIFY(list->currentItem()->text().contains(QStringLiteral("Ouvrir")));
        const QString shortcut = open->shortcut().toString(QKeySequence::NativeText);
        QVERIFY(list->currentItem()->text().contains(shortcut));

        search->setText(QStringLiteral("charger"));
        QCOMPARE(list->count(), 1);
        search->setText(QStringLiteral("broderie couture"));
        QCOMPARE(list->count(), 1);
        search->setText(QStringLiteral("absent"));
        QCOMPARE(list->count(), 0);
    }

    void enterTriggersOriginalActionOnce() {
        QWidget owner;
        QMenuBar menus(&owner);
        auto* file = menus.addMenu(QStringLiteral("Fichier"));
        QAction* action = file->addAction(QStringLiteral("Ouvrir"));
        QSignalSpy spy(action, &QAction::triggered);

        CommandPaletteDialog palette(&menus, &owner);
        auto* search = palette.findChild<QLineEdit*>(QStringLiteral("commandPaletteSearch"));
        QVERIFY(search != nullptr);
        palette.show();
        search->setText(QStringLiteral("ouvrir"));
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_COMPARE(spy.count(), 1);
        QVERIFY(!palette.isVisible());
    }

    void disabledActionStaysUntriggered() {
        QWidget owner;
        QMenuBar menus(&owner);
        auto* edit = menus.addMenu(QStringLiteral("Édition"));
        QAction* action = edit->addAction(QStringLiteral("Supprimer sélection"));
        action->setEnabled(false);
        action->setStatusTip(QStringLiteral("Sélectionnez d'abord une forme."));
        QSignalSpy spy(action, &QAction::triggered);

        CommandPaletteDialog palette(&menus, &owner);
        auto* search = palette.findChild<QLineEdit*>(QStringLiteral("commandPaletteSearch"));
        auto* list = palette.findChild<QListWidget*>(QStringLiteral("commandPaletteResults"));
        search->setText(QStringLiteral("supprimer"));
        QCOMPARE(list->count(), 1);
        QVERIFY(list->currentItem()->text().contains(QStringLiteral("indisponible")));
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(palette.findChild<QLabel*>(QStringLiteral("commandPaletteFeedback"))->text(),
                 QStringLiteral("Sélectionnez d'abord une forme."));
    }

    void windowMenuOpensPalette() {
        MainWindow window;
        auto* action = window.findChild<QAction*>(QStringLiteral("action_commandPalette"));
        QVERIFY(action != nullptr);
        QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_K));
        action->trigger();
        auto* palette = window.findChild<QDialog*>(QStringLiteral("commandPaletteDialog"));
        QVERIFY(palette != nullptr);
        QVERIFY(palette->isVisible());
        auto* list = palette->findChild<QListWidget*>(QStringLiteral("commandPaletteResults"));
        QVERIFY(list != nullptr);
        QVERIFY(list->count() >= 20);
        palette->close();
    }
};

QTEST_MAIN(CommandPaletteTest)
#include "test_command_palette.moc"
