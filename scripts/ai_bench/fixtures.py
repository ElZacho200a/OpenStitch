# SPDX-License-Identifier: Apache-2.0
"""Fixtures synthetiques du benchmark de segmentation (libres de droits, deterministes).

Scene maritime a aplats : voilier (coque, deux voiles de MEME couleur, mat et
cordages fins), phare (tour a bandes, lanterne, toit, fenetre = trou), mer
(eau, crete, ecume blanche de MEME couleur que la tour) et ciel. Les couleurs
partagees entre objets distincts sont voulues : un score qui se contente de
separer les couleurs ne doit pas pouvoir reussir.

    python scripts/ai_bench/fixtures.py --write tests/fixtures/ai-benchmark
    python scripts/ai_bench/fixtures.py --check tests/fixtures/ai-benchmark
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pngio  # noqa: E402

WIDTH, HEIGHT = 320, 240
PX_PER_MM = 2.0
SCHEMA = "openstitch.ai.benchmark-manifest/1"
CASE_ID = "marine-synth-01"

# (semantic_id, famille, genre, couleur, requis, fond_intentionnel)
# genre : "area" | "line". `required` : objet principal dont l'absence est un
# echec bloquant, quel que soit le reste de la scene.
OBJECTS = [
    ("sky", "sky", "area", (170, 205, 235), False),
    ("sea.water", "sea", "area", (30, 90, 150), True),
    ("sea.crest", "sea", "area", (20, 65, 120), False),
    ("sea.foam", "sea", "area", (245, 245, 245), False),
    ("boat.hull", "boat", "area", (110, 60, 30), True),
    ("boat.sail_main", "boat", "area", (250, 240, 210), True),
    ("boat.sail_jib", "boat", "area", (250, 240, 210), True),
    ("boat.mast", "boat", "line", (40, 30, 30), True),
    ("boat.rigging", "boat", "line", (40, 30, 30), False),
    ("lighthouse.tower_white", "lighthouse", "area", (245, 245, 245), True),
    ("lighthouse.tower_red", "lighthouse", "area", (200, 40, 40), True),
    ("lighthouse.lantern", "lighthouse", "area", (250, 215, 60), True),
    ("lighthouse.roof", "lighthouse", "area", (150, 30, 30), False),
    ("lighthouse.window", "lighthouse", "area", (30, 40, 60), False),
]
MAIN_SUBJECT_FAMILIES = ["boat", "lighthouse"]
# trous a conserver : la fenetre de la tour. Les poches fermees du ciel entre
# cordages et mat sont comptees mais ne sont pas des trous proteges.
PROTECT_HOLES = {"lighthouse.tower_white"}
RELATIONS = {
    "sea.water": {"under": ["boat.hull", "sea.crest", "sea.foam"]},
    "boat.hull": {"over": ["sea.water"]},
    "boat.sail_main": {"over": ["sky"]},
    "boat.sail_jib": {"over": ["sky"]},
    "boat.rigging": {"over": ["boat.sail_jib", "sky"]},
    "lighthouse.window": {"over": ["lighthouse.tower_white"]},
    "sea.foam": {"over": ["sea.water", "sky"]},
}


def _label_index(semantic_id: str) -> int:
    return [o[0] for o in OBJECTS].index(semantic_id) + 1


def _fill_polygon(canvas, points, label):
    ys = [p[1] for p in points]
    n = len(points)
    for y in range(min(ys), max(ys) + 1):
        xs = []
        for i in range(n):
            (x1, y1), (x2, y2) = points[i], points[(i + 1) % n]
            if y1 == y2 or not (min(y1, y2) <= y < max(y1, y2)):
                continue
            xs.append(x1 + (x2 - x1) * (y - y1) / (y2 - y1))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            for x in range(int(a + 0.5), int(b + 0.5)):
                if 0 <= x < WIDTH and 0 <= y < HEIGHT:
                    canvas[y * WIDTH + x] = label


def _rect(canvas, x0, y0, x1, y1, label):
    for y in range(y0, y1):
        for x in range(x0, x1):
            canvas[y * WIDTH + x] = label


def _disc(canvas, cx, cy, r, label):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r and 0 <= x < WIDTH and 0 <= y < HEIGHT:
                canvas[y * WIDTH + x] = label


def _line(canvas, x0, y0, x1, y1, label):
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        canvas[y0 * WIDTH + x0] = label
        if x0 == x1 and y0 == y1:
            return
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


def render_labels() -> bytearray:
    """Carte d'etiquettes visibles (0 = non etiquete), ordre de peinture = ordre de superposition."""
    c = bytearray(WIDTH * HEIGHT)
    L = _label_index
    _rect(c, 0, 0, WIDTH, 150, L("sky"))
    _rect(c, 0, 150, WIDTH, HEIGHT, L("sea.water"))
    _rect(c, 0, 175, WIDTH, 179, L("sea.crest"))
    _rect(c, 0, 205, WIDTH, 209, L("sea.crest"))
    for cx, cy, r in ((60, 150, 6), (200, 152, 7), (290, 151, 5)):
        _disc(c, cx, cy, r, L("sea.foam"))
    # phare : tour trapezoidale a bandes, puis lanterne, toit, fenetre
    for y in range(80, 150):
        half = 12 + (y - 80) * 8 // 70
        label = L("lighthouse.tower_red") if (80 <= y < 100 or 120 <= y < 140) else L("lighthouse.tower_white")
        _rect(c, 250 - half, y, 250 + half, y + 1, label)
    _rect(c, 240, 62, 260, 80, L("lighthouse.lantern"))
    _fill_polygon(c, [(238, 62), (262, 62), (250, 48)], L("lighthouse.roof"))
    _rect(c, 246, 104, 254, 114, L("lighthouse.window"))
    # voilier
    _fill_polygon(c, [(70, 150), (170, 150), (155, 172), (85, 172)], L("boat.hull"))
    _fill_polygon(c, [(120, 62), (120, 142), (165, 142)], L("boat.sail_main"))
    _fill_polygon(c, [(117, 72), (117, 142), (80, 142)], L("boat.sail_jib"))
    _rect(c, 118, 60, 120, 150, L("boat.mast"))
    _line(c, 119, 60, 72, 149, L("boat.rigging"))
    _line(c, 119, 60, 168, 149, L("boat.rigging"))
    return c


