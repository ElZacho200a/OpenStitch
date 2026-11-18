// SPDX-License-Identifier: Apache-2.0
#include "fade_controller.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "openstitch/project_ops/gradient_fill.hpp"
#include "task_pump.hpp"

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

namespace {
std::optional<MultiFadeSettings> multiSettingsOf(const QVariantList& inputs, bool complementary,
                                                 int texture, int geometry) {
    if (inputs.empty() || inputs.size() > 8) {
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

FadeController::FadeController(StudioController& studio, QObject* parent)
    : QObject(parent), studio_(studio) {
    connect(&studio_, &StudioController::changed, this, &FadeController::refresh);
    attachTaskPump(tasks_, this);
    refresh();
}

FadeController::~FadeController() {
    tasks_.cancelAll();
}

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
    const auto previousId = id_;
    id_ = count == 1 ? id : 0;
    threadLabels_.clear();
    settings_.reset();
    multiSettings_.reset();
    multiCurve_ = 0;
    axis_.reset();
    incompat_.clear();
    imageReason_ = id_ != 0 ? q(imageFadeIncompatibility(studio_.session().project(), id_))
                            : QStringLiteral("Sélectionnez un seul remplissage.");
    if (id_ != 0) {
        const auto& project = studio_.session().project();
        // Axe éditable au canevas pour TOUT fondu à axe (multicolore ou ancien couple).
        axis_ = fadeAxisView(project, id_);
        multiSettings_ = readMultiFade(project, id_);
        if (!multiSettings_) {
            settings_ = readFade(project, id_); // ancien couple à deux fils
        }
        if (!settings_) {
            if (multiSettings_) {
                const auto spec = project_ops::describe_multicolor_gradient(project, ObjectId{id_});
                if (spec) {
                    multiCurve_ = static_cast<int>(spec->transition.curve);
                }
                for (const auto& label : readFadeThreadLabels(project, id_)) {
                    threadLabels_.push_back(q(label));
                }
            } else {
                incompat_ = q(fadeIncompatibility(project, id_));
            }
        }
    } else {
        incompat_ = QStringLiteral("Sélectionnez un seul remplissage.");
    }
    const int threadCount = static_cast<int>(colors().size());
    const int clamped =
        id_ != previousId ? 0 : std::clamp(selectedThread_, 0, std::max(0, threadCount - 1));
    const bool selectionMoved = clamped != selectedThread_;
    selectedThread_ = clamped;
    emit changed();
    if (selectionMoved) {
        emit selectedThreadChanged();
    }
}

void FadeController::setSelectedThread(int index) {
    const int count = static_cast<int>(colors().size());
    const int clamped = std::clamp(index, 0, std::max(0, count - 1));
    if (clamped == selectedThread_) {
        return;
    }
    selectedThread_ = clamped;
    emit selectedThreadChanged();
}

int FadeController::softness() const {
    switch (static_cast<FadeCurve>(curve())) {
    case FadeCurve::Linear:
        return 0;
    case FadeCurve::Smooth:
        return 1;
    case FadeCurve::Steep:
        return 2;
    default:
        return -1;
    }
}

bool FadeController::setSoftness(int softness) {
    static constexpr FadeCurve kCurves[] = {FadeCurve::Linear, FadeCurve::Smooth, FadeCurve::Steep};
    if (softness < 0 || softness > 2) {
        return refuse(QStringLiteral("Transition inconnue."));
    }
    return setCurve(static_cast<int>(kCurves[softness]));
}

namespace {
bool hasRepeatedNeighbour(const std::vector<std::array<std::uint8_t, 3>>& colors) {
    for (std::size_t i = 1; i < colors.size(); ++i) {
        if (colors[i] == colors[i - 1]) {
            return true;
        }
    }
    return false;
}
} // namespace

bool FadeController::insertThread(int after) {
    const auto base = current();
    if (!base) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    auto next = *base;
    const int count = static_cast<int>(next.colors.size());
    if (count >= 8) {
        return refuse(QStringLiteral("Un fondu compte au plus 8 fils."));
    }
    const auto at = static_cast<std::size_t>(std::clamp(after, 0, count - 1));
    const auto a = next.colors[at];
    std::array<std::uint8_t, 3> added{};
    if (at + 1 < next.colors.size()) {
        // Teinte intermédiaire : un point de départ lisible, à remplacer par un vrai fil.
        const auto b = next.colors[at + 1];
        for (std::size_t c = 0; c < 3; ++c) {
            added[c] = static_cast<std::uint8_t>((a[c] + b[c] + 1) / 2);
        }
    } else {
        for (std::size_t c = 0; c < 3; ++c) {
            added[c] = static_cast<std::uint8_t>(a[c] > 127 ? a[c] / 2 : a[c] + (255 - a[c]) / 2);
        }
    }
    next.colors.insert(next.colors.begin() + static_cast<std::ptrdiff_t>(at) + 1, added);
    if (hasRepeatedNeighbour(next.colors)) {
        next.colors[at + 1][0] = static_cast<std::uint8_t>(next.colors[at + 1][0] ^ 0x20);
    }
    const int position = static_cast<int>(at) + 1;
    if (!apply(next, QStringLiteral("Fil %1 ajouté : choisissez sa couleur.").arg(position + 1))) {
        return false;
    }
    setSelectedThread(position);
    return true;
}

bool FadeController::removeThread(int index) {
    const auto base = current();
    if (!base) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    auto next = *base;
    const int count = static_cast<int>(next.colors.size());
    if (index < 0 || index >= count) {
        return refuse(QStringLiteral("Fil inconnu."));
    }
    if (count <= 1) {
        return refuse(QStringLiteral("Un fondu garde au moins 1 fil."));
    }
    next.colors.erase(next.colors.begin() + index);
    if (hasRepeatedNeighbour(next.colors)) {
        return refuse(QStringLiteral("Retirer ce fil collerait deux fils identiques."));
    }
    if (!apply(next, QStringLiteral("Fil %1 retiré.").arg(index + 1))) {
        return false;
    }
    setSelectedThread(std::max(0, std::min(index, count - 2)));
    return true;
}

bool FadeController::moveThread(int index, int offset) {
    const auto base = current();
    if (!base) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    auto next = *base;
    const int count = static_cast<int>(next.colors.size());
    const int target = index + offset;
    if ((offset != -1 && offset != 1) || index < 0 || index >= count || target < 0 ||
        target >= count) {
        return refuse(QStringLiteral("Ce fil est déjà au bout du fondu."));
    }
    std::swap(next.colors[static_cast<std::size_t>(index)],
              next.colors[static_cast<std::size_t>(target)]);
    if (hasRepeatedNeighbour(next.colors)) {
        return refuse(QStringLiteral("Deux fils identiques se suivraient."));
    }
    if (!apply(next, QStringLiteral("Fil déplacé en position %1.").arg(target + 1))) {
        return false;
    }
    setSelectedThread(target);
    return true;
}

bool FadeController::setThreadColor(int index, const QString& hex) {
    auto list = colors();
    if (index < 0 || index >= list.size()) {
        return refuse(QStringLiteral("Fil inconnu."));
    }
    list[index] = hex.trimmed();
    return setColors(list);
}

QString FadeController::objectId() const {
    return id_ == 0 ? QString() : QString::number(id_);
}

bool FadeController::satinTarget() const {
    if (multiSettings_) {
        return multiSettings_->method ==
               static_cast<int>(document::GradientMethod::SatinInterlaced);
    }
    if (id_ == 0) {
        return false;
    }
    const auto* layer = studio_.session().project().findEmbroidery(ObjectId{id_});
    return layer != nullptr && (layer->is_satin() || layer->is_auto_satin());
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

double FadeController::angle() const {
    if (id_ == 0) {
        return 0.0;
    }
    return readFadeAngle(studio_.session().project(), id_).value_or(0.0);
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
    for (int i = 0; i <= static_cast<int>(FadeCurve::ConcaveConvex); ++i) {
        list.push_back(
            QVariantMap{{QStringLiteral("id"), i},
                        {QStringLiteral("label"), q(fadeCurveLabel(static_cast<FadeCurve>(i)))}});
    }
    return list;
}

QVariantList FadeController::methods() const {
    return {QVariantMap{{QStringLiteral("id"), 0},
                        {QStringLiteral("label"), QStringLiteral("Directionnel")}},
            QVariantMap{{QStringLiteral("id"), 1},
                        {QStringLiteral("label"), QStringLiteral("Satin entrelacé")}},
            QVariantMap{{QStringLiteral("id"), 2},
                        {QStringLiteral("label"), QStringLiteral("Passé empiétant")}},
            QVariantMap{{QStringLiteral("id"), 3},
                        {QStringLiteral("label"), QStringLiteral("Dispersion contrôlée")}},
            QVariantMap{{QStringLiteral("id"), 4},
                        {QStringLiteral("label"), QStringLiteral("Hachures croisées")}}};
}

namespace {
struct StyleDef {
    int method;
    int texture;
    const char* label;
};
// Une seule liste « Technique » : la méthode de génération et l'aspect de la transition
// étaient deux réglages concurrents (texture « Tramé » = méthode « Dispersion »…).
constexpr StyleDef kStyles[] = {
    {0, 0, "Directionnel lisse"},   {0, 1, "Bords irréguliers"}, {2, 3, "Passé empiétant"},
    {3, 2, "Dispersion contrôlée"}, {4, 0, "Hachures croisées"}, {1, 0, "Satin entrelacé"},
};
constexpr int kStyleCount = static_cast<int>(std::size(kStyles));
} // namespace

int FadeController::style() const {
    const auto settings = current();
    if (!settings) {
        return satinTarget() ? kStyleCount - 1 : 0;
    }
    for (int i = 0; i < kStyleCount; ++i) {
        const auto& def = kStyles[i];
        if (def.method == settings->method &&
            (def.method != 0 || def.texture == settings->texture)) {
            return i;
        }
    }
    return 0;
}

QVariantList FadeController::styles() const {
    QVariantList list;
    for (int i = 0; i < kStyleCount; ++i) {
        list.push_back(QVariantMap{{QStringLiteral("id"), i},
                                   {QStringLiteral("label"), QString::fromUtf8(kStyles[i].label)}});
    }
    return list;
}

std::optional<MultiFadeSettings> FadeController::current() const {
    if (multiSettings_) {
        return multiSettings_;
    }
    if (!settings_) {
        return std::nullopt;
    }
    const QColor back = background();
    MultiFadeSettings settings;
    settings.colors = {{static_cast<std::uint8_t>(back.red()),
                        static_cast<std::uint8_t>(back.green()),
                        static_cast<std::uint8_t>(back.blue())},
                       settings_->foreground};
    settings.complementary = settings_->complementary;
    settings.texture = settings_->texture;
    settings.geometry = settings_->geometry;
    settings.method = settings_->texture == 3 ? 2 : settings_->texture == 2 ? 3 : 0;
    return settings;
}

bool FadeController::apply(const MultiFadeSettings& next, const QString& success) {
    if (id_ == 0 || !hasFade()) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    if (multiSettings_ && *multiSettings_ == next) {
        return true; // aucune commande pour un réglage inchangé
    }
    return report(editMultiFade(studio_.session(), id_, next), success);
}

bool FadeController::setColors(const QVariantList& colors) {
    const auto base = current();
    if (!base) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    const auto parsed = multiSettingsOf(colors, base->complementary, base->texture, base->geometry);
    if (!parsed) {
        return refuse(
            QStringLiteral("Choisissez entre 1 et 8 fils #rrggbb, chacun différent du précédent."));
    }
    auto next = *base;
    next.colors = parsed->colors;
    return apply(next, QStringLiteral("Fils du fondu modifiés."));
}

bool FadeController::setStyle(int style) {
    if (style < 0 || style >= kStyleCount) {
        return refuse(QStringLiteral("Technique de fondu inconnue."));
    }
    auto next = current();
    if (!next) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    next->method = kStyles[style].method;
    next->texture = kStyles[style].texture;
    return apply(*next, QStringLiteral("Technique du fondu modifiée."));
}

bool FadeController::setComplementary(bool complementary) {
    auto next = current();
    if (!next) {
        return refuse(QStringLiteral("Sélectionnez un fondu."));
    }
    next->complementary = complementary;
    return apply(*next, QStringLiteral("Répartition des fils modifiée."));
}

bool FadeController::setSatinDensity(double mm) {
    auto next = current();
    if (!next || !std::isfinite(mm) || mm < 0.1 || mm > 5.0) {
        return refuse(QStringLiteral("Densité satin entre 0,1 et 5 mm."));
    }
    next->satinDensityMm = mm;
    return apply(*next, QStringLiteral("Densité du satin entrelacé modifiée."));
}

bool FadeController::setCrosshatch(int layers, double angleDeg) {
    auto next = current();
    if (!next || layers < 2 || layers > 3 || !std::isfinite(angleDeg)) {
        return refuse(QStringLiteral("Hachures : 2 ou 3 directions et un angle valide."));
    }
    next->crosshatchLayers = layers;
    next->crosshatchAngleDeg = angleDeg;
    return apply(*next, QStringLiteral("Hachures croisées modifiées."));
}

bool FadeController::setSpacing(double mm) {
    auto next = current();
    if (!next || !std::isfinite(mm) || mm < 0.1 || mm > 4.0) {
        return refuse(QStringLiteral("Espacement des rangées entre 0,1 et 4 mm."));
    }
    next->spacingMm = std::round(mm * 1000.0) / 1000.0;
    return apply(*next, QStringLiteral("Espacement des rangées modifié."));
}

bool FadeController::setStitchAngle(bool custom, double degrees) {
    auto next = current();
    if (!next || !std::isfinite(degrees)) {
        return refuse(QStringLiteral("Direction des points invalide."));
    }
    next->customStitchAngle = custom;
    next->stitchAngleDeg = custom ? std::remainder(degrees, 180.0) : 0.0;
    return apply(*next, custom ? QStringLiteral("Direction des points imposée.")
                               : QStringLiteral("Points perpendiculaires au fondu."));
}

bool FadeController::setEndDensity(double percent) {
    auto next = current();
    if (!next || !std::isfinite(percent) || percent < 0.0 || percent > 100.0) {
        return refuse(QStringLiteral("Densité d'arrivée entre 0 et 100 %."));
    }
    next->endDensity = std::round(percent) / 100.0;
    return apply(*next, QStringLiteral("Densité d'arrivée modifiée."));
}

QVariantList FadeController::curveProfile() const {
    QVariantList list;
    for (const double v : fadeCurveProfile(static_cast<FadeCurve>(curve()), 32)) {
        list.push_back(v);
    }
    return list;
}

bool FadeController::fadeFromImage(int threads, double cellMm, bool continuousRows) {
    if (imageBusy_) {
        return refuse(QStringLiteral("Un calcul de fondu depuis l'image est déjà en cours."));
    }
    if (!imageReason_.isEmpty()) {
        return refuse(imageReason_);
    }
    if (threads < 2 || threads > 8) {
        return refuse(QStringLiteral("Choisissez entre 2 et 8 fils."));
    }
    if (!(cellMm >= 0.5 && cellMm <= 5.0)) {
        return refuse(QStringLiteral("La case de la grille doit mesurer de 0,5 à 5 mm."));
    }
    const auto snapshot = studio_.session().snapshot();
    const auto revision = studio_.session().revision();
    const auto id = id_;
    const ImageFadeSettings settings{threads, cellMm, continuousRows};
    imageBusy_ = true;
    emit busyChanged();
    ok_ = true;
    message_ = QStringLiteral("Calcul du fondu depuis l'image…");
    emit messageChanged();
    tasks_.submit<ImageFadePlan>(
        "studio.fade_from_image",
        [snapshot, id, settings](const TaskContext&) {
            return computeImageFade(*snapshot, id, settings);
        },
        [this, revision](Result<ImageFadePlan>&& result) {
            imageBusy_ = false;
            emit busyChanged();
            if (!result) {
                refuse(q(result.error().message));
                return;
            }
            if (studio_.session().revision() != revision) {
                refuse(QStringLiteral("Le document a changé pendant le calcul : relancez-le."));
                return;
            }
            report(commitImageFade(studio_.session(), *result),
                   QStringLiteral("Fondu depuis l'image appliqué : %1 fils (annulable)")
                       .arg(result->spec.palette.size()));
        });
    return true;
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
    if (id_ == 0 || !hasFade() || !document::is_valid_density_geometry(geometry)) {
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

bool FadeController::setAngle(double degrees) {
    if (id_ == 0 || !hasFade()) {
        return refuse(QStringLiteral("Aucun fondu à orienter."));
    }
    if (geometry() == 1 || geometry() == 4) {
        return refuse(QStringLiteral("Un fondu radial ou qui suit le contour n'a pas d'axe."));
    }
    return report(openstitch::desktop_v2::setFadeAngle(studio_.session(), id_, degrees),
                  QStringLiteral("Angle du fondu modifié."));
}

bool FadeController::createMulticolor(const QVariantList& colors, bool complementary, int texture,
                                      int geometry) {
    return createMulticolorAdvanced(colors, complementary, texture, geometry, satinTarget() ? 1 : 0,
                                    0.4, 2, 0.0);
}

bool FadeController::createMulticolorAdvanced(const QVariantList& colors, bool complementary,
                                              int texture, int geometry, int method,
                                              double satinDensityMm, int crosshatchLayers,
                                              double crosshatchAngleDeg) {
    if (!canCreate()) {
        return refuse(QStringLiteral("Sélectionnez un remplissage compatible."));
    }
    auto settings = multiSettingsOf(colors, complementary, texture, geometry);
    if (!settings) {
        return refuse(
            QStringLiteral("Choisissez entre 2 et 8 fils #rrggbb consécutifs distincts."));
    }
    settings->method = method;
    settings->satinDensityMm = satinDensityMm;
    settings->crosshatchLayers = crosshatchLayers;
    settings->crosshatchAngleDeg = crosshatchAngleDeg;
    return report(createMultiFade(studio_.session(), id_, *settings),
                  QStringLiteral("Camaïeu multicolore créé."));
}

bool FadeController::editMulticolor(const QVariantList& colors, bool complementary, int texture,
                                    int geometry) {
    return editMulticolorAdvanced(colors, complementary, texture, geometry, method(),
                                  satinDensityMm(), crosshatchLayers(), crosshatchAngleDeg());
}

bool FadeController::editMulticolorAdvanced(const QVariantList& colors, bool complementary,
                                            int texture, int geometry, int method,
                                            double satinDensityMm, int crosshatchLayers,
                                            double crosshatchAngleDeg) {
    if (!hasFade()) {
        return refuse(QStringLiteral("Sélectionnez un fondu existant."));
    }
    auto settings = multiSettingsOf(colors, complementary, texture, geometry);
    if (!settings) {
        return refuse(
            QStringLiteral("Choisissez entre 2 et 8 fils #rrggbb consécutifs distincts."));
    }
    settings->method = method;
    settings->satinDensityMm = satinDensityMm;
    settings->crosshatchLayers = crosshatchLayers;
    settings->crosshatchAngleDeg = crosshatchAngleDeg;
    if (multiSettings_ && settings->colors == multiSettings_->colors &&
        settings->texture == multiSettings_->texture &&
        settings->complementary == multiSettings_->complementary &&
        settings->geometry == multiSettings_->geometry &&
        settings->method == multiSettings_->method &&
        settings->satinDensityMm == multiSettings_->satinDensityMm &&
        settings->crosshatchLayers == multiSettings_->crosshatchLayers &&
        settings->crosshatchAngleDeg == multiSettings_->crosshatchAngleDeg) {
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
    return report(reverseFade(studio_.session(), id_), QStringLiteral("Sens du fondu inversé."));
}

int FadeController::layerCurve(int layer) const {
    if (id_ == 0 || layer < 0 || !multiSettings_) {
        return -1;
    }
    const auto spec =
        project_ops::describe_multicolor_gradient(studio_.session().project(), ObjectId{id_});
    if (!spec || static_cast<std::size_t>(layer) >= spec->palette.size()) {
        return -1;
    }
    return static_cast<int>(spec->palette[static_cast<std::size_t>(layer)].curve);
}

bool FadeController::setLayerCurve(int layer, int curve) {
    if (layer < 0 || !multiFade() || satinTarget()) {
        return refuse(QStringLiteral("Sélectionnez une couche de remplissage multicolore."));
    }
    return report(
        setMultiFadeLayerCurve(studio_.session(), id_, static_cast<std::size_t>(layer), curve),
        QStringLiteral("Profil de ce fil modifié."));
}

bool FadeController::setCurve(int curve) {
    if (satinTarget()) {
        return refuse(QStringLiteral("Le fondu satin suit les traversées des rails."));
    }
    if (id_ == 0 || curve < 0 || curve > static_cast<int>(FadeCurve::ConcaveConvex)) {
        return refuse(QStringLiteral("Courbe inconnue ou aucun fondu."));
    }
    if (multiSettings_) {
        auto spec =
            project_ops::describe_multicolor_gradient(studio_.session().project(), ObjectId{id_});
        if (!spec) {
            return refuse(QStringLiteral("Camaïeu introuvable."));
        }
        const auto selected = static_cast<document::DensityCurve>(curve);
        const bool allSelected = std::all_of(
            spec->palette.begin(), spec->palette.end(),
            [&](const document::GradientThread& thread) { return thread.curve == selected; });
        if (curve == multiCurve_ && allSelected) {
            return true;
        }
        spec->transition.curve = selected;
        // Le profil global reste un préréglage cohérent : il s'applique à
        // toutes les couches, avant les éventuelles retouches individuelles.
        for (auto& thread : spec->palette) {
            thread.curve = selected;
        }
        auto command = project_ops::make_edit_multicolor_gradient_command(
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
