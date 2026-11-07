// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QObject>
#include <QVariantList>

#include "studio_controller.hpp"

namespace openstitch::desktop_v2::studio {

// Read-only presentation of DocumentSession's command history; owns no commands or snapshots.
class HistoryController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(qlonglong savedIndex READ savedIndex NOTIFY changed)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
public:
    explicit HistoryController(StudioController& studio, QObject* parent = nullptr);
    ~HistoryController() override;
    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] qlonglong savedIndex() const;
    [[nodiscard]] qulonglong revision() const;
    [[nodiscard]] bool dirty() const;
    Q_INVOKABLE bool jumpTo(int index);
signals:
    void changed();

private:
    DocumentSession& session_;
    DocumentSession::Token token_{};
};

} // namespace openstitch::desktop_v2::studio
