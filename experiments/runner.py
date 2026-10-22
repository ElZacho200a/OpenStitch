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
SOURCE = ROOT / "tests/fixtures/tentabrode.png"
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
    rid += "-tentabrode-gh" + os.environ.get("GITHUB_RUN_ID", "local")
    main_sha = run("main_sha", ["git", "-C", str(ROOT), "rev-parse", "HEAD"])
    commit = (WORK / "main_sha.stdout.txt").read_text().strip() if main_sha["exit_code"] == 0 else None
    result = {
        "schema": "openstitch.trial/1", "run_id": rid, "created_utc": timestamp,
        "status": "blocked", "source": {
            "repo_path": "tests/fixtures/tentabrode.png", "sha256": None,
            "size_mm": None, "target_height_mm": 240
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
        if not SOURCE.is_file():
            result["reason"] = "Benchmark source absent du checkout main"
            return 0
        raw = SOURCE.read_bytes()
        result["source"]["sha256"] = hashlib.sha256(raw).hexdigest()
        im = Image.open(SOURCE).convert("RGBA")
        width, height = im.size
        if width <= 0 or height <= 0:
            result["reason"] = "Dimensions source invalides"
            return 0
        target_w = round(240.0 * width / height, 3)
        dpi = height * 25.4 / 240.0
        result["source"]["size_mm"] = [target_w, 240.0]
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
        result["status"] = "incomplete"
        result["reason"] = (
            "Baseline native executee; segmentation technique non validee semantiquement. "
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
