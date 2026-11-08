// SPDX-License-Identifier: Apache-2.0
#include "workspace_controller.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QtQml/qqml.h>

namespace openstitch::desktop_v2::studio {
namespace {

struct SpaceInfo {
    StageId id;
    const char* label;
    const char* shortcut;
};
constexpr std::array<SpaceInfo, 4> kSpaces{{
    {StageId::Image, "Image", "Ctrl+1"},
    {StageId::Segmentation, "Segmentation", "Ctrl+2"},
    {StageId::Vector, "Vecteurs", "Ctrl+3"},
    {StageId::Points, "Points", "Ctrl+4"},
}};

QString nameOf(StageId id) {
    return QString::fromLatin1(stageIdName(id).data());
}

int indexOfId(const QString& id) {
    for (std::size_t i = 0; i < kSpaces.size(); ++i) {
        if (id == nameOf(kSpaces[i].id)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::optional<StageState> parseState(const QString& state) {
    for (const auto s :
         {StageState::Empty, StageState::UpToDate, StageState::Stale, StageState::Blocked}) {
        if (state == QLatin1String(stageStateName(s).data())) {
            return s;
        }
    }
    return std::nullopt;
}

QString idOf(int index) {
    return nameOf(kSpaces[static_cast<std::size_t>(index)].id);
}

} // namespace

WorkspaceController::WorkspaceController(QObject* parent)
    : QObject(parent), current_(QStringLiteral("points")) {}

void WorkspaceController::setController(StudioController* controller) {
    if (controller_ == controller) {
        return;
    }
    if (controller_) {
        disconnect(controller_, nullptr, this, nullptr);
    }
    controller_ = controller;
    if (controller_) {
        connect(controller_, &StudioController::changed, this, [this] { invalidate(); });
        connect(controller_, &StudioController::documentReplaced, this, [this] {
            for (auto& published : published_) {
                published = Published{};
            }
            clearView();
            invalidate();
        });
        connect(controller_, &QObject::destroyed, this, [this] {
            controller_ = nullptr;
            invalidate();
        });
    }
    emit controllerChanged();
    invalidate();
}

void WorkspaceController::invalidate() {
    baseValid_ = false;
    if (refreshQueued_) {
        return;
    }
    refreshQueued_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            refreshQueued_ = false;
            emit spacesChanged();
        },
        Qt::QueuedConnection);
}

const std::array<StageStatus, 4>& WorkspaceController::base() const {
    if (!baseValid_) {
        StageInputs inputs;
        inputs.segmentationDirty = segmentationDirty_;
        inputs.dirtyReason = dirtyReason_.toStdString();
        std::shared_ptr<const document::Project> snapshot;
        if (controller_) {
            snapshot = controller_->session().snapshot();
        }
        base_ = snapshot ? computeStageStatus(*snapshot, inputs)
                         : computeStageStatus(document::Project{}, inputs);
        baseValid_ = true;
    }
    return base_;
}

StageState WorkspaceController::stateOf(int index) const {
    const auto& published = published_[static_cast<std::size_t>(index)];
    return published.state ? *published.state : base()[static_cast<std::size_t>(index)].state;
}

QString WorkspaceController::detailOf(int index) const {
    const auto& published = published_[static_cast<std::size_t>(index)];
    if (published.state) {
        return published.detail;
    }
    return QString::fromStdString(base()[static_cast<std::size_t>(index)].detail);
}

QVariantMap WorkspaceController::space(const QString& id) const {
    const int index = indexOfId(id);
    if (index < 0) {
        return {};
    }
    const auto& info = kSpaces[static_cast<std::size_t>(index)];
    const auto& published = published_[static_cast<std::size_t>(index)];
    return {{QStringLiteral("id"), id},
            {QStringLiteral("index"), index},
            {QStringLiteral("label"), QString::fromUtf8(info.label)},
            {QStringLiteral("shortcut"), QString::fromLatin1(info.shortcut)},
            {QStringLiteral("state"), QString::fromLatin1(stageStateName(stateOf(index)).data())},
            {QStringLiteral("detail"), detailOf(index)},
            {QStringLiteral("actionLabel"), published.actionLabel},
            {QStringLiteral("actionEnabled"), published.actionEnabled}};
}

QVariantList WorkspaceController::spaces() const {
    QVariantList out;
    for (int i = 0; i < static_cast<int>(kSpaces.size()); ++i) {
        out.push_back(space(idOf(i)));
    }
    return out;
}

int WorkspaceController::currentIndex() const {
    return indexOfId(current_);
}

int WorkspaceController::indexOf(const QString& id) const {
    return indexOfId(id);
}

void WorkspaceController::setCurrent(const QString& id) {
    if (indexOfId(id) < 0 || id == current_) {
        return;
    }
    current_ = id;
    emit currentChanged();
    emit spacesChanged(); // le bandeau dépend de l'espace courant
}

QVariantMap WorkspaceController::banner() const {
    const int index = currentIndex();
    QVariantMap out{{QStringLiteral("visible"), false}};
    if (index <= 0) {
        return out; // l'image n'a pas d'amont
    }
    const int upstream = index - 1;
    const auto state = stateOf(upstream);
    if (state != StageState::Stale && state != StageState::Blocked) {
        return out;
    }
    const auto& own = published_[static_cast<std::size_t>(index)];
    out[QStringLiteral("visible")] = true;
    out[QStringLiteral("stage")] = idOf(upstream);
    out[QStringLiteral("state")] = QString::fromLatin1(stageStateName(state).data());
    out[QStringLiteral("text")] = detailOf(upstream);
    out[QStringLiteral("actionLabel")] = own.actionLabel;
    out[QStringLiteral("actionEnabled")] = own.actionEnabled && state == StageState::Stale;
    return out;
}

void WorkspaceController::setStageStatus(const QString& id, const QString& state,
                                         const QString& detail) {
    const int index = indexOfId(id);
    const auto parsed = parseState(state);
    if (index < 0 || !parsed) {
        return;
    }
    auto& published = published_[static_cast<std::size_t>(index)];
    if (published.state == parsed && published.detail == detail) {
        return;
    }
    published.state = parsed;
    published.detail = detail;
    emit spacesChanged();
}

void WorkspaceController::clearStageStatus(const QString& id) {
    const int index = indexOfId(id);
    if (index < 0 || !published_[static_cast<std::size_t>(index)].state) {
        return;
    }
    published_[static_cast<std::size_t>(index)].state.reset();
    published_[static_cast<std::size_t>(index)].detail.clear();
    emit spacesChanged();
}

void WorkspaceController::setStageAction(const QString& id, const QString& label, bool enabled) {
    const int index = indexOfId(id);
    if (index < 0) {
        return;
    }
    auto& published = published_[static_cast<std::size_t>(index)];
    if (published.actionLabel == label && published.actionEnabled == enabled) {
        return;
    }
    published.actionLabel = label;
    published.actionEnabled = enabled;
    emit spacesChanged();
}

void WorkspaceController::requestUpdate(const QString& id) {
    if (indexOfId(id) >= 0) {
        emit updateRequested(id);
    }
}

void WorkspaceController::saveView(const QVariantMap& view) {
    view_ = view;
    view_.insert(QStringLiteral("selectionCount"),
                 controller_ ? static_cast<int>(controller_->session().selection().targets().size())
                             : 0);
    emit viewChanged();
}

void WorkspaceController::clearView() {
    if (view_.isEmpty()) {
        return;
    }
    view_.clear();
    emit viewChanged();
}

QVariantMap WorkspaceController::view() const {
    return view_;
}

void WorkspaceController::setSegmentationDirty(bool dirty) {
    if (segmentationDirty_ == dirty) {
        return;
    }
    segmentationDirty_ = dirty;
    invalidate();
}

void WorkspaceController::setDirtyReason(const QString& reason) {
    if (dirtyReason_ == reason) {
        return;
    }
    dirtyReason_ = reason;
    invalidate();
}

void WorkspaceController::setTreeMode(bool value) {
    if (treeMode_ != value) {
        treeMode_ = value;
        emit panelsChanged();
    }
}

void WorkspaceController::setEditOpen(bool value) {
    if (editOpen_ != value) {
        editOpen_ = value;
        emit panelsChanged();
    }
}

void registerWorkspaceTypes() {
    qmlRegisterType<WorkspaceController>("OpenStitch.Studio", 1, 0, "WorkspaceController");
}

} // namespace openstitch::desktop_v2::studio
