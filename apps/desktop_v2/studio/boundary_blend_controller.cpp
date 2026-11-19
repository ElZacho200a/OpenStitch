// SPDX-License-Identifier: Apache-2.0
#include "boundary_blend_controller.hpp"

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

#include "openstitch/document/boundary_blend.hpp"
#include "openstitch/geometry/polyline.hpp"
#include "openstitch/project_ops/boundary_blend.hpp"

namespace openstitch::desktop_v2::studio {

namespace {

constexpr int kCurveCount = 9; // document::DensityCurve::Linear .. ConcaveConvex

Micrometers um(double mm) {
    return Micrometers{static_cast<std::int32_t>(std::lround(mm * 1000.0))};
}

double mm(Micrometers v) {
    return static_cast<double>(v.value) / 1000.0;
}

QString rowsKey(document::BoundaryBlendRows rows) {
    switch (rows) {
    case document::BoundaryBlendRows::Shared:
        return QStringLiteral("shared");
    case document::BoundaryBlendRows::Rotating:
        return QStringLiteral("rotating");
    case document::BoundaryBlendRows::Interlaced:
        return QStringLiteral("interlaced");
    case document::BoundaryBlendRows::PerZone:
        break;
    }
    return QStringLiteral("per_zone");
}

} // namespace

QString BoundaryBlendController::rows() const {
    return rowsKey(rows_);
}

BoundaryBlendController::BoundaryBlendController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    // Recalcul seulement quand le document ou la sélection changent réellement (révisions) : les
    // signaux d'état (progression, statut) ne déclenchent aucun calcul de frontière.
    connect(&studio_, &StudioController::changed, this, [this] { refresh(); });
    connect(&studio_, &StudioController::selectionChanged, this, [this] { refresh(); });
    refresh();
}

void BoundaryBlendController::say(bool ok, const QString& text) {
    ok_ = ok;
    message_ = text;
    emit messageChanged();
}

void BoundaryBlendController::refresh() {
    auto& session = studio_.session();
    const std::uint64_t docRevision = session.revision();
    const std::uint64_t selRevision = session.selection().revision();
    if (docRevision == seenDoc_ && selRevision == seenSel_ && refreshed_) {
        return;
    }
    if (selRevision != seenSel_ || !refreshed_) {
        // Nouvelle sélection : le choix dans la liste ne survit pas (deux zones → la seconde, une
        // zone → le premier fondu).
        chosen_ = 0;
        rows_ = document::BoundaryBlendRows::PerZone;
    }
    seenDoc_ = docRevision;
    seenSel_ = selRevision;
    refreshed_ = true;
    rebuild();
}

void BoundaryBlendController::rebuild() {
    auto& session = studio_.session();
    const auto snapshot = session.snapshot();
    zones_.clear();
    for (const auto& t : session.selection().targets()) {
        if (t.kind == TargetKind::EmbroideryObject) {
            zones_.push_back(t.id);
        }
    }
    primary_ = zones_.empty() ? 0 : zones_.back();
    other_ = 0;
    canCreate_ = false;
    refusal_.clear();
    hasBlend_ = false;
    active_ = false;
    partnerName_.clear();
    widthMm_ = 2.5;
    partnerWidthMm_ = 2.5;
    curve_ = 0;
    frontierMm_ = 0.0;
    frontier_.clear();
    manual_.clear();
    blends_.clear();
    allFrontiers_.clear();
    const auto pendingRows = rows_;
    rows_ = document::BoundaryBlendRows::PerZone;

    std::vector<project_ops::BoundaryBlendInfo> infos;
    if (snapshot && primary_ != 0) {
        infos = project_ops::boundary_blend_infos(*snapshot, ObjectId{primary_});
    }
    const auto has = [&](std::uint64_t partner) {
        return std::any_of(infos.begin(), infos.end(),
                           [&](const auto& info) { return info.b.value == partner; });
    };
    // Couple courant : choix explicite dans la liste, sinon la seconde zone sélectionnée, sinon le
    // premier fondu de la zone principale.
    if (chosen_ != 0 && has(chosen_)) {
        other_ = chosen_;
    } else if (zones_.size() == 2) {
        other_ = zones_.front();
    } else if (!infos.empty()) {
        other_ = infos.front().b.value;
    }

    for (const auto& info : infos) {
        const bool current = info.b.value == other_;
        QString name;
        if (const auto* partner = snapshot->findEmbroidery(info.b)) {
            name = QString::fromStdString(partner->name);
        }
        QVariantList polylines;
        double length = 0.0;
        for (const auto& line : info.frontier) {
            QVariantList points;
            for (const Vec2um p : line.points) {
                points.push_back(
                    QVariantMap{{QStringLiteral("x"), mm(p.x)}, {QStringLiteral("y"), mm(p.y)}});
            }
            polylines.push_back(points);
            length += geometry::polyline_length(line.points) / 1000.0;
        }
        const QString partnerId = QString::number(info.b.value);
        if (current) {
            hasBlend_ = true;
            active_ = info.active;
            partnerName_ = name;
            widthMm_ = mm(info.options.width_a);
            partnerWidthMm_ = mm(info.options.width_b);
            curve_ = static_cast<int>(info.options.curve);
            manual_ = info.options.manual_frontier;
            rows_ = info.options.rows;
            frontier_ = polylines;
            frontierMm_ = length;
        }
        blends_.push_back(QVariantMap{
            {QStringLiteral("partnerId"), partnerId},
            {QStringLiteral("partnerName"), name},
            {QStringLiteral("widthMm"), mm(info.options.width_a)},
            {QStringLiteral("partnerWidthMm"), mm(info.options.width_b)},
            {QStringLiteral("curve"), static_cast<int>(info.options.curve)},
            {QStringLiteral("rows"), rowsKey(info.options.rows)},
            {QStringLiteral("active"), info.active},
            {QStringLiteral("manualFrontier"), !info.options.manual_frontier.empty()},
            {QStringLiteral("frontierMm"), length},
            {QStringLiteral("current"), current},
        });
        allFrontiers_.push_back(QVariantMap{{QStringLiteral("partnerId"), partnerId},
                                            {QStringLiteral("current"), current},
                                            {QStringLiteral("polylines"), polylines}});
    }
    if (!hasBlend_ && zones_.size() == 2 && other_ == zones_.front()) {
        // Deux zones sans fondu entre elles : le choix de rangées en attente reste valable.
        rows_ = pendingRows;
    }
    if (snapshot && zones_.size() == 2) {
        const auto refusal = project_ops::boundary_blend_refusal(*snapshot, ObjectId{primary_},
                                                                 ObjectId{zones_.front()});
        canCreate_ = !refusal.has_value();
        if (refusal) {
            refusal_ = QString::fromStdString(refusal->message);
        }
    }
    emit changed();
}

