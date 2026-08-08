// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/mask_result.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace openstitch::ai_segmentation;

namespace {

MaskCollection sampleCollection() {
    MaskCollection collection;
    collection.schema_version = 1;
    collection.source_file = "input.png";
    collection.model_worker_id = "tiny";
    collection.image_width = 640;
    collection.image_height = 480;

    MaskEntry a;
    a.id = 0;
    a.file = "masks/mask_0000.png";
    a.area_pixels = 12'345;
    a.bbox_xywh = {10, 20, 100, 80};
    a.predicted_iou = 0.91;
    a.stability_score = 0.95;
    a.component_count_before = 2;
    a.component_count_after_cleanup = 1;
    a.removed_island_count = 1;
    a.filled_hole_count = 0;
    collection.masks.push_back(a);

    MaskEntry b;
    b.id = 1;
    b.file = "masks/mask_0001.png";
    b.area_pixels = 500;
    b.bbox_xywh = {200, 150, 30, 30};
    b.predicted_iou = 0.6;
    b.stability_score = 0.7;
    collection.masks.push_back(b);

    return collection;
}

}  // namespace

TEST_CASE("masks.json round-trips through serialize and parse") {
    const MaskCollection original = sampleCollection();
    const std::string json = serialize_masks_json(original);

    const auto parsed = parse_masks_json(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->schema_version == original.schema_version);
    CHECK(parsed->source_file == original.source_file);
    CHECK(parsed->model_worker_id == original.model_worker_id);
    CHECK(parsed->image_width == original.image_width);
    CHECK(parsed->image_height == original.image_height);
    REQUIRE(parsed->masks.size() == 2);
    CHECK(parsed->masks[0].file == "masks/mask_0000.png");
    CHECK(parsed->masks[0].area_pixels == 12'345);
    CHECK(parsed->masks[0].bbox_xywh == std::array<int, 4>{10, 20, 100, 80});
    CHECK(parsed->masks[0].predicted_iou == Catch::Approx(0.91));
    CHECK(parsed->masks[0].removed_island_count == 1);
    CHECK(parsed->masks[1].stability_score == Catch::Approx(0.7));
}

TEST_CASE("parse_masks_json rejects malformed JSON") {
    const auto parsed = parse_masks_json("{not json");
    REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("parse_masks_json rejects a document missing required fields") {
    const auto parsed = parse_masks_json(R"({"schema_version": 1})");
    REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("parse_masks_json rejects a bbox with the wrong arity") {
    const std::string json = R"({
        "schema_version": 1, "source": "input.png", "model": "tiny",
        "width": 10, "height": 10,
        "masks": [{"id": 0, "file": "masks/mask_0000.png", "area_pixels": 5,
                   "bbox_xywh": [0, 0, 1], "predicted_iou": 0.5, "stability_score": 0.5}]
    })";
    const auto parsed = parse_masks_json(json);
    REQUIRE_FALSE(parsed.has_value());
}

TEST_CASE("parse_masks_json defaults optional cleanup stats to sane values") {
    const std::string json = R"({
        "schema_version": 1, "source": "input.png", "model": "large",
        "width": 10, "height": 10,
        "masks": [{"id": 0, "file": "masks/mask_0000.png", "area_pixels": 5,
                   "bbox_xywh": [0, 0, 1, 1], "predicted_iou": 0.5, "stability_score": 0.5}]
    })";
    const auto parsed = parse_masks_json(json);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->masks.size() == 1);
    CHECK(parsed->masks[0].component_count_before == 1);
    CHECK(parsed->masks[0].component_count_after_cleanup == 1);
    CHECK(parsed->masks[0].removed_island_count == 0);
    CHECK(parsed->masks[0].filled_hole_count == 0);
}
