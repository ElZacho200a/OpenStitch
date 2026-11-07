// SPDX-License-Identifier: Apache-2.0
#include "point_edit_controller.hpp"

#include <algorithm>
#include <cmath>

namespace openstitch::desktop_v2::studio {
namespace {
QString q(const std::string& s) {
    return QString::fromStdString(s);
}
} // namespace

PointEditController::PointEditController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    connect(&studio_, &StudioController::changed, this, &PointEditController::refresh);
    refresh();
}

PointEditController::~PointEditController() = default;

void PointEditController::setActive(bool value) {
    if (active_ == value) {
        return;
    }
    active_ = value;
    refresh();
}

void PointEditController::refresh() {
    const auto before = id_;
    std::uint64_t id = 0;
    int count = 0;
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind == TargetKind::EmbroideryObject) {
            id = target.id;
            ++count;
        }
    }
    id_ = count == 1 ? id : 0;
    view_ = PointView{};
    if (active_ && id_ != 0) {
        if (auto points = stitchPoints(studio_.session().project(), id_)) {
            view_ = std::move(*points);
        }
    }
    if (before != id_) {
        selected_.clear();
    } else {
        std::set<std::size_t> kept;
        for (const auto& p : view_.points) {
            if (selected_.contains(p.index)) {
                kept.insert(p.index);
            }
        }
        selected_ = std::move(kept);
    }
    emit changed();
}

QString PointEditController::objectId() const {
    return id_ == 0 ? QString() : QString::number(id_);
}

QString PointEditController::state() const {
    if (!active_ || id_ == 0 || view_.points.empty()) {
        return QStringLiteral("none");
    }
    switch (view_.state) {
    case PointEditState::Clean:
        return QStringLiteral("clean");
    case PointEditState::ManuallyEdited:
        return QStringLiteral("edited");
    case PointEditState::Dirty:
        return QStringLiteral("dirty");
    }
    return QStringLiteral("none");
}

QVariantList PointEditController::selectedPoints() const {
    QVariantList list;
    for (const auto i : selected_) {
        list.push_back(static_cast<qulonglong>(i));
    }
    return list;
}

QVariantMap PointEditController::pointInfo(int index) const {
    for (const auto& p : view_.points) {
        if (static_cast<int>(p.index) == index) {
            return {{QStringLiteral("index"), index},
                    {QStringLiteral("x"), p.pos.x},
                    {QStringLiteral("y"), p.pos.y},
                    {QStringLiteral("jump"), p.jump},
                    {QStringLiteral("deleted"), p.deleted},
                    {QStringLiteral("edited"), p.edited},
                    {QStringLiteral("trim"), p.trimAfter},
                    {QStringLiteral("movable"), p.movable},
                    {QStringLiteral("selected"), selected_.contains(p.index)}};
        }
    }
    return {};
}

int PointEditController::pointAt(double xMm, double yMm, double tolMm) const {
    int best = -1;
    double bestD = tolMm * tolMm;
    for (const auto& p : view_.points) {
        const double dx = p.pos.x - xMm;
        const double dy = p.pos.y - yMm;
        const double d = dx * dx + dy * dy;
        if (d <= bestD) {
            bestD = d;
            best = static_cast<int>(p.index);
        }
    }
    return best;
}

QVariantList PointEditController::pointsIn(double x0, double y0, double x1, double y1,
                                           int limit) const {
    QVariantList list;
    const double lx = std::min(x0, x1), hx = std::max(x0, x1);
    const double ly = std::min(y0, y1), hy = std::max(y0, y1);
    for (const auto& p : view_.points) {
        if (p.pos.x >= lx && p.pos.x <= hx && p.pos.y >= ly && p.pos.y <= hy) {
            list.push_back(pointInfo(static_cast<int>(p.index)));
            if (limit > 0 && list.size() >= limit) {
                break;
            }
        }
    }
    return list;
}

bool PointEditController::refuse(const QString& why) {
    ok_ = false;
    message_ = why;
    emit messageChanged();
    return false;
}

bool PointEditController::report(const ServiceResult& result, const QString& success) {
    ok_ = result.ok;
    message_ = result.ok ? success : q(result.message);
    emit messageChanged();
    return result.ok;
}

