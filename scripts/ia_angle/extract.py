"""IA-DATA-01 L3 : régions → angle des points d'un vrai numériseur (jeu jiang-s5, local).

Pour chaque motif présent dans les deux archives jiang-s5 :
- les régions sont les composantes 8-connexes de chaque couleur de
  `cleaned_color_layer.png` (même grille 256 x 256 que les directions) ;
- l'étiquette est l'axe dominant des points du DST dans la région (`dominant_axis`),
  avec la part des points à ±20° de cet axe (cohérence) et la part perpendiculaire
  (sous-couche) : forte cohérence = angle fixe (tatami), faible = direction qui suit
  la forme ;
- les features ne lisent que la FORME (masque de la région et voisins), jamais les
  pixels rendus : le modèle doit rester valable sur des aplats.

Repère : pixels de l'image (x à droite, y vers le bas) ; angles d'axe dans [0, pi).
Les données jiang-s5 restent hors du dépôt (licence Research-Only Non-Commercial) ;
ce script ne lit que des chemins locaux et n'écrit qu'un CSV local.

    python scripts/ia_angle/extract.py --data C:/Users/zache/datasets/jiang-s5 --out regions.csv
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

MIN_AREA_PX = 40  # ~2,3 mm² à 0,24 mm/px : en dessous, l'angle n'a pas de sens
MIN_DIR_PX = 25  # pixels porteurs d'une direction dans la région
MIN_MAG = 0.9  # norme minimale d'un pixel de direction (voir `regions_of`)
EIGHT = np.ones((3, 3), dtype=bool)

FEATURES = [
    "area_mm2",
    "perimeter_mm",
    "compactness",
    "anisotropy",
    "major_mm",
    "minor_mm",
    "solidity",
    "thick_mean_mm",
    "thick_max_mm",
    "thick_cv",
    "holes",
    "bbox_aspect",
    "neighbors",
    "same_color_regions",
    "area_frac",
    "layer_rank",
    "layers",
    "cos2_nb",
    "sin2_nb",
]


def axis_from_moments(ys: np.ndarray, xs: np.ndarray) -> tuple[float, float, float, float]:
    """Axe principal (angle dans [0, pi)), anisotropie, longueurs majeure et mineure (px)."""
    x = xs - xs.mean()
    y = ys - ys.mean()
    cxx = float((x * x).mean())
    cyy = float((y * y).mean())
    cxy = float((x * y).mean())
    tr = cxx + cyy
    det = cxx * cyy - cxy * cxy
    disc = math.sqrt(max(0.0, tr * tr / 4 - det))
    l1 = tr / 2 + disc
    l2 = max(tr / 2 - disc, 1e-9)
    angle = 0.5 * math.atan2(2 * cxy, cxx - cyy)
    return angle % math.pi, l1 / l2, 4 * math.sqrt(l1), 4 * math.sqrt(l2)


def perimeter_px(mask: np.ndarray) -> float:
    edge = mask & ~ndimage.binary_erosion(mask, structure=EIGHT, border_value=0)
    return float(edge.sum())


def cross2(a: np.ndarray, b: np.ndarray) -> float:
    return float(a[0] * b[1] - a[1] * b[0])


def convex_area_px(ys: np.ndarray, xs: np.ndarray) -> float:
    pts = np.unique(np.stack([xs, ys], axis=1), axis=0)
    if len(pts) < 3:
        return float(len(pts))
    pts = pts[np.lexsort((pts[:, 1], pts[:, 0]))]

    def half(points):
        h = []
        for p in points:
            while len(h) >= 2 and cross2(h[-1] - h[-2], p - h[-2]) <= 0:
                h.pop()
            h.append(p)
        return h

    hull = np.array(half(pts)[:-1] + half(pts[::-1])[:-1], dtype=float)
    x, y = hull[:, 0], hull[:, 1]
    return 0.5 * abs(float(np.dot(x, np.roll(y, 1)) - np.dot(y, np.roll(x, 1)))) + len(hull) / 2


def axial_distance(a: np.ndarray, b: float) -> np.ndarray:
    d = np.abs((a - b) % math.pi)
    return np.minimum(d, math.pi - d)


def dominant_axis(angles: np.ndarray, weights: np.ndarray) -> tuple[float, float, float]:
    """Axe dominant des points, part à ±20° de cet axe, part à 90° ± 20° (sous-couche).

    Le mode (histogramme circulaire de 5°, lissé sur ±1 case) et non la moyenne :
    la sous-couche perpendiculaire d'un tatami annulerait la moyenne des angles
    doublés. L'axe est ensuite affiné par moyenne doublée à ±15° du mode.
    """
    w = float(weights.sum())
    if w <= 0:
        return 0.0, 0.0, 0.0
    bins = 36
    hist = np.bincount((angles / math.pi * bins).astype(int) % bins, weights=weights, minlength=bins)
    smooth = hist + np.roll(hist, 1) + np.roll(hist, -1)
    mode = (int(np.argmax(smooth)) + 0.5) * math.pi / bins
    near = axial_distance(angles, mode) <= math.radians(15)
    c = float((weights[near] * np.cos(2 * angles[near])).sum())
    s = float((weights[near] * np.sin(2 * angles[near])).sum())
    axis = (0.5 * math.atan2(s, c)) % math.pi if near.any() else mode
    dist = axial_distance(angles, axis)
    share = float(weights[dist <= math.radians(20)].sum()) / w
    perp = float(weights[dist >= math.radians(70)].sum()) / w
    return axis, share, perp


def load_index(data: Path) -> list[dict]:
    geo = data / "dataset4_multiformat_all_geometry_graph"
    sup = data / "dataset4_multiformat_rendered_supervision"
    with open(geo / "manifest_dataset2.csv", encoding="utf-8-sig") as f:
        meta = {r["pair_id"]: r for r in csv.DictReader(f)}
    with open(sup / "manifest.csv", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    out = []
    for r in rows:
        pid = r["pair_id"]
        if pid not in meta:
            continue
        out.append(
            {
                "pair_id": pid,
                "family": meta[pid]["source_family"],
                "mm_per_px": float(meta[pid]["mm_per_px_est"] or 0) or None,
                "color": geo / meta[pid]["cleaned_color_layer_png"].replace("\\", "/"),
                "direction": sup / r["direction_npy"].replace("\\", "/"),
            }
        )
    out.sort(key=lambda r: r["pair_id"])
    return out


def regions_of(item: dict) -> list[dict]:
    if not item["mm_per_px"] or not item["color"].exists() or not item["direction"].exists():
        return []
    mmpp = item["mm_per_px"]
    color = np.array(Image.open(item["color"]))
    d = np.load(item["direction"]).astype(np.float64)
    if color.shape != d.shape[1:]:
        return []
    dx, dy = d[0], d[1]
    mag = np.hypot(dx, dy)
    ang = np.arctan2(dy, dx) % math.pi
    design_area = float((color > 0).sum()) or 1.0
    layers = sorted(int(v) for v in np.unique(color) if v > 0)

    labeled = np.zeros(color.shape, dtype=np.int32)
    comps = []  # (label id, couleur)
    for layer in layers:
        lab, n = ndimage.label(color == layer, structure=EIGHT)
        for k in range(1, n + 1):
            cid = len(comps) + 1
            labeled[lab == k] = cid
            comps.append(layer)

    # Axe principal de chaque composante, calculé une fois (sert aussi aux voisins).
    slices = ndimage.find_objects(labeled)
    axes: dict[int, tuple[float, float, float, float]] = {}
    for cid, sl in enumerate(slices, start=1):
        if sl is None:
            continue
        ly, lx = np.nonzero(labeled[sl] == cid)
        if len(lx) >= 3:
            axes[cid] = axis_from_moments(ly + float(sl[0].start), lx + float(sl[1].start))

    result = []
    for idx, layer in enumerate(comps, start=1):
        sl = slices[idx - 1]
        if sl is None or idx not in axes:
            continue
        mask = labeled == idx
        ys, xs = np.nonzero(mask)
        area = len(xs)
        if area < MIN_AREA_PX:
            continue
        # Seuls les pixels de norme proche de 1 portent une direction exacte : la carte
        # moyenne des vecteurs ORIENTÉS, et les allers-retours (satin, rangées) s'annulent.
        sel = mask & (mag > MIN_MAG)
        if sel.sum() < MIN_DIR_PX:
            continue
        true_angle, coherence, perp_share = dominant_axis(ang[sel], mag[sel])
        theta_p, aniso, major, minor = axes[idx]
        per = perimeter_px(mask)
        filled = ndimage.binary_fill_holes(mask)
        holes = ndimage.label(filled & ~mask, structure=EIGHT)[1]
        dist = ndimage.distance_transform_edt(np.pad(mask, 1))[1:-1, 1:-1][mask]
        thick = 2.0 * dist
        h = ys.max() - ys.min() + 1
        w = xs.max() - xs.min() + 1
        ring = ndimage.binary_dilation(mask, structure=EIGHT, iterations=2) & ~mask
        nb_ids = np.unique(labeled[ring])
        nb_ids = nb_ids[(nb_ids > 0) & (nb_ids != idx)]
        # Orientation moyenne des voisins (axes principaux), relative à celle de la région.
        nb_c = nb_s = 0.0
        for j in nb_ids:
            if int(j) in axes:
                tj = axes[int(j)][0]
                nb_c += math.cos(2 * (tj - theta_p))
                nb_s += math.sin(2 * (tj - theta_p))
        if len(nb_ids):
            nb_c /= len(nb_ids)
            nb_s /= len(nb_ids)
        # Accord des vrais points avec trois hypothèses (erreur axiale moyenne, degrés) :
        # - « travers » : le long du gradient de la transformée de distance, donc à travers
        #   la bande, même si elle tourne (satin) ;
        # - « constant » : l'axe dominant de la région (tatami) ;
        # - « long » : perpendiculaire au gradient, le long des bords (contour, point droit).
        dt = ndimage.gaussian_filter(ndimage.distance_transform_edt(np.pad(mask, 2)), 1.0)[2:-2, 2:-2]
        gy, gx = np.gradient(dt)
        valid = sel & (np.hypot(gx, gy) > 0.3)
        if valid.sum() >= MIN_DIR_PX:
            wv = mag[valid]
            across = np.arctan2(gy[valid], gx[valid]) % math.pi
            av = ang[valid]
            err_across = float((wv * axial_distance(av, across)).sum() / wv.sum())
            err_along = float((wv * axial_distance(av, (across + math.pi / 2) % math.pi)).sum() / wv.sum())
        else:
            err_across = err_along = float("nan")
        wsel = mag[sel]
        err_const = float((wsel * axial_distance(ang[sel], true_angle)).sum() / wsel.sum())
        length_mm = per * mmpp / 2.0  # même définition que `measure_band_shape`
        result.append(
            {
                "pair_id": item["pair_id"],
                "family": item["family"],
                "region": idx,
                "area_mm2": area * mmpp * mmpp,
                "perimeter_mm": per * mmpp,
                "compactness": 4 * math.pi * area / max(per, 1.0) ** 2,
                "anisotropy": aniso,
                "major_mm": major * mmpp,
                "minor_mm": minor * mmpp,
                "solidity": area / max(convex_area_px(ys, xs), 1.0),
                "thick_mean_mm": float(thick.mean()) * mmpp,
                "thick_max_mm": float(thick.max()) * mmpp,
                "thick_cv": float(thick.std() / max(thick.mean(), 1e-9)),
                "holes": holes,
                "bbox_aspect": max(h, w) / max(min(h, w), 1),
                "neighbors": len(nb_ids),
                "same_color_regions": sum(1 for c in comps if c == layer),
                "area_frac": area / design_area,
                "layer_rank": layers.index(layer) / max(len(layers) - 1, 1),
                "layers": len(layers),
                "cos2_nb": nb_c,
                "sin2_nb": nb_s,
                "theta_principal": theta_p,
                "theta_true": true_angle,
                "coherence": coherence,
                "perp_share": perp_share,
                "dir_px": int(sel.sum()),
                "length_mm": length_mm,
                "mean_width_mm": area * mmpp * mmpp / max(length_mm, 1e-9),
                "err_across": err_across,
                "err_const": err_const,
                "err_along": err_along,
            }
        )
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--min-mag", type=float, default=MIN_MAG)
    args = ap.parse_args()
    globals()["MIN_MAG"] = args.min_mag
    index = load_index(args.data)
    if args.limit:
        index = index[: args.limit]
    cols = ["pair_id", "family", "region", *FEATURES, "theta_principal", "theta_true", "coherence", "perp_share", "dir_px",
            "length_mm", "mean_width_mm", "err_across", "err_const", "err_along"]
    n = 0
    with open(args.out, "w", newline="", encoding="utf-8") as f:
        wr = csv.DictWriter(f, fieldnames=cols)
        wr.writeheader()
        for i, item in enumerate(index):
            for row in regions_of(item):
                wr.writerow({k: (f"{v:.6g}" if isinstance(v, float) else v) for k, v in row.items()})
                n += 1
            if (i + 1) % 100 == 0:
                print(f"{i + 1}/{len(index)} motifs, {n} régions", file=sys.stderr)
    print(f"{len(index)} motifs, {n} régions -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
