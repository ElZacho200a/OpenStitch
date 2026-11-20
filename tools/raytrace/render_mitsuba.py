# SPDX-License-Identifier: Apache-2.0
"""Rendu par lancer de rayons d'une scène de brins OpenStitch (RENDER-PBR-01, lot D, prototype).

Entrée : la scène écrite par `openstitch-cli render projet.osp ref.png --scene-json scene.json`
(brins de la séquence effective, ordre de couture, mm, Y vers le bas). Sortie : une image PNG.

Moteur : Mitsuba 3 (licence BSD-3, `python -m pip install --user mitsuba`), lancé comme outil
externe ; aucun code de moteur n'est lié à OpenStitch. Chaque brin devient un tube (courbe
linéaire à rayon = largeur / 2) posé sur le tissu : il s'enfonce aux pénétrations d'aiguille et
monte d'un cheveu avec l'ordre de couture, pour que les fils cousus plus tard passent dessus.
Ombres, occlusion et reflets entre fils sont donc calculés, pas imités.

Exemple :
    python tools/raytrace/render_mitsuba.py scene.json rendu.png --spp 64
    python tools/raytrace/render_mitsuba.py scene.json rendu.png --tilt 35   # vue « photo »
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import tempfile
import time
from pathlib import Path


def pick_variant(mi, wanted: str | None) -> str:
    order = [wanted] if wanted else ["cuda_ad_rgb", "llvm_ad_rgb", "scalar_rgb"]
    for v in order:
        if v in mi.variants():
            try:
                mi.set_variant(v)
                # Une variante CUDA sans pilote/GPU échoue à la première allocation.
                mi.load_dict({"type": "constant"})
                return v
            except Exception:  # noqa: BLE001 - on essaie la suivante
                continue
    raise SystemExit("Aucune variante Mitsuba utilisable")


def srgb_to_linear(c: float) -> float:
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def parse_rgb(text: str) -> list[float]:
    parts = [int(p) for p in text.split(",")]
    if len(parts) != 3 or any(p < 0 or p > 255 for p in parts):
        raise SystemExit(f"Couleur invalide : {text} (attendu r,g,b de 0 à 255)")
    return [srgb_to_linear(float(p)) for p in parts]


# Armures de tissu : (période du motif en mm, relief). Tuiles procédurales déterministes.
FABRIC_TYPES = {
    "uni": (1.0, 0.0),
    "toile": (0.6, 0.012),
    "serge": (0.9, 0.015),
    "jersey": (1.1, 0.018),
    "feutre": (3.0, 0.010),
    "eponge": (1.6, 0.030),
}


def fabric_tile(kind: str, size: int = 128):
    """Hauteur [0, 1] d'une tuile répétable de l'armure `kind` (numpy, float32)."""
    import numpy as np

    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    if kind == "toile":  # armure toile : dessus / dessous en damier, bosse le long du fil
        n = 4
        cx, cy = np.floor(x * n), np.floor(y * n)
        fx, fy = (x * n) % 1.0, (y * n) % 1.0
        over = ((cx + cy) % 2) == 0
        h = np.where(over, np.sin(np.pi * fx), np.sin(np.pi * fy)) * 0.8 + 0.2 * over
    elif kind == "serge":  # sergé 2/2 : côtes diagonales
        n = 8
        diag = np.floor(x * n + y * n) % 4
        fx = (x * n) % 1.0
        h = np.where(diag < 2, 0.6 + 0.4 * np.sin(np.pi * fx), 0.25 * np.sin(np.pi * fx))
    elif kind == "jersey":  # maille : colonnes de V
        n = 4
        fx = (x * n) % 1.0
        fy = (y * n * 2) % 1.0
        v = np.abs(fx - 0.5) * 2.0  # 0 au centre de la colonne
        h = np.clip(1.0 - np.abs(v - (1.0 - fy)) * 3.0, 0.0, 1.0)
    elif kind in ("feutre", "eponge"):  # bruit lissé déterministe (graine fixe)
        rng = np.random.default_rng(7 if kind == "feutre" else 11)
        h = rng.random((size, size)).astype(np.float32)
        k = 3 if kind == "feutre" else 6
        for _ in range(k):  # flou périodique simple
            h = (h + np.roll(h, 1, 0) + np.roll(h, -1, 0) + np.roll(h, 1, 1) + np.roll(h, -1, 1)) / 5
        h = (h - h.min()) / max(1e-6, float(h.max() - h.min()))
        if kind == "eponge":
            h = np.clip((h - 0.35) * 2.5, 0.0, 1.0)  # bouclettes
    else:  # uni
        h = np.zeros((size, size), dtype=np.float32)
    return h.astype(np.float32)


def fabric_bsdf(mi, kind: str, rgb: list[float], fw: float, fh: float) -> dict:
    period, relief = FABRIC_TYPES[kind]
    if relief <= 0.0:
        return {"type": "diffuse", "reflectance": {"type": "rgb", "value": rgb}}
    import numpy as np

    h = fabric_tile(kind)
    shade = 0.72 + 0.28 * h  # creux plus sombres
    albedo = np.stack([shade * c for c in rgb], axis=-1).astype(np.float32)
    to_uv = mi.ScalarTransform4f().scale([fw / period, fh / period, 1])
    return {
        "type": "bumpmap",
        "arbitrary": {"type": "bitmap", "bitmap": mi.Bitmap(h[..., None]), "raw": True,
                      "to_uv": to_uv},
        "scale": relief,
        "bsdf": {"type": "diffuse",
                 "reflectance": {"type": "bitmap", "bitmap": mi.Bitmap(albedo), "raw": True,
                                 "to_uv": to_uv}},
    }


def write_curves(segments, radius: float, folder: Path) -> dict[tuple[int, int, int], Path]:
    """Un fichier de courbes linéaires par couleur (format Mitsuba : x y z rayon ; ligne vide =
    nouvelle courbe). Repère Mitsuba : X droite, Y haut, Z vers la caméra (tissu en z = 0)."""
    import numpy as np

    # Carte de hauteur cumulée : chaque fil se pose sur ce qui a déjà été brodé sous lui
    # (ordre de couture), puis rehausse la carte de sa propre épaisseur (tassée). Le tampon est
    # plus étroit que le fil pour que deux rangées voisines d'un même remplissage ne
    # s'empilent pas l'une sur l'autre : seul un vrai recouvrement fait monter le relief.
    cell = radius / 2.0
    stamp_half = radius * 0.4
    pack = 0.75 * 2.0 * radius  # épaisseur ajoutée par couche (le fil s'écrase)
    sink = radius * 0.35        # le fil entre dans le tissu au point d'aiguille
    xs = [v for s in segments for v in (s[0], s[2])] or [0.0]
    ys = [v for s in segments for v in (s[1], s[3])] or [0.0]
    x0, y0 = min(xs) - 1.0, min(ys) - 1.0
    hmap = np.zeros((int((max(ys) - y0 + 1.0) / cell) + 2,
                     int((max(xs) - x0 + 1.0) / cell) + 2), dtype=np.float32)
    lat = np.linspace(-stamp_half, stamp_half, 3)

    by_colour: dict[tuple[int, int, int], list[str]] = {}
    for ax, ay, bx, by, r, g, b in segments:
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        if length < 1e-3:
            continue
        ramp = min(0.5, max(0.08, radius * 1.5 / length))
        k = max(2, int(length / (radius * 1.5)) + 1)
        ts = np.concatenate(([0.0], np.linspace(ramp, 1.0 - ramp, k), [1.0]))
        nx, ny = -dy / length, dx / length
        px = ax + dx * ts[:, None] + nx * lat[None, :]
        py = ay + dy * ts[:, None] + ny * lat[None, :]
        ci = ((px - x0) / cell).astype(int)
        ri = ((py - y0) / cell).astype(int)
        base = hmap[ri, ci].max(axis=1)
        z = base + radius
        z[0], z[-1] = base[0] + sink, base[-1] + sink
        lines = [
            f"{ax + dx * t:.5f} {-(ay + dy * t):.5f} {zz:.5f} {radius:.5f}" for t, zz in zip(ts, z)
        ]
        by_colour.setdefault((r, g, b), []).append("\n".join(lines))
        inner = slice(1, -1)
        np.maximum.at(hmap, (ri[inner].ravel(), ci[inner].ravel()),
                      np.repeat(base[inner] + pack, lat.size).astype(np.float32))
    files = {}
    for k, (colour, curves) in enumerate(sorted(by_colour.items())):
        path = folder / f"fil_{k:03d}.txt"
        path.write_text("\n\n".join(curves) + "\n", encoding="ascii")
        files[colour] = path
    return files


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("scene")
    ap.add_argument("output")
    ap.add_argument("--px-per-mm", type=float, default=10.0)
    ap.add_argument("--width-px", type=int, default=0,
                    help="largeur de l'image en pixels (remplace --px-per-mm ; 3840 = 4K)")
    ap.add_argument("--height-px", type=int, default=0,
                    help="avec --width-px : hauteur imposée (2160 = 4K UHD), cadre élargi")
    ap.add_argument("--margin-mm", type=float, default=3.0)
    ap.add_argument("--spp", type=int, default=64, help="échantillons par pixel (défaut 64)")
    ap.add_argument("--tilt", type=float, default=0.0, help="inclinaison de la caméra (degrés)")
    ap.add_argument("--variant", default=None, help="variante Mitsuba (défaut : cuda > llvm > scalar)")
    ap.add_argument("--fabric", default="245,243,238", help="couleur du tissu r,g,b")
    ap.add_argument("--fabric-type", default="toile", choices=sorted(FABRIC_TYPES),
                    help="armure du tissu (relief et grain simulés)")
    ap.add_argument("--background", default="200,200,204", help="couleur du fond r,g,b")
    ap.add_argument("--border", type=float, default=0.12,
                    help="fond visible autour du tissu, en fraction de sa taille (défaut 0,12)")
    args = ap.parse_args()

    import mitsuba as mi  # importé ici : --help sans Mitsuba installé

    data = json.loads(Path(args.scene).read_text(encoding="utf-8"))
    if data.get("format") != "openstitch-thread-scene":
        raise SystemExit("Fichier non reconnu (attendu : openstitch-thread-scene)")
    segments = data["segments"]
    if not segments:
        raise SystemExit("Scène vide")
    radius = float(data.get("thread_width_mm", 0.35)) / 2.0
    variant = pick_variant(mi, args.variant)

    xs = [v for s in segments for v in (s[0], s[2])]
    ys = [-v for s in segments for v in (s[1], s[3])]
    x0, x1 = min(xs) - args.margin_mm, max(xs) + args.margin_mm
    y0, y1 = min(ys) - args.margin_mm, max(ys) + args.margin_mm
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    fw, fh = x1 - x0, y1 - y0  # tissu = motif + marge
    border = max(0.0, args.border)
    w, h = fw * (1 + 2 * border), fh * (1 + 2 * border)  # cadre = tissu + fond autour
    if args.width_px and args.height_px:
        # Format imposé (ex. 3840 x 2160) : le cadre s'élargit sans déformer, tissu étendu.
        aspect = args.width_px / args.height_px
        if w / h < aspect:
            w = h * aspect
        else:
            h = w / aspect
        fw, fh = w / (1 + 2 * border), h / (1 + 2 * border)
    px_per_mm = args.width_px / w if args.width_px else args.px_per_mm
    width = max(16, round(w * px_per_mm))
    height = max(16, round(h * px_per_mm))
    fabric = parse_rgb(args.fabric)
    background = parse_rgb(args.background)

    t0 = time.perf_counter()
    with tempfile.TemporaryDirectory() as tmp:
        files = write_curves(segments, radius, Path(tmp))
        T = mi.ScalarTransform4f
        distance = max(w, h) * 4.0
        tilt = math.radians(args.tilt)
        origin = [cx, cy - distance * math.sin(tilt), distance * math.cos(tilt)]
        if args.tilt == 0.0:
            sensor = {
                "type": "orthographic",
                "to_world": T().look_at(origin=origin, target=[cx, cy, 0], up=[0, 1, 0])
                # Le film applique déjà le rapport largeur / hauteur : échelle uniforme réglée
                # sur la largeur.
                @ T().scale([w / 2, w / 2, 1]),
            }
        else:
            fov = 2 * math.degrees(math.atan((max(w, h) / 2) / distance)) * 1.05
            sensor = {
                "type": "perspective",
                "fov": fov,
                "fov_axis": "x" if w >= h else "y",
                "to_world": T().look_at(origin=origin, target=[cx, cy, 0], up=[0, 1, 0]),
            }
        sensor["film"] = {"type": "hdrfilm", "width": width, "height": height,
                          "pixel_format": "rgb", "rfilter": {"type": "gaussian"}}
        sensor["sampler"] = {"type": "independent", "sample_count": args.spp}

        scene = {
            "type": "scene",
            "integrator": {"type": "path", "max_depth": 6},
            "sensor": sensor,
            # Lumière haut-gauche (comme libs/stitch_render/src/raster.cpp) + ciel doux.
            "sun": {"type": "directional", "direction": [0.64, -0.77, -0.9],
                    "irradiance": {"type": "rgb", "value": 2.2}},
            "sky": {"type": "constant", "radiance": {"type": "rgb", "value": 0.45}},
            # Tissu (rectangle [-1, 1]² mis à l'échelle de la demi-taille), puis fond plus bas.
            "fabric": {
                "type": "rectangle",
                "to_world": T().translate([cx, cy, 0.0]) @ T().scale([fw / 2, fh / 2, 1]),
                "bsdf": fabric_bsdf(mi, args.fabric_type, fabric, fw, fh),
            },
            "background": {
                "type": "rectangle",
                "to_world": T().translate([cx, cy, -0.4]) @ T().scale([w * 4, h * 4, 1]),
                "bsdf": {"type": "diffuse", "reflectance": {"type": "rgb", "value": background}},
            },
        }
        for k, (colour, path) in enumerate(files.items()):
            scene[f"fil_{k}"] = {
                "type": "linearcurve",
                "filename": str(path),
                "bsdf": {
                    "type": "principled",
                    "base_color": {"type": "rgb", "value": [srgb_to_linear(c) for c in colour]},
                    "roughness": 0.35,
                    "specular": 0.6,
                    "sheen": 0.4,
                },
            }
        loaded = mi.load_dict(scene)
        # Mitsuba lance au plus ~2^32 échantillons par appel : au-delà (8K, beaucoup de spp), on
        # découpe en passes de graines fixes et on moyenne (résultat reproductible).
        budget = 1 << 30
        passes = max(1, math.ceil(width * height * args.spp / budget))
        spp_pass = max(1, math.ceil(args.spp / passes))
        image = None
        for seed in range(passes):
            part = mi.render(loaded, spp=spp_pass, seed=seed)
            image = part if image is None else image + part
        if passes > 1:
            image = image / passes
        mi.util.write_bitmap(args.output, image)
    elapsed = time.perf_counter() - t0
    print(f"Rendu Mitsuba ({variant}) : {args.output} ({width} x {height} px, {args.spp} spp, "
          f"{len(segments)} brins, {len(files)} couleurs, tissu {args.fabric_type}) "
          f"en {elapsed:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
