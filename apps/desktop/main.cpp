// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QLocale>
#include <QTimer>

#include "app_language.hpp"
#include "app_settings.hpp"
#include "app_theme.hpp"
#include "main_window.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/core/log.hpp"
#include "ui_icons.hpp"

int main(int argc, char** argv) {
    openstitch::init_logging();

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("OpenStitch"));
    QApplication::setApplicationName(QString::fromUtf8(openstitch::kAppName));
    QApplication::setApplicationVersion(QString::fromUtf8(openstitch::kAppVersion));

    // Réglages dans un fichier INI (conservés d'une instance à l'autre) ; anciennes clés du
    // registre importées une fois. Avant toute lecture de QSettings (thème compris).
    openstitch::desktop::configureSettingsStorage();

    QApplication::setWindowIcon(openstitch::desktop::icons::appIcon());

    // Langue de l'interface (préférence ou langue du système), appliquée au démarrage : Qt
    // (boutons standard) et traduction de l'application (HP-I18N-001).
    openstitch::desktop::installTranslators(openstitch::desktop::resolveLanguage(
        openstitch::desktop::loadLanguagePreference(), QLocale::system()));

    // Design system : palette + feuille de style centralisées (thème persistant).
    openstitch::desktop::AppTheme::instance().applyToApp(app);

    openstitch::desktop::MainWindow window;
    window.show();
    // Fichier passé en argument (double-clic sur un .osp, « Ouvrir avec ») : ouvert une fois la
    // fenêtre affichée, par les mêmes gardes que les menus.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) {
        const QString path = args.at(1);
        QTimer::singleShot(0, &window, [&window, path] { window.openPath(path); });
    }
    return QApplication::exec();
}
