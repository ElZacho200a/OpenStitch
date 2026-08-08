// SPDX-License-Identifier: Apache-2.0
#include "wsl_path_converter.hpp"

#include <QRegularExpression>

namespace openstitch::desktop {

QString WslPathConverter::toWsl(const QString& windowsPath) {
    if (windowsPath.startsWith(QLatin1Char('/'))) {
        return windowsPath;  // déjà un chemin WSL (ou relatif)
    }
    QString path = windowsPath;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (path.size() >= 2 && path.at(1) == QLatin1Char(':')) {
        const QChar drive = path.at(0).toLower();
        return QStringLiteral("/mnt/%1%2").arg(drive).arg(path.mid(2));
    }
    return path;
}

QString WslPathConverter::toWindows(const QString& wslPath) {
    static const QRegularExpression pattern(QStringLiteral(R"(^/mnt/([a-zA-Z])(/.*)?$)"));
    const auto match = pattern.match(wslPath);
    if (!match.hasMatch()) {
        return wslPath;
    }
    const QString drive = match.captured(1).toUpper();
    QString rest = match.captured(2);
    rest.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return drive + QStringLiteral(":") + rest;
}

}  // namespace openstitch::desktop
