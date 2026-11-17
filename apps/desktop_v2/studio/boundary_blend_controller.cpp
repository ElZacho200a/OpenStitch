// SPDX-License-Identifier: Apache-2.0
#include "boundary_blend_controller.hpp"

#include <QVariantMap>

#include <cmath>
#include <utility>

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

} // namespace

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
    seenDoc_ = docRevision;
    seenSel_ = selRevision;
    refreshed_ = true;

    const auto snapshot = session.snapshot();
    std::vector<std::uint64_t> zones;
    for (const auto& t : session.selection().targets()) {
        if (t.kind == TargetKind::EmbroideryObject) {
            zones.push_back(t.id);
        }
    }
    primary_ = zones.empty() ? 0 : zones.back();
    other_ = zones.size() == 2 ? zones.front() : 0;
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

    if (snapshot && primary_ != 0) {
        if (const auto info = project_ops::boundary_blend_info(*snapshot, ObjectId{primary_})) {
            hasBlend_ = true;
            active_ = info->active;
            widthMm_ = mm(info->options.width_a);
            partnerWidthMm_ = mm(info->options.width_b);
            curve_ = static_cast<int>(info->options.curve);
            if (const auto* partner = snapshot->findEmbroidery(info->b)) {
                partnerName_ = QString::fromStdString(partner->name);
            }
            for (const auto& line : info->frontier) {
                QVariantList points;
                for (const Vec2um p : line.points) {
                    points.push_back(QVariantMap{{QStringLiteral("x"), mm(p.x)},
                                                 {QStringLiteral("y"), mm(p.y)}});
                }
                frontier_.push_back(points);
                frontierMm_ += geometry::polyline_length(line.points) / 1000.0;
            }
            if (other_ == 0) {
                other_ = info->b.value;
            }
        }
        if (zones.size() == 2) {
            const auto refusal = project_ops::boundary_blend_refusal(*snapshot, ObjectId{primary_},
                                                                     ObjectId{zones.front()});
            canCreate_ = !refusal.has_value();
            if (refusal) {
                refusal_ = QString::fromStdString(refusal->message);
            }
        }
    }
    emit changed();
}

QString BoundaryBlendController::summary() const {
    if (!hasBlend_) {
        return {};
    }
    const QString with = partnerName_.isEmpty() ? QStringLiteral("la zone voisine") : partnerName_;
    if (!active_) {
        return QStringLiteral("Fondu avec %1 inactif : les zones ne se touchent plus").arg(with);
    }
    return QStringLiteral("Fondu avec %1 · %2 mm ici, %3 mm chez la voisine · frontière %4 mm")
        .arg(with)
        .arg(widthMm_, 0, 'f', 1)
        .arg(partnerWidthMm_, 0, 'f', 1)
        .arg(frontierMm_, 0, 'f', 1);
}

bool BoundaryBlendController::apply(std::uint64_t a, std::uint64_t b, double widthA, double widthB,
                                    int curve) {
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

bool BoundaryBlendController::create(double widthMm, double partnerWidthMm, int curve) {
    refresh();
    std::uint64_t second = 0;
    for (const auto& t : studio_.session().selection().targets()) {
        if (t.kind == TargetKind::EmbroideryObject && t.id != primary_) {
            second = t.id;
        }
    }
    return apply(primary_, second, widthMm, partnerWidthMm, curve);
}

bool BoundaryBlendController::edit(double widthMm, double partnerWidthMm, int curve) {
    refresh();
    if (!hasBlend_) {
        say(false, QStringLiteral("Cette zone n'a pas de fondu à la frontière"));
        return false;
    }
    return apply(primary_, other_, widthMm, partnerWidthMm, curve);
}

bool BoundaryBlendController::remove() {
    refresh();
    const auto snapshot = studio_.session().snapshot();
    if (!snapshot || primary_ == 0) {
        say(false, QStringLiteral("Sélectionnez une zone qui porte un fondu"));
        return false;
    }
    auto cmd = project_ops::make_remove_boundary_blend_command(*snapshot, ObjectId{primary_});
    if (!cmd) {
        say(false, QString::fromStdString(cmd.error().message));
        return false;
    }
    studio_.session().execute(std::move(*cmd));
    say(true, QStringLiteral("Fondu à la frontière retiré"));
    return true;
}

} // namespace openstitch::desktop_v2::studio
