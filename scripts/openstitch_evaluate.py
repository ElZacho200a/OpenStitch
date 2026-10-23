#!/usr/bin/env python3
"""Scoring guardrail for OpenStitch digitization experiments (stdlib only).

This is NOT a visual judge: an independent reviewer must inspect the real
source, semantic map, stitched render and native CLI diagnostics first.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

SCHEMA = "openstitch.evaluation/1"
WEIGHTS = {
    "fidelity": 5,
    "stitch_choices": 4,
    "fabric_feasibility": 5,
    "technical_cleanliness": 3,
    "aesthetics": 2,
    "file_integrity": 1,
}
ARTIFACTS = (
    "source", "semantic_map", "osp", "dst", "preview",
    "check_report", "diagnose_report", "fidelity_report", "density_report",
)
CHECKS = ("project_check", "project_diagnose", "dst_export", "preview_render")
GOOD = {"pass", "warning"}
ALL_STATUSES = GOOD | {"fail", "not_run"}


def evaluate(data: dict, base: Path) -> dict:
    """Return a machine-readable, reproducible assessment; never invent grades."""
    errors: list[str] = []
    blockers: list[str] = []
    score = None
    raw_score = None
    points: dict[str, float] = {}

    if data.get("schema") != SCHEMA:
        errors.append(f"schema attendu : {SCHEMA}")

    artifacts = data.get("artifacts")
    if not isinstance(artifacts, dict):
        artifacts = {}
    for key in ARTIFACTS:
        value = artifacts.get(key)
        if not isinstance(value, str) or not value.strip():
            errors.append(f"artefact manquant : {key}")
            continue
        file = (base / value).resolve()
        if not file.is_file() or file.stat().st_size == 0:
            errors.append(f"artefact absent ou vide : {key} ({value})")
        if key.endswith("_report") and file.is_file():
            try:
                report = json.loads(file.read_text(encoding="utf-8"))
                if not isinstance(report, dict):
                    raise ValueError("JSON racine non objet")
            except (OSError, ValueError, UnicodeError) as exc:
                errors.append(f"rapport invalide : {key} ({exc})")

    checks = data.get("checks")
    if not isinstance(checks, dict):
        checks = {}
    for key in CHECKS:
        item = checks.get(key)
        if not isinstance(item, dict):
            errors.append(f"contrôle absent : {key}")
            continue
        status = item.get("status")
        if status not in ALL_STATUSES:
            errors.append(f"statut invalide : {key}")
            continue
        if status == "not_run":
            errors.append(f"contrôle non exécuté : {key}")
        if status == "fail":
            blockers.append(f"contrôle en échec : {key}")
        if not isinstance(item.get("evidence"), str) or len(item["evidence"].strip()) < 10:
            errors.append(f"preuve du contrôle insuffisante : {key}")

    details = data.get("protected_details")
    if not isinstance(details, list) or not details:
        errors.append("liste des détails protégés absente")
    else:
        for i, item in enumerate(details):
            if not isinstance(item, dict) or not isinstance(item.get("name"), str):
                errors.append(f"détail protégé invalide : #{i}")
                continue
            if not isinstance(item.get("preserved"), bool):
                errors.append(f"préservation non évaluée : {item['name']}")
            elif not item["preserved"] and item.get("critical", True):
                blockers.append(f"détail identitaire critique perdu : {item['name']}")

    grading = data.get("rubric")
    if not isinstance(grading, dict):
        grading = {}
    for key, maximum in WEIGHTS.items():
        row = grading.get(key)
        if not isinstance(row, dict):
            errors.append(f"critère non noté : {key}")
            continue
        value = row.get("score")
        if (isinstance(value, bool) or not isinstance(value, (float, int))
                or not math.isfinite(value) or not 0 <= value <= maximum
                or value * 2 != int(value * 2)):
            errors.append(f"note invalide : {key} (0..{maximum}, pas 0.5)")
            continue
        if not isinstance(row.get("evidence"), str) or len(row["evidence"].strip()) < 20:
            errors.append(f"justification insuffisante : {key}")
        points[key] = float(value)

    source_id = data.get("source_id")
    size_mm = data.get("size_mm")
    if not isinstance(source_id, str) or not source_id.strip():
        errors.append("source_id obligatoire pour comparer les versions")
    if (not isinstance(size_mm, list) or len(size_mm) != 2
            or any(isinstance(v, bool) or not isinstance(v, (int, float))
                   or not math.isfinite(v) or v <= 0 for v in size_mm)):
        errors.append("size_mm doit contenir [largeur, hauteur] positives")
    if data.get("reviewer") == data.get("digitizer") or not data.get("reviewer"):
        errors.append("relecteur indépendant identifié requis")
    if not isinstance(data.get("machine_tested"), bool):
        errors.append("machine_tested (booléen) obligatoire")

    if not errors:
        raw_score = round(sum(points.values()), 1)
        score = min(raw_score, 9.0) if blockers else raw_score
    status = ("incomplete" if errors else "blocked" if blockers else
              "accepted" if score >= 17 else "improve")
    return {
        "schema": "openstitch.evaluation-result/1",
        "source_id": source_id,
        "size_mm": size_mm,
        "status": status,
        "score": score,
        "raw_score": raw_score,
        "out_of": 20,
        "category_scores": points,
        "threshold": 17,
        "errors": errors,
        "blockers": blockers,
        "machine_tested": data.get("machine_tested"),
        "validation_level": "machine_tested" if data.get("machine_tested") else "software_only",
        "next_action": ("complete_evidence" if errors else "fix_blockers" if blockers else
                        "deliver" if score >= 17 else "iterate"),
        "note": "Une note logicielle n'est pas une validation textile sur machine.",
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Evalue un dossier OpenStitch documente sur /20")
    p.add_argument("review_json", type=Path)
    p.add_argument("-o", "--output", type=Path, help="Resultat JSON; stdout si absent")
    args = p.parse_args(argv)
    try:
        data = json.loads(args.review_json.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError("la racine doit être un objet JSON")
        result = evaluate(data, args.review_json.resolve().parent)
    except (OSError, ValueError, UnicodeError) as exc:
        result = {"schema": "openstitch.evaluation-result/1", "status": "incomplete",
                  "score": None, "errors": [str(exc)]}
    output = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8")
    else:
        print(output, end="")
    return 0 if result["status"] in ("accepted", "improve") else 2


if __name__ == "__main__":
    sys.exit(main())
