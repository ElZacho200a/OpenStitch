// SPDX-License-Identifier: Apache-2.0
#include "crash_report_ticket.hpp"

#include <QRegularExpression>
#include <QSysInfo>
#include <QUrlQuery>

#include "openstitch/core/app_info.hpp"

namespace openstitch::desktop {

namespace {
constexpr int kMaxBodyChars = 4000; // l'URL encodée reste sous les limites des navigateurs
}

QString redactPaths(const QString& text) {
    static const QRegularExpression winPath(QStringLiteral(R"([A-Za-z]:[\\/][^\s"'<>|*?]*)"));
    static const QRegularExpression uncPath(QStringLiteral(R"(\\\\[^\s"'<>|*?]+)"));
    static const QRegularExpression posixPath(
        QStringLiteral(R"((?<![\w:/])/(?:[\w.\-~ ]+/)+[\w.\-~]*)"));
    static const QRegularExpression fileName(
        QStringLiteral(R"([\w\-. ]+\.(?:osp|dst|pes|jef|exp|svg|dxf|png|jpe?g|bmp|tiff?)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    QString out = text;
    const QString mask = QStringLiteral("<chemin>");
    out.replace(winPath, mask);
    out.replace(uncPath, mask);
    out.replace(posixPath, mask);
    out.replace(fileName, mask);
    return out;
}

QString crashReportBody(const QStringList& logTail, bool hasDump) {
    QStringList lines;
    lines << QStringLiteral("## Rapport de plantage") << QString()
          << QStringLiteral("- Application : %1 %2")
                 .arg(QString::fromUtf8(openstitch::kAppName),
                      QString::fromUtf8(openstitch::kAppVersion))
          << QStringLiteral("- Système : %1 (%2)")
                 .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture())
          << QStringLiteral("- Qt : %1").arg(QString::fromLatin1(qVersion()))
          << QStringLiteral("- Minidump : %1")
                 .arg(hasDump ? QStringLiteral("présent, à joindre à ce ticket si vous le "
                                               "souhaitez (non joint automatiquement)")
                              : QStringLiteral("aucun"))
          << QString() << QStringLiteral("## Que faisiez-vous ?") << QString()
          << QStringLiteral("(à compléter)") << QString()
          << QStringLiteral("## Dernières lignes du journal (chemins masqués)") << QString()
          << QStringLiteral("```");
    for (const QString& l : logTail) {
        lines << redactPaths(l);
    }
    lines << QStringLiteral("```");
    QString body = lines.join(QLatin1Char('\n'));
    if (body.size() > kMaxBodyChars) {
        body = body.left(kMaxBodyChars) + QStringLiteral("\n```\n(tronqué)");
    }
    return body;
}

QUrl crashReportUrl(const QStringList& logTail, bool hasDump) {
    QUrl url(QStringLiteral("https://github.com/ElZacho200a/OpenStitch/issues/new"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("title"), QStringLiteral("Plantage de %1 %2")
                                                .arg(QString::fromUtf8(openstitch::kAppName),
                                                     QString::fromUtf8(openstitch::kAppVersion)));
    q.addQueryItem(QStringLiteral("body"), crashReportBody(logTail, hasDump));
    q.addQueryItem(QStringLiteral("labels"), QStringLiteral("crash"));
    url.setQuery(q);
    return url;
}

} // namespace openstitch::desktop
