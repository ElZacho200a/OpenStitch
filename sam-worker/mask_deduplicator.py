# SPDX-License-Identifier: Apache-2.0
"""Suppression des masques quasi-doublons produits par
SAM2AutomaticMaskGenerator (plusieurs points de la grille tombent souvent
sur la meme region et produisent des masques quasi identiques)."""
from __future__ import annotations

import numpy as np


def mask_iou(mask_a: np.ndarray, mask_b: np.ndarray) -> float:
    a = mask_a.astype(bool)
    b = mask_b.astype(bool)
    union = np.logical_or(a, b).sum()
    if union == 0:
        return 0.0
    inter = np.logical_and(a, b).sum()
    return float(inter) / float(union)


def deduplicate(masks: list[dict], iou_threshold: float = 0.9) -> list[int]:
    """Renvoie les indices (dans `masks`) a conserver, triees par ordre
    d'insertion d'origine. Priorite : stability_score, puis predicted_iou,
    puis aire -- un masque est un doublon d'un masque deja retenu si son IoU
    avec lui depasse `iou_threshold`."""
    order = sorted(
        range(len(masks)),
        key=lambda i: (
            masks[i].get("stability_score", 0.0),
            masks[i].get("predicted_iou", 0.0),
            masks[i].get("area_pixels", 0),
        ),
        reverse=True,
    )
    kept_masks: list[np.ndarray] = []
    kept_indices: list[int] = []
    for idx in order:
        candidate = masks[idx]["segmentation"]
        if any(mask_iou(candidate, kept) >= iou_threshold for kept in kept_masks):
            continue
        kept_masks.append(candidate)
        kept_indices.append(idx)
    return sorted(kept_indices)
