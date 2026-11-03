#!/usr/bin/env python3
"""Bridge OpenStitch's JSON CLI for automation agents (Python standard library only).

This script does not replace OpenStitch's native commands or alter projects.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from collections import Counter
from pathlib import Path

SCHEMA = "openstitch.agent/1"


def find_cli(explicit: str | None) -> str | None:
    """Select an executable deterministically; never download or run a shell."""
    root = Path(__file__).resolve().parent.parent
    choices = [explicit, os.environ.get("OPENSTITCH_CLI"), shutil.which("openstitch-cli"),
               str(Path.home() / ".local/bin/openstitch-cli"),
               str(root / "build/linux-apt/apps/cli/openstitch-cli")]
    for value in choices:
        if not value:
            continue
        p = Path(value).expanduser()
        if p.is_file() and os.access(p, os.X_OK):
            return str(p.resolve())
    return None


def invoke(executable: str, *args: str, timeout: int = 45) -> str:
    try:
        result = subprocess.run([executable, *args], check=False, capture_output=True,
                                text=True, timeout=timeout, encoding="utf-8", errors="replace")
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RuntimeError(f"Impossible d'executer {' '.join(args)} : {exc}") from exc
    if result.returncode:
        raise RuntimeError(f"Commande {' '.join(args)} : code {result.returncode}; "
                           f"{result.stderr.strip()[:500]}")
    return result.stdout.strip()


def read_json(executable: str, *args: str, timeout: int = 45) -> dict:
    raw = invoke(executable, *args, timeout=timeout)
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"JSON non valide pour {' '.join(args)} : {exc}") from exc
    if not isinstance(data, dict):
        raise RuntimeError("Reponse JSON attendue : objet")
    return data


def doctor(executable: str | None) -> tuple[dict, int]:
    result: dict = {"schema": SCHEMA, "action": "doctor", "ready": False,
                    "cli": executable, "version": None, "checks": [], "capabilities": {}}
    if not executable:
        result["checks"].append({"name": "executable", "ok": False,
                                 "message": "Client absent; voir scripts/install-cli-linux.sh"})
        return result, 2
    probes = [
        ("version", ("--version",), None),
        ("commands", ("commands", "--json"), "openstitch.commands/1"),
        ("operations", ("project", "operations", "--json"), "openstitch.operations/1"),
    ]
    for name, args, expected in probes:
        try:
            value = invoke(executable, *args) if expected is None else read_json(executable, *args)
            if expected and value.get("schema") != expected:
                raise RuntimeError(f"Schema inattendu : {value.get('schema')!r}")
            if name == "version":
                result["version"] = value
            elif name == "commands":
                items = value.get("subcommands")
                if not isinstance(items, list):
                    raise RuntimeError("Catalogue des commandes absent")
                result["capabilities"]["commands"] = sorted(str(x.get("name", "")) for x in items)
            elif name == "operations":
                items = value.get("operations")
                if not isinstance(items, list):
                    raise RuntimeError("Catalogue des operations absent")
                result["capabilities"]["operation_count"] = len(items)
                result["capabilities"]["operation_names"] = sorted(str(x.get("name", "")) for x in items)
            result["checks"].append({"name": name, "ok": True})
        except RuntimeError as exc:
            result["checks"].append({"name": name, "ok": False, "message": str(exc)})
    result["ready"] = all(x["ok"] for x in result["checks"])
    return result, 0 if result["ready"] else 2


def compact_inspect(executable: str, project: str, *, kind: str | None,
                    color: str | None, min_area: float, sort_key: str,
                    offset: int, limit: int) -> dict:
    data = read_json(executable, "project", "inspect", project, "--json", timeout=180)
    if data.get("schema") != "openstitch.project-inspect/1":
        raise RuntimeError("Schema project inspect inattendu")
    objects = data.get("embroidery_objects", [])
    if not isinstance(objects, list):
        raise RuntimeError("embroidery_objects doit etre une liste")
    counts = Counter(o.get("type", "unknown") for o in objects)
    def stitches(o: dict) -> dict:
        return o.get("stitches") or {}
    totals = {"objects": len(objects), "by_type": dict(sorted(counts.items())),
              "stitches": sum(stitches(o).get("total", 0) for o in objects),
              "jumps": sum(stitches(o).get("jumps", 0) for o in objects)}
    filtered = [o for o in objects if (kind is None or o.get("type") == kind)
                and (color is None or str(o.get("color", "")).lower() == color.lower())
                and float(o.get("region_area_mm2") or 0) >= min_area]
    def key(o: dict) -> tuple:
        if sort_key == "jumps":
            metric = stitches(o).get("jumps", 0)
        elif sort_key == "stitches":
            metric = stitches(o).get("total", 0)
        elif sort_key == "area":
            metric = o.get("region_area_mm2") or 0
        else:
            metric = o.get("order", 0)
        return (-metric, o.get("id", 0)) if sort_key != "order" else (metric, o.get("id", 0))
    filtered.sort(key=key)
    selected = filtered[offset:offset + limit]
    def summarize(o: dict) -> dict:
        return {"id": o.get("id"), "name": o.get("name"), "type": o.get("type"),
                "color": o.get("color"), "order": o.get("order"),
                "area_mm2": o.get("region_area_mm2"), "bounds": o.get("geometry_bounds"),
                "stitches": stitches(o).get("total", 0), "jumps": stitches(o).get("jumps", 0)}
    return {"schema": SCHEMA, "action": "inspect", "project": project,
            "canvas": data.get("canvas"), "totals": totals,
            "filters": {"type": kind, "color": color, "min_area_mm2": min_area,
                        "sort": sort_key, "offset": offset, "limit": limit},
            "matching": len(filtered), "objects": [summarize(o) for o in selected],
            "warnings": data.get("warnings", []),
            "notice": "Tri d'inspection, pas un diagnostic de qualite textile. Utiliser project diagnose/check."}


def contract() -> dict:
    """Stable self-description, available even before the native CLI is installed."""
    return {
        "schema": SCHEMA,
        "action": "contract",
        "exit_codes": {"0": "success", "2": "validation_or_execution_error"},
        "stdout": "exactly_one_json_object",
        "stderr": "reserved_for_argparse",
        "commands": [
            {"name": "contract", "requires_cli": False, "arguments": []},
            {"name": "doctor", "requires_cli": False, "arguments": [],
             "description": "Probe native CLI version, command and operation schemas"},
            {"name": "inspect", "requires_cli": True,
             "arguments": [
                 {"name": "project", "type": "path", "required": True},
                 {"name": "--type", "type": "string", "required": False},
                 {"name": "--color", "type": "string", "required": False},
                 {"name": "--min-area-mm2", "type": "number", "minimum": 0, "default": 0},
                 {"name": "--sort", "type": "enum",
                  "values": ["order", "area", "stitches", "jumps"], "default": "jumps"},
                 {"name": "--offset", "type": "integer", "minimum": 0, "default": 0},
                 {"name": "--limit", "type": "integer", "minimum": 1, "maximum": 1000, "default": 30}
             ],
             "description": "Read-only filtered inspection; object IDs originate in the OSP"}
        ],
        "global_arguments": [{"name": "--cli", "type": "path", "required": False}],
        "errors": {"ok": False, "error": "human-readable diagnostic"},
        "examples": ["contract", "doctor", "inspect motif.osp --sort jumps --limit 20"],
        "notes": ["No OSP mutation", "No installation/download", "Native CLI schemas are probed by doctor"]
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="Commandes compactes pour agents OpenStitch (JSON stdout)")
    p.add_argument("--cli", help="Chemin du binaire OpenStitch (sinon PATH / OPENSTITCH_CLI)")
    sub = p.add_subparsers(dest="action", required=True)
    sub.add_parser("contract", help="Decrire le contrat JSON stable sans client installe")
    sub.add_parser("doctor", help="Verifier executable et contrats de commandes")
    inspect = sub.add_parser("inspect", help="Resumer et filtrer un projet .osp")
    inspect.add_argument("project")
    inspect.add_argument("--type")
    inspect.add_argument("--color")
    inspect.add_argument("--min-area-mm2", type=float, default=0)
    inspect.add_argument("--sort", choices=["order", "area", "stitches", "jumps"], default="jumps")
    inspect.add_argument("--offset", type=int, default=0)
    inspect.add_argument("--limit", type=int, default=30)
    args = p.parse_args(argv)
    if args.action == "inspect" and (args.limit < 1 or args.limit > 1000 or args.offset < 0 or args.min_area_mm2 < 0):
        p.error("limit 1..1000, offset >= 0 et min-area-mm2 >= 0 requis")
    cli = find_cli(args.cli)
    try:
        if args.action == "contract":
            output, code = contract(), 0
        elif args.action == "doctor":
            output, code = doctor(cli)
        elif not cli:
            raise RuntimeError("openstitch-cli introuvable : fournir --cli ou OPENSTITCH_CLI")
        else:
            output = compact_inspect(cli, args.project, kind=args.type, color=args.color,
                                     min_area=args.min_area_mm2, sort_key=args.sort,
                                     offset=args.offset, limit=args.limit)
            code = 0
    except RuntimeError as exc:
        output, code = {"schema": SCHEMA, "action": args.action, "ok": False, "error": str(exc)}, 2
    print(json.dumps(output, ensure_ascii=False, separators=(",", ":"), sort_keys=True))
    return code


if __name__ == "__main__":
    sys.exit(main())
