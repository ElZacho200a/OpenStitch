#!/usr/bin/env python3
"""Baseline OpenStitch sur un benchmark public, SANS note visuelle inventee.

Experimental runner for the isolated 'agent-trials' branch. A quantized
technical map is NOT a semantic object map; the result is never self-approved.
"""
from __future__ import annotations

import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import sys
import time

from PIL import Image

REPO = "ElZacho200a/OpenStitch"
ROOT = Path(os.environ["OPENSTITCH_SOURCE_ROOT"])
WORK = Path(os.environ["OPENSTITCH_WORK_ROOT"])
BIN = Path(os.environ["OPENSTITCH_CLI_INSTALL_DIR"]) / "openstitch-cli"
MANIFEST_PATH = Path(__file__).resolve().parent / "benchmarks" / "benchmarks.json"
MANIFEST = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
BENCHMARK = next(c for c in MANIFEST["cases"] if c["id"] == MANIFEST["preferred_case_id"])
SOURCE_PATH = os.environ.get("OPENSTITCH_BENCHMARK_IMAGE")
SOURCE = Path(SOURCE_PATH) if SOURCE_PATH else None
COMMANDS = []


def run(label: str, cmd: list[str], timeout: int = 180) -> dict:
    started = time.monotonic()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout, check=False)
        result = {"name": label, "exit_code": proc.returncode,
                  "elapsed_s": round(time.monotonic() - started, 2),
                  "stdout_file": f"{label}.stdout.txt",
                  "stderr_file": f"{label}.stderr.txt"}
        (WORK / result["stdout_file"]).write_text(proc.stdout, encoding="utf-8")
        (WORK / result["stderr_file"]).write_text(proc.stderr, encoding="utf-8")
    except (OSError, subprocess.TimeoutExpired) as exc:
        result = {"name": label, "exit_code": None,
                  "elapsed_s": round(time.monotonic() - started, 2),
                  "error": str(exc)}
    COMMANDS.append(result)
    return result


