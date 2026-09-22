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


class _PreparedMask:
    """Masque booleen + aire + boite englobante, calcules UNE fois par masque
    (audit perf 2026-09, docs/performance-audit.md : l'ancienne boucle
    reconvertissait et parcourait les deux images entieres a chaque
    comparaison candidat/masque retenu)."""

    __slots__ = ("mask", "area", "r0", "r1", "c0", "c1")

    def __init__(self, mask: np.ndarray) -> None:
        self.mask = np.asarray(mask).astype(bool, copy=False)
        self.area = int(self.mask.sum())
        if self.area:
            rows = np.flatnonzero(self.mask.any(axis=1))
            cols = np.flatnonzero(self.mask.any(axis=0))
            self.r0, self.r1 = int(rows[0]), int(rows[-1]) + 1
            self.c0, self.c1 = int(cols[0]), int(cols[-1]) + 1
        else:
            self.r0 = self.r1 = self.c0 = self.c1 = 0


def _is_duplicate(a: _PreparedMask, b: _PreparedMask, iou_threshold: float) -> bool:
    """Equivalent exact de `mask_iou(a, b) >= iou_threshold` :
    - intersection et union sont les memes entiers (l'intersection ne peut
      exister que dans le recouvrement des deux boites, union = aire A +
      aire B - intersection), donc la meme IoU flottante ;
    - IoU <= min(aires) / max(aires) (inter <= min, union >= max ; la
      division flottante arrondie est monotone), donc un rapport d'aires
      sous le seuil exclut le doublon sans aucun parcours de pixels."""
    if a.area == 0 or b.area == 0:
        return 0.0 >= iou_threshold  # union = aire de l'autre (ou 0) : IoU = 0.0
    small, large = (a.area, b.area) if a.area <= b.area else (b.area, a.area)
    if float(small) / float(large) < iou_threshold:
        return False
    r0, r1 = max(a.r0, b.r0), min(a.r1, b.r1)
    c0, c1 = max(a.c0, b.c0), min(a.c1, b.c1)
    if r0 >= r1 or c0 >= c1:
        inter = 0
    else:
        inter = int(np.logical_and(a.mask[r0:r1, c0:c1], b.mask[r0:r1, c0:c1]).sum())
    union = a.area + b.area - inter
    return float(inter) / float(union) >= iou_threshold


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
    kept_masks: list[_PreparedMask] = []
    kept_indices: list[int] = []
    for idx in order:
        candidate = _PreparedMask(masks[idx]["segmentation"])
        if any(_is_duplicate(candidate, kept, iou_threshold) for kept in kept_masks):
            continue
        kept_masks.append(candidate)
        kept_indices.append(idx)
    return sorted(kept_indices)
