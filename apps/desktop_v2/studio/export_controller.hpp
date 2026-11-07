// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "advanced_export.hpp"
#include "file_controller.hpp"
#include "studio_controller.hpp"
#include "task_manager.hpp"

namespace openstitch::desktop_v2::studio {

// Adaptateur uniquement : conversion des options QML et travail sur instantané via TaskManager.
class ExportController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    ExportController(StudioController& studio, FileController& files, QObject* parent = nullptr);
    ~ExportController() override;
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] bool lastOk() const { return ok_; }
    [[nodiscard]] QString message() const { return message_; }
    Q_INVOKABLE void openDialog() { emit dialogRequested(); }
    Q_INVOKABLE QVariantMap options() const;
    Q_INVOKABLE void exportMachine(const QUrl& url, const QVariantMap& values = {});
    Q_INVOKABLE void exportAgain();
    Q_INVOKABLE void exportAppliqueCut(const QUrl& url);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void dialogRequested();

private:
    void say(bool ok, const QString& message);
    StudioController& studio_;
    FileController& files_;
    TaskManager tasks_;
    QTimer pump_;
    bool busy_{false};
    bool ok_{true};
    QString message_;
    QUrl lastFile_;
    QVariantMap lastValues_;
};

} // namespace openstitch::desktop_v2::studio
