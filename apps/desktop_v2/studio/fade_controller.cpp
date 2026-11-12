// SPDX-License-Identifier: Apache-2.0
#include "fade_controller.hpp"

#include <algorithm>

#include "openstitch/project_ops/gradient_fill.hpp"

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
    const auto& session = studio_.session();
    const auto& targets = session.selection().targets();
    if (lastSeenRevision_ == session.revision() && lastSeenSelection_ == targets) {
        return; // aucun axe de fondu à recalculer pour un simple statut
    }
    lastSeenRevision_ = session.revision();
    lastSeenSelection_ = targets;
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
    multiSettings_.reset();
    multiCurve_ = 0;
    axis_.reset();
    incompat_.clear();
    if (id_ != 0) {
        const auto& project = studio_.session().project();
        settings_ = readFade(project, id_);
        if (settings_) {
            axis_ = fadeAxisView(project, id_);
        } else {
            multiSettings_ = readMultiFade(project, id_);
            if (multiSettings_) {
                const auto spec =
                    project_ops::describe_multicolor_gradient_fill(project, ObjectId{id_});
                if (spec) {
                    multiCurve_ = static_cast<int>(spec->gradient.curve);
                }
            } else {
                incompat_ = q(fadeIncompatibility(project, id_));
            }
        }
    } else {
        incompat_ = QStringLiteral("Sélectionnez un seul remplissage.");
    }
    emit changed();
}

QString FadeController::objectId() const {
    return id_ == 0 ? QString() : QString::number(id_);
}

bool FadeController::satinTarget() const {
    if (id_ == 0) {
        return false;
    }
    const auto* layer = studio_.session().project().findEmbroidery(ObjectId{id_});
    return layer != nullptr && layer->is_satin();
}

QColor FadeController::foreground() const {
    if (multiSettings_ && !multiSettings_->colors.empty()) {
        return colour(multiSettings_->colors.back());
    }
    return settings_ ? colour(settings_->foreground) : QColor();
}

QColor FadeController::background() const {
    if (multiSettings_ && !multiSettings_->colors.empty()) {
        return colour(multiSettings_->colors.front());
    }
    if (axis_) {
        return colour(axis_->background);
    }
    if (id_ != 0) {
        if (const auto* object = studio_.session().project().findEmbroidery(ObjectId{id_})) {
            return colour(object->rgb);
        }
    }
    return QColor();
}

QString FadeController::summary() const {
    if (multiSettings_) {
        return QStringLiteral("Camaïeu de %1 fils. Couches cousues séparément, "
                              "avec profil de densité par couleur.")
            .arg(multiSettings_->colors.size());
    }
    return axis_ ? q(fadeSummary(*axis_)) : QString();
}

int FadeController::curve() const {
    return multiSettings_ ? multiCurve_ : axis_ ? static_cast<int>(axis_->curve) : 0;
}

QVariantList FadeController::colors() const {
    QVariantList result;
    if (multiSettings_) {
        for (const auto& rgb : multiSettings_->colors) {
            result.push_back(colour(rgb).name());
        }
    } else if (settings_) {
        result.push_back(background().name());
        result.push_back(foreground().name());
    } else if (id_ != 0) {
        result.push_back(background().name());
        result.push_back(QStringLiteral("#c81e1e"));
    }
    return result;
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
    return createWithTexture(foreground, complementary, 0);
}

bool FadeController::createWithTexture(const QColor& foreground, bool complementary, int texture) {
    if (id_ == 0) {
        return refuse(QStringLiteral("Sélectionnez un seul remplissage."));
    }
    auto settings = settingsOf(foreground, complementary);
    settings.texture = texture;
    return report(createFade(studio_.session(), id_, settings), QStringLiteral("Fondu créé."));
}

bool FadeController::setTexture(int texture) {
    if (satinTarget()) {
        return refuse(
            QStringLiteral("Le satin utilise la répartition de traversées, sans tramage."));
    }
    if (id_ == 0 || !hasFade() || texture < 0 || texture > 3) {
        return refuse(QStringLiteral("Technique de fondu inconnue ou aucune sélection."));
    }
    if (this->texture() == texture) {
        return true; // aucune régénération pour une sélection identique
    }
    if (multiSettings_) {
        auto settings = *multiSettings_;
        settings.texture = texture;
        return report(editMultiFade(studio_.session(), id_, settings),
                      QStringLiteral("Technique multicolore modifiée."));
    }
    auto settings = *settings_;
    settings.texture = texture;
    return report(editFade(studio_.session(), id_, settings),
                  QStringLiteral("Technique de fondu modifiée."));
}

bool FadeController::setGeometry(int geometry) {
    if (satinTarget()) {
        return refuse(QStringLiteral("Le fondu satin suit ses rails de couture."));
    }
    if (id_ == 0 || !hasFade() || geometry < 0 || geometry > 2) {
        return refuse(QStringLiteral("Géométrie inconnue ou aucun fondu."));
    }
    if (this->geometry() == geometry) {
        return true;
    }
    if (multiSettings_) {
        auto settings = *multiSettings_;
        settings.geometry = geometry;
        return report(editMultiFade(studio_.session(), id_, settings),
                      QStringLiteral("Géométrie multicolore modifiée."));
    }
    auto settings = *settings_;
    settings.geometry = geometry;
    return report(editFade(studio_.session(), id_, settings),
                  QStringLiteral("Géométrie modifiée."));
}

