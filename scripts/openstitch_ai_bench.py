#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Benchmark reproductible de segmentation : baseline CIELAB vs SAM 2.1 (PR-01).

Hors ligne, bibliotheque standard seulement. Un backend indisponible est `skipped`
avec sa raison ; aucune metrique n'est jamais synthetisee.

    python scripts/openstitch_ai_bench.py --out bench-out --cli build/msvc/apps/cli/Debug/openstitch-cli.exe
    python scripts/openstitch_ai_bench.py --out bench-out --sam-models ~/sam2-models --backends cielab,sam2
    python scripts/openstitch_ai_bench.py --out bench-out --controls   # + temoins de la metrique

Sorties : report.json (openstitch.ai.benchmark-report/1), summary.md, compare_<backend>.png.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "ai_bench"))
import backends as bk  # noqa: E402
import metrics  # noqa: E402
import pngio  # noqa: E402

REPORT_SCHEMA = "openstitch.ai.benchmark-report/1"
RUNNER_VERSION = 1
DEFAULT_MANIFEST = HERE.parent / "tests" / "fixtures" / "ai-benchmark" / "manifest.json"
CONTROLS = ("oracle", "lighthouse_only")


class ManifestError(Exception):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise ManifestError(f"manifeste illisible : {exc}") from exc
    if manifest.get("schema") != "openstitch.ai.benchmark-manifest/1":
        raise ManifestError(f"schema de manifeste inconnu : {manifest.get('schema')!r}")
    root = path.parent
    for case in manifest["cases"]:
        entries = [(case["source"]["path"], case["source"]["sha256"])]
        entries += [(o["gold_mask"]["path"], o["gold_mask"]["sha256"]) for o in case["objects"]]
        for rel, expected in entries:
            target = (root / rel).resolve()
            if root.resolve() not in target.parents:
                raise ManifestError(f"chemin hors du corpus : {rel}")
            if not target.is_file():
                raise ManifestError(f"fichier du corpus absent : {rel}")
            if sha256_bytes(target.read_bytes()) != expected:
                raise ManifestError(f"empreinte differente (entree non epinglee) : {rel}")
    return manifest


def _git_rev() -> str | None:
    try:
        out = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=HERE, timeout=10)
        return out.stdout.strip() or None if out.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None


def _cli_version(cli: str | None) -> str | None:
    if not cli:
        return None
    try:
        out = subprocess.run([cli, "--version"], capture_output=True, text=True, timeout=20)
        return out.stdout.strip().splitlines()[0] if out.returncode == 0 and out.stdout.strip() else None
    except (OSError, subprocess.SubprocessError):
        return None


def _palette(index: int) -> tuple[int, int, int]:
    if index == 0:
        return (0, 0, 0)
    h = hashlib.sha256(str(index).encode()).digest()
    return (60 + h[0] % 190, 60 + h[1] % 190, 60 + h[2] % 190)


def _label_image(labels: list[int] | None, width: int, height: int) -> bytes:
    if labels is None:
        return bytes([128, 128, 128]) * (width * height)
    return b"".join(bytes(_palette(v)) for v in labels)


def write_comparison(path: Path, case_root: Path, case: dict, result: bk.BackendResult) -> None:
    w, h = case["source"]["width_px"], case["source"]["height_px"]
    sw, sh, sch, src = pngio.decode((case_root / case["source"]["path"]).read_bytes())
    gold = [0] * (w * h)
    for o in case["objects"]:
        gw, gh, gc, gpx = pngio.decode((case_root / o["gold_mask"]["path"]).read_bytes())
        for i, v in enumerate(pngio.to_gray(gw, gh, gc, gpx)):
            if v:
                gold[i] = o["label"]
    panels = [src if sch == 3 else b"".join(src[i : i + 3] for i in range(0, len(src), sch)),
              _label_image(gold, w, h), _label_image(result.label_map, w, h)]
    rows = b"".join(p[y * w * 3 : (y + 1) * w * 3] for y in range(h) for p in panels)
    path.write_bytes(pngio.encode(w * 3, h, "RGB", rows))


