#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Banc des recettes de points (STCH-CROS-01, L5) : chaque recette sur un corpus de formes.

Pour chaque (recette, forme) : projet vide -> add_shape -> apply_recipe -> export DST (points
régénérés depuis l'OSP) -> statistiques. Mesure le temps de l'export (génération comprise) et
rend une vignette PNG par recette (rendu réaliste OpenStitch, aucune image tierce).

Usage :
  python scripts/recipe_bench.py --cli build/msvc/apps/cli/Release/openstitch-cli.exe \
      --out build/recipe-bench [--md docs/research/recipe-benchmark.md]
Les temps dépendent de la machine et de la configuration (Debug/Release) : le Markdown les
indique avec la configuration du binaire.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import time

SHAPES = {
    # nom : opération add_shape (centre au milieu d'un cadre de 300 mm)
    "rect30x20": {"shape": "rect", "width_mm": 30, "height_mm": 20, "center_mm": [150, 150]},
    "ellipse40x30": {"shape": "ellipse", "width_mm": 40, "height_mm": 30, "center_mm": [150, 150]},
    "concave_L": {"shape": "polygon",
                  "points_mm": [[130, 130], [170, 130], [170, 140], [140, 140], [140, 170], [130, 170]]},
    "star": {"shape": "polygon",
             "points_mm": [[150, 175], [156, 158], [174, 158], [160, 147], [165, 130], [150, 140],
                           [135, 130], [140, 147], [126, 158], [144, 158]]},
    "rect240": {"shape": "rect", "width_mm": 240, "height_mm": 240, "center_mm": [150, 150]},
}


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--md", help="écrit un tableau Markdown")
    ap.add_argument("--config", default="", help="configuration du binaire (Debug/Release)")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    recipes = json.loads(run([a.cli, "recipes", "--json"]).stdout)
    rows = []
    for r in recipes:
        for shape, op in SHAPES.items():
            osp = out / f"{r['id']}__{shape}.osp"
            if run([a.cli, "project", "new", str(osp), "--width-mm", "300", "--height-mm", "300"]).returncode:
                run([a.cli, "project", "new", str(osp)])
            batch = {"schema": "openstitch.batch/1", "operations": [
                dict(op="add_shape", stitch_type="tatami", **{"as": "a"}, **op),
                {"op": "apply_recipe", "object": "$a", "recipe": r["id"]}]}
            bfile = out / "batch.json"
            bfile.write_text(json.dumps(batch), encoding="utf-8")
            applied = run([a.cli, "project", "apply", str(osp), str(bfile)])
            if applied.returncode:
                rows.append((r["id"], shape, "refus", "", "", "", ""))
                continue
            dst = osp.with_suffix(".dst")
            t0 = time.perf_counter()
            exported = run([a.cli, "project", "export", str(osp), "-o", str(dst)])
            ms = (time.perf_counter() - t0) * 1000.0
            if exported.returncode:
                rows.append((r["id"], shape, "échec export", "", "", "", ""))
                continue
            st = json.loads(run([a.cli, "stats", str(dst), "--json"]).stdout)
            rows.append((r["id"], shape, "ok", st.get("stitches"), st.get("jumps"), st.get("trims"),
                         f"{ms:.0f}"))
            if shape == "ellipse40x30":
                run([a.cli, "render", str(osp), str(out / f"{r['id']}.png")])
    header = "| recette | forme | état | points | sauts | coupes | export (ms) |\n|---|---|---|---|---|---|---|\n"
    table = header + "".join("| " + " | ".join(str(c) for c in row) + " |\n" for row in rows)
    print(table)
    if a.md:
        pathlib.Path(a.md).write_text(
            "# Banc des recettes de points (STCH-CROS-01)\n\n"
            "Généré par `python scripts/recipe_bench.py` (export = génération complète depuis "
            f"l'OSP puis écriture DST, temps mur, binaire {a.config or 'non précisé'}).\n\n"
            + table, encoding="utf-8")
    return 0 if all(r[2] == "ok" for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
