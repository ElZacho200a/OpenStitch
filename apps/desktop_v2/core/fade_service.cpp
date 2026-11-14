// SPDX-License-Identifier: Apache-2.0
#include "fade_service.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "openstitch/project_ops/gradient_fill.hpp"

namespace openstitch::desktop_v2 {

namespace {
ServiceResult refuse(std::string why) {
    return {false, std::move(why)};
}
const document::EmbroideryObject* findObject(const document::Project& p, std::uint64_t id) {
    const auto it =
        std::ranges::find_if(p.embroidery_objects, [&](const auto& o) { return o.id.value == id; });
    return it == p.embroidery_objects.end() ? nullptr : &*it;
}

bool valid_mode(int texture, int geometry) {
    return texture >= 0 && texture <= 3 && geometry >= 0 && geometry <= 2;
}

// Les coordonnées du champ sont définies une seule fois lors du changement
// de géométrie, jamais dans la boucle de génération des points.
bool setup_geometry(const document::Project& project, ObjectId id,
                    document::DensityGradient& gradient, int geometry, bool resetAxis) {
    const auto* layer = project.findEmbroidery(id);
    const auto* source = layer ? project.findObject(layer->source_vector) : nullptr;
    if (source == nullptr || source->paths.empty()) {
        return false;
    }
    if (resetAxis) {
        const auto initial = project_ops::default_gradient_fill(project, id);
        if (!initial) {
            return false;
        }
        gradient.from = initial->gradient.from;
        gradient.to = initial->gradient.to;
    }
    if (geometry == 1 && resetAxis) {
        double x0 = std::numeric_limits<double>::max();
        double y0 = x0;
        double x1 = std::numeric_limits<double>::lowest();
        double y1 = x1;
        for (const auto& path : source->paths) {
            for (const auto& node : path.outer.nodes) {
                x0 = std::min(x0, static_cast<double>(node.pos.x.value));
                y0 = std::min(y0, static_cast<double>(node.pos.y.value));
                x1 = std::max(x1, static_cast<double>(node.pos.x.value));
                y1 = std::max(y1, static_cast<double>(node.pos.y.value));
            }
        }
        if (x0 > x1 || y0 > y1) {
            return false;
        }
        const double cx = (x0 + x1) * 0.5;
        const double cy = (y0 + y1) * 0.5;
        const double radius = std::hypot((x1 - x0) * 0.5, (y1 - y0) * 0.5);
        if (radius < 1.0 || cx + radius >= std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
        gradient.from = Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(cx))},
                               Micrometers{static_cast<std::int32_t>(std::lround(cy))}};
        gradient.to = Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(cx + radius))},
                             gradient.from.y};
    }
    gradient.geometry = static_cast<document::DensityGeometry>(geometry);
    gradient.curve_bend = geometry == 2 ? 0.35 : 0.0;
    return true;
}

std::optional<double> gradient_angle(const document::DensityGradient& gradient) {
    const double dx = static_cast<double>(gradient.to.x.value) - gradient.from.x.value;
    const double dy = static_cast<double>(gradient.to.y.value) - gradient.from.y.value;
    if (std::hypot(dx, dy) < 1.0) {
        return std::nullopt;
    }
    return std::atan2(dy, dx) * 180.0 / std::numbers::pi;
}

bool rotate_gradient(document::DensityGradient& gradient, double degrees) {
    if (!std::isfinite(degrees)) {
        return false;
    }
    const double dx = static_cast<double>(gradient.to.x.value) - gradient.from.x.value;
    const double dy = static_cast<double>(gradient.to.y.value) - gradient.from.y.value;
    const double length = std::hypot(dx, dy);
    if (length < 1.0) {
        return false;
    }
    const double radians = std::remainder(degrees, 360.0) * std::numbers::pi / 180.0;
    const double cx = (static_cast<double>(gradient.from.x.value) + gradient.to.x.value) * 0.5;
    const double cy = (static_cast<double>(gradient.from.y.value) + gradient.to.y.value) * 0.5;
    const double hx = std::cos(radians) * length * 0.5;
    const double hy = std::sin(radians) * length * 0.5;
    const auto inside = [](double value) {
        return value >= std::numeric_limits<std::int32_t>::min() &&
               value <= std::numeric_limits<std::int32_t>::max();
    };
    if (!inside(cx - hx) || !inside(cx + hx) || !inside(cy - hy) || !inside(cy + hy)) {
        return false;
    }
    gradient.from = Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(cx - hx))},
                           Micrometers{static_cast<std::int32_t>(std::lround(cy - hy))}};
    gradient.to = Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(cx + hx))},
                         Micrometers{static_cast<std::int32_t>(std::lround(cy + hy))}};
    return true;
}
} // namespace

