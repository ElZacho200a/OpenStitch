// SPDX-License-Identifier: Apache-2.0
#include "app_language.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStringList>
#include <QTranslator>

namespace openstitch::desktop {

namespace {

constexpr const char* kLanguageKey = "ui/language";

// Les traducteurs doivent vivre autant que l'application : un par rôle, réutilisés si
// `installTranslators` est rappelée (tests, futur changement de langue à chaud).
QTranslator& qtTranslator() {
    static QTranslator translator;
    return translator;
}

QTranslator& appTranslator() {
    static QTranslator translator;
    return translator;
}

} // namespace

const QStringList& supportedLanguages() {
    static const QStringList languages{QStringLiteral("fr"), QStringLiteral("en")};
    return languages;
}

QString loadLanguagePreference() {
    const QString value = QSettings().value(QLatin1String(kLanguageKey)).toString();
    return supportedLanguages().contains(value) ? value : QString::fromLatin1(kLanguageSystem);
}

void saveLanguagePreference(const QString& preference) {
    QSettings().setValue(QLatin1String(kLanguageKey), preference);
}

QString resolveLanguage(const QString& preference, const QLocale& system) {
    if (supportedLanguages().contains(preference)) {
        return preference;
    }
    return system.language() == QLocale::French ? QStringLiteral("fr") : QStringLiteral("en");
}

bool installTranslators(const QString& language) {
    QCoreApplication::removeTranslator(&qtTranslator());
    QCoreApplication::removeTranslator(&appTranslator());

    // Traductions de Qt (boutons standard Annuler / Oui / Non, menus contextuels des champs).
    // Cherchées dans le dossier de Qt puis dans « translations » à côté de l'exécutable (déployé
    // par windeployqt) ; absentes, l'interface reste fonctionnelle avec les libellés de Qt.
    const QLocale locale(language);
    for (const QString& dir :
         {QLibraryInfo::path(QLibraryInfo::TranslationsPath),
          QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations"))}) {
        if (qtTranslator().load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), dir)) {
            QCoreApplication::installTranslator(&qtTranslator());
            break;
        }
    }

    if (language == QLatin1String("fr")) {
        return false; // langue source : rien à charger
    }
    if (appTranslator().load(QStringLiteral(":/i18n/openstitch_%1.qm").arg(language))) {
        QCoreApplication::installTranslator(&appTranslator());
        return true;
    }
    return false;
}

} // namespace openstitch::desktop
