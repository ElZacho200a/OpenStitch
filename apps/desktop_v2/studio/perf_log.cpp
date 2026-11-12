// SPDX-License-Identifier: Apache-2.0
#include "perf_log.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

namespace openstitch::desktop_v2::studio {

namespace {
QByteArray logPath() {
    return qgetenv(
        "OPENSTITCH_PERF_LOG"); // relue à chaque appel : activable à chaud, coût négligeable
}
QMutex& logMutex() {
    static QMutex mutex;
    return mutex;
}
} // namespace

bool perfLogEnabled() {
    return !logPath().isEmpty();
}

void perfLog(const QString& event, const QVariantMap& fields) {
    if (!perfLogEnabled()) {
        return;
    }
    QJsonObject object = QJsonObject::fromVariantMap(fields);
    object.insert(QStringLiteral("event"), event);
    object.insert(QStringLiteral("t"), QDateTime::currentMSecsSinceEpoch());
    const QMutexLocker lock(&logMutex());
    QFile file(QString::fromUtf8(logPath()));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        file.write("\n");
    }
}

} // namespace openstitch::desktop_v2::studio
