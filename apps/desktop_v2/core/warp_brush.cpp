// SPDX-License-Identifier: Apache-2.0
#include "warp_brush.hpp"
#include "openstitch/commands/vector_path_commands.hpp"
#include "shape_resolver.hpp"
#include <cmath>

namespace openstitch::desktop_v2 {
Result<void> validateWarpOptions(WarpOptions options) {
    if (!std::isfinite(options.radiusMm) || options.radiusMm < 0.5 || options.radiusMm > 60 ||
        !std::isfinite(options.strength) || options.strength < 0.05 || options.strength > 1)
        return fail(ErrorCategory::UserInput, "Rayon : 0,5 à 60 mm ; intensité : 5 à 100 %");
    return {};
}
Result<WarpPreview> previewWarp(const document::Project& project, Target target,
                                WarpOptions options, const std::vector<WarpTouch>& touches,
                                bool prepareCommand, const TaskContext& task, WarpStroke resume) {
    if (const auto valid = validateWarpOptions(options); !valid)
        return std::unexpected(valid.error());
    if (touches.empty() || touches.size() > 4096)
        return fail(ErrorCategory::UserInput, "Geste vide ou trop long (4096 positions maximum)");
    // La forme est l'unité d'édition : la broderie éventuelle n'est pas requise.
    const auto shape = shapeOfTarget(project, target);
    const auto* vector = shape ? project.findObject(*shape) : nullptr;
    const auto* embroidery = target.kind == TargetKind::EmbroideryObject
                                 ? project.findEmbroidery(ObjectId{target.id})
                                 : nullptr;
    if (!vector || project.shapeEditLocked(vector->id) || !vector->visible ||
        (embroidery && (embroidery->locked || !embroidery->visible)))
        return fail(ErrorCategory::UserInput,
                    "Sélectionnez une forme vectorielle visible et déverrouillée");
    // Reprise : on ne rejoue que les touches non encore consommees. Sans reprise valide, on part
    // du contour de la forme (meme suite de touches de pinceau, donc memes octets).
    const bool resuming = resume.consumed > 0 && resume.consumed <= touches.size() &&
                          resume.paths.size() == vector->paths.size();
    WarpPreview result{resuming ? std::move(resume.paths) : vector->paths, {}, {}};
    auto last = resuming ? resume.last : touches.front().point;
    for (std::size_t index = resuming ? resume.consumed : 0; index < touches.size(); ++index) {
        const auto& touch = touches[index];
        if (task.cancelled())
            return cancelled_error();
        if (std::abs(static_cast<double>(touch.point.x.value)) > 1000000 ||
            std::abs(static_cast<double>(touch.point.y.value)) > 1000000)
            return fail(ErrorCategory::UserInput, "Position du pinceau hors limites (1000 mm)");
        const double dx = static_cast<double>(touch.point.x.value) - last.x.value;
        const double dy = static_cast<double>(touch.point.y.value) - last.y.value;
        if (std::hypot(dx, dy) < options.radiusMm * 1000 / 8)
            continue;
        geometry::BrushDab dab{touch.point, options.radiusMm * 1000, options.strength,
                               touch.smooth ? geometry::BrushMode::Smooth
                                            : geometry::BrushMode::Push,
                               touch.point - last};
        for (auto& set : result.paths)
            set = geometry::apply_brush_dab(set, dab);
        last = touch.point;
    }
    result.stroke = {result.paths, last, touches.size()};
    if (prepareCommand) {
        auto made = commands::make_brush_stroke_command(project, vector->id, result.paths);
        if (!made.command)
            return fail(ErrorCategory::UserInput, made.error);
        result.command = std::move(made.command);
    }
    return result;
}
} // namespace openstitch::desktop_v2