def evaluate(case: dict, case_root: Path, backend, workdir: Path, tol_mm: float) -> dict:
    w, h = case["source"]["width_px"], case["source"]["height_px"]
    grid = metrics.Grid(w, h)
    result = backend.run(case, case_root, grid, workdir)
    entry: dict = {"backend": result.backend, "status": result.status, "reason": result.reason, "meta": result.meta}
    if result.status != "ok":
        entry.update(objects=None, aggregate=None, gate=None)
        return entry, result
    tol_px = max(1, round(tol_mm * case["px_per_mm"]))
    scores = []
    for obj in case["objects"]:
        gw, gh, gc, gpx = pngio.decode((case_root / obj["gold_mask"]["path"]).read_bytes())
        gold = grid.pack(bytes(1 if v else 0 for v in pngio.to_gray(gw, gh, gc, gpx)))
        scores.append(metrics.score_object(grid, obj, gold, result.regions, tol_px))
    gate = metrics.semantic_gate(scores, case.get("main_subject_families", []))
    covered = 0
    for r in result.regions:
        covered |= r.mask
    protected = [s for s in scores if s.protected]
    fam_scores = []
    for fam in case.get("families", []):
        gw, gh, gc, gpx = pngio.decode((case_root / fam["gold_mask"]["path"]).read_bytes())
        fgold = grid.pack(bytes(1 if v else 0 for v in pngio.to_gray(gw, gh, gc, gpx)))
        fam_scores.append(metrics.score_family(grid, fam, fgold, result.regions))
    fam_missing = [f.family for f in fam_scores if f.main_subject and not f.present]
    entry["family_gate"] = {"passed": not fam_missing, "main_missing": fam_missing,
                            "families": [f.__dict__ for f in fam_scores]}

    def mean(values):
        values = [v for v in values if v is not None]
        return round(sum(values) / len(values), 4) if values else None

    entry["objects"] = [s.__dict__ for s in scores]
    entry["gate"] = gate
    entry["aggregate"] = {
        "regions": len(result.regions),
        "pixel_coverage": round(metrics.count(covered) / (w * h), 4),
        "protected_present": f"{sum(1 for s in protected if s.present)}/{len(protected)}",
        "mean_union_iou_protected": mean(s.union_iou for s in protected),
        "mean_edge_f_protected": mean(s.edge_f for s in protected),
        "mean_fragmentation": mean(float(s.regions_used) for s in scores if s.present),
        "holes_preserved": [s.semantic_id for s in scores if s.holes_preserved is True],
        "holes_lost": [s.semantic_id for s in scores if s.holes_preserved is False],
    }
    if result.project_check is not None:
        summary = result.project_check.get("summary", {})
        seq = result.project_check.get("sequence", {})
        entry["project_check"] = {"summary": summary, "stitches": seq.get("stitches"),
                                  "jumps": seq.get("jumps"), "trims": seq.get("trims")}
    else:
        entry["project_check"] = None
        entry["project_check_reason"] = result.project_check_reason or "non applicable a ce backend"
    return entry, result


