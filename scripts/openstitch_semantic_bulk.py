#!/usr/bin/env python3
"""Apply native OpenStitch batch edits to objects selected by semantic identity."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from openstitch_semantic_import import call


def choose(inspection, mapping, family=None, prefix=None, stitch_type=None):
    if not family and not prefix:
        raise ValueError("Set --family or --semantic-prefix")
    names = mapping.get("objects", {})
    ids = set()
    for sid, item in sorted(names.items()):
        if family and item.get("family") != family:
            continue
        if prefix and not (sid == prefix or sid.startswith(prefix + ".")):
            continue
        ids.add(sid)
    selected = []
    for obj in inspection["embroidery_objects"]:
        for sid in ids:
            if obj.get("name", "").startswith("sem:" + sid + " "):
                if not stitch_type or obj["type"] == stitch_type:
                    if obj.get("locked"):
                        raise ValueError(f"Locked semantic object: {sid}")
                    selected.append(obj)
                break
    return sorted(selected, key=lambda x: x["id"])


def bulk(project, output, cli, family=None, prefix=None, stitch_type=None,
         color=None, params=None, apply=False, limit=20):
    sidecar = project.with_suffix(".semantic.json")
    mapping = json.loads(sidecar.read_text(encoding="utf-8"))
    if mapping.get("schema") != "openstitch.semantic-map/1":
        raise ValueError("Invalid semantic mapping")
    inspection = call(cli, "project", "inspect", str(project))
    selected = choose(inspection, mapping, family, prefix, stitch_type)
    if not selected or len(selected) > limit:
        raise ValueError(f"Selected {len(selected)} objects; expected 1..{limit}")
    if bool(color) == bool(params):
        raise ValueError("Choose exactly one of --rgb / --params")
    operations = []
    for obj in selected:
        if color:
            operations.append({"op": "set_color", "object": obj["id"], "rgb": color})
        else:
            operations.append({"op": "set_params", "object": obj["id"], "params": params})
    batch = {"schema": "openstitch.batch/1", "operations": operations}
    import tempfile
    with tempfile.TemporaryDirectory(prefix="openstitch-semantic-bulk-") as td:
        file = Path(td) / "batch.json"
        file.write_text(json.dumps(batch, sort_keys=True), encoding="utf-8")
        preview = call(cli, "project", "apply", str(project), str(file), "--dry-run")
        if apply:
            if output is None:
                raise ValueError("--out required with --apply")
            if output.exists() or output.with_suffix(".semantic.json").exists():
                raise FileExistsError(output)
            call(cli, "project", "apply", str(project), str(file), "-o", str(output))
            shutil.copyfile(sidecar, output.with_suffix(".semantic.json"))
    return {"schema": "openstitch.semantic-bulk/1", "applied": apply,
            "source": str(project), "output": str(output) if apply else None,
            "semantic_selection": {"family": family, "prefix": prefix},
            "objects": [x["id"] for x in selected],
            "dry_run_ok": bool(preview.get("ok"))}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("project", type=Path)
    p.add_argument("--family")
    p.add_argument("--semantic-prefix")
    p.add_argument("--type", dest="stitch_type")
    p.add_argument("--rgb")
    p.add_argument("--params", help="JSON object of native stitch parameters")
    p.add_argument("--apply", action="store_true")
    p.add_argument("--out", type=Path)
    p.add_argument("--max-objects", type=int, default=20)
    p.add_argument("--cli", default=os.environ.get("OPENSTITCH_CLI", "openstitch-cli"))
    a = p.parse_args()
    try:
        if a.max_objects < 1 or a.max_objects > 1000:
            raise ValueError("--max-objects must be in 1..1000")
        params = json.loads(a.params) if a.params else None
        if params is not None and (not isinstance(params, dict) or not params):
            raise ValueError("--params requires a nonempty JSON object")
        result = bulk(a.project.resolve(), a.out.resolve() if a.out else None,
                      a.cli, a.family, a.semantic_prefix, a.stitch_type,
                      a.rgb, params, a.apply, a.max_objects)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, json.JSONDecodeError,
            subprocess.TimeoutExpired) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
