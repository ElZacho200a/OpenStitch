// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/model_catalog.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace openstitch::ai_segmentation;

TEST_CASE("model catalog maps every size to its exact config and checkpoint filenames") {
    const auto& tiny = model_descriptor(ModelId::Tiny);
    CHECK(tiny.worker_id == "tiny");
    CHECK(tiny.config_name == "configs/sam2.1/sam2.1_hiera_t.yaml");
    CHECK(tiny.checkpoint_file == "sam2.1_hiera_tiny.pt");

    const auto& small = model_descriptor(ModelId::Small);
    CHECK(small.worker_id == "small");
    CHECK(small.config_name == "configs/sam2.1/sam2.1_hiera_s.yaml");
    CHECK(small.checkpoint_file == "sam2.1_hiera_small.pt");

    const auto& basePlus = model_descriptor(ModelId::BasePlus);
    CHECK(basePlus.worker_id == "base_plus");
    CHECK(basePlus.config_name == "configs/sam2.1/sam2.1_hiera_b+.yaml");
    CHECK(basePlus.checkpoint_file == "sam2.1_hiera_base_plus.pt");

    const auto& large = model_descriptor(ModelId::Large);
    CHECK(large.worker_id == "large");
    CHECK(large.config_name == "configs/sam2.1/sam2.1_hiera_l.yaml");
    CHECK(large.checkpoint_file == "sam2.1_hiera_large.pt");
}

TEST_CASE("all_models returns the four sizes in Tiny..Large order") {
    const auto& models = all_models();
    REQUIRE(models.size() == 4);
    CHECK(models[0].id == ModelId::Tiny);
    CHECK(models[1].id == ModelId::Small);
    CHECK(models[2].id == ModelId::BasePlus);
    CHECK(models[3].id == ModelId::Large);
}

TEST_CASE("model_id_from_worker_id round-trips every catalog entry") {
    for (const auto& entry : all_models()) {
        const auto found = model_id_from_worker_id(entry.worker_id);
        REQUIRE(found.has_value());
        CHECK(*found == entry.id);
    }
    CHECK_FALSE(model_id_from_worker_id("huge").has_value());
    CHECK_FALSE(model_id_from_worker_id("").has_value());
}