namespace {
std::optional<MultiFadeSettings> multiSettingsOf(const QVariantList& inputs, bool complementary,
                                                 int texture, int geometry) {
    if (inputs.size() < 2 || inputs.size() > 8) {
        return std::nullopt;
    }
    MultiFadeSettings settings;
    settings.complementary = complementary;
    settings.texture = texture;
    settings.geometry = geometry;
    for (const auto& value : inputs) {
        const QString raw = value.toString().trimmed();
        if (raw.size() != 7 || !raw.startsWith(QLatin1Char('#'))) {
            return std::nullopt;
        }
        const QColor chosen(raw);
        if (!chosen.isValid()) {
            return std::nullopt;
        }
        const std::array<std::uint8_t, 3> rgb{static_cast<std::uint8_t>(chosen.red()),
                                              static_cast<std::uint8_t>(chosen.green()),
                                              static_cast<std::uint8_t>(chosen.blue())};
        if (!settings.colors.empty() && rgb == settings.colors.back()) {
            return std::nullopt;
        }
        settings.colors.push_back(rgb);
    }
    return settings;
}
} // namespace

bool FadeController::createMulticolor(const QVariantList& colors, bool complementary, int texture,
                                      int geometry) {
    if (!canCreate()) {
        return refuse(QStringLiteral("Sélectionnez un remplissage compatible."));
    }
    const auto settings = multiSettingsOf(colors, complementary, texture, geometry);
    if (!settings) {
        return refuse(
            QStringLiteral("Choisissez entre 2 et 8 fils #rrggbb consécutifs distincts."));
    }
    return report(createMultiFade(studio_.session(), id_, *settings),
                  QStringLiteral("Camaïeu multicolore créé."));
}

bool FadeController::editMulticolor(const QVariantList& colors, bool complementary, int texture,
                                    int geometry) {
    if (!hasFade()) {
        return refuse(QStringLiteral("Sélectionnez un fondu existant."));
    }
    const auto settings = multiSettingsOf(colors, complementary, texture, geometry);
    if (!settings) {
        return refuse(
            QStringLiteral("Choisissez entre 2 et 8 fils #rrggbb consécutifs distincts."));
    }
    if (multiSettings_ && settings->colors == multiSettings_->colors &&
        settings->texture == multiSettings_->texture &&
        settings->complementary == multiSettings_->complementary &&
        settings->geometry == multiSettings_->geometry) {
        return true; // aucune commande pour un clic Appliquer sans changement
    }
    return report(editMultiFade(studio_.session(), id_, *settings),
                  QStringLiteral("Camaïeu multicolore enregistré."));
}

bool FadeController::edit(const QColor& foreground, bool complementary) {
    if (id_ == 0 || !hasFade()) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    if (multiSettings_) {
        auto settings = *multiSettings_;
        const auto c = settingsOf(foreground, complementary);
        settings.colors.back() = c.foreground;
        settings.complementary = complementary;
        return report(editMultiFade(studio_.session(), id_, settings),
                      QStringLiteral("Fondu multicolore modifié."));
    }
    auto settings = settingsOf(foreground, complementary);
    if (settings_) {
        settings.texture = settings_->texture;
        settings.geometry = settings_->geometry;
    }
    return report(editFade(studio_.session(), id_, settings), QStringLiteral("Fondu modifié."));
}

bool FadeController::remove() {
    if (id_ == 0) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    const auto result = multiSettings_ ? removeMultiFade(studio_.session(), id_)
                                       : removeFade(studio_.session(), id_);
    return report(result, QStringLiteral("Fondu supprimé."));
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

bool FadeController::reverse() {
    if (id_ == 0 || !hasFade()) {
        return refuse(QStringLiteral("Aucun fondu à inverser."));
    }
    if (multiSettings_) {
        auto settings = *multiSettings_;
        std::reverse(settings.colors.begin(), settings.colors.end());
        return report(editMultiFade(studio_.session(), id_, settings),
                      QStringLiteral("Ordre des couleurs inversé."));
    }
    return report(reverseFade(studio_.session(), id_), QStringLiteral("Sens du fondu inversé."));
}

bool FadeController::setCurve(int curve) {
    if (satinTarget()) {
        return refuse(QStringLiteral("Le fondu satin suit les traversées des rails."));
    }
    if (id_ == 0 || curve < 0 || curve > static_cast<int>(FadeCurve::EaseOut)) {
        return refuse(QStringLiteral("Courbe inconnue ou aucun fondu."));
    }
    if (multiSettings_) {
        if (curve == multiCurve_) {
            return true;
        }
        auto spec = project_ops::describe_multicolor_gradient_fill(studio_.session().project(),
                                                                   ObjectId{id_});
        if (!spec) {
            return refuse(QStringLiteral("Camaïeu introuvable."));
        }
        spec->gradient.curve = static_cast<document::DensityCurve>(curve);
        auto command = project_ops::make_edit_multicolor_gradient_fill_command(
            studio_.session().projectForIdAllocation(), ObjectId{id_}, *spec);
        if (!command) {
            return refuse(q(command.error().message));
        }
        studio_.session().execute(std::move(*command));
        return report(ServiceResult{true, {}}, QStringLiteral("Profil modifié."));
    }
    return report(setFadeCurve(studio_.session(), id_, static_cast<FadeCurve>(curve)),
                  QStringLiteral("Courbe de transition modifiée."));
}

} // namespace openstitch::desktop_v2::studio
