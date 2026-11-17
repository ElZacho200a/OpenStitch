#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""A/B entrelacé de deux exécutables openstitch-profile-engine (R02).

Les deux versions tournent en alternance, processus par processus (ordre A,B puis B,A), dans
la même fenêtre de temps : une charge extérieure passagère touche les deux côtés. Pour chaque
motif : médianes et p95 chauds de chaque côté, rapport des médianes B/A avec intervalle de
confiance bootstrap à 95 % (graine fixe), et égalité des empreintes.

Usage :
  python ab_interleave.py --a BASE.exe --b NEW.exe --out DOSSIER [--processes 6] [--reps 11]
                          [--threads 0] [--text-bitmap chemin.bmp]
Le texte utilise la bitmap produite par profile_engine.ps1 (build/<campagne>/text-240mm.bmp).
"""
import argparse
import datetime
import json
import os
import random
import statistics
import subprocess
import sys
from pathlib import Path

CASES = {
    "logo": ("tests/fixtures/cli/logo_small.png", "30.48", []),
    "text": (None, "25.4", ["--satin-bands"]),
    "tentabrode": ("tests/fixtures/tentabrode.png", "119.2106667", ["--satin-bands"]),
    "marine": ("tests/fixtures/ai-benchmark/marine-synth-01/source.png", "50.8",
               ["--dark-lines", "--linen"]),
}


def p95(values):
    ordered = sorted(values)
    return ordered[max(0, -(-95 * len(ordered) // 100) - 1)]


def ratio_ci(a, b, rounds=2000, seed=20261117):
    rng = random.Random(seed)
    ratios = []
    for _ in range(rounds):
        sa = [rng.choice(a) for _ in a]
        sb = [rng.choice(b) for _ in b]
        ratios.append(statistics.median(sb) / statistics.median(sa))
    ratios.sort()
    return ratios[int(0.025 * rounds)], ratios[int(0.975 * rounds) - 1]


def run(exe, image, dpi, reps, options, env):
    out = subprocess.run([exe, image, dpi, str(reps), *options], capture_output=True, text=True,
                         env=env, check=True)
    return [json.loads(line) for line in out.stdout.splitlines() if line.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--processes", type=int, default=6)
    ap.add_argument("--reps", type=int, default=11)
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--text-bitmap", default=None)
    ap.add_argument("--cases", default="logo,text,tentabrode,marine")
    args = ap.parse_args()
    env = dict(os.environ)
    env.pop("OPENSTITCH_THREADS", None)
    if args.threads > 0:
        env["OPENSTITCH_THREADS"] = str(args.threads)
    out = Path(args.out)
    # Un dossier par campagne : refuser d'écrire dans un dossier existant évite qu'une seconde
    # exécution (simultanée ou non) écrase ou mélange des échantillons.
    out.mkdir(parents=True, exist_ok=False)
    summary = {"a": args.a, "b": args.b, "threads": args.threads, "processes": args.processes,
               "reps": args.reps, "pid": os.getpid(),
               "started": datetime.datetime.now().isoformat(timespec="seconds"), "cases": {}}
    failed = False
    for case in args.cases.split(","):
        image, dpi, options = CASES[case]
        if image is None:
            if not args.text_bitmap:
                print(f"{case}: --text-bitmap manquant, ignoré")
                continue
            image = args.text_bitmap
            # Même DPI que profile_engine.ps1 : largeur de la bitmap pour 240 mm.
            width = int.from_bytes(Path(image).read_bytes()[18:22], "little", signed=True)
            dpi = repr(width * 25.4 / 240.0)
        rows = {"a": [], "b": []}
        for proc in range(args.processes):
            order = ("a", "b") if proc % 2 == 0 else ("b", "a")
            for side in order:
                exe = args.a if side == "a" else args.b
                got = run(exe, image, dpi, args.reps, options, env)
                for r in got:
                    r["process"] = proc
                rows[side].extend(got)
        for side in ("a", "b"):
            with open(out / f"{case}.{side}.jsonl", "w", encoding="utf-8") as f:
                for r in rows[side]:
                    f.write(json.dumps(r) + "\n")
        warm = {s: [r["wall_ms"] for r in rows[s] if r["run"] > 0] for s in rows}
        prints = {s: {(r["sequence_hash"], r["label_hash"], r["preview_hash"]) for r in rows[s]}
                  for s in rows}
        same = prints["a"] == prints["b"] and len(prints["a"]) == 1
        failed = failed or not same
        lo, hi = ratio_ci(warm["a"], warm["b"])
        med_a, med_b = statistics.median(warm["a"]), statistics.median(warm["b"])
        entry = {"n": len(warm["a"]), "a_p50": med_a, "a_p95": p95(warm["a"]), "b_p50": med_b,
                 "b_p95": p95(warm["b"]), "ratio_p50": med_b / med_a, "ratio_ci95": [lo, hi],
                 "fingerprints_identical": same}
        summary["cases"][case] = entry
        print(f"{case}: n={entry['n']} A p50/p95 {med_a:.2f}/{entry['a_p95']:.2f} ms | "
              f"B p50/p95 {med_b:.2f}/{entry['b_p95']:.2f} ms | B/A {entry['ratio_p50']:.3f} "
              f"[{lo:.3f} ; {hi:.3f}] | empreintes {'identiques' if same else 'DIFFÉRENTES'}")
    summary["finished"] = datetime.datetime.now().isoformat(timespec="seconds")
    with open(out / "summary.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
