#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Banc de mesure de la numérisation automatique (AUTO-DIGITIZE AD-1, fiche Notion E14).

Chaque cas du corpus (formes synthétiques générées à l'identique + quelques images du dépôt)
passe par le vrai moteur : `openstitch-cli project digitize` puis `project check`,
`project fidelity` et `project density` en JSON. Le banc ne note rien à la main : il recopie
les mesures du moteur, les agrège et compare à une baseline versionnée.

  python scripts/autodigitize_bench.py run --cli <openstitch-cli> --out build/ad-bench
  python scripts/autodigitize_bench.py compare build/ad-bench/report.json \
      --baseline tests/fixtures/autodigitize-bench/baseline.json
  python scripts/autodigitize_bench.py corpus --out build/ad-corpus   # images seules

`compare` sort en code 1 si une mesure régresse au-delà de sa tolérance. Les temps sont
consignés mais jamais comparés (ils dépendent de la machine). Une mesure numérique ne prouve
pas la brodabilité : un essai physique reste nécessaire.

Bibliothèque standard seulement (PNG via scripts/ai_bench/pngio.py).
"""
from __future__ import annotations

import argparse
import functools
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from ai_bench import pngio  # noqa: E402

REPORT_SCHEMA = "openstitch.autodigitize-bench/1"
COMPARE_SCHEMA = "openstitch.autodigitize-bench-compare/1"

# --- Corpus synthétique -----------------------------------------------------------------------
# 400 x 400 px à 203,2 dpi = 50 mm de côté (échelle de référence des essais physiques).
SIZE_PX = 400
SYNTH_DPI = 203.2
MM_PER_PX = 25.4 / SYNTH_DPI

WHITE = (255, 255, 255)
BLACK = (20, 20, 20)
RED = (205, 35, 45)
BLUE = (30, 80, 190)
YELLOW = (245, 200, 30)
GREEN = (40, 150, 70)
ORANGE = (240, 120, 30)
SKY = (120, 190, 235)
BROWN = (120, 70, 35)


def _mm(v: float) -> float:
    return v / MM_PER_PX


def _seg_dist(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> float:
    dx, dy = bx - ax, by - ay
    den = dx * dx + dy * dy
    t = 0.0 if den == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / den))
    qx, qy = ax + t * dx, ay + t * dy
    return math.hypot(px - qx, py - qy)


def _polyline_dist(px: float, py: float, pts: list) -> float:
    return min(_seg_dist(px, py, *pts[i], *pts[i + 1]) for i in range(len(pts) - 1))


def _in_poly(px: float, py: float, pts: list) -> bool:
    inside = False
    j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > py) != (yj > py) and px < (xj - xi) * (py - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def _star(cx: float, cy: float, r_out: float, r_in: float, n: int = 5) -> list:
    pts = []
    for k in range(2 * n):
        r = r_out if k % 2 == 0 else r_in
        a = -math.pi / 2 + k * math.pi / n
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def _s_curve() -> list:
    pts = []
    for i in range(81):
        t = i / 80.0
        x = _mm(8) + t * _mm(34)
        y = SIZE_PX / 2 + _mm(12) * math.sin(t * 2 * math.pi)
        pts.append((x, y))
    return pts


S_CURVE = _s_curve()
STAR = _star(SIZE_PX / 2, SIZE_PX / 2, _mm(21), _mm(9))
HOUSE = [(_mm(14), _mm(30)), (_mm(24), _mm(21)), (_mm(34), _mm(30)), (_mm(34), _mm(42)),
         (_mm(14), _mm(42))]
Y_ARMS = [[(_mm(25), _mm(46)), (_mm(25), _mm(26))], [(_mm(25), _mm(26)), (_mm(12), _mm(6))],
          [(_mm(25), _mm(26)), (_mm(38), _mm(6))]]


def _color_logo(x: float, y: float) -> tuple:
    c = SIZE_PX / 2
    if _mm(6) <= x <= _mm(30) and _mm(18) <= y <= _mm(44):
        return RED
    if math.hypot(x - _mm(30), y - _mm(20)) <= _mm(14):
        return BLUE
    return WHITE


def _color_ring(x: float, y: float) -> tuple:
    d = math.hypot(x - SIZE_PX / 2, y - SIZE_PX / 2)
    return BLACK if _mm(15) <= d <= _mm(21) else WHITE


def _color_ribbon(x: float, y: float) -> tuple:
    if abs(y - SIZE_PX / 2) > _mm(15) or not _mm(5) <= x <= _mm(45):
        return WHITE
    return GREEN if _polyline_dist(x, y, S_CURVE) <= _mm(2.5) else WHITE


def _color_star(x: float, y: float) -> tuple:
    if not _in_poly(x, y, STAR):
        return WHITE
    edge = _polyline_dist(x, y, STAR + [STAR[0]])
    return BLACK if edge <= _mm(0.9) else YELLOW


def _color_letter_y(x: float, y: float) -> tuple:
    d = min(_polyline_dist(x, y, arm) for arm in Y_ARMS)
    return BLUE if d <= _mm(2.0) else WHITE


def _color_thin_lines(x: float, y: float) -> tuple:
    if not (_mm(5) <= x <= _mm(45) and _mm(5) <= y <= _mm(45)):
        return WHITE
    for k in range(5):
        lx = _mm(10 + 7.5 * k)
        if abs(x - lx) <= _mm(0.35):
            return BLACK
    if _seg_dist(x, y, _mm(5), _mm(45), _mm(45), _mm(5)) <= _mm(0.35):
        return BLACK
    return SKY


def _color_details(x: float, y: float) -> tuple:
    if not (_mm(4) <= x <= _mm(46) and _mm(4) <= y <= _mm(46)):
        return WHITE
    for i in range(4):
        for j in range(4):
            cx, cy = _mm(10 + 10 * i), _mm(10 + 10 * j)
            r = _mm(1.5) if (i + j) % 2 == 0 else _mm(0.8)
            if math.hypot(x - cx, y - cy) <= r:
                return RED
    return YELLOW


def _color_holes(x: float, y: float) -> tuple:
    if not (_mm(8) <= x <= _mm(42) and _mm(8) <= y <= _mm(42)):
        return WHITE
    for cx, cy, r in ((17, 17, 4.5), (33, 17, 3.0), (25, 33, 5.5)):
        if math.hypot(x - _mm(cx), y - _mm(cy)) <= _mm(r):
            return WHITE
    return ORANGE


def _color_flat_scene(x: float, y: float) -> tuple:
    if math.hypot(x - _mm(38), y - _mm(11)) <= _mm(6):
        return YELLOW
    if _in_poly(x, y, HOUSE):
        if _mm(21) <= x <= _mm(27) and _mm(34) <= y <= _mm(42):
            return BROWN
        return RED
    hill = _mm(36) + _mm(5) * math.sin(x / _mm(50) * math.pi * 2)
    if y >= hill:
        return GREEN
    return SKY


SYNTHETIC = {
    # nom : (fonction couleur, ce que le cas éprouve)
    "logo-two-color": (_color_logo, "aplats chevauchants deux couleurs"),
    "ring": (_color_ring, "anneau large : satin fermé ou tatami"),
    "s-ribbon": (_color_ribbon, "ruban en S de 5 mm : colonne satin courbe"),
    "star-outlined": (_color_star, "étoile jaune à contour noir de 0,9 mm"),
    "letter-y": (_color_letter_y, "branche en Y de 4 mm : bifurcation"),
    "thin-lines": (_color_thin_lines, "traits de 0,7 mm sur aplat (linework)"),
    "small-details": (_color_details, "points de 1,6 et 3 mm : préservation des détails"),
    "holes": (_color_holes, "aplat percé de trois trous"),
    "flat-scene": (_color_flat_scene, "illustration cinq couleurs"),
}

# Images du dépôt (résolution native du fichier, réglages par défaut du moteur).
REPO_IMAGES = {
    "repo-logo-small": "tests/fixtures/cli/logo_small.png",
    "repo-tentabrode": "tests/fixtures/tentabrode.png",
    "repo-marine-synth": "tests/fixtures/ai-benchmark/marine-synth-01/source.png",
}


@functools.lru_cache(maxsize=None)
def render_synthetic(name: str) -> bytes:
    """PNG RGB déterministe (octet pour octet) du cas synthétique `name`."""
    color_of = SYNTHETIC[name][0]
    buf = bytearray()
    for py in range(SIZE_PX):
        for px in range(SIZE_PX):
            buf.extend(color_of(px + 0.5, py + 0.5))
    return pngio.encode(SIZE_PX, SIZE_PX, "RGB", bytes(buf))


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_corpus(out: Path, include_repo: bool = True, only: list | None = None) -> list:
    """Écrit les images dans `out` et rend la liste des cas (nom, image, dpi, sha256)."""
    out.mkdir(parents=True, exist_ok=True)
    cases = []
    for name in SYNTHETIC:
        if only and name not in only:
            continue
        data = render_synthetic(name)
        path = out / f"{name}.png"
        path.write_bytes(data)
        cases.append({"name": name, "kind": "synthetic", "image": str(path), "dpi": SYNTH_DPI,
                      "purpose": SYNTHETIC[name][1], "sha256": sha256(data)})
    if include_repo:
        for name, rel in REPO_IMAGES.items():
            src = ROOT / rel
            if not src.is_file() or (only and name not in only):
                continue
            data = src.read_bytes()
            cases.append({"name": name, "kind": "repository", "image": str(src), "dpi": None,
                          "purpose": rel, "sha256": sha256(data)})
    return cases


# Profils de réglages mesurés. « logo » reprend le préréglage Studio Logo / aplats (satin des
# bandes, léger recouvrement, contours lissés) : sans lui, aucun satin n'est jamais choisi.
PROFILES = {
    "default": [],
    "logo": ["--satin-bands", "--fill-overlap", "0.15", "--curve-tolerance", "0.1"],
    # AUTO-SATIN-01 : même profil, satin choisi parmi les candidats mesurés.
    "logo-compare": ["--satin-bands", "--satin-compare", "--fill-overlap", "0.15",
                     "--curve-tolerance", "0.1"],
}

# --- Mesures ---------------------------------------------------------------------------------
# nom : (sens, tolérance relative, tolérance absolue). sens "lower" = plus petit est mieux.
METRICS = {
    "objects": ("info", 0.0, 0.0),
    "stitches": ("lower", 0.05, 50),
    "jumps": ("lower", 0.10, 2),
    "trims": ("lower", 0.10, 2),
    "color_changes": ("lower", 0.0, 0),
    "travel_mm": ("lower", 0.10, 20.0),
    "short_stitches": ("lower", 0.10, 10),
    "long_moves_without_trim": ("lower", 0.0, 0),
    "quality_errors": ("lower", 0.0, 0),
    "quality_warnings": ("lower", 0.0, 1),
    "missing_ratio": ("lower", 0.0, 0.01),
    "interior_hole_ratio": ("lower", 0.0, 0.005),
    "lost_detail_ratio": ("lower", 0.0, 0.01),
    "wrong_colour_ratio": ("lower", 0.0, 0.01),
    "layers_p99": ("lower", 0.0, 0.25),
    "layers_max": ("lower", 0.0, 0.5),
    "hotspot_area_mm2": ("lower", 0.0, 1.0),
}


def _get(d: dict, *keys, default=None):
    cur = d
    for k in keys:
        if not isinstance(cur, dict) or k not in cur:
            return default
        cur = cur[k]
    return cur


def metrics_from(check: dict, fidelity: dict, density: dict, digitize: dict) -> dict:
    """Aplati les rapports moteur. Une clé absente donne None (jamais une valeur inventée)."""
    seq = check.get("sequence", {})
    moves = (_get(seq, "moves_between", "travel_mm", default=0.0) or 0.0) + \
        (_get(seq, "moves_within", "travel_mm", default=0.0) or 0.0)
    objects = digitize.get("objects")
    if isinstance(objects, dict) and "total" in objects:
        n_objects = objects["total"]
    else:
        n_objects = len(check.get("objects", [])) if "objects" in check else None

    def ratio(key: str):
        """Part du motif (0..1) lue dans `<key>.percent` du rapport de fidélité."""
        pct = _get(fidelity, key, "percent")
        return None if pct is None else pct / 100.0

    return {
        "objects": n_objects,
        "stitches": seq.get("stitches"),
        "jumps": seq.get("jumps"),
        "trims": seq.get("trims"),
        "color_changes": seq.get("color_changes"),
        "travel_mm": round(moves, 3),
        "short_stitches": seq.get("short_stitches"),
        "long_moves_without_trim": seq.get("long_moves_without_trim"),
        "quality_errors": _get(check, "summary", "errors"),
        "quality_warnings": _get(check, "summary", "warnings"),
        "missing_ratio": None if ratio("missing") is None else round(ratio("missing"), 5),
        "interior_hole_ratio": None if ratio("interior_holes") is None
        else round(ratio("interior_holes"), 5),
        "lost_detail_ratio": None if ratio("lost_detail") is None
        else round(ratio("lost_detail"), 5),
        "wrong_colour_ratio": None if ratio("wrong_colour") is None
        else round(ratio("wrong_colour"), 5),
        "layers_p99": density.get("layers_p99"),
        "layers_max": density.get("layers_max"),
        "hotspot_area_mm2": density.get("hotspot_area_mm2",
                                        _get(density, "area_over_mm2", "4_layers")),
    }


def object_types(digitize: dict) -> dict:
    """Compte des objets créés par type, si le rapport de numérisation le fournit."""
    objects = digitize.get("objects")
    if isinstance(objects, dict):
        by_type = objects.get("ids_by_type")
        if isinstance(by_type, dict):
            return {k: len(v) for k, v in sorted(by_type.items()) if isinstance(v, list)}
        return {k: v for k, v in sorted(objects.items())
                if isinstance(v, int) and v and k not in ("total", "vector")}
    if isinstance(objects, list):
        counts: dict = {}
        for o in objects:
            t = o.get("type", "?") if isinstance(o, dict) else "?"
            counts[t] = counts.get(t, 0) + 1
        return dict(sorted(counts.items()))
    return {}


class CliEngine:
    """Exécute openstitch-cli ; `run(args) -> (code, stdout)` est la seule couture des tests."""

    def __init__(self, cli: str):
        self.cli = cli

    def run(self, args: list) -> tuple:
        p = subprocess.run([self.cli] + args, capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        return p.returncode, p.stdout

    def version(self) -> str:
        rc, out = self.run(["--version"])
        return out.strip().splitlines()[0] if rc == 0 and out.strip() else "unknown"


def _json(out: str):
    try:
        doc = json.loads(out)
    except json.JSONDecodeError:
        return None
    return doc if isinstance(doc, dict) else None


def measure_case(engine, case: dict, workdir: Path, profile: str = "default") -> dict:
    record = {"name": case["name"], "profile": profile, "kind": case["kind"],
              "sha256": case["sha256"],
              "status": "failed", "error": None, "metrics": None, "object_types": {},
              "time_s": None}
    osp = workdir / f"{case['name']}@{profile}.osp"
    dst = workdir / f"{case['name']}@{profile}.dst"
    args = ["project", "digitize", case["image"], "-o", str(osp), "--dst", str(dst), "--json"]
    if case.get("dpi"):
        args += ["--dpi", f"{case['dpi']:g}"]
    args += PROFILES[profile]
    t0 = time.perf_counter()
    rc, out = engine.run(args)
    record["time_s"] = round(time.perf_counter() - t0, 3)
    digitize = _json(out)
    if digitize is None or digitize.get("schema") == "openstitch.error/1" or rc != 0:
        err = (digitize or {}).get("error") or {}
        record["error"] = {"stage": "digitize", "rc": rc, "code": err.get("code", "E_BAD_JSON")}
        return record
    reports = {}
    for name in ("check", "fidelity", "density"):
        rc, out = engine.run(["project", name, str(osp), "--json"])
        doc = _json(out)
        if doc is None or doc.get("schema") == "openstitch.error/1":
            err = (doc or {}).get("error") or {}
            record["error"] = {"stage": name, "rc": rc, "code": err.get("code", "E_BAD_JSON")}
            return record
        reports[name] = doc
    record["metrics"] = metrics_from(reports["check"], reports["fidelity"], reports["density"],
                                     digitize)
    record["object_types"] = object_types(digitize)
    record["status"] = "measured"
    return record


def run_bench(engine, out: Path, include_repo: bool = True, only: list | None = None) -> dict:
    corpus_dir = out / "corpus"
    work = out / "work"
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    cases = build_corpus(corpus_dir, include_repo, only)
    results = [measure_case(engine, c, work, p) for c in cases for p in PROFILES]
    measured = [r for r in results if r["status"] == "measured"]
    report = {
        "schema": REPORT_SCHEMA,
        "engine": engine.version(),
        "profiles": PROFILES,
        "corpus": {"synthetic_dpi": SYNTH_DPI, "size_px": SIZE_PX,
                   "cases": [{k: c[k] for k in ("name", "kind", "purpose", "sha256")}
                             for c in cases]},
        "summary": {"cases": len(results), "measured": len(measured),
                    "failed": len(results) - len(measured),
                    "totals": totals(measured)},
        "results": results,
    }
    return report


def totals(measured: list) -> dict:
    keys = ("stitches", "jumps", "trims", "color_changes", "quality_errors", "quality_warnings")
    return {k: sum((r["metrics"].get(k) or 0) for r in measured) for k in keys}


# --- Comparaison à la baseline ---------------------------------------------------------------
def classify(metric: str, base, cur) -> str:
    """'same' | 'better' | 'worse' | 'regression' | 'info' | 'missing'."""
    sense, rel, absolute = METRICS.get(metric, ("info", 0.0, 0.0))
    if base is None or cur is None:
        return "missing" if (base is None) != (cur is None) else "same"
    if sense == "info":
        return "same" if base == cur else "info"
    delta = cur - base
    if delta == 0:
        return "same"
    tol = max(absolute, abs(base) * rel)
    improves = delta < 0 if sense == "lower" else delta > 0
    if improves:
        return "better"
    return "regression" if abs(delta) > tol + 1e-12 else "worse"


def compare(current: dict, baseline: dict) -> dict:
    def key(r: dict) -> str:
        return f"{r['name']}@{r.get('profile', 'default')}"

    base_by = {key(r): r for r in baseline.get("results", [])}
    rows, regressions, improvements = [], 0, 0
    for r in current.get("results", []):
        b = base_by.get(key(r))
        entry = {"name": key(r), "status": r["status"], "metrics": {}}
        if b is None:
            entry["note"] = "absent de la baseline"
        elif b.get("sha256") != r.get("sha256"):
            entry["note"] = "image différente de la baseline : comparaison ignorée"
        elif r["status"] != "measured":
            entry["note"] = "échec de mesure"
            if b.get("status") == "measured":
                regressions += 1
        else:
            for k in METRICS:
                bv = (b.get("metrics") or {}).get(k)
                cv = r["metrics"].get(k)
                verdict = classify(k, bv, cv)
                if verdict not in ("same",):
                    entry["metrics"][k] = {"baseline": bv, "current": cv, "verdict": verdict}
                regressions += verdict == "regression"
                improvements += verdict == "better"
        rows.append(entry)
    missing = sorted(set(base_by) - {key(r) for r in current.get("results", [])})
    return {"schema": COMPARE_SCHEMA, "baseline_engine": baseline.get("engine"),
            "current_engine": current.get("engine"), "regressions": regressions,
            "improvements": improvements, "missing_cases": missing, "cases": rows}


def markdown(report: dict, comparison: dict | None = None) -> str:
    cols = ("objects", "stitches", "trims", "color_changes", "quality_errors", "missing_ratio",
            "interior_hole_ratio", "lost_detail_ratio", "layers_max")
    lines = [f"# Banc auto-digitize — {report.get('engine', '?')}", "",
             f"{report['summary']['measured']}/{report['summary']['cases']} cas mesurés.", "",
             "| cas | types | " + " | ".join(cols) + " | s |",
             "|---|---|" + "---|" * (len(cols) + 1)]
    for r in report["results"]:
        if r["status"] != "measured":
            lines.append(f"| {r['name']}@{r['profile']} | échec {r['error']} |"
                         + " |" * (len(cols) + 1))
            continue
        types = ", ".join(f"{k}:{v}" for k, v in r["object_types"].items()) or "?"
        vals = " | ".join("—" if r["metrics"].get(c) is None else f"{r['metrics'][c]:g}"
                          for c in cols)
        lines.append(f"| {r['name']}@{r['profile']} | {types} | {vals} | {r['time_s']} |")
    if comparison is not None:
        lines += ["", f"Comparaison : {comparison['regressions']} régression(s), "
                  f"{comparison['improvements']} amélioration(s).", ""]
        for c in comparison["cases"]:
            for k, v in c["metrics"].items():
                if v["verdict"] in ("regression", "better", "worse", "missing"):
                    lines.append(f"- {c['name']} / {k} : {v['baseline']} → {v['current']} "
                                 f"({v['verdict']})")
            if c.get("note"):
                lines.append(f"- {c['name']} : {c['note']}")
    lines += ["", "Mesures numériques du moteur : elles ne prouvent pas la brodabilité."]
    return "\n".join(lines) + "\n"


def write_json(path: Path, doc: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, ensure_ascii=False, indent=1, sort_keys=True) + "\n",
                    encoding="utf-8")


def find_cli(explicit: str | None) -> str:
    candidates = [explicit, os.environ.get("OPENSTITCH_CLI"),
                  str(ROOT / "build/msvc/apps/cli/Release/openstitch-cli.exe"),
                  str(ROOT / "build/dev-release/apps/cli/openstitch-cli"),
                  shutil.which("openstitch-cli")]
    for c in candidates:
        if c and Path(c).is_file():
            return c
    raise SystemExit("openstitch-cli introuvable : passer --cli ou OPENSTITCH_CLI")


def main(argv: list | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="numérise et mesure tout le corpus")
    r.add_argument("--cli")
    r.add_argument("--out", type=Path, default=ROOT / "build/autodigitize-bench")
    r.add_argument("--baseline", type=Path, help="compare aussitôt à cette baseline")
    r.add_argument("--no-repo-images", action="store_true")
    r.add_argument("--only", nargs="*", help="noms de cas à mesurer")
    c = sub.add_parser("compare", help="compare un rapport à une baseline")
    c.add_argument("report", type=Path)
    c.add_argument("--baseline", type=Path, required=True)
    k = sub.add_parser("corpus", help="écrit seulement les images du corpus")
    k.add_argument("--out", type=Path, required=True)
    args = ap.parse_args(argv)

    if args.cmd == "corpus":
        cases = build_corpus(args.out, include_repo=False)
        print(json.dumps([{k: c[k] for k in ("name", "sha256")} for c in cases], indent=1))
        return 0
    if args.cmd == "compare":
        current = json.loads(args.report.read_text(encoding="utf-8"))
        baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
        result = compare(current, baseline)
        write_json(args.report.with_name("compare.json"), result)
        print(markdown(current, result))
        return 1 if result["regressions"] else 0
    engine = CliEngine(find_cli(args.cli))
    report = run_bench(engine, args.out, include_repo=not args.no_repo_images, only=args.only)
    write_json(args.out / "report.json", report)
    result = None
    if args.baseline:
        result = compare(report, json.loads(args.baseline.read_text(encoding="utf-8")))
        write_json(args.out / "compare.json", result)
    md = markdown(report, result)
    (args.out / "report.md").write_text(md, encoding="utf-8")
    print(md)
    if report["summary"]["failed"]:
        return 1
    return 1 if result and result["regressions"] else 0


if __name__ == "__main__":
    sys.exit(main())
