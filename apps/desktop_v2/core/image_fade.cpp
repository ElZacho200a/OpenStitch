// SPDX-License-Identifier: Apache-2.0
#include "image_fade.hpp"

#include <algorithm>
#include <cmath>

#include "openstitch/document/multi_blend.hpp"

namespace openstitch::desktop_v2 {

std::string imageFadeIncompatibility(const document::Project& project, std::uint64_t objectId) {
    if (project.imageSource().original.rgba.empty()) {
        return "Ce projet n'a pas d'image source : importez l'image à reproduire.";
    }
    const auto* object = project.findEmbroidery(ObjectId{objectId});
    if (object == nullptr) {
        return "Sélectionnez un seul remplissage.";
    }
    if (document::find_gradient_group(project, ObjectId{objectId})) {
        return {};
    }
    if (!object->is_tatami() && !object->is_directional()) {
        return "Choisissez un remplissage (tatami ou directionnel) ou un fondu.";
    }
    return {};
}

Result<ImageFadePlan> computeImageFade(const document::Project& project, std::uint64_t objectId,
                                       const ImageFadeSettings& settings) {
    if (auto why = imageFadeIncompatibility(project, objectId); !why.empty()) {
        return std::unexpected(Error{ErrorCategory::UserInput, std::move(why), {}, {}, {}});
    }
    // Réglages conseillés (mesurés sur un Turner) ; seuls les trois réglages de l'utilisateur
    // varient.
    project_ops::ImageBlendRequest request;
    request.threads = static_cast<std::size_t>(std::clamp(settings.threads, 2, 8));
    request.cell = Micrometers{
        static_cast<std::int32_t>(std::lround(std::clamp(settings.cellMm, 0.5, 5.0) * 1000.0))};
    request.continuous_rows = settings.continuousRows;
    request.min_share = 0.15;
    request.max_threads_per_point = 3;
    auto spec = project_ops::image_blend_spec(project, ObjectId{objectId}, request);
    if (!spec) {
        return std::unexpected(spec.error());
    }
    ImageFadePlan plan;
    plan.objectId = objectId;
    plan.existingFade = document::find_gradient_group(project, ObjectId{objectId}).has_value();
    plan.spec = std::move(*spec);
    return plan;
}

ServiceResult commitImageFade(DocumentSession& session, const ImageFadePlan& plan) {
    auto command = plan.existingFade
                       ? project_ops::make_edit_multicolor_gradient_command(
                             session.projectForIdAllocation(), ObjectId{plan.objectId}, plan.spec)
                       : project_ops::make_create_multicolor_gradient_command(
                             session.projectForIdAllocation(), ObjectId{plan.objectId}, plan.spec);
    if (!command) {
        return {false, command.error().message};
    }
    session.execute(std::move(*command));
    return {true, {}};
}

} // namespace openstitch::desktop_v2
