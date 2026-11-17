#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Décomposition par étape d'une campagne profile_engine (passages chauds, médianes).

Usage : python stage_breakdown.py <dossier_campagne> [<dossier_campagne> ...]
Lit les fichiers <cas>.NN.jsonl, ignore le premier passage de chaque processus (froid)."""
import json, statistics, sys
from pathlib import Path

TOP = ["wall_ms", "digitize_ms", "sequence_ms", "fabric_ms", "preview_ms", "peak_rss_bytes"]

def load(folder):
    cases = {}
    for f in sorted(Path(folder).glob("*.jsonl")):
        case = f.name.split(".")[0]
        rows = [json.loads(l) for l in f.read_text(encoding="utf-8").splitlines() if l.strip()]
        cases.setdefault(case, []).extend(r for r in rows if r["run"] > 0)
    return cases

def med(rows, key):
    return statistics.median(r[key] for r in rows)

for folder in sys.argv[1:]:
    print(f"## {folder}")
    for case, rows in load(folder).items():
        hashes = {(r["sequence_hash"], r["label_hash"], r["preview_hash"]) for r in rows}
        print(f"{case}: n={len(rows)} stitches={rows[0]['stitches']} objets={rows[0]['objects']} "
              f"empreintes_distinctes={len(hashes)} seq={rows[0]['sequence_hash']}")
        print("  " + " | ".join(
            f"{k}={med(rows, k) / (1 << 20):.1f}Mio" if k == "peak_rss_bytes" else f"{k}={med(rows, k):.2f}"
            for k in TOP))
        stages = rows[0].get("stages") or {}
        parts = []
        for name in stages:
            v = med(rows, None) if False else statistics.median(r["stages"][name]["task_ms"] for r in rows)
            if v >= 0.5:
                calls = rows[0]["stages"][name]["calls"]
                parts.append(f"{name}={v:.1f}({calls})")
        print("  étapes task_ms(appels): " + ", ".join(parts))