bool PointEditController::selectPoint(int index, bool additive) {
    const auto it = std::find_if(view_.points.begin(), view_.points.end(),
                                 [&](const auto& p) { return static_cast<int>(p.index) == index; });
    if (it == view_.points.end()) {
        return false;
    }
    if (!additive) {
        selected_.clear();
        selected_.insert(it->index);
    } else if (selected_.erase(it->index) == 0) {
        selected_.insert(it->index);
    }
    emit changed();
    return true;
}

bool PointEditController::selectRange(int fromIndex, int toIndex) {
    const auto lo = std::min(fromIndex, toIndex);
    const auto hi = std::max(fromIndex, toIndex);
    std::set<std::size_t> range;
    for (const auto& p : view_.points) {
        const int i = static_cast<int>(p.index);
        if (i >= lo && i <= hi) {
            range.insert(p.index);
        }
    }
    if (range.empty()) {
        return false;
    }
    selected_ = std::move(range);
    emit changed();
    return true;
}

int PointEditController::selectAllPoints() {
    selected_.clear();
    for (const auto& p : view_.points) {
        selected_.insert(p.index);
    }
    emit changed();
    return static_cast<int>(selected_.size());
}

void PointEditController::clearPointSelection() {
    selected_.clear();
    emit changed();
}

std::vector<std::size_t> PointEditController::selectedIndices() const {
    return {selected_.begin(), selected_.end()};
}

bool PointEditController::moveSelected(double dxMm, double dyMm) {
    if (id_ == 0 || selected_.empty()) {
        return refuse(QStringLiteral("Sélectionnez au moins un point."));
    }
    std::vector<std::pair<std::size_t, PointMm>> moves;
    for (const auto& p : view_.points) {
        if (selected_.contains(p.index)) {
            moves.emplace_back(p.index, PointMm{p.pos.x + dxMm, p.pos.y + dyMm});
        }
    }
    return report(moveStitchPoints(studio_.session(), id_, moves),
                  QStringLiteral("Points déplacés."));
}

bool PointEditController::movePoint(int index, double xMm, double yMm) {
    if (id_ == 0) {
        return refuse(QStringLiteral("Aucun objet à retoucher."));
    }
    return report(moveStitchPoints(studio_.session(), id_,
                                   {{static_cast<std::size_t>(index), PointMm{xMm, yMm}}}),
                  QStringLiteral("Point déplacé."));
}

bool PointEditController::insertAfter(int index, double xMm, double yMm) {
    if (id_ == 0) {
        return refuse(QStringLiteral("Aucun objet à retoucher."));
    }
    selected_.clear(); // les indices suivants changent avec l'insertion
    return report(insertStitchPoint(studio_.session(), id_, static_cast<std::size_t>(index),
                                    PointMm{xMm, yMm}),
                  QStringLiteral("Point inséré."));
}

bool PointEditController::deleteSelected() {
    if (id_ == 0 || selected_.empty()) {
        return refuse(QStringLiteral("Sélectionnez au moins un point."));
    }
    return report(deleteStitchPoints(studio_.session(), id_, selectedIndices()),
                  QStringLiteral("Points supprimés."));
}

bool PointEditController::restoreSelected() {
    if (id_ == 0 || selected_.empty()) {
        return refuse(QStringLiteral("Sélectionnez au moins un point."));
    }
    return report(restoreStitchPoints(studio_.session(), id_, selectedIndices()),
                  QStringLiteral("Points restaurés."));
}

bool PointEditController::setJump(bool jump) {
    if (id_ == 0 || selected_.empty()) {
        return refuse(QStringLiteral("Sélectionnez au moins un point."));
    }
    return report(setStitchPointsJump(studio_.session(), id_, selectedIndices(), jump),
                  jump ? QStringLiteral("Points passés en saut.")
                       : QStringLiteral("Points passés en points cousus."));
}

bool PointEditController::setTrim(bool trim) {
    if (id_ == 0 || selected_.empty()) {
        return refuse(QStringLiteral("Sélectionnez au moins un point."));
    }
    return report(setStitchPointsTrim(studio_.session(), id_, selectedIndices(), trim),
                  trim ? QStringLiteral("Coupe du fil posée.")
                       : QStringLiteral("Coupe du fil retirée."));
}

bool PointEditController::discardEdits() {
    if (id_ == 0) {
        return refuse(QStringLiteral("Aucun objet à retoucher."));
    }
    selected_.clear();
    return report(discardStitchEdits(studio_.session(), id_),
                  QStringLiteral("Retouches effacées."));
}

} // namespace openstitch::desktop_v2::studio
