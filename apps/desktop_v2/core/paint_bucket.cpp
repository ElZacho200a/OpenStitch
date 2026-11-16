// SPDX-License-Identifier: Apache-2.0
#include "paint_bucket.hpp"
#include "openstitch/commands/project_commands.hpp"
#include "openstitch/geometry/fill_gap.hpp"
#include <cmath>

namespace openstitch::desktop_v2 {
Result<geometry::PathSet> previewBucket(const document::Project& project, Vec2um seed,
                                        Micrometers seal, const TaskContext& task) {
    if (seal.value < 0 || seal.value > 1000 ||
        std::abs(static_cast<double>(seed.x.value)) > 1000000 ||
        std::abs(static_cast<double>(seed.y.value)) > 1000000)
        return fail(ErrorCategory::UserInput, "Position ou fermeture du pot hors limites");
    if (task.cancelled())
        return cancelled_error();
    std::vector<geometry::PathSet> obstacles;
    for (const auto& object : project.vector_objects)
        if (object.visible)
            obstacles.insert(obstacles.end(), object.paths.begin(), object.paths.end());
    auto result = geometry::fill_gap_at(obstacles, seed, seal);
    if (task.cancelled())
        return cancelled_error();
    return result;
}
Result<ObjectId> commitBucket(DocumentSession& session, std::uint64_t revision,
                              geometry::PathSet preview) {
    if (session.revision() != revision)
        return fail(ErrorCategory::UserInput, "Le document a changé ; recommencez le remplissage");
    if (!preview.outer.closed || preview.outer.nodes.size() < 3)
        return fail(ErrorCategory::UserInput, "Le remplissage ne forme pas un contour fermé");
    document::VectorObject object;
    object.id = session.allocateObjectId();
    const auto id = object.id;
    object.name = "Vide rempli";
    object.rgb = {80, 120, 200};
    object.paths.push_back(std::move(preview));
    session.execute(std::make_unique<commands::AddVectorObjectCommand>(std::move(object)));
    return id;
}
} // namespace openstitch::desktop_v2
