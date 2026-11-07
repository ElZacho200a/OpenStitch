// SPDX-License-Identifier: Apache-2.0
#include "history_controller.hpp"

#include <QVariantMap>

namespace openstitch::desktop_v2::studio {

HistoryController::HistoryController(StudioController& studio, QObject* parent)
    : QObject(parent), session_(studio.session()) {
    token_ = session_.subscribe([this](ChangeKind) { emit changed(); });
}

HistoryController::~HistoryController() {
    session_.unsubscribe(token_);
}

QVariantList HistoryController::entries() const {
    auto names = session_.undoNames();
    const auto redo = session_.redoNames();
    names.insert(names.end(), redo.begin(), redo.end());
    QVariantList result;
    for (std::size_t index = 0; index <= names.size(); ++index) {
        result.push_back(QVariantMap{
            {QStringLiteral("index"), static_cast<int>(index)},
            {QStringLiteral("label"),
             index == 0 ? tr("État initial") : QString::fromStdString(names[index - 1])},
            {QStringLiteral("current"), index == session_.historyPosition()},
            {QStringLiteral("saved"),
             static_cast<long long>(index) == session_.savedHistoryPosition()},
            {QStringLiteral("future"), index > session_.historyPosition()}});
    }
    return result;
}

int HistoryController::currentIndex() const {
    return static_cast<int>(session_.historyPosition());
}

qlonglong HistoryController::savedIndex() const {
    return session_.savedHistoryPosition();
}

qulonglong HistoryController::revision() const {
    return session_.revision();
}

bool HistoryController::dirty() const {
    return session_.dirty();
}

bool HistoryController::jumpTo(int index) {
    return index >= 0 && session_.navigateHistory(static_cast<std::size_t>(index));
}

} // namespace openstitch::desktop_v2::studio
