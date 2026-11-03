#!/usr/bin/env python3
"""Generate an annotated OpenStitch render and per-semantic-object diagnostics."""
from __future__ import annotations
import argparse
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from xml.sax.saxutils import escape
from openstitch_semantic_import import call


def issues_for_object(quality, *, min_coverage=0.95, max_overflow=0.20):
    issues = []
    if quality and quality.get("measured", False):
        if quality.get("covered_ratio", 1) < min_coverage:
            issues.append("coverage")
        if quality.get("overflow_ratio", 0) > max_overflow:
            issues.append("overflow")
    if quality and quality.get("findings_count", 0) > 0:
        issues.append("quality")
    return issues


def geometry_to_rect(bounds, scene, image_size):
    (x0, y0), (x1, y1) = bounds["min_mm"], bounds["max_mm"]
    minx, miny, maxx, maxy = scene
    w, h = image_size
    sx = w / (maxx - minx)
    sy = h / (maxy - miny)
    return (round((x0 - minx) * sx, 2), round((maxy - y1) * sy, 2),
            round((x1 - x0) * sx, 2), round((y1 - y0) * sy, 2))


def png_dimensions(path):
    data = path.read_bytes()[:24]
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Render did not produce a PNG")
    return struct.unpack(">II", data[16:24])


def review(project, output, cli, reference=None, min_coverage=0.95,
           max_overflow=0.20):
    output.mkdir(parents=True, exist_ok=False)
    inspect = call(cli, "project", "inspect", str(project))
    p = subprocess.run([cli, "project", "check", str(project), "--json"],
                       text=True, capture_output=True, timeout=180, check=False)
    if p.returncode not in (0, 2):
        raise RuntimeError(f"OpenStitch check failed: {p.stderr[:300]}")
    check = json.loads(p.stdout)
    render = output / "preview.png"
    r = subprocess.run([cli, "project", "render", str(project), "-o",
                        str(render), "--px-per-mm", "6", "--fabric", "#FFFFFF"],
                       capture_output=True, text=True, timeout=180, check=False)
    if r.returncode:
        raise RuntimeError(f"OpenStitch render failed: {r.stderr[:300]}")
    width, height = png_dimensions(render)
    objects = [o for o in inspect["embroidery_objects"]
               if o.get("name", "").startswith("sem:") and o.get("geometry_bounds")]
    if not objects:
        raise ValueError("No named semantic objects found")
    quality = {o["id"]: o for o in check.get("objects", [])}
    extents = [o["geometry_bounds"] for o in inspect["embroidery_objects"]
               if o.get("geometry_bounds")]
    scene = (min(b["min_mm"][0] for b in extents),
             min(b["min_mm"][1] for b in extents),
             max(b["max_mm"][0] for b in extents),
             max(b["max_mm"][1] for b in extents))
    items = []
    shapes = []
    for obj in sorted(objects, key=lambda o: o["id"]):
        sid = obj["name"].split(" ", 1)[0][4:]
        q = quality.get(obj["id"], {})
        issues = issues_for_object(q, min_coverage=min_coverage,
                                   max_overflow=max_overflow)
        x, y, w, h = geometry_to_rect(obj["geometry_bounds"], scene, (width, height))
        items.append({"semantic_id": sid, "osp_id": obj["id"],
                      "name": obj["name"], "type": obj["type"],
                      "coverage": q.get("covered_ratio") if q.get("measured") else None,
                      "overflow": q.get("overflow_ratio") if q.get("measured") else None,
                      "issues": issues})
        if issues:
            label = escape(sid)
            shapes.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" '
                          'fill="none" stroke="#ff3344" stroke-width="2"/>')
            shapes.append(f'<text x="{x+2}" y="{max(14, y-3)}" font-size="13" '
                          f'fill="#ff3344">{label}</text>')
    data_url = base64.b64encode(render.read_bytes()).decode("ascii")
    annotated = output / "annotated.svg"
    annotated.write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
        f'height="{height}" viewBox="0 0 {width} {height}">'
        f'<image href="data:image/png;base64,{data_url}" width="{width}" '
        f'height="{height}"/>' + "".join(shapes) + "</svg>", encoding="utf-8")
    report = {"schema": "openstitch.semantic-review/1",
              "project": str(project), "summary": check.get("summary"),
              "limits": {"min_coverage": min_coverage, "max_overflow": max_overflow},
              "semantic_objects": items, "annotated": str(annotated)}
    (output / "review.json").write_text(json.dumps(report, sort_keys=True,
        indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    if reference:
        import shutil
        ref_name = "reference" + reference.suffix.lower()
        shutil.copyfile(reference, output / ref_name)
        (output / "compare.html").write_text(
            '<!doctype html><meta charset="utf-8"><title>OpenStitch review</title>'
            '<style>body{font-family:sans-serif}main{display:flex;gap:16px}'
            'img{max-width:48vw;object-fit:contain}</style><main>'
            f'<img src="{escape(ref_name)}" alt="Reference">'
            '<img src="annotated.svg" alt="OpenStitch render annotated"></main>'
            '<p>Comparaison visuelle humaine. Aucun score perceptuel '
            'automatique ne decoule de ces deux images.</p>',
            encoding="utf-8")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("project", type=Path)
    p.add_argument("--out-dir", required=True, type=Path)
    p.add_argument("--reference", type=Path)
    p.add_argument("--min-coverage", type=float, default=0.95)
    p.add_argument("--max-overflow", type=float, default=0.20)
    p.add_argument("--cli", default=os.environ.get("OPENSTITCH_CLI", "openstitch-cli"))
    a = p.parse_args()
    try:
        if not (0 <= a.min_coverage <= 1 and 0 <= a.max_overflow <= 1):
            raise ValueError("Thresholds must be in [0,1]")
        result = review(a.project.resolve(), a.out_dir.resolve(), a.cli,
                        a.reference.resolve() if a.reference else None,
                        a.min_coverage, a.max_overflow)
        print(json.dumps({"schema": result["schema"], "output": str(a.out_dir),
                          "objects": len(result["semantic_objects"])}, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
