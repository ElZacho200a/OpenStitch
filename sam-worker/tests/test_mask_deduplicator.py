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


def _reference_deduplicate(masks, iou_threshold=0.9):
    """Implementation d'origine (avant l'audit perf 2026-09), reference d'equivalence."""
    order = sorted(
        range(len(masks)),
        key=lambda i: (
            masks[i].get("stability_score", 0.0),
            masks[i].get("predicted_iou", 0.0),
            masks[i].get("area_pixels", 0),
        ),
        reverse=True,
    )
    kept_masks, kept_indices = [], []
    for idx in order:
        candidate = masks[idx]["segmentation"]
        if any(mask_iou(candidate, kept) >= iou_threshold for kept in kept_masks):
            continue
        kept_masks.append(candidate)
        kept_indices.append(idx)
    return sorted(kept_indices)


def test_deduplicate_matches_reference_on_random_near_duplicates():
    rng = np.random.default_rng(2026)
    for trial in range(30):
        h, w = 60, 80
        masks = []
        for _ in range(int(rng.integers(5, 25))):
            m = np.zeros((h, w), dtype=bool)
            y0, x0 = int(rng.integers(0, h - 5)), int(rng.integers(0, w - 5))
            y1, x1 = int(rng.integers(y0 + 1, h + 1)), int(rng.integers(x0 + 1, w + 1))
            m[y0:y1, x0:x1] = True
            masks.append(m)
            # quasi-doublons : quelques pixels retires/ajoutes
            for _ in range(int(rng.integers(0, 3))):
                d = m.copy()
                flips = rng.integers(0, h * w, size=int(rng.integers(0, 12)))
                d.flat[flips] = ~d.flat[flips]
                masks.append(d)
        if trial % 7 == 0:
            masks.append(np.zeros((h, w), dtype=bool))  # masque vide
        entries = [
            {
                "segmentation": m if k % 2 else m.astype(np.uint8),  # bool ou uint8
                "stability_score": float(rng.choice([0.9, 0.95, 0.97])),
                "predicted_iou": float(rng.random()),
                "area_pixels": int(m.sum()),
            }
            for k, m in enumerate(masks)
        ]
        for threshold in (0.5, 0.8, 0.9, 0.97):
            assert deduplicate(entries, threshold) == _reference_deduplicate(entries, threshold)
