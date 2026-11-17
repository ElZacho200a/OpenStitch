// SPDX-License-Identifier: Apache-2.0
#include "stage_status.hpp"

#include <algorithm>

namespace openstitch::desktop_v2 {

namespace {

StageStatus make(StageId id, StageState state, std::string detail) {
    return StageStatus{id, state, std::move(detail)};
}

} // namespace

std::string_view stageIdName(StageId id) {
    switch (id) {
    case StageId::Image:
        return "image";
    case StageId::Segmentation:
        return "segmentation";
    case StageId::Vector:
        return "vector";
    case StageId::Points:
        return "points";
    }
    return "image";
}

std::string_view stageStateName(StageState state) {
    switch (state) {
    case StageState::Empty:
        return "empty";
    case StageState::UpToDate:
        return "upToDate";
    case StageState::Stale:
        return "stale";
    case StageState::Blocked:
        return "blocked";
    }
    return "empty";
}

std::array<StageStatus, 4> computeStageStatus(const document::Project& project,
                                              const StageInputs& inputs) {
    std::array<StageStatus, 4> out;

    // --- image -------------------------------------------------------------------------------
    const bool hasImage = !project.imageSource().original.empty();
    out[0] =
        hasImage
            ? make(StageId::Image, StageState::UpToDate,
                   "Image de travail : " + std::to_string(project.imageSource().original.width) +
                       " x " + std::to_string(project.imageSource().original.height) + " px, " +
                       std::to_string(project.imageSource().ops.size()) + " retouche(s)")
            : make(StageId::Image, StageState::Empty, "Aucune image dans le projet");

    // --- segmentation ------------------------------------------------------------------------
    const bool hasSegmentation = project.imageSource().segmentation.has_value();
    const std::string reason =
        inputs.dirtyReason.empty() ? "des régions ont été retouchées" : inputs.dirtyReason;
    const bool segmentationStale = hasSegmentation && inputs.segmentationDirty;
    if (!hasImage) {
        out[1] = make(StageId::Segmentation, StageState::Blocked,
                      "Segmentation impossible : aucune image");
    } else if (!hasSegmentation) {
        out[1] = make(StageId::Segmentation, StageState::Empty, "Image pas encore segmentée");
    } else if (segmentationStale) {
        out[1] = make(StageId::Segmentation, StageState::Stale,
                      "Segmentation obsolète : " + reason + ", formes pas à jour");
    } else {
        out[1] =
            make(StageId::Segmentation, StageState::UpToDate,
                 std::to_string(project.imageSource().segmentation->region_count()) + " région(s)");
    }

    // --- vectorisation -----------------------------------------------------------------------
    const auto& vectors = project.vector_objects;
    std::size_t orphans = 0;
    for (const auto& vector : vectors) {
        const auto* source = project.findImageSource(vector.source_image);
        if (vector.source_region && (source == nullptr || !source->segmentation ||
                                     !source->segmentation->find(*vector.source_region))) {
            ++orphans;
        }
    }
    if (vectors.empty()) {
        out[2] = make(StageId::Vector, StageState::Empty, "Aucune forme vectorielle");
    } else if (segmentationStale) {
        out[2] = make(StageId::Vector, StageState::Stale,
                      "Formes pas à jour : la segmentation a changé (" + reason + ")");
    } else if (orphans > 0) {
        out[2] = make(StageId::Vector, StageState::Stale,
                      std::to_string(orphans) + " forme(s) ne suivent plus leur région");
    } else {
        out[2] = make(StageId::Vector, StageState::UpToDate,
                      std::to_string(vectors.size()) + " forme(s)");
    }

    // --- points ------------------------------------------------------------------------------
    const auto& objects = project.embroidery_objects;
    std::size_t dangling = 0;
    for (const auto& object : objects) {
        if (object.source_vector.valid() &&
            std::none_of(vectors.begin(), vectors.end(),
                         [&](const auto& v) { return v.id == object.source_vector; })) {
            ++dangling;
        }
    }
    if (objects.empty()) {
        out[3] = make(StageId::Points, StageState::Empty, "Aucun objet de broderie");
    } else if (out[2].state == StageState::Stale) {
        out[3] = make(StageId::Points, StageState::Stale,
                      "Points obsolètes : les formes ne sont pas à jour");
    } else if (dangling > 0) {
        out[3] = make(StageId::Points, StageState::Stale,
                      std::to_string(dangling) + " objet(s) sans forme source");
    } else {
        out[3] = make(StageId::Points, StageState::UpToDate,
                      std::to_string(objects.size()) + " objet(s) de broderie");
    }
    return out;
}

std::optional<StageId> stageForOperation(std::string_view op) {
    for (const std::string_view neutral : {"set_canvas", "set_machine", "rename", "set_semantic",
                                           "group", "ungroup", "set_visible", "set_locked"}) {
        if (op == neutral) {
            return std::nullopt;
        }
    }
    if (op == "set_source_image" || op.starts_with("image_")) {
        return StageId::Image;
    }
    if (op == "segment_image" || op.starts_with("seg_")) {
        return StageId::Segmentation;
    }
    if (op == "vectorize_segmentation" || op == "remove_small_pieces" || op == "fill_small_holes" ||
        op == "import_svg" || op == "import_dxf") {
        return StageId::Vector;
    }
    return StageId::Points;
}

std::optional<StageId> stageForLiveTool(std::string_view tool,
                                        const std::vector<std::string>& operations) {
    if (tool == "project_digitize" || tool == "semantic_import" || tool == "semantic_replace") {
        return StageId::Points;
    }
    if (tool != "project_apply") {
        return std::nullopt;
    }
    for (auto it = operations.rbegin(); it != operations.rend(); ++it) {
        if (const auto stage = stageForOperation(*it)) {
            return stage;
        }
    }
    return std::nullopt;
}

} // namespace openstitch::desktop_v2
