# SPDX-License-Identifier: Apache-2.0
"""Banc (pas un test pytest) : deduplication des masques, ancienne vs nouvelle
implementation, sur un jeu synthetique proche d'un profil SAM "balanced"
(1024x1024, ~250 masques dont un tiers de quasi-doublons). Cf.
docs/performance-audit.md. Usage : python tests/bench_dedup.py"""
from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from mask_deduplicator import deduplicate  # noqa: E402
from test_mask_deduplicator import _reference_deduplicate  # noqa: E402


def make_masks(n_base: int, size: int, seed: int) -> list[dict]:
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:size, 0:size]
    masks = []
    for _ in range(n_base):
        cy, cx = rng.integers(0, size, size=2)
        r = int(rng.integers(8, size // 5))
        m = (yy - cy) ** 2 + (xx - cx) ** 2 <= r * r
        masks.append(m)
        if rng.random() < 0.5:
            d = m.copy()
            d[max(0, cy - r) : cy - r + 3, :] = False
            masks.append(d)
    return [
        {
            "segmentation": m,
            "stability_score": float(rng.random()),
            "predicted_iou": float(rng.random()),
            "area_pixels": int(m.sum()),
        }
        for m in masks
    ]


def main() -> None:
    masks = make_masks(170, 1024, 7)
    print(f"{len(masks)} masques 1024x1024")
    for name, fn in (("reference", _reference_deduplicate), ("optimise", deduplicate)):
        times = []
        for _ in range(3):
            t0 = time.perf_counter()
            kept = fn(masks, 0.9)
            times.append(time.perf_counter() - t0)
        print(f"{name:10s} min {min(times) * 1000:9.1f} ms  conserves {len(kept)}")
    assert deduplicate(masks, 0.9) == _reference_deduplicate(masks, 0.9)
    print("resultats identiques")


if __name__ == "__main__":
    main()
