#!/usr/bin/env python3
"""Palette réelle d'un projet .osp, dans l'ordre de couture, pour render_dst.py --palette.

    openstitch-cli project inspect p.osp --json | python3 -I osp_palette.py palette.json [--report]

Un « bloc » du DST commence à chaque changement de couleur entre deux objets visibles consécutifs
(même règle que le moteur de points). `--report` affiche aussi, par couleur : nombre d'objets,
aire cumulée et part de la segmentation — c'est la vérification à faire AVANT de juger un rendu.
Dépend uniquement de la bibliothèque standard.
"""
import json
import sys
from collections import Counter

out = sys.argv[1]
report = "--report" in sys.argv
d = json.load(sys.stdin)

vec = {v["id"]: v for v in d["vector_objects"]}
objs = sorted((e for e in d["embroidery_objects"] if e.get("visible", True)), key=lambda e: e["order"])

blocks = []
for e in objs:
    c = e["color"]
    if not blocks or blocks[-1] != c:
        blocks.append(c)

rgb = lambda h: [int(h[i : i + 2], 16) for i in (1, 3, 5)]
json.dump([rgb(c) for c in blocks], open(out, "w"))
print(f"{len(blocks)} blocs de couleur (ordre de couture) : {' '.join(blocks)}")

if report:
    n, area = Counter(), Counter()
    for e in objs:
        n[e["color"]] += 1
        area[e["color"]] += vec[e["source_vector"]]["area_mm2"]
    seg = Counter()
    for r in d["segmentation"]["regions"]:
        seg[r["color"]] += r["pixels"]
    tot = sum(seg.values()) or 1
    print("couleur   objets   aire mm2   % segmentation")
    for c in sorted(set(seg) | set(n), key=lambda c: -seg[c]):
        print(f"{c}  {n[c]:6d}  {area[c]:9.0f}  {100 * seg[c] / tot:6.1f}")
