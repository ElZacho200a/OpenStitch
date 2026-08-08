# SPDX-License-Identifier: Apache-2.0
import json

import numpy as np
from PIL import Image

from result_writer import write_job_results


def test_write_job_results_produces_expected_schema(tmp_path):
    mask_a = np.zeros((10, 10), dtype=bool)
    mask_a[2:5, 2:5] = True
    masks = [
        {
            "segmentation": mask_a,
            "area_pixels": 9,
            "bbox_xywh": [2, 2, 3, 3],
            "predicted_iou": 0.9,
            "stability_score": 0.95,
            "component_count_before": 1,
            "component_count_after_cleanup": 1,
            "removed_island_count": 0,
            "filled_hole_count": 0,
        }
    ]
    masks_json_path = write_job_results(tmp_path, 10, 10, "input.png", "tiny", masks)
    assert masks_json_path == tmp_path / "masks.json"

    payload = json.loads(masks_json_path.read_text(encoding="utf-8"))
    assert payload["schema_version"] == 1
    assert payload["source"] == "input.png"
    assert payload["model"] == "tiny"
    assert payload["width"] == 10
    assert payload["height"] == 10
    assert len(payload["masks"]) == 1

    entry = payload["masks"][0]
    assert entry["file"] == "masks/mask_0000.png"
    assert entry["area_pixels"] == 9
    assert entry["bbox_xywh"] == [2, 2, 3, 3]
    assert (tmp_path / "masks" / "mask_0000.png").exists()
    assert (tmp_path / "preview.png").exists()
    assert not (tmp_path / "masks.json.tmp").exists()


def test_write_job_results_writes_binary_mask_pixels_correctly(tmp_path):
    mask_a = np.zeros((4, 4), dtype=bool)
    mask_a[0, 0] = True
    masks = [
        {
            "segmentation": mask_a,
            "area_pixels": 1,
            "bbox_xywh": [0, 0, 1, 1],
            "predicted_iou": 0.5,
            "stability_score": 0.5,
        }
    ]
    write_job_results(tmp_path, 4, 4, "input.png", "large", masks)
    saved = np.array(Image.open(tmp_path / "masks" / "mask_0000.png"))
    assert saved[0, 0] == 255
    assert saved[1, 1] == 0


def test_write_job_results_numbers_masks_in_order(tmp_path):
    empty = np.zeros((3, 3), dtype=bool)
    empty[0, 0] = True
    masks = [
        {"segmentation": empty, "area_pixels": 1, "bbox_xywh": [0, 0, 1, 1],
         "predicted_iou": 0.5, "stability_score": 0.5}
        for _ in range(3)
    ]
    write_job_results(tmp_path, 3, 3, "input.png", "small", masks)
    for i in range(3):
        assert (tmp_path / "masks" / f"mask_{i:04d}.png").exists()