def render_summary(report: dict) -> str:
    lines = ["# Benchmark de segmentation", "",
             f"Manifeste `{report['inputs']['manifest_sha256'][:12]}` · runner v{report['runner']['version']}", ""]
    for case in report["cases"]:
        lines += [f"## {case['case_id']}", "",
                  "| backend | statut | sujets principaux (familles) | porte objets requis | protégés présents | IoU moyen | contour/trait F | régions | project_check |",
                  "|---|---|---|---|---|---|---|---|---|"]
        for r in case["results"]:
            if r["status"] != "ok":
                lines.append(f"| {r['backend']} | **{r['status']}** — {r['reason']} | — | — | — | — | — | — | — |")
                continue
            a, g = r["aggregate"], r["gate"]
            pc = r.get("project_check")
            pcs = "—" if not pc else f"{pc['summary'].get('errors')} err / {pc['summary'].get('warnings')} avert."
            fg = r["family_gate"]
            fam = "**OK**" if fg["passed"] else "**ÉCHEC** (" + ", ".join(fg["main_missing"]) + ")"
            lines.append(
                f"| {r['backend']} | ok | {fam} | {'**OK**' if g['passed'] else '**ÉCHEC** (' + ', '.join(g['required_missing']) + ')'} "
                f"| {a['protected_present']} | {a['mean_union_iou_protected']} | {a['mean_edge_f_protected']} "
                f"| {a['regions']} | {pcs} |")
        for r in case["results"]:
            if r["status"] != "ok":
                continue
            lines += ["", f"### {r['backend']} — détail par objet", "",
                      "| objet | requis | présent | IoU (union) | IoU (1 région) | contour/trait F | régions | trou |",
                      "|---|---|---|---|---|---|---|---|"]
            for o in r["objects"]:
                hole = "—" if o["holes_preserved"] is None else ("conservé" if o["holes_preserved"] else "perdu")
                lines.append(f"| {o['semantic_id']} | {'oui' if o['required'] else ''} | {'oui' if o['present'] else '**non**'} "
                             f"| {o['union_iou']} | {o['best_single_iou']} | {o['edge_f']} | {o['regions_used']} | {hole} |")
    lines += ["", "## Lecture", "",
              "- Une instance est *présente* si l'union des régions majoritairement contenues dans l'objet couvre au moins 50 % de IoU.",
              "- La porte « sujets principaux » échoue dès qu'un objet requis manque, quel que soit le score des autres.",
              "- Fixtures synthétiques : ces chiffres valident la métrique et la chaîne, **ils ne classent pas** les modèles sur de vraies illustrations.",
              "- `skipped` = poids/CLI absents, pas un échec du modèle. Aucune valeur n'est remplacée par une estimation.", ""]
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--manifest", default=str(DEFAULT_MANIFEST))
    ap.add_argument("--out", required=True, help="dossier de sortie (cree)")
    ap.add_argument("--backends", default="cielab,sam2")
    ap.add_argument("--controls", action="store_true", help="ajoute les temoins oracle et lighthouse_only")
    ap.add_argument("--cli", default=os.environ.get("OPENSTITCH_CLI"))
    ap.add_argument("--sam-models", default=os.environ.get("OPENSTITCH_SAM_MODELS"))
    ap.add_argument("--sam-model", default="small", choices=["tiny", "small", "base_plus", "large"])
    ap.add_argument("--sam-python", default=os.environ.get("OPENSTITCH_SAM_PYTHON"))
    ap.add_argument("--pipelines", help="JSON {nom: {planner:[commande...], segmenter:[commande...]}} (protocole generique)")
    ap.add_argument("--tol-mm", type=float, default=0.5, help="tolerance de contour en mm")
    args = ap.parse_args(argv)

    manifest_path = Path(args.manifest).resolve()
    try:
        manifest = load_manifest(manifest_path)
    except ManifestError as exc:
        print(f"erreur : {exc}", file=sys.stderr)
        return 2
    wanted = [b for b in args.backends.split(",") if b]
    if args.controls:
        wanted += [c for c in CONTROLS if c not in wanted]
    pipelines = json.loads(Path(args.pipelines).read_text(encoding="utf-8")) if args.pipelines else None
    pool = bk.available(args.cli, args.sam_models, args.sam_model, args.sam_python, pipelines)
    unknown = [b for b in wanted if b not in pool]
    if unknown:
        print(f"backend inconnu : {', '.join(unknown)} (disponibles : {', '.join(pool)})", file=sys.stderr)
        return 2

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": REPORT_SCHEMA,
        "runner": {"version": RUNNER_VERSION, "python": platform.python_version(), "git_rev": _git_rev(),
                   "openstitch_cli": _cli_version(args.cli)},
        "inputs": {"manifest": manifest_path.name, "manifest_sha256": sha256_bytes(manifest_path.read_bytes()),
                   "tolerance_mm": args.tol_mm},
        "cases": [],
    }
    with tempfile.TemporaryDirectory(prefix="openstitch-aibench-") as tmp:
        for case in manifest["cases"]:
            results = []
            for name in wanted:
                entry, res = evaluate(case, manifest_path.parent, pool[name], Path(tmp) / case["case_id"], args.tol_mm)
                results.append(entry)
                if entry["status"] == "ok":
                    write_comparison(out / f"compare_{case['case_id']}_{name}.png", manifest_path.parent, case, res)
            report["cases"].append({"case_id": case["case_id"], "results": results})
    (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (out / "summary.md").write_text(render_summary(report), encoding="utf-8")
    for case in report["cases"]:
        for r in case["results"]:
            tag = r["status"] if r["status"] != "ok" else ("gate OK" if r["gate"]["passed"] else "gate ECHEC")
            print(f"{case['case_id']} / {r['backend']}: {tag}" + (f" — {r['reason']}" if r["reason"] else ""))
    print(f"rapport : {out / 'report.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
