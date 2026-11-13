// SPDX-License-Identifier: Apache-2.0
#include "fade_axis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "document_session.hpp"
#include "openstitch/project_ops/gradient_fill.hpp"

namespace openstitch::desktop_v2 {

namespace {

constexpr double kMaxCoordMm = 1000.0;

[[nodiscard]] PointMm toMm(Vec2um p) {
    return PointMm{p.x.value / 1000.0, p.y.value / 1000.0};
}

[[nodiscard]] Vec2um toUm(PointMm p) {
    return Vec2um{Micrometers{static_cast<std::int32_t>(std::lround(p.x * 1000.0))},
                  Micrometers{static_cast<std::int32_t>(std::lround(p.y * 1000.0))}};
}

[[nodiscard]] const std::vector<geometry::PathSet>* shapeOf(const document::Project& project,
                                                            std::uint64_t objectId) {
    const auto* object = project.findEmbroidery(ObjectId{objectId});
    if (object == nullptr) {
        return nullptr;
    }
    const auto* vector = project.findObject(object->source_vector);
    return vector != nullptr ? &vector->paths : nullptr;
}

[[nodiscard]] FadeCurve curveOf(document::DensityCurve c) {
    switch (c) {
    case document::DensityCurve::Smooth:
        return FadeCurve::Smooth;
    case document::DensityCurve::Steep:
        return FadeCurve::Steep;
    case document::DensityCurve::EaseIn:
        return FadeCurve::EaseIn;
    case document::DensityCurve::EaseOut:
        return FadeCurve::EaseOut;
    case document::DensityCurve::Convex:
        return FadeCurve::Convex;
    case document::DensityCurve::Concave:
        return FadeCurve::Concave;
    case document::DensityCurve::ConvexConcave:
        return FadeCurve::ConvexConcave;
    case document::DensityCurve::ConcaveConvex:
        return FadeCurve::ConcaveConvex;
    case document::DensityCurve::Linear:
        break;
    }
    return FadeCurve::Linear;
}

[[nodiscard]] document::DensityCurve engineCurve(FadeCurve c) {
    switch (c) {
    case FadeCurve::Smooth:
        return document::DensityCurve::Smooth;
    case FadeCurve::Steep:
        return document::DensityCurve::Steep;
    case FadeCurve::EaseIn:
        return document::DensityCurve::EaseIn;
    case FadeCurve::EaseOut:
        return document::DensityCurve::EaseOut;
    case FadeCurve::Convex:
        return document::DensityCurve::Convex;
    case FadeCurve::Concave:
        return document::DensityCurve::Concave;
    case FadeCurve::ConvexConcave:
        return document::DensityCurve::ConvexConcave;
    case FadeCurve::ConcaveConvex:
        return document::DensityCurve::ConcaveConvex;
    case FadeCurve::Linear:
        break;
    }
    return document::DensityCurve::Linear;
}

[[nodiscard]] FadeAxisView viewOf(const project_ops::GradientFillSpec& spec,
                                  const std::vector<geometry::PathSet>& shape) {
    FadeAxisView view;
    view.start = toMm(spec.gradient.from);
    view.end = toMm(spec.gradient.to);
    view.centre = {(view.start.x + view.end.x) / 2.0, (view.start.y + view.end.y) / 2.0};
    // Poignée de rotation à 90° de l'axe, à une distance lisible (au moins 6 mm).
    const double dx = view.end.x - view.start.x;
    const double dy = view.end.y - view.start.y;
    const double length = std::hypot(dx, dy);
    const double offset = std::max(6.0, length * 0.25);
    if (length > 0.0) {
        view.rotate = {view.centre.x - dy / length * offset, view.centre.y + dx / length * offset};
    } else {
        view.rotate = view.centre;
    }
    if (const auto zone = project_ops::gradient_zone(shape, spec.gradient)) {
        view.zoneStart = zone->start;
        view.zoneEnd = zone->end;
    }
    view.background = spec.background;
    view.foreground = spec.foreground;
    view.curve = curveOf(spec.gradient.curve);
    return view;
}

[[nodiscard]] project_ops::GradientHandle engineHandle(FadeHandle handle) {
    switch (handle) {
    case FadeHandle::Start:
        return project_ops::GradientHandle::Start;
    case FadeHandle::End:
        return project_ops::GradientHandle::End;
    case FadeHandle::Move:
        return project_ops::GradientHandle::Move;
    case FadeHandle::Rotate:
        return project_ops::GradientHandle::Rotate;
    }
    return project_ops::GradientHandle::Move;
}

[[nodiscard]] bool inRange(PointMm p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= kMaxCoordMm &&
           std::abs(p.y) <= kMaxCoordMm;
}

} // namespace

std::optional<FadeAxisView> fadeAxisView(const document::Project& project, std::uint64_t objectId) {
    const auto spec = project_ops::describe_gradient_fill(project, ObjectId{objectId});
    const auto* shape = shapeOf(project, objectId);
    if (!spec || shape == nullptr) {
        return std::nullopt;
    }
    return viewOf(*spec, *shape);
}

std::optional<FadeAxisView> previewFadeDrag(const document::Project& project,
                                            std::uint64_t objectId, FadeHandle handle,
                                            PointMm pointer, bool symmetric) {
    if (!inRange(pointer)) {
        return std::nullopt;
    }
    auto spec = project_ops::describe_gradient_fill(project, ObjectId{objectId});
    const auto* shape = shapeOf(project, objectId);
    if (!spec || shape == nullptr) {
        return std::nullopt;
    }
    const auto moved = project_ops::drag_gradient_handle(
        *shape, spec->gradient, engineHandle(handle), toUm(pointer), symmetric);
    if (!moved) {
        return std::nullopt;
    }
    spec->gradient = *moved;
    return viewOf(*spec, *shape);
}

ServiceResult dragFadeHandle(DocumentSession& session, std::uint64_t objectId, FadeHandle handle,
                             PointMm pointer, bool symmetric) {
    if (!inRange(pointer)) {
        return {false, "Position hors de la zone de travail."};
    }
    auto spec = project_ops::describe_gradient_fill(session.project(), ObjectId{objectId});
    const auto* shape = shapeOf(session.project(), objectId);
    if (!spec || shape == nullptr) {
        return {false, "Cet objet ne porte pas de fondu."};
    }
    const auto moved = project_ops::drag_gradient_handle(
        *shape, spec->gradient, engineHandle(handle), toUm(pointer), symmetric);
    if (!moved) {
        return {false, "Ce déplacement rendrait l'axe du fondu inutilisable."};
    }
    if (*moved == spec->gradient) {
        return {true, {}}; // rien n'a bougé : aucun pas d'annulation inutile
    }
    spec->gradient = *moved;
    auto command =
        project_ops::make_edit_gradient_fill_command(session.project(), ObjectId{objectId}, *spec);
    if (!command) {
        return {false, command.error().message};
    }
    session.execute(std::move(*command));
    return {true, {}};
}

ServiceResult reverseFade(DocumentSession& session, std::uint64_t objectId) {
    auto spec = project_ops::describe_gradient_fill(session.project(), ObjectId{objectId});
    if (!spec) {
        return {false, "Cet objet ne porte pas de fondu."};
    }
    std::swap(spec->gradient.from, spec->gradient.to);
    std::swap(spec->gradient.spacing_from, spec->gradient.spacing_to);
    auto command =
        project_ops::make_edit_gradient_fill_command(session.project(), ObjectId{objectId}, *spec);
    if (!command) {
        return {false, command.error().message};
    }
    session.execute(std::move(*command));
    return {true, {}};
}

std::string fadeCurveLabel(FadeCurve curve) {
    switch (curve) {
    case FadeCurve::Linear:
        return "Linéaire";
    case FadeCurve::Smooth:
        return "Adoucie";
    case FadeCurve::Steep:
        return "Resserrée";
    case FadeCurve::EaseIn:
        return "Départ lent";
    case FadeCurve::EaseOut:
        return "Départ rapide";
    case FadeCurve::Convex:
        return "Convexe cubique";
    case FadeCurve::Concave:
        return "Concave cubique";
    case FadeCurve::ConvexConcave:
        return "Convexe-concave";
    case FadeCurve::ConcaveConvex:
        return "Concave-convexe";
    }
    return {};
}

double fadeCurveValue(FadeCurve curve, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (curve) {
    case FadeCurve::Smooth:
        return t * t * (3.0 - 2.0 * t);
    case FadeCurve::Steep:
        return t * t * t * (t * (6.0 * t - 15.0) + 10.0);
    case FadeCurve::EaseIn:
        return t * t;
    case FadeCurve::EaseOut:
        return t * (2.0 - t);
    case FadeCurve::Convex:
        return t * t * t;
    case FadeCurve::Concave: {
        const double u = 1.0 - t;
        return 1.0 - u * u * u;
    }
    case FadeCurve::ConvexConcave:
        return t <= 0.5 ? 4.0 * t * t * t
                        : 1.0 - 4.0 * (1.0 - t) * (1.0 - t) * (1.0 - t);
    case FadeCurve::ConcaveConvex: {
        if (t <= 0.5) {
            const double u = 1.0 - 2.0 * t;
            return 0.5 * (1.0 - u * u * u);
        }
        const double u = 2.0 * t - 1.0;
        return 0.5 + 0.5 * u * u * u;
    }
    case FadeCurve::Linear:
        break;
    }
    return t;
}

std::vector<double> fadeCurveProfile(FadeCurve curve, int samples) {
    std::vector<double> out;
    samples = std::max(2, samples);
    out.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        out.push_back(fadeCurveValue(curve, static_cast<double>(i) / (samples - 1)));
    }
    return out;
}

ServiceResult setFadeCurve(DocumentSession& session, std::uint64_t objectId, FadeCurve curve) {
    auto spec = project_ops::describe_gradient_fill(session.project(), ObjectId{objectId});
    if (!spec) {
        return {false, "Cet objet ne porte pas de fondu."};
    }
    if (spec->gradient.curve == engineCurve(curve)) {
        return {true, {}}; // déjà cette courbe : aucun pas d'annulation inutile
    }
    spec->gradient.curve = engineCurve(curve);
    auto command =
        project_ops::make_edit_gradient_fill_command(session.project(), ObjectId{objectId}, *spec);
    if (!command) {
        return {false, command.error().message};
    }
    session.execute(std::move(*command));
    return {true, {}};
}

std::string fadeSummary(const FadeAxisView& view) {
    const double length = std::hypot(view.end.x - view.start.x, view.end.y - view.start.y);
    char buffer[160];
    std::snprintf(buffer, sizeof buffer,
                  "Transition de %.0f %% à %.0f %% de la forme, sur %.1f mm (courbe %s).",
                  view.zoneStart * 100.0, view.zoneEnd * 100.0, length,
                  fadeCurveLabel(view.curve).c_str());
    return buffer;
}

} // namespace openstitch::desktop_v2
