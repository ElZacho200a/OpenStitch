// SPDX-License-Identifier: Apache-2.0
#include <QApplication>

#include "app_theme.hpp"
#include "main_window.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/core/log.hpp"

int main(int argc, char** argv) {
    openstitch::init_logging();

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("OpenStitch"));
    QApplication::setApplicationName(QString::fromUtf8(openstitch::kAppName));
    QApplication::setApplicationVersion(QString::fromUtf8(openstitch::kAppVersion));

    // Identité visuelle propre : Fusion n'est que la base neutre (jamais le style natif
    // de Windows), forcée AVANT le thème car setStyle() réinitialise la palette ;
    // `-style` / QT_STYLE_OVERRIDE de l'utilisateur sont ignorés.
    if (QApplication::setStyle(QStringLiteral("Fusion")) == nullptr) {
        qWarning("OpenStitch: style Fusion indisponible, rendu de base conserve");
    }

    // Design system : palette + feuille de style centralisées (thème persistant).
    openstitch::desktop::AppTheme::instance().applyToApp(app);

    openstitch::desktop::MainWindow window;
    window.show();
    return QApplication::exec();
}
