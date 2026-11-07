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
struct WarpPreview {
    std::vector<geometry::PathSet> paths;
    std::unique_ptr<commands::ICommand> command;
};
[[nodiscard]] Result<void> validateWarpOptions(WarpOptions options);
// `object` is an embroidery id, as in the Studio selection. Work only on the supplied snapshot.
[[nodiscard]] Result<WarpPreview> previewWarp(const document::Project& project, ObjectId object,
                                              WarpOptions options,
                                              const std::vector<WarpTouch>& touches,
                                              bool prepareCommand, const TaskContext& task = {});
} // namespace openstitch::desktop_v2
