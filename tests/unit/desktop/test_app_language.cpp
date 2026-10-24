// SPDX-License-Identifier: Apache-2.0
// Infrastructure de traduction (HP-I18N-001) : résolution de la langue, préférence persistée,
// chargement du `.qm` embarqué et repli sur le français (langue source). Réglages redirigés vers
// un dossier temporaire : jamais le vrai fichier de l'utilisateur.

#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>
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

    // HP-I18N-002 : le catalogue anglais source (`.ts`) est complet. Aucune entrée « unfinished »,
    // aucune traduction vide, et aucune chaîne accentuée du français recopiée telle quelle.
    void englishCatalogIsComplete() {
        const QString path = QFileInfo(QStringLiteral(__FILE__)).absolutePath() +
                             QStringLiteral("/../../../apps/desktop/i18n/openstitch_en.ts");
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        QXmlStreamReader xml(&file);
        int messages = 0;
        QStringList problems;
        QString source;
        QString context;
        bool inName = false;
        bool inSource = false;
        bool inTranslation = false;
        bool unfinished = false;
        QStringList forms;
        QString current;
        bool inForm = false;
        bool numerus = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const QStringView n = xml.name();
                if (n == u"name")
                    inName = true;
                else if (n == u"message") {
                    numerus = xml.attributes().value(QStringLiteral("numerus")) == u"yes";
                    forms.clear();
                    source.clear();
                } else if (n == u"source")
                    inSource = true;
                else if (n == u"translation") {
                    inTranslation = true;
                    unfinished = xml.attributes().hasAttribute(QStringLiteral("type"));
                    current.clear();
                } else if (n == u"numerusform") {
                    inForm = true;
                    current.clear();
                }
            } else if (xml.isCharacters()) {
                if (inName)
                    context = xml.text().toString();
                else if (inSource)
                    source += xml.text();
                else if (inForm || (inTranslation && !numerus))
                    current += xml.text();
            } else if (xml.isEndElement()) {
                const QStringView n = xml.name();
                if (n == u"name")
                    inName = false;
                else if (n == u"source")
                    inSource = false;
                else if (n == u"numerusform") {
                    forms << current;
                    inForm = false;
                } else if (n == u"translation") {
                    inTranslation = false;
                    if (!numerus)
                        forms << current;
                    ++messages;
                    if (unfinished)
                        problems << QStringLiteral("unfinished: ") + context +
                                        QStringLiteral(" / ") + source;
                    static const QRegularExpression accent(QStringLiteral("[éèàùçêîôûœ«»]"));
                    for (const QString& form : forms) {
                        if (form.isEmpty() && !source.isEmpty())
                            problems << QStringLiteral("empty: ") + source;
                        else if (QString(form).remove(QStringLiteral("Bézier")).contains(accent))
                            problems << QStringLiteral("french: ") + form;
                    }
                }
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(messages > 2000);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.mid(0, 20).join(QLatin1Char('\n'))));
    }

    void englishCatalogTranslatesMainWindowStrings() {
        QVERIFY(installTranslators(QStringLiteral("en")));
        QCOMPARE(QCoreApplication::translate("openstitch::desktop::MainWindow", "&Fichier"),
                 QStringLiteral("&File"));
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