bool BoundaryBlendController::selectBlend(const QString& partnerId) {
    refresh();
    bool ok = false;
    const std::uint64_t partner = partnerId.toULongLong(&ok);
    const bool known =
        ok && std::any_of(blends_.begin(), blends_.end(), [&](const QVariant& entry) {
            return entry.toMap().value(QStringLiteral("partnerId")).toString() == partnerId;
        });
    if (!known) {
        say(false, QStringLiteral("Cette zone ne fond pas avec cette voisine"));
        return false;
    }
    if (partner == other_ && chosen_ == partner) {
        return true;
    }
    chosen_ = partner;
    rebuild();
    return true;
}

QString BoundaryBlendController::summary() const {
    if (!hasBlend_) {
        return {};
    }
    const QString with = partnerName_.isEmpty() ? QStringLiteral("la zone voisine") : partnerName_;
    if (!active_) {
        return QStringLiteral("Fondu avec %1 inactif : les zones ne se touchent plus").arg(with);
    }
    QString text =
        QStringLiteral("Fondu avec %1 · %2 mm ici, %3 mm chez la voisine · frontière %4 mm")
            .arg(with)
            .arg(widthMm_, 0, 'f', 1)
            .arg(partnerWidthMm_, 0, 'f', 1)
            .arg(frontierMm_, 0, 'f', 1);
    if (rows_ == document::BoundaryBlendRows::Shared) {
        text += QStringLiteral(" · angle commun");
    } else if (rows_ == document::BoundaryBlendRows::Rotating) {
        text += QStringLiteral(" · rangées qui tournent");
    } else if (rows_ == document::BoundaryBlendRows::Interlaced) {
        text += QStringLiteral(" · entrelacé");
    }
    return text;
}

bool BoundaryBlendController::apply(std::uint64_t a, std::uint64_t b, double widthA, double widthB,
                                    int curve, const std::vector<Vec2um>& frontier) {
    if (a == 0 || b == 0) {
        say(false, QStringLiteral("Sélectionnez deux zones remplies voisines"));
        return false;
    }
    if (curve < 0 || curve >= kCurveCount) {
        say(false, QStringLiteral("Profil de transition inconnu"));
        return false;
    }
    const auto snapshot = studio_.session().snapshot();
    if (!snapshot) {
        return false;
    }
    project_ops::BoundaryBlendOptions options;
    options.width_a = um(widthA);
    options.width_b = um(widthB);
    options.curve = static_cast<document::DensityCurve>(curve);
    options.manual_frontier = frontier;
    options.rows = rows_;
    auto cmd =
        project_ops::make_boundary_blend_command(*snapshot, ObjectId{a}, ObjectId{b}, options);
    if (!cmd) {
        say(false, QString::fromStdString(cmd.error().message));
        return false;
    }
    studio_.session().execute(std::move(*cmd)); // UN pas d'annulation
    say(true, QStringLiteral("Fondu à la frontière appliqué"));
    return true;
}

