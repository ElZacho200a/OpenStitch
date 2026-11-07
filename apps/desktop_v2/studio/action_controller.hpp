// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "action_catalog.hpp"
#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

// Qt adapter over the declarative action catalog (same definitions as the CLI, Claude and the
// Widgets shell): it lists actions, describes their fields so QML can build a form, validates and
// runs them through DocumentSession (one undo step). No engine logic lives here.
class ActionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList actions READ actions NOTIFY actionsChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY resultChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY resultChanged)
public:
    explicit ActionController(StudioController& studio, QObject* parent = nullptr);
    ~ActionController() override;
    // [{id, label, group, needsSelection, hasFields, enabled}] filtered by an accent-insensitive
    // query ("" = all).
    Q_INVOKABLE [[nodiscard]] QVariantList search(const QString& query) const;
    [[nodiscard]] QVariantList actions() const { return search({}); }
    // [{key, label, kind, min, max, value, unit, choices:[{value,label}], required}]
    Q_INVOKABLE [[nodiscard]] QVariantList fields(const QString& id) const;
    // Validates then runs; returns "" on success, otherwise a readable refusal.
    Q_INVOKABLE QString run(const QString& id, const QVariantMap& values);
    [[nodiscard]] QString lastMessage() const { return message_; }
    [[nodiscard]] bool lastOk() const { return ok_; }
signals:
    void actionsChanged();
    void resultChanged();

private:
    [[nodiscard]] const ActionDef* find(const QString& id) const;
    [[nodiscard]] ActionContext context() const;
    QString finish(const ActionDef& def, bool ok, const QString& message);
    StudioController& studio_;
    DocumentSession::Token token_{};
    QString message_;
    bool ok_{true};
};

} // namespace openstitch::desktop_v2::studio
