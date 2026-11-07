// SPDX-License-Identifier: Apache-2.0
#include "fade_controller.hpp"

namespace openstitch::desktop_v2::studio {
namespace {
QString q(const std::string& s) {
    return QString::fromStdString(s);
}

QVariantMap pt(const PointMm& p) {
    return {{QStringLiteral("x"), p.x}, {QStringLiteral("y"), p.y}};
}

QColor colour(const std::array<std::uint8_t, 3>& c) {
    return QColor(c[0], c[1], c[2]);
}
} // namespace

FadeController::FadeController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    connect(&studio_, &StudioController::changed, this, &FadeController::refresh);
    refresh();
}

FadeController::~FadeController() = default;

void FadeController::refresh() {
    std::uint64_t id = 0;
    int count = 0;
    for (const auto& target : studio_.session().selection().targets()) {
        if (target.kind == TargetKind::EmbroideryObject) {
            id = target.id;
            ++count;
        }
    }
    id_ = count == 1 ? id : 0;
    settings_.reset();
    axis_.reset();
    incompat_.clear();
    if (id_ != 0) {
        const auto& project = studio_.session().project();
        settings_ = readFade(project, id_);
        if (settings_) {
            axis_ = fadeAxisView(project, id_);
        } else {
            incompat_ = q(fadeIncompatibility(project, id_));
        }
    } else {
        incompat_ = QStringLiteral("Sélectionnez un seul remplissage.");
    }
    emit changed();
}

QString FadeController::objectId() const {
    return id_ == 0 ? QString() : QString::number(id_);
}

QColor FadeController::foreground() const {
    return settings_ ? colour(settings_->foreground) : QColor();
}

QColor FadeController::background() const {
    return axis_ ? colour(axis_->background) : QColor();
}

QString FadeController::summary() const {
    return axis_ ? q(fadeSummary(*axis_)) : QString();
}

QVariantMap FadeController::axisMap(const std::optional<FadeAxisView>& v) {
    if (!v) {
        return {};
    }
    return {{QStringLiteral("start"), pt(v->start)},
            {QStringLiteral("end"), pt(v->end)},
            {QStringLiteral("centre"), pt(v->centre)},
            {QStringLiteral("rotate"), pt(v->rotate)},
            {QStringLiteral("zoneStart"), v->zoneStart},
            {QStringLiteral("zoneEnd"), v->zoneEnd},
            {QStringLiteral("curve"), static_cast<int>(v->curve)}};
}

QVariantList FadeController::curves() const {
    QVariantList list;
    for (int i = 0; i <= static_cast<int>(FadeCurve::EaseOut); ++i) {
        list.push_back(
            QVariantMap{{QStringLiteral("id"), i},
                        {QStringLiteral("label"), q(fadeCurveLabel(static_cast<FadeCurve>(i)))}});
    }
    return list;
}

QVariantList FadeController::curveProfile() const {
    QVariantList list;
    for (const double v : fadeCurveProfile(static_cast<FadeCurve>(curve()), 32)) {
        list.push_back(v);
    }
    return list;
}

bool FadeController::refuse(const QString& why) {
    ok_ = false;
    message_ = why;
    emit messageChanged();
    return false;
}

bool FadeController::report(const ServiceResult& result, const QString& success) {
    ok_ = result.ok;
    message_ = result.ok ? success : q(result.message);
    emit messageChanged();
    return result.ok;
}

namespace {
FadeSettings settingsOf(const QColor& c, bool complementary) {
    return FadeSettings{{static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                         static_cast<std::uint8_t>(c.blue())},
                        complementary};
}
} // namespace

bool FadeController::create(const QColor& foreground, bool complementary) {
    if (id_ == 0) {
        return refuse(QStringLiteral("Sélectionnez un seul remplissage."));
    }
    return report(createFade(studio_.session(), id_, settingsOf(foreground, complementary)),
                  QStringLiteral("Fondu créé."));
}

bool FadeController::edit(const QColor& foreground, bool complementary) {
    if (id_ == 0) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    return report(editFade(studio_.session(), id_, settingsOf(foreground, complementary)),
                  QStringLiteral("Fondu modifié."));
}

bool FadeController::remove() {
    if (id_ == 0) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    return report(removeFade(studio_.session(), id_), QStringLiteral("Fondu supprimé."));
}

QVariantMap FadeController::previewHandle(int handle, double xMm, double yMm,
                                          bool symmetric) const {
    if (id_ == 0 || handle < 0 || handle > 3) {
        return {};
    }
    return axisMap(previewFadeDrag(studio_.session().project(), id_,
                                   static_cast<FadeHandle>(handle), PointMm{xMm, yMm}, symmetric));
}

bool FadeController::commitHandle(int handle, double xMm, double yMm, bool symmetric) {
    if (id_ == 0 || handle < 0 || handle > 3) {
        return refuse(QStringLiteral("Aucun fondu à manipuler."));
    }
    return report(dragFadeHandle(studio_.session(), id_, static_cast<FadeHandle>(handle),
                                 PointMm{xMm, yMm}, symmetric),
                  QStringLiteral("Axe du fondu modifié."));
}

bool FadeController::setCurve(int curve) {
    if (id_ == 0 || curve < 0 || curve > static_cast<int>(FadeCurve::EaseOut)) {
        return refuse(QStringLiteral("Courbe inconnue ou aucun fondu."));
    }
    return report(setFadeCurve(studio_.session(), id_, static_cast<FadeCurve>(curve)),
                  QStringLiteral("Courbe de transition modifiée."));
}

} // namespace openstitch::desktop_v2::studio
