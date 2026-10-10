// SPDX-License-Identifier: Apache-2.0
#include <QApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

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

    QApplication::setWindowIcon(openstitch::desktop::icons::appIcon());

    // Traductions de Qt (boutons standard Annuler / Oui / Non, menus contextuels des champs,
    // « Show Details… ») : français. Cherchées dans le dossier de Qt puis dans « translations »
    // à côté de l'exécutable (déployé par windeployqt) ; absentes, l'interface reste
    // fonctionnelle avec les libellés anglais de Qt (aucune erreur).
    QTranslator qtTranslator;
    const QLocale french(QLocale::French);
    for (const QString& dir :
         {QLibraryInfo::path(QLibraryInfo::TranslationsPath),
          QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations"))}) {
        if (qtTranslator.load(french, QStringLiteral("qtbase"), QStringLiteral("_"), dir)) {
            QApplication::installTranslator(&qtTranslator);
            break;
        }
    }

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
