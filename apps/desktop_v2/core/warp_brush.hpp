// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "document_session.hpp"
#include "openstitch/core/task_control.hpp"
#include "openstitch/geometry/brush.hpp"

namespace openstitch::desktop_v2 {
struct WarpOptions {
    double radiusMm{5};
    double strength{0.6};
};
struct WarpTouch {
    Vec2um point;
    bool smooth{false};
};
// Etat d'un geste en cours : contour apres les touches deja jouees. Permet de ne rejouer que les
// nouvelles touches (le cout d'une mise a jour ne grandit plus avec la longueur du trait).
struct WarpStroke {
    std::vector<geometry::PathSet> paths;
    Vec2um last{};
    std::size_t consumed{0};
};
struct WarpPreview {
    std::vector<geometry::PathSet> paths;
    std::unique_ptr<commands::ICommand> command;
    WarpStroke
        stroke; // a repasser en `resume` avec la meme cible, les memes options et les memes touches
};
[[nodiscard]] Result<void> validateWarpOptions(WarpOptions options);
// `target` is a shape or an embroidery of the Studio selection: the warp only moves the shape's
// contours, so an unconverted shape works too. Work only on the supplied snapshot.
[[nodiscard]] Result<WarpPreview> previewWarp(const document::Project& project, Target target,
                                              WarpOptions options,
                                              const std::vector<WarpTouch>& touches,
                                              bool prepareCommand, const TaskContext& task = {},
                                              WarpStroke resume = {});
} // namespace openstitch::desktop_v2
