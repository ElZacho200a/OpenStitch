#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare les empreintes (séquence, étiquettes, aperçu) de deux campagnes profile_engine.

Usage : python compare_fingerprints.py <campagne_a> <campagne_b>. Code de sortie 1 si elles diffèrent."""
import json, sys
from pathlib import Path

def fingerprints(folder):
    out = set()
    for f in sorted(Path(folder).glob("*.jsonl")):
        case = f.name.split(".")[0]
        for line in f.read_text(encoding="utf-8").splitlines():
            if line.strip():
                r = json.loads(line)
                out.add((case, r["sequence_hash"], r["label_hash"], r["preview_hash"]))
    return out

a, b = fingerprints(sys.argv[1]), fingerprints(sys.argv[2])
print(f"{sys.argv[1]} vs {sys.argv[2]} : {'identiques' if a == b else 'DIFFÉRENTES'} ({len(a)} empreintes)")
for item in sorted(a ^ b):
    print("  écart :", item)
sys.exit(0 if a == b else 1)
