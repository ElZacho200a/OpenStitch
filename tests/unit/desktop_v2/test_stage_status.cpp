// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include "stage_status.hpp"

using namespace openstitch;
using namespace openstitch::desktop_v2;

namespace {

document::Project withImage() {
    document::Project project;
    project.original.width = 2;
    project.original.height = 2;
    project.original.rgba.assign(16, 255);
    return project;
}

void addSegmentation(document::Project& project, std::uint64_t regionId) {
    segmentation::Segmentation seg;
    seg.width = 2;
    seg.height = 2;
    seg.labels.assign(4, 1);
    segmentation::Region region;
    region.id = RegionId{regionId};
    region.pixel_count = 4;
    seg.region_slots.push_back(region);
    project.segmentation = seg;
}

void addVector(document::Project& project, std::uint64_t id, std::uint64_t region) {
    document::VectorObject vector;
    vector.id = ObjectId{id};
    if (region != 0) {
        vector.source_region = RegionId{region};
    }
    project.vector_objects.push_back(vector);
}

void addObject(document::Project& project, std::uint64_t id, std::uint64_t vector) {
    document::EmbroideryObject object;
    object.id = ObjectId{id};
    object.source_vector = ObjectId{vector};
    project.embroidery_objects.push_back(object);
}

} // namespace

TEST_CASE("stage status: an empty project is empty and the segmentation is blocked",
          "[stage_status]") {
    const auto status = computeStageStatus(document::Project{});
    CHECK(status[0].state == StageState::Empty);
    CHECK(status[1].state == StageState::Blocked);
    CHECK(status[2].state == StageState::Empty);
    CHECK(status[3].state == StageState::Empty);
    CHECK(stageIdName(status[2].id) == "vector");
    CHECK(stageStateName(status[1].state) == "blocked");
}

TEST_CASE("stage status: a complete chain is up to date", "[stage_status]") {
    auto project = withImage();
    addSegmentation(project, 1);
    addVector(project, 10, 1);
    addObject(project, 20, 10);
    const auto status = computeStageStatus(project);
    for (const auto& stage : status) {
        CHECK(stage.state == StageState::UpToDate);
        CHECK(!stage.detail.empty());
    }
}

TEST_CASE("stage status: image without segmentation leaves the segmentation empty",
          "[stage_status]") {
    const auto status = computeStageStatus(withImage());
    CHECK(status[0].state == StageState::UpToDate);
    CHECK(status[1].state == StageState::Empty);
}

TEST_CASE("stage status: pending region corrections make shapes and points stale",
          "[stage_status]") {
    auto project = withImage();
    addSegmentation(project, 1);
    addVector(project, 10, 1);
    addObject(project, 20, 10);
    StageInputs inputs;
    inputs.segmentationDirty = true;
    inputs.dirtyReason = "3 regions corrigees";
    const auto status = computeStageStatus(project, inputs);
    CHECK(status[0].state == StageState::UpToDate);
    CHECK(status[1].state == StageState::Stale);
    CHECK(status[1].detail.find("3 regions corrigees") != std::string::npos);
    CHECK(status[2].state == StageState::Stale);
    CHECK(status[3].state == StageState::Stale);
}

TEST_CASE("stage status: dirty flag without shapes only stales the segmentation",
          "[stage_status]") {
    auto project = withImage();
    addSegmentation(project, 1);
    StageInputs inputs;
    inputs.segmentationDirty = true;
    const auto status = computeStageStatus(project, inputs);
    CHECK(status[1].state == StageState::Stale);
    CHECK(status[2].state == StageState::Empty);
    CHECK(status[3].state == StageState::Empty);
}

TEST_CASE("stage status: a shape whose region disappeared is stale, hand drawn shapes are not",
          "[stage_status]") {
    auto project = withImage();
    addSegmentation(project, 1);
    addVector(project, 10, 99); // region 99 does not exist
    addVector(project, 11, 0);  // drawn by hand
    const auto status = computeStageStatus(project);
    CHECK(status[2].state == StageState::Stale);

    auto handDrawn = withImage();
    addVector(handDrawn, 11, 0);
    CHECK(computeStageStatus(handDrawn)[2].state == StageState::UpToDate);
}

TEST_CASE("stage status: an embroidery object without its source shape is stale",
          "[stage_status]") {
    auto project = withImage();
    addVector(project, 10, 0);
    addObject(project, 20, 77); // vector 77 does not exist
    const auto status = computeStageStatus(project);
    CHECK(status[2].state == StageState::UpToDate);
    CHECK(status[3].state == StageState::Stale);
}