def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    rid = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    rid += "-ruines-marais-gh" + os.environ.get("GITHUB_RUN_ID", "local")
    main_sha = run("main_sha", ["git", "-C", str(ROOT), "rev-parse", "HEAD"])
    commit = (WORK / "main_sha.stdout.txt").read_text().strip() if main_sha["exit_code"] == 0 else None
    result = {
        "schema": "openstitch.trial/1", "run_id": rid, "created_utc": timestamp,
        "status": "blocked", "source": {
            "benchmark_id": BENCHMARK["id"],
            "library_path": BENCHMARK.get("source_library_path"),
            "sha256": None, "size_mm": None,
            "target_height_mm": BENCHMARK["target_height_mm"]
        }, "openstitch_commit": commit, "cli_version": None,
        "semantic_gate": "not_run", "visual_review": "not_run",
        "reason": None, "score_before": None, "score_after": None,
        "evaluation": None, "artifact": {"kind": "unavailable", "url": None, "retention_days": None},
        "changes": [], "lessons": [],
        "parent_run_id": None, "commands": COMMANDS, "machine_tested": False,
        "expected": ["OSP", "DST", "real-render", "CLI JSON diagnostics"],
        "produced": []
    }
    def persist() -> None:
        (WORK / "result.json").write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    try:
        if SOURCE is None or not SOURCE.is_file():
            result["reason"] = (
                "Le benchmark prioritaire est dans ChatGPT Library privee, absent du runner "
                "GitHub Actions. Fournir OPENSTITCH_BENCHMARK_IMAGE avec le vrai fichier "
                "verifie (ne pas remplacer par tentabrode.png)."
            )
            result["lessons"] = ["L'image privée doit être transférée explicitement avant de lancer le moteur."]
            return 0
        raw = SOURCE.read_bytes()
        sha = hashlib.sha256(raw).hexdigest()
        result["source"]["sha256"] = sha
        if sha != BENCHMARK.get("source_sha256"):
            result["reason"] = "SHA-256 du benchmark incompatible avec le manifeste: exécution refusée."
            return 0
        im = Image.open(SOURCE).convert("RGBA")
        width, height = im.size
        if width <= 0 or height <= 0:
            result["reason"] = "Dimensions source invalides"
            return 0
        target_height = float(BENCHMARK["target_height_mm"])
        target_w = round(target_height * width / height, 3)
        dpi = height * 25.4 / target_height
        result["source"]["size_mm"] = [target_w, target_height]
        result["source"]["size_px"] = [width, height]
        result["parameters"] = {"dpi": round(dpi, 6), "max_colors": 10,
                                "skip_background": "auto", "mode": "shapes"}
        # A technical color grouping is useful for a baseline, but it is
        # explicitly NOT semantic reconstruction and receives no A-pass.
        quant = im.quantize(colors=10, method=Image.Quantize.FASTOCTREE)
        prepared = WORK / "technical-proxy.png"
        quant.convert("RGBA").save(prepared)
        result["semantic_gate"] = "not_verified_technical_proxy_only"
        result["produced"].append("technical-proxy.png")
        install = run("cli_install", ["bash", str(ROOT / "scripts/install-cli-linux.sh"),
                                     "--install-dir", str(BIN.parent)], timeout=240)
        if install["exit_code"] != 0 or not BIN.is_file():
            result["reason"] = "CLI officiel impossible a installer; voir cli_install.stderr.txt"
            result["lessons"] = ["Besoin d'un runner avec acces a la release Linux et verification SHA-256."]
            return 0
        version = run("cli_version", [str(BIN), "--version"], timeout=30)
        if version["exit_code"] == 0:
            result["cli_version"] = (WORK / "cli_version.stdout.txt").read_text().strip()
        osp = WORK / "baseline.osp"
        dst = WORK / "baseline.dst"
        dig = run("digitize", [str(BIN), "project", "digitize", str(prepared),
                              "-o", str(osp), "--dpi", str(round(dpi, 6)),
                              "--max-colors", "10", "--mode", "shapes",
                              "--skip-background", "auto", "--json"], timeout=480)
        if dig["exit_code"] != 0 or not osp.is_file():
            result["reason"] = "Echec de la numerisation native; voir digitize.stderr.txt"
            return 0
        result["produced"].append("baseline.osp")
        for label, args in [
            ("inspect", ["project", "inspect", str(osp), "--json"]),
            ("check", ["project", "check", str(osp), "--json"]),
            ("diagnose", ["project", "diagnose", str(osp), "--json"]),
            ("fidelity", ["project", "fidelity", str(osp), "--json"]),
            ("density", ["project", "density", str(osp), "--json"]),
            ("render", ["project", "render", str(osp), "-o",
                        str(WORK / "preview.png"), "--px-per-mm", "4"]),
            ("osp2dst", ["osp2dst", str(osp), str(dst)]),
        ]:
            outcome = run(label, [str(BIN), *args], timeout=480)
            if label in ("check", "diagnose") and outcome["exit_code"] == 2:
                result["lessons"].append(label + ": erreurs ou avertissements detectes par le moteur; examiner le rapport")
        for fn in ("preview.png", "baseline.dst"):
            if (WORK / fn).is_file():
                result["produced"].append(fn)
        if dst.is_file():
            run("stats", [str(BIN), "stats", str(dst), "--json"], timeout=120)
        # Controlled V2: correct the actual stitched height and the 100x100 mm
        # default frame that the initial quality report correctly rejects.
        try:
            initial_quality = json.loads((WORK / "check.stdout.txt").read_text(encoding="utf-8"))
            stitched_height = float(initial_quality["sequence"]["height_mm"])
            initial_errors = int(initial_quality["summary"]["errors"])
            if stitched_height > 0 and initial_errors >= 0:
                refined_dpi = round(dpi * stitched_height / target_height, 6)
                refined_osp = WORK / "refined.osp"
                refined_dst = WORK / "refined.dst"
                refinement = run("refined_digitize", [
                    str(BIN), "project", "digitize", str(prepared),
                    "-o", str(refined_osp), "--dpi", str(refined_dpi),
                    "--max-colors", "10", "--mode", "shapes",
                    "--skip-background", "auto", "--json"], timeout=480)
                if refinement["exit_code"] == 0 and refined_osp.is_file():
                    new_frame = run("refined_frame", [
                        str(BIN), "project", "set", str(refined_osp),
                        "--width-mm", "225", "--height-mm", "270",
                        "--margin-mm", "5"], timeout=90)
                    if new_frame["exit_code"] == 0:
                        result["produced"].append("refined.osp")
                        for label, args in [
                            ("refined_check", ["project", "check", str(refined_osp), "--json"]),
                            ("refined_diagnose", ["project", "diagnose", str(refined_osp), "--json"]),
                            ("refined_fidelity", ["project", "fidelity", str(refined_osp), "--json"]),
                            ("refined_density", ["project", "density", str(refined_osp), "--json"]),
                            ("refined_render", ["project", "render", str(refined_osp),
                                                "-o", str(WORK / "refined-preview.png"),
                                                "--px-per-mm", "4"]),
                            ("refined_export", ["osp2dst", str(refined_osp), str(refined_dst)]),
                        ]:
                            run(label, [str(BIN), *args], timeout=480)
                        if refined_dst.is_file():
                            run("refined_stats", [str(BIN), "stats", str(refined_dst), "--json"])
                            result["produced"].append("refined.dst")
                        if (WORK / "refined-preview.png").is_file():
                            result["produced"].append("refined-preview.png")
                        quality2 = json.loads((WORK / "refined_check.stdout.txt").read_text(encoding="utf-8"))
                        result["comparison"] = {
                            "hypothesis": "Correct frame dimensions and target actual stitched height; no semantic edits",
                            "baseline": {
                                "check_errors": initial_errors,
                                "height_mm": round(stitched_height, 3),
                                "frame_mm": [100, 100]
                            },
                            "refined": {
                                "check_errors": quality2.get("summary", {}).get("errors"),
                                "height_mm": quality2.get("sequence", {}).get("height_mm"),
                                "frame_mm": [225, 270],
                                "dpi": refined_dpi
                            },
                            "preferred_for_geometry": "refined.osp"
                                if quality2.get("summary", {}).get("errors", 999999) < initial_errors
                                else None
                        }
                        result["changes"].append(
                            "V2: ajustement DPI sur hauteur réellement cousue + cadre 225x270 mm, marge 5 mm")
                        result["lessons"].append(
                            "La taille image importe moins que la boite réelle des points; contrôler le cadre après digitize.")
        except (ValueError, KeyError, TypeError, OSError, json.JSONDecodeError) as exc:
            result["lessons"].append("V2 non achevée : " + str(exc))
        result["status"] = "incomplete"
        result["reason"] = (
            "Baseline native executee sur image validee; segmentation technique non validee semantiquement. "
            "Aucun second juge visuel independant disponible sur ce runner : note /20 non attribuee."
        )
        result["lessons"].append(
            "Ne pas confondre la quantification de couleurs et la segmentation semantique d'instances."
        )
        return 0
    except Exception as exc:
        result["status"] = "blocked"
        result["reason"] = f"Exception du runner : {type(exc).__name__}: {exc}"
        return 0
    finally:
        persist()
        print("TRIAL_RESULT=" + json.dumps(
            {"run_id": rid, "status": result["status"], "reason": result["reason"],
             "produced": result["produced"], "score": result["score_after"]},
            ensure_ascii=False))


if __name__ == "__main__":
    sys.exit(main())