def render_source(labels: bytearray) -> bytes:
    palette = {i + 1: o[3] for i, o in enumerate(OBJECTS)}
    out = bytearray()
    for v in labels:
        out += bytes(palette.get(v, (0, 0, 0)))
    return bytes(out)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _components_and_holes(mask: bytearray):
    """(composantes 8-connexes, trous = fonds 4-connexes fermes) d'un masque 0/1."""
    seen = bytearray(len(mask))
    comps = 0
    for start in range(len(mask)):
        if mask[start] and not seen[start]:
            comps += 1
            stack = [start]
            seen[start] = 1
            while stack:
                p = stack.pop()
                py, px = divmod(p, WIDTH)
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = py + dy, px + dx
                        if 0 <= ny < HEIGHT and 0 <= nx < WIDTH:
                            q = ny * WIDTH + nx
                            if mask[q] and not seen[q]:
                                seen[q] = 1
                                stack.append(q)
    outside = bytearray(len(mask))
    holes = 0
    for start in range(len(mask)):
        if not mask[start] and not outside[start]:
            touches = False
            stack = [start]
            outside[start] = 1
            while stack:
                p = stack.pop()
                py, px = divmod(p, WIDTH)
                if py in (0, HEIGHT - 1) or px in (0, WIDTH - 1):
                    touches = True
                for ny, nx in ((py - 1, px), (py + 1, px), (py, px - 1), (py, px + 1)):
                    if 0 <= ny < HEIGHT and 0 <= nx < WIDTH:
                        q = ny * WIDTH + nx
                        if not mask[q] and not outside[q]:
                            outside[q] = 1
                            stack.append(q)
            if not touches:
                holes += 1
    return comps, holes


