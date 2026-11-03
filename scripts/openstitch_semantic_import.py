#!/usr/bin/env python3
"""Import openstitch.semantic/1 masks as native editable OSP vector objects."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from openstitch_semantic import validate


def call(cli, *args):
    r = subprocess.run([cli, *args, "--json"], text=True, capture_output=True,
                       encoding="utf-8", timeout=180, check=False)
    if r.returncode:
        raise RuntimeError(f"OpenStitch {args[:2]} failed: {r.stderr[:400]} {r.stdout[:400]}")
    data = json.loads(r.stdout)
    if data.get("ok") is False:
        raise RuntimeError(str(data))
    return data


def mask_to_svg(source, destination, width_px, height_px, width_mm, height_mm):
    try:
        import cv2
    except ImportError as exc:
        raise RuntimeError("pip install opencv-python-headless required") from exc
    bitmap = cv2.imread(str(source), cv2.IMREAD_UNCHANGED)
    if bitmap is None or bitmap.shape[:2] != (height_px, width_px):
        raise ValueError(f"Invalid mask size: {source}")
    if bitmap.ndim == 2:
        binary = bitmap > 127
    elif bitmap.shape[2] == 4:
        binary = bitmap[:, :, 3] > 127
    else:
        binary = cv2.cvtColor(bitmap, cv2.COLOR_BGR2GRAY) > 127
    contours, hierarchy = cv2.findContours(binary.astype("uint8") * 255,
                                           cv2.RETR_CCOMP, cv2.CHAIN_APPROX_SIMPLE)
    if hierarchy is None:
        raise ValueError(f"Empty mask: {source}")
    paths = []
    for i, c in enumerate(contours):
        if hierarchy[0][i][3] != -1:
            continue
        segments = []
        def trace(j):
            points = cv2.approxPolyDP(contours[j], 0.65, True).reshape(-1, 2)
            if len(points) >= 3:
                segments.append("M " + " L ".join(f"{int(x)},{int(y)}" for x, y in points) + " Z")
        trace(i)
        child = hierarchy[0][i][2]
        while child != -1:
            trace(child)
            child = hierarchy[0][child][0]
        if segments:
            paths.append('<path fill-rule="evenodd" d="' + " ".join(segments) + '"/>')
    if not paths:
        raise ValueError(f"No polygon: {source}")
    destination.write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width_mm}mm" '
        f'height="{height_mm}mm" viewBox="0 0 {width_px} {height_px}">' +
        "".join(paths) + "</svg>", encoding="utf-8")
    return len(paths)


def ordering(regions):
    indexed = {r["semantic_id"]: r for r in regions}
    parents = {sid: set() for sid in indexed}
    for r in regions:
        sid = r["semantic_id"]
        for above in r.get("under", []):
            parents[above].add(sid)
        for below in r.get("over", []):
            parents[sid].add(below)
    result, done = [], set()
    while len(done) != len(indexed):
        ready = sorted(sid for sid in indexed if sid not in done and parents[sid] <= done)
        if not ready:
            raise ValueError("Occlusion cycle")
        sid = ready[0]
        done.add(sid)
        result.append(indexed[sid])
    return result


def import_manifest(source, output, cli):
    manifest = json.loads(source.read_text(encoding="utf-8"))
    validation = validate(manifest, source.parent)
    if not validation["ok"]:
        raise ValueError(json.dumps(validation["errors"]))
    sidecar = output.with_suffix(".semantic.json")
    if output.exists() or sidecar.exists():
        raise FileExistsError("Output or sidecar already exists")
    src = manifest["source_image"]
    design = manifest["design"]
    with tempfile.TemporaryDirectory(prefix="openstitch-semantic-") as tmp:
        folder = Path(tmp)
        project = folder / "stage.osp"
        call(cli, "project", "new", str(project), "--width-mm",
             str(design["width_mm"] + 2), "--height-mm", str(design["height_mm"] + 2))
        result = {}
        for i, r in enumerate(ordering(manifest["regions"])):
            intent = r.get("stitch_intent")
            if r.get("decision", "keep") != "keep" or intent in (None, "unstitched"):
                continue
            if intent not in ("tatami", "running", "auto_satin"):
                raise ValueError(f"Unsupported stitch intent without guides: {intent}")
            sid = r["semantic_id"]
            if len("sem:" + sid) > 118:
                raise ValueError(f"Semantic name exceeds OSP limit: {sid}")
            svg = folder / f"mask_{i:04}.svg"
            mask_to_svg((source.parent / r["mask_path"]).resolve(), svg,
                        src["width_px"], src["height_px"],
                        design["width_mm"], design["height_mm"])
            imported = call(cli, "project", "op", str(project), "import_svg",
                            "--arg", f"file={svg}", "--arg",
                            f'stitch_type={"running" if intent == "running" else "tatami"}',
                            "--arg", f'rgb={r.get("thread_rgb", "#5078C8")}',
                            "--arg", f"name=sem:{sid}")
            ids = imported["outcomes"][0]["result"]["created"]
            if not ids:
                raise ValueError(f"No objects generated: {sid}")
            if intent == "auto_satin":
                for ident in ids:
                    call(cli, "project", "op", str(project), "auto_satin",
                         "--arg", f"object={ident}", "--arg", "strategy=skeleton")
            result[sid] = {"ids": ids, "family": r["family"],
                           "parent_id": r.get("parent_id"), "mask_path": r["mask_path"]}
        if not result:
            raise ValueError("No stitchable regions")
        if not call(cli, "project", "inspect", str(project))["embroidery_objects"]:
            raise ValueError("Empty OSP")
        os.replace(project, output)
    sidecar.write_text(json.dumps({"schema": "openstitch.semantic-map/1",
                                   "objects": result}, sort_keys=True, indent=2) + "\n",
                       encoding="utf-8")
    return {"schema": "openstitch.semantic-import/1", "osp": str(output),
            "mapping": str(sidecar), "objects": result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--cli", default=os.environ.get("OPENSTITCH_CLI", "openstitch-cli"))
    args = parser.parse_args()
    try:
        print(json.dumps(import_manifest(args.manifest.resolve(), args.out.resolve(), args.cli),
                         sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
