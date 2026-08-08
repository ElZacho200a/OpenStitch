// SPDX-License-Identifier: Apache-2.0
#include "openstitch/ai_segmentation/mask_result.hpp"

#include <nlohmann/json.hpp>

namespace openstitch::ai_segmentation {

namespace {
using json = nlohmann::json;
}  // namespace

Result<MaskCollection> parse_masks_json(std::string_view json_text) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::parse_error& e) {
        return fail(ErrorCategory::InvalidFile, "masks.json invalide (JSON malformé)", e.what());
    }

    try {
        MaskCollection collection;
        collection.schema_version = root.at("schema_version").get<int>();
        collection.source_file = root.at("source").get<std::string>();
        collection.model_worker_id = root.at("model").get<std::string>();
        collection.image_width = root.at("width").get<int>();
        collection.image_height = root.at("height").get<int>();

        for (const auto& entry : root.at("masks")) {
            MaskEntry mask;
            mask.id = entry.at("id").get<int>();
            mask.file = entry.at("file").get<std::string>();
            mask.area_pixels = entry.at("area_pixels").get<std::size_t>();
            const auto bbox = entry.at("bbox_xywh");
            if (bbox.size() != 4) {
                return fail(ErrorCategory::InvalidFile,
                            "masks.json invalide : bbox_xywh doit avoir 4 éléments");
            }
            for (std::size_t i = 0; i < 4; ++i) {
                mask.bbox_xywh[i] = bbox.at(i).get<int>();
            }
            mask.predicted_iou = entry.at("predicted_iou").get<double>();
            mask.stability_score = entry.at("stability_score").get<double>();
            mask.component_count_before = entry.value("component_count_before", 1);
            mask.component_count_after_cleanup = entry.value("component_count_after_cleanup", 1);
            mask.removed_island_count = entry.value("removed_island_count", 0);
            mask.filled_hole_count = entry.value("filled_hole_count", 0);
            collection.masks.push_back(std::move(mask));
        }
        return collection;
    } catch (const json::exception& e) {
        return fail(ErrorCategory::InvalidFile, "masks.json invalide (champ manquant ou de type incorrect)",
                    e.what());
    }
}

std::string serialize_masks_json(const MaskCollection& collection) {
    json root;
    root["schema_version"] = collection.schema_version;
    root["source"] = collection.source_file;
    root["model"] = collection.model_worker_id;
    root["width"] = collection.image_width;
    root["height"] = collection.image_height;
    json masks = json::array();
    for (const auto& mask : collection.masks) {
        json entry;
        entry["id"] = mask.id;
        entry["file"] = mask.file;
        entry["area_pixels"] = mask.area_pixels;
        entry["bbox_xywh"] = mask.bbox_xywh;
        entry["predicted_iou"] = mask.predicted_iou;
        entry["stability_score"] = mask.stability_score;
        entry["component_count_before"] = mask.component_count_before;
        entry["component_count_after_cleanup"] = mask.component_count_after_cleanup;
        entry["removed_island_count"] = mask.removed_island_count;
        entry["filled_hole_count"] = mask.filled_hole_count;
        masks.push_back(std::move(entry));
    }
    root["masks"] = std::move(masks);
    return root.dump(2);
}

}  // namespace openstitch::ai_segmentation
