// SPDX-License-Identifier: Apache-2.0
// Réglages persistants (EditSettings), migration du registre vers l'INI et dialogue Paramètres.
// Tout est redirigé vers un dossier temporaire : jamais le vrai fichier de l'utilisateur.

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "app_settings.hpp"
#include "settings_dialog.hpp"

using namespace openstitch::desktop;

namespace {

struct SettingsSandbox {
    QTemporaryDir dir;
    QSettings::Format savedFormat = QSettings::defaultFormat();
    SettingsSandbox() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dir.path());
    }
    ~SettingsSandbox() { QSettings::setDefaultFormat(savedFormat); }
};

QSettings sandboxSettings() {
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("OpenStitch"),
                     QStringLiteral("OpenStitch Studio"));
}

} // namespace

class TestAppSettings : public QObject {
    Q_OBJECT
private slots:
    void defaultsWhenFileIsEmpty() {
        SettingsSandbox box;
        QCOMPARE(loadEditSettings(), EditSettings::defaults());
    }

    void saveThenLoadRoundTrips() {
        SettingsSandbox box;
        EditSettings s;
        s.unionGapMm = 0.12;
        s.knifeWidthMm = 0.04;
        s.knifeMinLengthMm = 2.0;
        s.snapNodesOnDrag = true;
        s.regionMapOpacity = 0.5;
        s.longPressMs = 700;
        saveEditSettings(s);
        QCOMPARE(loadEditSettings(), s);
        QVERIFY(QFileInfo::exists(settingsFilePath()));
    }

    void outOfRangeAndGarbageValuesAreClamped() {
        SettingsSandbox box;
        {
            QSettings s = sandboxSettings();
            s.setValue(QStringLiteral("edit/unionGapMm"), 99.0);
            s.setValue(QStringLiteral("edit/knifeWidthMm"), QStringLiteral("abc"));
            s.setValue(QStringLiteral("edit/knifeMinLengthMm"), -3.0);
            s.setValue(QStringLiteral("ui/regionMapOpacity"), 5.0);
            s.setValue(QStringLiteral("navigation/longPressMs"), 10);
        }
        const EditSettings loaded = loadEditSettings();
        QCOMPARE(loaded.unionGapMm, 0.5);
        QCOMPARE(loaded.knifeWidthMm, EditSettings::defaults().knifeWidthMm);
        QCOMPARE(loaded.knifeMinLengthMm, 0.1);
        QCOMPARE(loaded.regionMapOpacity, 1.0);
        QCOMPARE(loaded.longPressMs, 300);
    }

    void migrationCopiesIntoEmptyTargetOnly() {
        QTemporaryDir dirFrom;
        QTemporaryDir dirTo;
        QSettings from(dirFrom.filePath(QStringLiteral("from.ini")), QSettings::IniFormat);
        from.setValue(QStringLiteral("ui/lastImportDir"), QStringLiteral("C:/images"));
        from.setValue(QStringLiteral("edit/snapNodesOnDrag"), true);
        QSettings to(dirTo.filePath(QStringLiteral("to.ini")), QSettings::IniFormat);

        QCOMPARE(migrateSettings(from, to), 2);
        QCOMPARE(to.value(QStringLiteral("ui/lastImportDir")).toString(),
                 QStringLiteral("C:/images"));

        // Cible déjà remplie : rien n'est écrasé.
        from.setValue(QStringLiteral("ui/lastImportDir"), QStringLiteral("D:/autre"));
        QCOMPARE(migrateSettings(from, to), 0);
        QCOMPARE(to.value(QStringLiteral("ui/lastImportDir")).toString(),
                 QStringLiteral("C:/images"));
    }

    void dialogReflectsAndReturnsSettings() {
        SettingsDialog dialog;
        EditSettings s;
        s.unionGapMm = 0.2;
        s.snapNodesOnDrag = true;
        s.regionMapOpacity = 0.6;
        s.longPressMs = 800;
        dialog.setSettings(s);
        const EditSettings back = dialog.settings();
        QCOMPARE(back.unionGapMm, 0.2);
        QVERIFY(back.snapNodesOnDrag);
        QCOMPARE(back.regionMapOpacity, 0.6);
        QCOMPARE(back.longPressMs, 800);

        dialog.setSettings(EditSettings::defaults());
        QCOMPARE(dialog.settings(), EditSettings::defaults());
    }
};

QTEST_MAIN(TestAppSettings)
#include "test_app_settings.moc"
