#!/usr/bin/env python3
"""OpenStitch Studio UX gate: native capture matrix and PR review contract.

No third-party Python dependencies. The screenshot check is mechanical: it does
not claim to judge whether a layout is intuitive. Independent visual review is
a separate, explicitly required PR step.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

SPACES = ("image", "segmentation", "vector", "points")
SIZES = ("1440x900", "1000x640")
REVIEW_FIELDS = {
    "Parcours novice": r"(?im)^\s*[-*]?\s*Parcours novice\s*:\s*(.+)$",
    "Clics avant/apres": r"(?im)^\s*[-*]?\s*Clics avant/apr[eè]s\s*:\s*(.+)$",
    "Captures inspectees": r"(?im)^\s*[-*]?\s*Captures inspect[eé]es\s*:\s*(.+)$",
    "Revue independante": r"(?im)^\s*[-*]?\s*Revue ind[eé]pendante\s*:\s*(.+)$",
    "Tests": r"(?im)^\s*[-*]?\s*Tests UX\s*:\s*(.+)$",
}
PLACEHOLDERS = ("todo", "tbd", "a faire", "à faire", "pending", "non teste",
                "non testé", "non verifie", "non vérifié", "n/a", "aucun", "...")
PNG_HEADER = b"\x89PNG\r\n\x1a\n"


def ui_paths(paths: list[str]) -> list[str]:
    """Only user-facing implementation, not tests/docs/CI for this gate."""
    prefixes = ("apps/desktop_v2/studio/", "apps/desktop_v2/core/",
                "resources/themes/")
    return sorted(p for p in paths if p.replace("\\", "/").startswith(prefixes))


def read_png(path: Path) -> tuple[int, int, str]:
    data = path.read_bytes()
    if len(data) < 1200 or not data.startswith(PNG_HEADER):
        raise ValueError(f"{path}: image PNG manquante, vide ou tronquee")
    pos = len(PNG_HEADER)
    width = height = 0
    saw_idat = saw_end = False
    while pos + 12 <= len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        end = pos + 12 + length
        if length > 100_000_000 or end > len(data):
            raise ValueError(f"{path}: longueur de bloc PNG invalide")
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        crc = struct.unpack_from(">I", data, pos + 8 + length)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError(f"{path}: CRC PNG invalide")
        if pos == len(PNG_HEADER):
            if kind != b"IHDR" or length != 13:
                raise ValueError(f"{path}: IHDR invalide")
            width, height = struct.unpack_from(">II", payload)
            if not (100 <= width <= 16384 and 100 <= height <= 16384):
                raise ValueError(f"{path}: dimensions PNG invalides")
        if kind == b"IDAT":
            saw_idat = True
        if kind == b"IEND":
            saw_end = length == 0 and end == len(data)
            break
        pos = end
    if not saw_end or not saw_idat:
        raise ValueError(f"{path}: IDAT/IEND absent ou PNG incomplet")
    return width, height, hashlib.sha256(data).hexdigest()


def git_head() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        return "unknown"


def workspace_fingerprint() -> str:
    """Bind evidence to the exact HEAD and the local working tree."""
    h = hashlib.sha256(git_head().encode("ascii", errors="replace"))
    try:
        for cmd in (["git", "diff", "--binary", "HEAD"],
                    ["git", "diff", "--cached", "--binary"],
                    ["git", "status", "--porcelain", "--untracked-files=normal"]):
            h.update(subprocess.check_output(cmd, stderr=subprocess.DEVNULL))
    except (OSError, subprocess.CalledProcessError):
        h.update(b"git-state-unavailable")
    return h.hexdigest()


def check_report(path: Path, require_review: bool = False) -> dict:
    report = json.loads(path.read_text(encoding="utf-8"))
    if report.get("schema") != 1 or report.get("head_sha") != git_head():
        raise ValueError("Capture perimee: recommencer apres le dernier commit")
    if report.get("workspace_fingerprint") != workspace_fingerprint():
        raise ValueError("Capture perimee: le code ou l'arbre de travail a change")
    rows = report.get("screenshots", [])
    if not rows:
        raise ValueError("Aucune capture native verifiable")
    for row in rows:
        shot = path.parent / Path(row["filename"]).name
        width, height, digest = read_png(shot)
        if (width, height, digest) != (row["width"], row["height"], row["sha256"]):
            raise ValueError(f"Capture differente du manifeste: {shot}")
    if require_review:
        review = report.get("visual_review")
        if not isinstance(review, dict) or review.get("verdict") != "PASS":
            raise ValueError("Relecture UX independante PASS non fournie")
        if len(review.get("reviewer", "").strip()) < 3:
            raise ValueError("Relecteur UX independant non identifie")
        if len(review.get("notes", "").strip()) < 20:
            raise ValueError("Notes de revue UX insuffisantes")
    return report


def record_review(args: argparse.Namespace) -> int:
    path = Path(args.report).resolve()
    report = check_report(path)
    report["visual_review"] = {
        "reviewer": args.reviewer.strip(),
        "verdict": args.verdict,
        "notes": args.notes.strip(),
        "reference": args.reference.strip(),
    }
    if not report["visual_review"]["reviewer"] or not report["visual_review"]["notes"]:
        raise ValueError("La revue necessite un auteur et des observations reelles")
    path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("Revue UX enregistree dans le manifeste local (pas une certification independante)")
    return 0


def capture(args: argparse.Namespace) -> int:
    studio = Path(args.studio).resolve()
    project = Path(args.project).resolve()
    if not studio.is_file() or not project.is_file():
        raise ValueError("Le Studio compile et un projet OSP reel sont requis")
    output = Path(args.out).resolve()
    output.mkdir(parents=True, exist_ok=True)
    panels = {}
    for entry in args.panel:
        if "=" not in entry:
            raise ValueError(f"Panel cible invalide: {entry} (attendu espace=panneau)")
        space_key, panel_names = entry.split("=", 1)
        if space_key.strip() not in SPACES or not panel_names.strip():
            raise ValueError(f"Panel cible invalide: {entry}")
        panels[space_key.strip()] = panel_names.strip()
    rows = []
    for size in args.sizes.split(","):
        match = re.fullmatch(r"(\d+)x(\d+)", size.strip())
        if not match:
            raise ValueError(f"Taille invalide: {size}")
        requested_w, requested_h = map(int, match.groups())
        for space in args.spaces.split(","):
            space = space.strip()
            if space not in SPACES:
                raise ValueError(f"Espace inconnu: {space}")
            shot = output / f"ux-{space}-{requested_w}x{requested_h}.png"
            shot.unlink(missing_ok=True)  # jamais accepter une ancienne capture
            command = [str(studio), str(project), "--screenshot", str(shot),
                       "--open-space", space, "--window-size", size]
            if space in panels:
                # main.cpp processes --open-panels before --open-space.
                # Explicitly select the workspace first for the target panel.
                command += ["--open-panels", f"espace-{space},{panels[space]}"]
            if args.backend == "software":
                command += ["--software", "--flat"]
            env = dict(os.environ)
            if args.backend == "software":
                env["QT_QUICK_BACKEND"] = "software"
            try:
                result = subprocess.run(command, capture_output=True, text=True,
                                        timeout=args.timeout, env=env, errors="replace")
            except subprocess.TimeoutExpired as exc:
                raise ValueError(f"Studio fige ({space} {size}, {args.timeout}s)") from exc
            if result.returncode:
                raise ValueError(f"Studio crash/erreur {result.returncode} ({space}, {size}): "
                                 + (result.stderr or result.stdout)[-1600:])
            if not shot.is_file():
                raise ValueError(f"Pas de capture creee: {shot}")
            width, height, digest = read_png(shot)
            if width < requested_w * 0.85 or height < requested_h * 0.85:
                raise ValueError(f"Fenetre/capture tronquee: {shot} ({width}x{height})")
            rows.append({"space": space, "requested_size": size, "width": width,
                         "height": height, "sha256": digest,
                         "filename": shot.name, "bytes": shot.stat().st_size})
            print(f"OK {space} {size}: {width}x{height} ({shot.stat().st_size} octets)")
    for size in {r["requested_size"] for r in rows}:
        group = [r for r in rows if r["requested_size"] == size]
        if len(group) > 1 and len({r["sha256"] for r in group}) == 1:
            raise ValueError(f"Meme capture pour tous les espaces {size}: navigation probablement cassee")
    report = {"schema": 1, "head_sha": git_head(),
              "workspace_fingerprint": workspace_fingerprint(), "backend": args.backend,
              "source": "real Qt Studio window grab (software may be offscreen)",
              "project": project.name, "screenshots": rows,
              "visual_review": "NOT_REVIEWED", "note": "Dimensions/PNG verifies, UX non jugee"}
    (output / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                                        encoding="utf-8")
    print(f"Captures et manifeste: {output / 'report.json'}")
    return 0


def validate_pr(body: str, paths: list[str]) -> list[str]:
    affected = ui_paths(paths)
    if not affected:
        return []
    errors = []
    if not re.search(r"(?im)^##\s+UX Gate\s*$", body):
        errors.append("Ajouter la section '## UX Gate' a la description de la PR")
    for name, pattern in REVIEW_FIELDS.items():
        match = re.search(pattern, body)
        if not match or not match.group(1).strip():
            errors.append(f"Renseigner '{name}'")
            continue
        value = match.group(1).strip()
        if any(x in value.casefold() for x in PLACEHOLDERS):
            errors.append(f"'{name}' est encore un placeholder")
        if name == "Clics avant/apres" and not re.search(r"\d+\s*(?:->|→|/)\s*\d+", value):
            errors.append("Indiquer des clics avant -> apres numeriques, mesures")
        if name == "Captures inspectees" and not (
            "https://" in value or "openstitch-studio-screenshots-windows" in value
        ):
            errors.append("Donner une URL de captures ou l'artefact CI Studio")
        if name == "Revue independante" and not re.search(
            r"\b(pass|friction|bloquant|fail|blocked)\b", value, re.I
        ):
            errors.append("Indiquer relecteur distinct et verdict PASS/FRICTION/BLOQUANT")
    if not re.search(r"(?im)^[-*]?\s*Revue ind[eé]pendante\s*:\s*.+(?:agent|relecteur|reviewer|@)", body):
        errors.append("Identifier explicitement l'agent/relecteur UX independant")
    return errors


def check_pr(args: argparse.Namespace) -> int:
    files = Path(args.changed_files).read_text(encoding="utf-8").splitlines()
    body = os.environ.get(args.body_env, "") if args.body_env else Path(args.body_file).read_text(encoding="utf-8")
    affected = ui_paths(files)
    if not affected:
        print("UX Gate: pas de modification de production Studio / themes ; sans objet.")
        return 0
    errors = validate_pr(body, files)
    print("UX Gate: fichiers d'interface impactes:\n- " + "\n- ".join(affected[:40]))
    if errors:
        print("UX Gate NON VALIDE:\n- " + "\n- ".join(errors), file=sys.stderr)
        return 1
    print("UX Gate: contrat PR renseigne. L'exactitude de la revue est a verifier.")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    shots = sub.add_parser("capture", help="produire et verifier les captures Studio reelles")
    shots.add_argument("--studio", required=True)
    shots.add_argument("--project", required=True)
    shots.add_argument("--out", required=True)
    shots.add_argument("--backend", choices=("native", "software"), default="native")
    shots.add_argument("--sizes", default=",".join(SIZES))
    shots.add_argument("--spaces", default=",".join(SPACES))
    shots.add_argument("--panel", action="append", default=[],
                       help="panneau ouvert dans un espace (ex. points=vue, image=inspecteur)")
    shots.add_argument("--timeout", type=int, default=90)
    review = sub.add_parser("check-pr", help="controler le contrat UX de la PR")
    review.add_argument("--changed-files", required=True)
    source = review.add_mutually_exclusive_group(required=True)
    source.add_argument("--body-file")
    source.add_argument("--body-env")
    reviewed = sub.add_parser("review", help="consigner les observations du second agent")
    reviewed.add_argument("--report", required=True)
    reviewed.add_argument("--reviewer", required=True)
    reviewed.add_argument("--verdict", choices=("PASS", "FRICTION", "BLOQUANT"), required=True)
    reviewed.add_argument("--notes", required=True)
    reviewed.add_argument("--reference", default="")
    checked = sub.add_parser("check-report", help="reverifier les captures et leur revue")
    checked.add_argument("--report", required=True)
    checked.add_argument("--require-review", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.action == "capture":
            return capture(args)
        if args.action == "check-pr":
            return check_pr(args)
        if args.action == "review":
            return record_review(args)
        check_report(Path(args.report), require_review=args.require_review)
        print("UX Gate: preuves locales a jour")
        return 0
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        print(f"UX Gate: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
