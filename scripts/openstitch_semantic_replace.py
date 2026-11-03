#!/usr/bin/env python3
"""Replace one semantic object's mask in an OSP, leaving other objects intact."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from openstitch_semantic import validate
from openstitch_semantic_import import call, mask_to_svg


def semantic_objects(inspect, sid):
    # Exact prefix: sem:a must not match sem:abc.
    return [obj for obj in inspect["embroidery_objects"]
            if obj.get("name", "").startswith("sem:" + sid + " ")]


def replace(project, manifest_path, sid, mask_path, output, cli):
    if output.exists() or output.with_suffix(".semantic.json").exists():
        raise FileExistsError("Output exists")
    scene = json.loads(manifest_path.read_text(encoding="utf-8"))
    validation = validate(scene, manifest_path.parent)
    if not validation["ok"]:
        raise ValueError(str(validation["errors"]))
    regions = {r["semantic_id"]: r for r in scene["regions"]}
    if sid not in regions:
        raise ValueError(f"Unknown semantic ID: {sid}")
    region = regions[sid]
    intent = region.get("stitch_intent")
    if intent not in ("tatami", "running", "auto_satin"):
        raise ValueError(f"Unsupported intent: {intent}")
    original = call(cli, "project", "inspect", str(project))
    previous = semantic_objects(original, sid)
    if not previous:
        raise ValueError(f"Semantic ID not in OSP: {sid}")
    first_order = min(obj["order"] for obj in previous)
    # Refuse a stale source map: a mask update must target the same ID and type.
    if intent != "auto_satin" and any(o["type"] != intent for o in previous):
        raise ValueError("OSP object type differs from manifest")
    with tempfile.TemporaryDirectory(prefix="openstitch-replace-") as tmp:
        folder = Path(tmp)
        svg = folder / "replacement.svg"
        mask_to_svg(mask_path, svg, scene["source_image"]["width_px"],
                    scene["source_image"]["height_px"],
                    scene["design"]["width_mm"], scene["design"]["height_mm"])
        operations = [{"op": "remove_object", "object": o["id"],
                       "with_source": True} for o in previous]
        operations.append({"op": "import_svg", "file": str(svg),
                           "stitch_type": "running" if intent == "running" else "tatami",
                           "rgb": region.get("thread_rgb", previous[0]["color"]),
                           "name": "sem:" + sid})
        batch = folder / "edit.json"
        batch.write_text(json.dumps({"schema": "openstitch.batch/1",
                                     "operations": operations}))
        call(cli, "project", "apply", str(project), str(batch), "--dry-run")
        staged = folder / "staged.osp"
        response = call(cli, "project", "apply", str(project), str(batch),
                        "-o", str(staged))
        created = response["outcomes"][-1]["result"]["created"]
        for idx, identifier in enumerate(created):
            if intent == "auto_satin":
                call(cli, "project", "op", str(staged), "auto_satin",
                     "--arg", f"object={identifier}", "--arg", "strategy=skeleton")
            call(cli, "project", "op", str(staged), "set_order",
                 "--arg", f"object={identifier}",
                 "--arg", f"position={first_order + idx}")
        verified = call(cli, "project", "inspect", str(staged))
        if set(o["id"] for o in semantic_objects(verified, sid)) != set(created):
            raise RuntimeError("Replacement IDs were not preserved in names")
        # Import always generates fresh internal IDs; the semantic name is stable.
        os.replace(staged, output)
    old_map = project.with_suffix(".semantic.json")
    mapping = json.loads(old_map.read_text()) if old_map.is_file() else {
        "schema": "openstitch.semantic-map/1", "objects": {}}
    mapping["objects"][sid] = {"ids": created, "family": region["family"],
                               "parent_id": region.get("parent_id"),
                               "mask_path": str(mask_path)}
    output.with_suffix(".semantic.json").write_text(
        json.dumps(mapping, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return {"schema": "openstitch.semantic-replace/1",
            "replaced": sid, "old_ids": [o["id"] for o in previous],
            "new_ids": created, "osp": str(output)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("project", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("--semantic-id", required=True)
    p.add_argument("--mask", required=True, type=Path)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--cli", default=os.environ.get("OPENSTITCH_CLI", "openstitch-cli"))
    a = p.parse_args()
    try:
        print(json.dumps(replace(a.project.resolve(), a.manifest.resolve(),
                                 a.semantic_id, a.mask.resolve(), a.out.resolve(),
                                 a.cli), sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
