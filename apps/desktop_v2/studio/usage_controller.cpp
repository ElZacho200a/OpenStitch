// SPDX-License-Identifier: Apache-2.0
#include "usage_controller.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QEvent>
#include <QShortcutEvent>
#include <QStandardPaths>

#include "action_controller.hpp"
#include "tool_controller.hpp"

namespace openstitch::desktop_v2::studio {

UsageController::UsageController(UsageLog& log, QObject* parent) : QObject(parent), log_(log) {
    lockedByEnvironment_ = usageLogDisabledByEnvironment([](std::string_view key) {
        return QString::fromLocal8Bit(qgetenv(std::string(key).c_str())).toStdString();
    });
}

void UsageController::setEnabled(bool on) {
    if (on == log_.enabled() || (on && lockedByEnvironment_)) {
        return;
    }
    if (!usageLogSetMarker(log_.dir(), on)) {
        return;
    }
    log_.setEnabled(on);
    emit enabledChanged();
}

QString UsageController::folder() const {
    return QString::fromStdString(log_.currentFile().parent_path().generic_string());
}

void UsageController::click(const QString& id) {
    record(QStringLiteral("click"), id);
}

void UsageController::track(const QString& kind, const QString& id, const QString& detail) {
    record(kind, id, detail);
}

bool UsageController::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Shortcut) {
        const auto* shortcut = static_cast<QShortcutEvent*>(event);
        record(QStringLiteral("key"), shortcut->key().toString(QKeySequence::PortableText));
    }
    return QObject::eventFilter(watched, event);
}

void UsageController::record(const QString& kind, const QString& id, const QString& detail) {
    if (!log_.enabled()) {
        return;
    }
    log_.record(kind.toStdString(), id.toStdString(), detail.toStdString());
}

void UsageController::observe(ActionController& actions, ToolController& tools) {
    if (auto* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    connect(&actions, &ActionController::ran, this, [this](const QString& id, bool ok) {
        record(QStringLiteral("action"), id, ok ? QString() : QStringLiteral("refused"));
    });
    connect(&tools, &ToolController::activeToolChanged, this,
            [this, &tools] { record(QStringLiteral("tool"), tools.activeTool()); });
}

UsageLogOptions UsageController::defaultOptions(const QString& version) {
    const GetEnv getenv = [](std::string_view key) {
        return QString::fromLocal8Bit(qgetenv(std::string(key).c_str())).toStdString();
    };
    UsageLogOptions options;
    options.dir = defaultUsageDir(
        getenv,
        std::filesystem::path(
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation).toStdString()));
    options.app = "studio";
    options.version = version.toStdString();
    options.enabled = usageLogEnabled(getenv, options.dir);
    return options;
}

} // namespace openstitch::desktop_v2::studio
