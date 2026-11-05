// SPDX-License-Identifier: Apache-2.0
#include "fade_axis.hpp"

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

std::string fadeSummary(const FadeAxisView& view) {
    const double length = std::hypot(view.end.x - view.start.x, view.end.y - view.start.y);
    char buffer[160];
    std::snprintf(buffer, sizeof buffer,
                  "Transition de %.0f %% à %.0f %% de la forme, sur %.1f mm.",
                  view.zoneStart * 100.0, view.zoneEnd * 100.0, length);
    return buffer;
}

} // namespace openstitch::desktop_v2