def build() -> dict[str, bytes]:
    """Retourne {chemin relatif: octets} pour toute la fixture, manifeste compris."""
    labels = render_labels()
    files: dict[str, bytes] = {}
    source = pngio.encode(WIDTH, HEIGHT, "RGB", render_source(labels))
    files[f"{CASE_ID}/source.png"] = source
    objects = []
    for i, (sid, family, kind, rgb, required) in enumerate(OBJECTS):
        label = i + 1
        mask = bytearray(1 if v == label else 0 for v in labels)
        area = sum(mask)
        xs = [p % WIDTH for p in range(len(mask)) if mask[p]]
        ys = [p // WIDTH for p in range(len(mask)) if mask[p]]
        comps, holes = _components_and_holes(mask)
        # le trou de la tour est la fenetre : il compte dans le masque de la tour blanche
        gold_png = pngio.encode(WIDTH, HEIGHT, "L", bytes(v * 255 for v in mask))
        rel = f"{CASE_ID}/gold/{sid}.png"
        files[rel] = gold_png
        # ancrage : premier pixel du masque (ordre de balayage), toujours dans l'objet
        first = mask.index(1)
        entry = {
            "semantic_id": sid,
            "family": family,
            "kind": kind,
            "label": label,
            "color_rgb": list(rgb),
            "protected": kind == "line" or required,
            "required": required,
            "main_subject": family in MAIN_SUBJECT_FAMILIES,
            "gold_mask": {"path": rel, "sha256": _sha(gold_png)},
            "area_px": area,
            "bbox_px": [min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1],
            "anchor_px": [first % WIDTH, first // WIDTH],
            "components": comps,
            "holes": holes,
            "protect_holes": sid in PROTECT_HOLES,
        }
        if kind == "line":
            entry["width_mm_max"] = 1.0
        entry.update(RELATIONS.get(sid, {}))
        objects.append(entry)
    manifest = {
        "schema": SCHEMA,
        "cases": [
            {
                "case_id": CASE_ID,
                "image_type": "synthetic-illustration",
                "source": {
                    "path": f"{CASE_ID}/source.png",
                    "sha256": _sha(source),
                    "width_px": WIDTH,
                    "height_px": HEIGHT,
                },
                "design": {"width_mm": WIDTH / PX_PER_MM, "height_mm": HEIGHT / PX_PER_MM},
                "px_per_mm": PX_PER_MM,
                "rights": {
                    "license": "CC0-1.0",
                    "author": "OpenStitch contributors",
                    "generator": "scripts/ai_bench/fixtures.py",
                    "redistributable": True,
                },
                "main_subject_families": MAIN_SUBJECT_FAMILIES,
                "notes": "Voilier + phare + mer + ecume + elements fins. Couleurs partagees voulues : "
                "voiles principale/foc, ecume/tour blanche, mat/cordages.",
                "objects": objects,
            }
        ],
    }
    files["manifest.json"] = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    return files


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", metavar="DIR", help="ecrit les fixtures dans DIR")
    group.add_argument("--check", metavar="DIR", help="verifie que DIR est identique a la regeneration")
    args = ap.parse_args(argv)
    files = build()
    if args.write:
        root = Path(args.write)
        for rel, data in files.items():
            target = root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        print(f"{len(files)} fichiers ecrits dans {root}")
        return 0
    root = Path(args.check)
    bad = [rel for rel, data in files.items() if not (root / rel).is_file() or (root / rel).read_bytes() != data]
    if bad:
        print("fixtures differentes de la regeneration : " + ", ".join(bad), file=sys.stderr)
        return 2
    print("fixtures conformes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
