# SPDX-License-Identifier: Apache-2.0
"""Nettoyage OpenCV d'un masque binaire individuel : suppression des ilots
(petites composantes disjointes du masque), comblement des petits trous
enclos. Fonctionne uniquement avec numpy/opencv-python -- jamais de
dependance a `sam2._C` (son absence ne doit jamais empecher le nettoyage,
cf. exigence "doit marcher sans l'extension C optionnelle").
"""
from __future__ import annotations

import cv2
import numpy as np


def count_components(mask: np.ndarray) -> int:
    """Nombre de composantes connexes (8-connexite) du masque, hors fond."""
    num, _ = cv2.connectedComponents((mask > 0).astype(np.uint8), connectivity=8)
    return max(0, num - 1)


def remove_small_islands(mask: np.ndarray, min_area_px: int) -> tuple[np.ndarray, int]:
    """Ne garde que la plus grande composante connexe et toute composante
    d'aire >= `min_area_px` ; les autres retournent au fond. La plus grande
    composante est TOUJOURS conservee (meme sous le seuil) pour ne jamais
    vider entierement un masque valide mais petit."""
    binary = (mask > 0).astype(np.uint8)
    num, labels, stats, _ = cv2.connectedComponentsWithStats(binary, connectivity=8)
    if num <= 1:
        return binary, 0
    areas = stats[1:, cv2.CC_STAT_AREA]
    largest = int(np.argmax(areas)) + 1
    cleaned = np.zeros_like(binary)
    removed = 0
    for component_id in range(1, num):
        area = int(stats[component_id, cv2.CC_STAT_AREA])
        if component_id == largest or area >= min_area_px:
            cleaned[labels == component_id] = 1
        else:
            removed += 1
    return cleaned, removed


def fill_small_holes(mask: np.ndarray, max_area_px: int) -> tuple[np.ndarray, int]:
    """Comble les composantes de fond entierement enclose (ne touchant pas
    le bord de l'image) dont l'aire est <= `max_area_px`."""
    binary = (mask > 0).astype(np.uint8)
    inverse = (binary == 0).astype(np.uint8)
    num, labels, stats, _ = cv2.connectedComponentsWithStats(inverse, connectivity=8)
    if num <= 1:
        return binary, 0
    height, width = binary.shape
    filled = binary.copy()
    filled_count = 0
    for component_id in range(1, num):
        x, y, w, h, area = stats[component_id]
        touches_border = x == 0 or y == 0 or (x + w) == width or (y + h) == height
        if touches_border or area > max_area_px:
            continue
        filled[labels == component_id] = 1
        filled_count += 1
    return filled, filled_count
