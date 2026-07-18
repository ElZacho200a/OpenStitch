// SPDX-License-Identifier: Apache-2.0
#include <QApplication>

#include "main_window.hpp"
#include "openstitch/core/app_info.hpp"
#include "openstitch/core/log.hpp"

int main(int argc, char** argv) {
    openstitch::init_logging();

    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromUtf8(openstitch::kAppName));
    QApplication::setApplicationVersion(QString::fromUtf8(openstitch::kAppVersion));

    openstitch::desktop::MainWindow window;
    window.show();
    return QApplication::exec();
}
