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

    // Design system : palette + feuille de style centralisées (thème persistant).
    openstitch::desktop::AppTheme::instance().applyToApp(app);

    openstitch::desktop::MainWindow window;
    window.show();
    return QApplication::exec();
}
