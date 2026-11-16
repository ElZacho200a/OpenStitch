// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "document_session.hpp"
#include "openstitch/core/task_control.hpp"

namespace openstitch::desktop_v2 {
[[nodiscard]] Result<geometry::PathSet> previewBucket(const document::Project& project, Vec2um seed,
                                                      Micrometers seal = Micrometers{100},
                                                      const TaskContext& task = {});
// The preview must have been produced for `revision`. Allocates an id only on explicit commit.
[[nodiscard]] Result<ObjectId> commitBucket(DocumentSession& session, std::uint64_t revision,
                                            geometry::PathSet preview);
} // namespace openstitch::desktop_v2