std::uint64_t BoundaryBlendController::secondSelected() const {
    std::uint64_t second = 0;
    for (const auto& t : studio_.session().selection().targets()) {
        if (t.kind == TargetKind::EmbroideryObject && t.id != primary_) {
            second = t.id;
        }
    }
    return second;
}

bool BoundaryBlendController::create(double widthMm, double partnerWidthMm, int curve) {
    refresh();
    return apply(primary_, secondSelected(), widthMm, partnerWidthMm, curve, {});
}

bool BoundaryBlendController::edit(double widthMm, double partnerWidthMm, int curve) {
    refresh();
    if (!hasBlend_) {
        say(false, QStringLiteral("Cette zone n'a pas de fondu à la frontière"));
        return false;
    }
    return apply(primary_, other_, widthMm, partnerWidthMm, curve, manual_);
}

bool BoundaryBlendController::setRows(const QString& rows) {
    refresh();
    document::BoundaryBlendRows value{};
    if (rows == QLatin1String("per_zone")) {
        value = document::BoundaryBlendRows::PerZone;
    } else if (rows == QLatin1String("shared")) {
        value = document::BoundaryBlendRows::Shared;
    } else if (rows == QLatin1String("rotating")) {
        value = document::BoundaryBlendRows::Rotating;
    } else if (rows == QLatin1String("interlaced")) {
        value = document::BoundaryBlendRows::Interlaced;
    } else {
        say(false, QStringLiteral("Sens des rangées inconnu"));
        return false;
    }
    if (value == rows_) {
        return true;
    }
    if (!hasBlend_) {
        rows_ = value; // choix en attente pour la prochaine création
        emit changed();
        return true;
    }
    const auto previous = rows_;
    rows_ = value;
    if (!apply(primary_, other_, widthMm_, partnerWidthMm_, curve_, manual_)) {
        rows_ = previous;
        emit changed();
        return false;
    }
    return true;
}

bool BoundaryBlendController::setFrontier(const QVariantList& pointsMm) {
    refresh();
    std::vector<Vec2um> frontier;
    for (const auto& value : pointsMm) {
        const auto map = value.toMap();
        bool okX = false;
        bool okY = false;
        const double x = map.value(QStringLiteral("x")).toDouble(&okX);
        const double y = map.value(QStringLiteral("y")).toDouble(&okY);
        if (!okX || !okY || !std::isfinite(x) || !std::isfinite(y)) {
            say(false, QStringLiteral("Point de frontière invalide"));
            return false;
        }
        frontier.push_back(Vec2um{um(x), um(y)});
    }
    if (frontier.size() < 2) {
        say(false, QStringLiteral("Tracez la frontière avec au moins deux points"));
        return false;
    }
    if (hasBlend_) {
        return apply(primary_, other_, widthMm_, partnerWidthMm_, curve_, frontier);
    }
    return apply(primary_, secondSelected(), widthMm_, partnerWidthMm_, curve_, frontier);
}

bool BoundaryBlendController::clearFrontier() {
    refresh();
    if (!hasBlend_ || manual_.empty()) {
        say(false, QStringLiteral("Ce fondu n'a pas de frontière tracée"));
        return false;
    }
    return apply(primary_, other_, widthMm_, partnerWidthMm_, curve_, {});
}

bool BoundaryBlendController::remove() {
    refresh();
    return removeBlend(currentPartner());
}

bool BoundaryBlendController::removeBlend(const QString& partnerId) {
    refresh();
    const auto snapshot = studio_.session().snapshot();
    bool ok = false;
    const std::uint64_t partner = partnerId.toULongLong(&ok);
    const auto* zone =
        snapshot && primary_ != 0 ? snapshot->findEmbroidery(ObjectId{primary_}) : nullptr;
    if (zone == nullptr || !ok || partner == 0 ||
        document::boundary_blend_link(*zone, ObjectId{partner}) == nullptr) {
        say(false, QStringLiteral("Sélectionnez une zone qui porte un fondu"));
        return false;
    }
    auto cmd = project_ops::make_remove_boundary_blend_command(*snapshot, ObjectId{primary_},
                                                               ObjectId{partner});
    if (!cmd) {
        say(false, QString::fromStdString(cmd.error().message));
        return false;
    }
    studio_.session().execute(std::move(*cmd)); // UN pas d'annulation
    say(true, QStringLiteral("Fondu à la frontière retiré"));
    return true;
}

} // namespace openstitch::desktop_v2::studio
