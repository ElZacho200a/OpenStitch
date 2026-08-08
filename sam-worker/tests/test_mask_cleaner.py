# SPDX-License-Identifier: Apache-2.0
import numpy as np

from mask_cleaner import count_components, fill_small_holes, remove_small_islands


def test_count_components_counts_disjoint_blobs():
    mask = np.zeros((10, 10), dtype=np.uint8)
    mask[1:3, 1:3] = 1
    mask[6:8, 6:8] = 1
    assert count_components(mask) == 2


def test_remove_small_islands_keeps_largest_and_drops_tiny_specks():
    mask = np.zeros((10, 10), dtype=np.uint8)
    mask[0:5, 0:5] = 1  # grande composante : 25 px
    mask[8, 8] = 1  # ilot isole : 1 px
    cleaned, removed = remove_small_islands(mask, min_area_px=4)
    assert removed == 1
    assert cleaned[8, 8] == 0
    assert cleaned[0:5, 0:5].sum() == 25


def test_remove_small_islands_always_keeps_the_largest_component_even_if_tiny():
    mask = np.zeros((10, 10), dtype=np.uint8)
    mask[0, 0] = 1  # unique composante, 1 px, sous tout seuil raisonnable
    cleaned, removed = remove_small_islands(mask, min_area_px=1000)
    assert removed == 0
    assert cleaned[0, 0] == 1


def test_fill_small_holes_fills_enclosed_background_below_threshold():
    mask = np.ones((10, 10), dtype=np.uint8)
    mask[5, 5] = 0  # trou enclos, 1 px
    filled, count = fill_small_holes(mask, max_area_px=4)
    assert count == 1
    assert filled[5, 5] == 1


def test_fill_small_holes_ignores_background_touching_the_border():
    mask = np.zeros((10, 10), dtype=np.uint8)
    mask[2:8, 2:8] = 1  # le fond exterieur touche le bord : jamais un "trou"
    filled, count = fill_small_holes(mask, max_area_px=1000)
    assert count == 0
    assert filled[0, 0] == 0


def test_fill_small_holes_leaves_holes_above_the_threshold_alone():
    mask = np.ones((20, 20), dtype=np.uint8)
    mask[5:15, 5:15] = 0  # grand trou enclos, 100 px
    filled, count = fill_small_holes(mask, max_area_px=4)
    assert count == 0
    assert filled[10, 10] == 0
