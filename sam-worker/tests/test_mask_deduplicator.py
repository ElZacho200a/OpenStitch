# SPDX-License-Identifier: Apache-2.0
import numpy as np

from mask_deduplicator import deduplicate, mask_iou


def test_mask_iou_identical_masks_is_one():
    mask = np.zeros((10, 10), dtype=bool)
    mask[2:6, 2:6] = True
    assert mask_iou(mask, mask) == 1.0


def test_mask_iou_disjoint_masks_is_zero():
    a = np.zeros((10, 10), dtype=bool)
    a[0:3, 0:3] = True
    b = np.zeros((10, 10), dtype=bool)
    b[7:10, 7:10] = True
    assert mask_iou(a, b) == 0.0


def test_deduplicate_drops_near_duplicate_keeping_the_higher_scored_one():
    a = np.zeros((10, 10), dtype=bool)
    a[0:6, 0:6] = True
    b = a.copy()
    b[5, 5] = False  # quasi identique (IoU tres eleve)
    masks = [
        {"segmentation": a, "stability_score": 0.7, "predicted_iou": 0.7, "area_pixels": 36},
        {"segmentation": b, "stability_score": 0.95, "predicted_iou": 0.95, "area_pixels": 35},
    ]
    kept = deduplicate(masks, iou_threshold=0.9)
    assert kept == [1]


def test_deduplicate_keeps_distinct_masks():
    a = np.zeros((10, 10), dtype=bool)
    a[0:3, 0:3] = True
    b = np.zeros((10, 10), dtype=bool)
    b[7:10, 7:10] = True
    masks = [
        {"segmentation": a, "stability_score": 0.9, "predicted_iou": 0.9, "area_pixels": 9},
        {"segmentation": b, "stability_score": 0.9, "predicted_iou": 0.9, "area_pixels": 9},
    ]
    kept = deduplicate(masks, iou_threshold=0.9)
    assert kept == [0, 1]
