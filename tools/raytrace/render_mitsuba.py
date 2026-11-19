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


def write_curves(segments, radius: float, folder: Path) -> dict[tuple[int, int, int], Path]:
    """Un fichier de courbes linéaires par couleur (format Mitsuba : x y z rayon ; ligne vide =
    nouvelle courbe). Repère Mitsuba : X droite, Y haut, Z vers la caméra (tissu en z = 0)."""
    n = max(1, len(segments))
    by_colour: dict[tuple[int, int, int], list[str]] = {}
    for i, (ax, ay, bx, by, r, g, b) in enumerate(segments):
        dx, dy = bx - ax, by - ay
        length = math.hypot(dx, dy)
        if length < 1e-3:
            continue
        top = radius * (1.0 + 0.6 * i / n)  # empilement dans l'ordre de couture
        sink = radius * 0.35                # le fil entre dans le tissu au point d'aiguille
        ramp = min(0.5, max(0.08, radius * 1.5 / length))
        pts = [(0.0, sink), (ramp, top), (1.0 - ramp, top), (1.0, sink)]
        lines = [
            f"{ax + dx * t:.5f} {-(ay + dy * t):.5f} {z:.5f} {radius:.5f}" for t, z in pts
        ]
        by_colour.setdefault((r, g, b), []).append("\n".join(lines))
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
    ap.add_argument("--margin-mm", type=float, default=3.0)
    ap.add_argument("--spp", type=int, default=64, help="échantillons par pixel (défaut 64)")
    ap.add_argument("--tilt", type=float, default=0.0, help="inclinaison de la caméra (degrés)")
    ap.add_argument("--variant", default=None, help="variante Mitsuba (défaut : cuda > llvm > scalar)")
    ap.add_argument("--fabric", default="245,243,238", help="couleur du tissu r,g,b")
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
    w, h = x1 - x0, y1 - y0
    width = max(16, round(w * args.px_per_mm))
    height = max(16, round(h * args.px_per_mm))
    fabric = [srgb_to_linear(float(c)) for c in args.fabric.split(",")]

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
            "fabric": {
                "type": "rectangle",
                "to_world": T().translate([cx, cy, 0.0]) @ T().scale([w, h, 1]),
                "bsdf": {"type": "diffuse", "reflectance": {"type": "rgb", "value": fabric}},
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
        image = mi.render(loaded)
        mi.util.write_bitmap(args.output, image)
    elapsed = time.perf_counter() - t0
    print(f"Rendu Mitsuba ({variant}) : {args.output} ({width} x {height} px, {args.spp} spp, "
          f"{len(segments)} brins, {len(files)} couleurs) en {elapsed:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