std::string fadeIncompatibility(const document::Project& project, std::uint64_t objectId) {
    const auto* emb = project.findEmbroidery(ObjectId{objectId});
    if (emb && emb->is_satin()) {
        // Contrairement à l'ancien fondu, le satin entrelacé autonome sait dériver ses rails
        // depuis le contour source. C'est le correctif réel du diagnostic couvert par la PR #340 :
        // le simple choix de Satin ne doit plus conduire à l'erreur trompeuse « tatami seulement ».
        return project_ops::multicolor_gradient_incompatibility(
            project, ObjectId{objectId}, document::GradientMethod::SatinInterlaced);
    }
    return project_ops::gradient_incompatibility(project, ObjectId{objectId});
}

std::optional<FadeSettings> readFade(const document::Project& project, std::uint64_t objectId) {
    const auto spec = project_ops::describe_gradient_fill(project, ObjectId{objectId});
    if (!spec) {
        return std::nullopt;
    }
    return FadeSettings{spec->foreground, spec->complementary,
                        static_cast<int>(spec->gradient.texture),
                        static_cast<int>(spec->gradient.geometry)};
}

ServiceResult createFade(DocumentSession& session, std::uint64_t objectId,
                         const FadeSettings& settings) {
    const auto* object = findObject(session.project(), objectId);
    if (object == nullptr) {
        return refuse("Sélectionnez le remplissage qui portera le fondu.");
    }
    if (const auto why = fadeIncompatibility(session.project(), objectId); !why.empty()) {
        return refuse(why);
    }
    auto spec = project_ops::default_gradient_fill(session.project(), ObjectId{objectId});
    if (!spec) {
        return refuse("Ce remplissage ne peut pas porter un fondu (forme vide ou dégénérée).");
    }
    spec->foreground = settings.foreground;
    spec->foreground_thread.reset(); // couleur libre : pas de fil de nuancier imposé
    spec->complementary = settings.complementary;
    if (!valid_mode(settings.texture, settings.geometry)) {
        return refuse("Technique ou géométrie de fondu inconnue.");
    }
    spec->gradient.texture = static_cast<document::DensityTexture>(settings.texture);
    if (!setup_geometry(session.project(), ObjectId{objectId}, spec->gradient, settings.geometry,
                        settings.geometry != 0)) {
        return refuse("Forme incompatible avec cette géométrie.");
    }
    if (spec->foreground == spec->background) {
        return refuse("Choisissez un fil du dessus différent du fil du fond.");
    }
    const auto* shape = session.project().findObject(object->source_vector);
    if (shape == nullptr) {
        return refuse("Forme source introuvable.");
    }
    auto command = project_ops::make_create_gradient_fill_command(
        session.projectForIdAllocation(), ObjectId{objectId}, *spec, shape->paths);
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

ServiceResult editFade(DocumentSession& session, std::uint64_t objectId,
                       const FadeSettings& settings) {
    auto spec = project_ops::describe_gradient_fill(session.project(), ObjectId{objectId});
    if (!spec) {
        return refuse("Cet objet ne porte pas de fondu.");
    }
    spec->foreground = settings.foreground;
    spec->foreground_thread.reset();
    spec->complementary = settings.complementary;
    if (!valid_mode(settings.texture, settings.geometry)) {
        return refuse("Technique ou géométrie de fondu inconnue.");
    }
    spec->gradient.texture = static_cast<document::DensityTexture>(settings.texture);
    if (static_cast<int>(spec->gradient.geometry) != settings.geometry) {
        if (!setup_geometry(session.project(), ObjectId{objectId}, spec->gradient,
                            settings.geometry, true)) {
            return refuse("Forme incompatible avec cette géométrie.");
        }
    }
    auto command =
        project_ops::make_edit_gradient_fill_command(session.project(), ObjectId{objectId}, *spec);
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

ServiceResult removeFade(DocumentSession& session, std::uint64_t objectId) {
    auto command =
        project_ops::make_remove_gradient_fill_command(session.project(), ObjectId{objectId});
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

std::optional<MultiFadeSettings> readMultiFade(const document::Project& project,
                                               std::uint64_t objectId) {
    const auto spec = project_ops::describe_multicolor_gradient(project, ObjectId{objectId});
    if (!spec) {
        return std::nullopt;
    }
    MultiFadeSettings settings;
    settings.colors.reserve(spec->palette.size());
    for (const auto& thread : spec->palette) {
        settings.colors.push_back(thread.rgb);
    }
    settings.complementary = spec->complementary;
    settings.texture = static_cast<int>(spec->transition.texture);
    settings.geometry = static_cast<int>(spec->transition.geometry);
    settings.method = static_cast<int>(spec->method);
    settings.satinDensityMm = static_cast<double>(spec->satin_density.value) / 1000.0;
    settings.crosshatchLayers = spec->crosshatch_layers;
    settings.crosshatchAngleDeg = spec->crosshatch_angle.radians * 180.0 / std::numbers::pi;
    return settings;
}

std::optional<double> readFadeAngle(const document::Project& project, std::uint64_t objectId) {
    if (const auto spec = project_ops::describe_multicolor_gradient(project, ObjectId{objectId})) {
        return gradient_angle(spec->transition);
    }
    if (const auto spec = project_ops::describe_gradient_fill(project, ObjectId{objectId})) {
        return gradient_angle(spec->gradient);
    }
    return std::nullopt;
}

ServiceResult setFadeAngle(DocumentSession& session, std::uint64_t objectId, double degrees) {
    if (auto spec =
            project_ops::describe_multicolor_gradient(session.project(), ObjectId{objectId})) {
        const auto before = spec->transition;
        if (!rotate_gradient(spec->transition, degrees)) {
            return refuse("Angle invalide ou axe de fondu dégénéré.");
        }
        if (spec->transition == before) {
            return {true, {}};
        }
        auto command = project_ops::make_edit_multicolor_gradient_command(
            session.projectForIdAllocation(), ObjectId{objectId}, *spec);
        if (!command) {
            return refuse(command.error().message);
        }
        session.execute(std::move(*command));
        return {true, {}};
    }
    if (auto spec = project_ops::describe_gradient_fill(session.project(), ObjectId{objectId})) {
        const auto before = spec->gradient;
        if (!rotate_gradient(spec->gradient, degrees)) {
            return refuse("Angle invalide ou axe de fondu dégénéré.");
        }
        if (spec->gradient == before) {
            return {true, {}};
        }
        auto command = project_ops::make_edit_gradient_fill_command(session.project(),
                                                                    ObjectId{objectId}, *spec);
        if (!command) {
            return refuse(command.error().message);
        }
        session.execute(std::move(*command));
        return {true, {}};
    }
    return refuse("Cet objet ne porte pas de fondu.");
}

ServiceResult createMultiFade(DocumentSession& session, std::uint64_t objectId,
                              const MultiFadeSettings& settings) {
    if (!valid_mode(settings.texture, settings.geometry) || settings.method < 0 ||
        settings.method > static_cast<int>(document::GradientMethod::CrossHatching)) {
        return refuse("Technique de fondu inconnue.");
    }
    const auto* base = findObject(session.project(), objectId);
    if (base == nullptr || settings.colors.size() < 2 || settings.colors.size() > 8) {
        return refuse("Choisissez un remplissage et entre 2 et 8 fils.");
    }
    const auto initial =
        project_ops::default_multicolor_gradient(session.project(), ObjectId{objectId});
    if (!initial) {
        return refuse("La forme n'est pas compatible avec un fondu.");
    }
    project_ops::MulticolorGradientSpec spec = *initial;
    spec.method = static_cast<document::GradientMethod>(settings.method);
    spec.palette.clear();
    spec.palette.reserve(settings.colors.size());
    for (const auto& rgb : settings.colors) {
        spec.palette.push_back(
            document::GradientThread{rgb, std::nullopt, document::DensityCurve::Linear});
    }
    spec.complementary = settings.complementary;
    spec.transition.texture = static_cast<document::DensityTexture>(settings.texture);
    spec.satin_density = Micrometers{static_cast<std::int32_t>(
        std::lround(std::clamp(settings.satinDensityMm, 0.1, 5.0) * 1000.0))};
    spec.crosshatch_layers = std::clamp(settings.crosshatchLayers, 2, 3);
    spec.crosshatch_angle =
        Angle{std::remainder(settings.crosshatchAngleDeg, 360.0) * std::numbers::pi / 180.0};
    if (!setup_geometry(session.project(), ObjectId{objectId}, spec.transition, settings.geometry,
                        settings.geometry != 0)) {
        return refuse("Forme incompatible avec cette géométrie.");
    }
    auto command = project_ops::make_create_multicolor_gradient_command(
        session.projectForIdAllocation(), ObjectId{objectId}, spec);
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

ServiceResult editMultiFade(DocumentSession& session, std::uint64_t objectId,
                            const MultiFadeSettings& settings) {
    if (!valid_mode(settings.texture, settings.geometry) || settings.method < 0 ||
        settings.method > static_cast<int>(document::GradientMethod::CrossHatching)) {
        return refuse("Technique de fondu inconnue.");
    }
    auto spec = project_ops::describe_multicolor_gradient(session.project(), ObjectId{objectId});
    if (!spec) {
        // Conversion contrôlée et annulable de la relation binaire historique.
        const auto old = project_ops::describe_gradient_fill(session.project(), ObjectId{objectId});
        if (!old) {
            return refuse("Sélectionnez un fondu de fils existant.");
        }
        const auto initial =
            project_ops::default_multicolor_gradient(session.project(), ObjectId{objectId});
        if (!initial) {
            return refuse("La forme n'est pas compatible avec un fondu multicolore.");
        }
        spec = *initial;
        spec->transition = old->gradient;
        spec->palette = {{old->background, std::nullopt, document::DensityCurve::Linear},
                         {old->foreground, std::nullopt, document::DensityCurve::Linear}};
        spec->complementary = old->complementary;
    }
    const bool geometryChanged = static_cast<int>(spec->transition.geometry) != settings.geometry;
    const auto commonCurve = spec->transition.curve;
    const auto oldPalette = spec->palette;
    spec->palette.clear();
    spec->palette.reserve(settings.colors.size());
    for (std::size_t i = 0; i < settings.colors.size(); ++i) {
        const auto curve = i < oldPalette.size() ? oldPalette[i].curve : commonCurve;
        spec->palette.push_back({settings.colors[i], std::nullopt, curve});
    }
    spec->complementary = settings.complementary;
    spec->method = static_cast<document::GradientMethod>(settings.method);
    spec->transition.texture = static_cast<document::DensityTexture>(settings.texture);
    spec->satin_density = Micrometers{static_cast<std::int32_t>(
        std::lround(std::clamp(settings.satinDensityMm, 0.1, 5.0) * 1000.0))};
    spec->crosshatch_layers = std::clamp(settings.crosshatchLayers, 2, 3);
    spec->crosshatch_angle =
        Angle{std::remainder(settings.crosshatchAngleDeg, 360.0) * std::numbers::pi / 180.0};
    if (geometryChanged && !setup_geometry(session.project(), ObjectId{objectId}, spec->transition,
                                           settings.geometry, true)) {
        return refuse("Forme incompatible avec cette géométrie.");
    }
    auto command = project_ops::make_edit_multicolor_gradient_command(
        session.projectForIdAllocation(), ObjectId{objectId}, *spec);
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

ServiceResult setMultiFadeLayerCurve(DocumentSession& session, std::uint64_t objectId,
                                     std::size_t layerIndex, int curve) {
    if (curve < 0 || curve > static_cast<int>(document::DensityCurve::ConcaveConvex)) {
        return refuse("Profil de densité inconnu.");
    }
    auto spec = project_ops::describe_multicolor_gradient(session.project(), ObjectId{objectId});
    if (!spec || layerIndex >= spec->palette.size()) {
        return refuse("Sélectionnez une couche de remplissage multicolore.");
    }
    const auto selected = static_cast<document::DensityCurve>(curve);
    if (spec->palette[layerIndex].curve == selected) {
        return {true, {}};
    }
    spec->palette[layerIndex].curve = selected;
    auto command = project_ops::make_edit_multicolor_gradient_command(
        session.projectForIdAllocation(), ObjectId{objectId}, *spec);
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

ServiceResult removeMultiFade(DocumentSession& session, std::uint64_t objectId) {
    auto command =
        project_ops::make_remove_multicolor_gradient_command(session.project(), ObjectId{objectId});
    if (!command) {
        return refuse(command.error().message);
    }
    session.execute(std::move(*command));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
