#!/usr/bin/env python3
"""PR #336 black-box QA: exercise *built* CLI artifact, keep actual OSP/DST/previews."""
from __future__ import annotations
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import traceback
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "qa-gradient-results"
OUT.mkdir(parents=True, exist_ok=True)
CLI = Path(os.environ.get("OPENSTITCH_CLI", "openstitch-cli.exe")).resolve()
LOG = []
COLORS = [[24, 42, 99], [40, 83, 141], [76, 122, 179], [125, 171, 205],
          [183, 216, 229], [236, 231, 208], [241, 182, 134], [224, 105, 79]]

def call(label, args, timeout=85):
    begin = time.perf_counter()
    try:
        p = subprocess.run([str(CLI), *map(str, args)],
                           capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=timeout)
        result = {"command": label, "args": list(map(str, args)),
                  "rc": p.returncode,
                  "ms": round((time.perf_counter() - begin) * 1000, 1),
                  "stdout": p.stdout[-3500:], "stderr": p.stderr[-3500:]}
    except Exception as e:
        result = {"command": label, "rc": -999,
                  "ms": round((time.perf_counter() - begin) * 1000, 1),
                  "exception": str(e)}
    LOG.append(result)
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return result

def patch_osp(base: Path, target: Path, mutate):
    with zipfile.ZipFile(base, "r") as archive:
        entries = [(v.filename, archive.read(v.filename))
                   for v in archive.infolist() if not v.is_dir()]
    contents = dict(entries)
    root = json.loads(contents["project.json"].decode("utf-8-sig"))
    mutate(root["document"])
    contents["project.json"] = json.dumps(root, ensure_ascii=False, separators=(",", ":")).encode()
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, value in contents.items():
            archive.writestr(name, value)

def bbox(document):
    nodes = document["vectorObjects"][0]["paths"][0]["outer"]["nodes"]
    xy = [node["pos"] for node in nodes]
    return (min(p[0] for p in xy), min(p[1] for p in xy),
            max(p[0] for p in xy), max(p[1] for p in xy))

def tag(group, index, count, comp=True):
    return {"group": group, "index": index, "count": count, "complementary": comp}

