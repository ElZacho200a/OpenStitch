// SPDX-License-Identifier: Apache-2.0
// Infrastructure de traduction (HP-I18N-001) : résolution de la langue, préférence persistée,
// chargement du `.qm` embarqué et repli sur le français (langue source). Réglages redirigés vers
// un dossier temporaire : jamais le vrai fichier de l'utilisateur.

#include <QLocale>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "app_language.hpp"
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

} // namespace

class TestAppLanguage : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QCoreApplication::setOrganizationName(QStringLiteral("OpenStitch"));
        QCoreApplication::setApplicationName(QStringLiteral("OpenStitch Studio"));
    }

    // Laisse l'application en français (langue source) quel que soit le résultat du test.
    void cleanup() { installTranslators(QStringLiteral("fr")); }

    void systemLanguageFrenchResolvesToFrenchOtherwiseEnglish() {
        const QString system = QString::fromLatin1(kLanguageSystem);
        QCOMPARE(resolveLanguage(system, QLocale(QLocale::French, QLocale::France)),
                 QStringLiteral("fr"));
        QCOMPARE(resolveLanguage(system, QLocale(QLocale::German, QLocale::Germany)),
                 QStringLiteral("en"));
    }

    void explicitPreferenceWinsOverSystem() {
        QCOMPARE(resolveLanguage(QStringLiteral("en"), QLocale(QLocale::French)),
                 QStringLiteral("en"));
        QCOMPARE(resolveLanguage(QStringLiteral("fr"), QLocale(QLocale::German)),
                 QStringLiteral("fr"));
        // Valeur inconnue : retombe sur la langue du système.
        QCOMPARE(resolveLanguage(QStringLiteral("xx"), QLocale(QLocale::French)),
                 QStringLiteral("fr"));
    }

    void preferenceRoundTripsAndGarbageFallsBackToSystem() {
        SettingsSandbox box;
        QCOMPARE(loadLanguagePreference(), QString::fromLatin1(kLanguageSystem));
        saveLanguagePreference(QStringLiteral("en"));
        QCOMPARE(loadLanguagePreference(), QStringLiteral("en"));
        saveLanguagePreference(QStringLiteral("klingon"));
        QCOMPARE(loadLanguagePreference(), QString::fromLatin1(kLanguageSystem));
    }

    void englishCatalogTranslatesAndFrenchIsTheSource() {
        // Français = langue source : aucun catalogue, le texte du code est affiché tel quel.
        QVERIFY(!installTranslators(QStringLiteral("fr")));
        {
            SettingsDialog dialog;
            QCOMPARE(dialog.windowTitle(), QStringLiteral("Paramètres"));
        }

        // Anglais : le `.qm` embarqué est trouvé et appliqué à un dialogue construit ensuite.
        QVERIFY(installTranslators(QStringLiteral("en")));
        {
            SettingsDialog dialog;
            QCOMPARE(dialog.windowTitle(), QStringLiteral("Settings"));
        }

        // Retour au français : le catalogue anglais est bien retiré.
        QVERIFY(!installTranslators(QStringLiteral("fr")));
        SettingsDialog dialog;
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Paramètres"));
    }

    void unknownLanguageLoadsNothingWithoutError() {
        QVERIFY(!installTranslators(QStringLiteral("xx")));
    }

    void dialogReflectsAndReturnsLanguagePreference() {
        SettingsDialog dialog;
        QCOMPARE(dialog.language(), QString::fromLatin1(kLanguageSystem));
        dialog.setLanguage(QStringLiteral("en"));
        QCOMPARE(dialog.language(), QStringLiteral("en"));
        dialog.setLanguage(QStringLiteral("fr"));
        QCOMPARE(dialog.language(), QStringLiteral("fr"));
        // Valeur inconnue : retombe sur « système », sans plantage.
        dialog.setLanguage(QStringLiteral("xx"));
        QCOMPARE(dialog.language(), QString::fromLatin1(kLanguageSystem));
    }
};

QTEST_MAIN(TestAppLanguage)
#include "test_app_language.moc"