def gradient(box, texture=0, geometry=0, curve=1, layer=None, n=2, comp=True):
    x0, y0, x1, y1 = box
    cy = (y0 + y1) // 2
    if geometry == 1:
        cx = (x0 + x1) // 2
        cy = (y0 + y1) // 2
        radius = int(((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5 / 2)
        fx, fy, tx, ty = cx, cy, cx + radius, cy
    else:
        fx, fy, tx, ty = x0, cy, x1, cy
    g = {"fromX": fx, "fromY": fy, "toX": tx, "toY": ty,
         "spacingFrom": 420, "spacingTo": 420,
         "interpolateDensity": True, "curve": curve}
    if texture:
        g.update(texture=texture, textureStrength=0.35, textureSeed=101)
    if geometry:
        g["geometry"] = geometry
    if geometry == 2:
        g["bend"] = 0.65
    if layer is not None:
        g["densityStops"] = [
            [j / (n - 1), (1.0 if j == layer else 0.0) if comp else
             (1.0 if layer == 0 else (0.30 if j == layer else 0.0))]
            for j in range(n)]
    return g

def make_variant(doc, name, n, texture=0, geometry=0, comp=True, satin=False, legacy=False):
    base = copy.deepcopy(doc["embroideryObjects"][0])
    shape_box = bbox(doc)
    doc["embroideryObjects"] = []
    base_id = int(base["id"])
    for i in range(n):
        ob = copy.deepcopy(base)
        ob["id"] = base_id + i
        ob["rgb"] = COLORS[i] if not legacy else [COLORS[0], COLORS[4]][i]
        ob["name"] = f"QA {name} : fil {i+1}/{n}"
        ob.pop("colorBlend", None)
        ob.pop("multiBlend", None)
        ob.pop("thread", None)
        if satin:
            x0, y0, x1, y1 = shape_box
            cx = (x0 + x1) // 2
            cy = (y0 + y1) // 2
            left = cx - 15_000
            right = cx + 15_000
            def rail(y):
                return {"closed": False, "nodes": [
                    {"pos": [left, y], "smooth": False},
                    {"pos": [right, y], "smooth": False}]}
            ob["params"] = {
                "type": "satin", "railA": rail(cy-1_600),
                "railB": rail(cy+1_600), "rungs": [],
                "density": 450, "pullCompensation": 0,
                "centerUnderlay": i == 0, "maxWidth": 10_000,
                "maxWidthHard": 48_000, "shortStitch": 0,
                "splitStitch": 1, "capStart": 0, "capEnd": 0,
                "maxStitchLength": 7_000, "underlayEdge": False,
                "underlayZigzag": False, "pullLeft": 0, "pullRight": 0,
                "pushStart": 0, "pushEnd": 0, "lockStart": 0, "lockEnd": 0,
                "lockLength": 800, "lockPasses": 2}
        else:
            # The CLI built directional defaults and actual guide geometry.
            p = ob["params"]
            if p["type"] != "directional":
                raise RuntimeError(f"Expected directional, got {p['type']}")
            p["underlayEdge"] = False
            p["underlayParallel"] = False
            p["underlayZigzag"] = False
            p["densityGradient"] = gradient(shape_box, texture, geometry,
                                            layer=None if legacy else i,
                                            n=n, comp=comp)
            if legacy:
                p["densityGradient"].pop("densityStops", None)
                p["densityGradient"]["spacingFrom"] = 420 if i == 0 else 4000
                p["densityGradient"]["spacingTo"] = 4000 if i == 0 else 420
                p["densityGradient"]["interpolateDensity"] = True
        if legacy:
            ob["colorBlend"] = {"partner": base_id + (1-i), "foreground": bool(i)}
        else:
            ob["multiBlend"] = tag(base_id, i, n, comp)
        doc["embroideryObjects"].append(ob)
    doc["objectIdLast"] = max(doc["objectIdLast"], base_id+n-1)

def build():
    if not CLI.is_file():
        raise RuntimeError(f"CI executable missing: {CLI}")
    base = OUT / "base.osp"
    batch = OUT / "base.json"
    batch.write_text(json.dumps({"schema": "openstitch.batch/1",
                                "operations": [
                                    {"op": "add_shape", "shape": "rect",
                                     "width_mm": 40, "height_mm": 20,
                                     "as": "test"},
                                    {"op": "set_stitch_type", "object": "$test",
                                     "type": "directional"}]}), encoding="utf-8")
    for label, args in [
        ("cli_version", ["--version"]),
        ("new_base", ["project", "new", base, "--width-mm", 100, "--height-mm", 70]),
        ("add_shape_and_directional", ["project", "apply", base, batch])
    ]:
        if call(label, args, timeout=60)["rc"] != 0:
            raise RuntimeError(f"Cannot initialize baseline: {label}")
    manifest = []
    cases = [
        ("legacy_2_linear",2,0,0,True,False,True),
        ("legacy_2_feathered",2,1,0,True,False,True),
        ("legacy_2_stippled",2,2,0,True,False,True),
        ("multi_2_linear",2,0,0,True,False,False),
        ("multi_3_linear",3,0,0,True,False,False),
        ("multi_5_linear",5,0,0,True,False,False),
        ("multi_5_feathered",5,1,0,True,False,False),
        ("multi_5_stippled",5,2,0,True,False,False),
        ("multi_5_longshort",5,3,0,True,False,False),
        ("multi_5_radial",5,0,1,True,False,False),
        ("multi_5_curved",5,0,2,True,False,False),
        ("multi_5_overlay",5,0,0,False,False,False),
        ("multi_8_linear",8,0,0,True,False,False),
        ("satin_3",3,0,0,True,True,False),
        ("satin_5",5,0,0,True,True,False)
    ]
    for name,n,texture,geo,complementary,satin,legacy in cases:
        entry = {"name": name, "n": n, "texture": texture,
                 "geometry": geo, "complementary": complementary,
                 "satin": satin, "legacy": legacy}
        target = OUT / f"{name}.osp"
        try:
            patch_osp(base, target,
                      lambda d: make_variant(d,name,n,texture,geo,complementary,satin,legacy))
            entry["osp_bytes"] = target.stat().st_size
            operations = [
                ("inspect", ["project", "inspect", target, "--json"]),
                ("render", ["project", "render", target, "-o", OUT/f"{name}.png",
                            "--px-per-mm", "10", "--fabric", "#ece7df"]),
                ("export", ["project", "export", target, "-o", OUT/f"{name}.dst"])]
            for opname, args in operations:
                result = call(f"{name}.{opname}", args)
                entry[opname+"_rc"] = result["rc"]
                entry[opname+"_ms"] = result["ms"]
                if opname == "inspect" and result["rc"] == 0:
                    try:
                        metrics = json.loads(result["stdout"])
                        entry["counts"] = metrics.get("counts",{})
                        entry["layers"] = [
                            {"id": o.get("id"), "stitches": o.get("stitches",{}).get("total",0)}
                            for o in metrics.get("embroidery_objects",[])]
                    except Exception as e:
                        entry["inspect_parse"] = str(e)
            p = OUT / f"{name}.png"
            entry["png_bytes"] = p.stat().st_size if p.exists() else 0
            if p.exists():
                entry["png_sha256"] = hashlib.sha256(p.read_bytes()).hexdigest()
        except Exception as exc:
            entry["exception"] = str(exc)
            traceback.print_exc()
        manifest.append(entry)
        (OUT/"manifest.json").write_text(json.dumps(manifest,indent=2,ensure_ascii=False),
                                         encoding="utf-8")
    results = {"pr":336,"source":"GitHub Actions Windows artifact 13988349715",
               "sample":"40 x 20 mm rectangle + 30 x 3.2 mm satin rails",
               "cases":manifest,"commands":LOG}
    (OUT/"report.json").write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding="utf-8")
    failures = [e["name"] for e in manifest if e.get("render_rc") != 0 or e.get("export_rc") != 0]
    print("QA SUMMARY:", json.dumps({"scenarios":len(manifest),"failures":failures},
                                    ensure_ascii=False),flush=True)

if __name__ == "__main__":
    try:
        build()
    except Exception as e:
        traceback.print_exc()
        (OUT/"fatal.txt").write_text(str(e),encoding="utf-8")
        sys.exit(1)
