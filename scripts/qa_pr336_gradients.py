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
# Never accept OSP/DST/PNG left behind by an earlier local or CI execution.
# In particular, a failed export must not be scored using a stale DST.
for previous in OUT.iterdir():
    if previous.is_file():
        previous.unlink()
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
        # Keep the log compact, but parse the COMPLETE JSON before truncation.
        # The inspect response exceeds 12 KB for five satin colors.
        if label.endswith(".inspect") and p.returncode == 0:
            try:
                payload = json.loads(p.stdout)
                result["inspection"] = {
                    "counts": payload.get("counts", {}),
                    "sequence": payload.get("sequence", {}),
                    "layers": [
                        {"id": o.get("id"), "stitches": o.get("stitches", {}).get("total", 0)}
                        for o in payload.get("embroidery_objects", [])
                    ],
                }
            except Exception as e:
                result["inspect_parse"] = str(e)
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
            profile = {"multi_5_convex": 5, "multi_5_concave": 6,
                       "multi_5_convex_concave": 7, "multi_5_concave_convex": 8}
            if name in profile:
                p["densityGradient"]["curve"] = profile[name]
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

def dst_quality(path: Path):
    """Count actual needle-down records and diagonal connectors; DST coordinates in 0.1 mm."""
    blob = path.read_bytes()[512:]
    needle = jumps = colors = bad_diag = 0
    bad_len = 0.0
    worst = 0.0
    for offset in range(0, len(blob) - 2, 3):
        a, b, c = blob[offset:offset+3]
        if c == 0xF3:
            break
        if c == 0xC3:
            colors += 1
            continue
        # Tajima DST ternary 1/3/9/27/81 units, each 0.1 mm.
        dx = ((a & 1) > 0) - ((a & 2) > 0)
        dx += 3 * (((b & 1) > 0) - ((b & 2) > 0))
        dx += 9 * (((a & 4) > 0) - ((a & 8) > 0))
        dx += 27 * (((b & 4) > 0) - ((b & 8) > 0))
        dx += 81 * (((c & 4) > 0) - ((c & 8) > 0))
        dy = ((a & 128) > 0) - ((a & 64) > 0)
        dy += 3 * (((b & 128) > 0) - ((b & 64) > 0))
        dy += 9 * (((a & 32) > 0) - ((a & 16) > 0))
        dy += 27 * (((b & 32) > 0) - ((b & 16) > 0))
        dy += 81 * (((c & 32) > 0) - ((c & 16) > 0))
        import math
        length = 0.1 * math.hypot(dx, dy)
        if c & 0x80:
            jumps += 1
        else:
            needle += 1
            worst = max(worst, length)
            if length > 5.0 and abs(dx) > 2 and abs(dy) > 2:
                bad_diag += 1
                bad_len += length
    return {"stitches": needle, "jumps": jumps, "color_changes": colors,
            "diagonals_over_5mm": bad_diag, "diagonal_length_mm": round(bad_len, 1),
            "max_needle_step_mm": round(worst, 2)}

def scale_for_benchmark(document, factor):
    """Scale the design geometry while retaining *physical* stitch spacing."""
    for vector in document["vectorObjects"]:
        for shape in vector["paths"]:
            for path in [shape["outer"], *shape.get("holes", [])]:
                for node in path["nodes"]:
                    node["pos"] = [int(v * factor) for v in node["pos"]]
                    for handle in ("tanIn", "tanOut"):
                        if handle in node:
                            node[handle] = [int(v * factor) for v in node[handle]]
    for obj in document["embroideryObjects"]:
        params = obj["params"]
        gradient = params.get("densityGradient")
        if gradient:
            for key in ("fromX", "fromY", "toX", "toY"):
                gradient[key] = int(gradient[key] * factor)
        if params.get("type") == "satin":
            # Scale the rail *length*, not its physical width. Scaling both
            # axes turns a 3.2 mm satin into a 19.2 mm satin at x6, which
            # necessarily requires long needle penetrations and makes the
            # benchmark measure an unsafe ribbon rather than a large motif.
            # The fixture rails run along X; preserve Y and stitch spacing.
            for rail_name in ("railA", "railB"):
                for node in params.get(rail_name, {}).get("nodes", []):
                    node["pos"][0] = int(node["pos"][0] * factor)
                    for handle in ("tanIn", "tanOut"):
                        if handle in node:
                            node[handle][0] = int(node[handle][0] * factor)
            for rung in params.get("rungs", []):
                for key in ("a", "b", "start", "end"):
                    if isinstance(rung.get(key), list) and len(rung[key]) == 2:
                        rung[key][0] = int(rung[key][0] * factor)
        for guide in params.get("guides", []):
            for node in guide.get("nodes", []):
                node["pos"] = [int(v * factor) for v in node["pos"]]
    if "canvas" in document:
        canvas = document["canvas"]
        canvas["width"] = int(canvas["width"] * factor)
        canvas["height"] = int(canvas["height"] * factor)


def large_design_benchmarks():
    """Process-level elapsed times; informative, not a hardware-independent threshold."""
    benchmarks = []
    for source in ("multi_5_linear", "multi_8_linear", "satin_8"):
        for factor in (3, 6):
            name = source + "_x" + str(factor)
            target = OUT / (name + ".osp")
            patch_osp(OUT / (source + ".osp"), target,
                      lambda doc: scale_for_benchmark(doc, factor))
            dst = OUT / (name + ".dst")
            result = call(name + ".benchmark",
                          ["project", "export", target, "-o", dst], timeout=85)
            stats = dst_quality(dst) if result["rc"] == 0 and dst.is_file() else {}
            preview = OUT / (name + ".png")
            rendered = call(name + ".render",
                            ["project", "render", target, "-o", preview,
                             "--px-per-mm", "3", "--fabric", "#ece7df"], timeout=85)
            png_valid = (rendered["rc"] == 0 and preview.is_file()
                         and preview.stat().st_size >= 45
                         and preview.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
                         and preview.read_bytes().endswith(b"IEND\xaeB`\x82"))
            entry = {"name": name, "factor": factor, "width_mm": 40 * factor,
                     "height_mm": (3.2 if source == "satin_8" else 20 * factor),
                     "elapsed_ms": result["ms"],
                     "return_code": result["rc"], "dst_quality": stats,
                     "render_return_code": rendered["rc"], "png_valid": png_valid}
            benchmarks.append(entry)
    (OUT / "benchmarks.json").write_text(
        json.dumps(benchmarks, ensure_ascii=False, indent=2), encoding="utf-8")
    return benchmarks


def verify_ci_source_sha():
    """Reject a QA run using a checkout unrelated to the advertised PR head.

    GitHub pull_request checkouts normally use a synthetic merge commit, so
    GITHUB_SHA is *not* the PR head. The merge commit's second parent must be
    the head SHA. Direct-head checkouts are also supported.
    """
    expected = os.environ.get("OPENSTITCH_PR_HEAD_SHA", "").strip().lower()
    if not expected:
        return
    def git(*args):
        result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                                text=True, check=True)
        return result.stdout.strip().lower()
    checkout = git("rev-parse", "HEAD")
    # GitHub Actions checkout defaults to fetch-depth: 1. rev-list treats a
    # shallow checkout commit as a root and omits its parents, even when the
    # commit object itself records the merge parents. Read that object instead.
    commit_object = git("cat-file", "-p", "HEAD")
    parents = [line.split(" ", 1)[1] for line in commit_object.splitlines()
               if line.startswith("parent ")]
    github_sha = os.environ.get("GITHUB_SHA", "").strip().lower()
    if github_sha and checkout != github_sha:
        raise RuntimeError(f"QA checkout {checkout} differs from GITHUB_SHA {github_sha}")
    if checkout != expected and (len(parents) < 2 or parents[1] != expected):
        raise RuntimeError(
            f"QA source mismatch: expected PR head {expected}, checkout {checkout}, "
            f"parents {parents}; refusing to attribute results to another SHA")
    print("QA SOURCE VERIFIED:", json.dumps({
        "checkout_sha": checkout, "pr_head_sha": expected,
        "checkout_kind": "head" if checkout == expected else "merge"
    }), flush=True)


def build():
    verify_ci_source_sha()
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
        ("multi_5_convex",5,0,0,True,False,False),
        ("multi_5_concave",5,0,0,True,False,False),
        ("multi_5_convex_concave",5,0,0,True,False,False),
        ("multi_5_concave_convex",5,0,0,True,False,False),
        ("multi_5_radial",5,0,1,True,False,False),
        ("multi_5_curved",5,0,2,True,False,False),
        ("multi_5_overlay",5,0,0,False,False,False),
        ("multi_8_linear",8,0,0,True,False,False),
        ("satin_3",3,0,0,True,True,False),
        ("satin_5",5,0,0,True,True,False),
        ("satin_8",8,0,0,True,True,False)
    ]
    # Keep the reported QA matrix aligned with the actual scenarios.
    assert len(cases) == len({case[0] for case in cases}), "Duplicate QA case names"
    assert len(cases) == 20, f"PR336 QA matrix drift: expected 20 cases, got {len(cases)}"
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
                    inspection = result.get("inspection")
                    if inspection is None:
                        entry["inspect_parse"] = result.get("inspect_parse",
                                                            "missing JSON inspection")
                    else:
                        entry["counts"] = inspection["counts"]
                        entry["sequence"] = inspection["sequence"]
                        entry["layers"] = inspection["layers"]
            p = OUT / f"{name}.png"
            entry["png_bytes"] = p.stat().st_size if p.exists() else 0
            # A nonempty file is not necessarily a valid rendered PNG.
            entry["png_valid"] = (p.is_file() and p.stat().st_size >= 45
                                  and p.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
                                  and p.read_bytes().endswith(b"IEND\xaeB`\x82"))
            dst = OUT / f"{name}.dst"
            if dst.exists():
                entry["dst_quality"] = dst_quality(dst)
            if p.exists():
                entry["png_sha256"] = hashlib.sha256(p.read_bytes()).hexdigest()
        except Exception as exc:
            entry["exception"] = str(exc)
            traceback.print_exc()
        manifest.append(entry)
        (OUT/"manifest.json").write_text(json.dumps(manifest,indent=2,ensure_ascii=False),
                                         encoding="utf-8")
    by_name = {entry["name"]: entry for entry in manifest}
    reference = by_name["multi_5_linear"].get("png_sha256")
    for profile_name in ("multi_5_convex", "multi_5_concave",
                         "multi_5_convex_concave", "multi_5_concave_convex"):
        if by_name[profile_name].get("png_sha256") == reference:
            raise RuntimeError("Profile " + profile_name +
                               " gives the identical preview as linear fill")
    benchmarks = large_design_benchmarks()
    results = {"pr":336,"source":str(CLI),
               "checkout_sha":os.environ.get("GITHUB_SHA"),
               "pr_head_sha":os.environ.get("OPENSTITCH_PR_HEAD_SHA"),
               "sample":"40 x 20 mm rectangle + 30 x 3.2 mm satin rails",
               "cases":manifest, "benchmarks": benchmarks, "commands": LOG}
    (OUT/"report.json").write_text(json.dumps(results,indent=2,ensure_ascii=False),encoding="utf-8")
    failures = [e["name"] for e in manifest if e.get("render_rc") != 0 or e.get("export_rc") != 0 or not e.get("png_valid", False)]
    # Echec sur de vrais points dangereux, pas seulement sur des commandes
    # CLI qui retournent zero. Un filet de securite, pas un essai sur tissu.
    quality_failures = []
    for entry in benchmarks:
        if entry["return_code"] != 0 or not entry["dst_quality"]:
            quality_failures.append(entry["name"] + ": benchmark export failed")
        if not entry["png_valid"]:
            quality_failures.append(entry["name"] + ": benchmark preview missing/invalid")
        # A failed preview must not suppress the independent DST safety check.
        # An empty DST quality result is already reported as an export failure.
        if entry["dst_quality"] and entry["dst_quality"]["max_needle_step_mm"] > 7.0:
            quality_failures.append(entry["name"] + ": unsafe large-design needle step")
        if entry["name"].startswith("satin_8_x") and entry["dst_quality"]:
            stats = entry["dst_quality"]
            # Large satin motifs must not silently regress to one jump per
            # needle-down. Use a scale-independent ratio, not a fixed count.
            if stats["stitches"] <= 0 or stats["jumps"] * 3 > stats["stitches"]:
                quality_failures.append(
                    entry["name"] + ": excessive satin travel (" +
                    str(stats["jumps"]) + " jumps / " +
                    str(stats["stitches"]) + " stitches; max 1/3)")
        if entry["dst_quality"] and entry["dst_quality"]["diagonals_over_5mm"] > 0:
            quality_failures.append(
                entry["name"] + ": " + str(entry["dst_quality"]["diagonals_over_5mm"])
                + " diagonal sewn connectors > 5 mm in large-design DST")
    for entry in manifest:
        if entry.get("inspect_rc") != 0 or entry.get("inspect_parse"):
            quality_failures.append(entry["name"] + ": projet OSP non inspectable")
        if entry.get("counts", {}).get("embroidery_objects") != entry["n"]:
            quality_failures.append(entry["name"] + ": nombre de couches OSP incorrect")
        if entry.get("sequence", {}).get("color_changes") != entry["n"] - 1:
            quality_failures.append(entry["name"] + ": changements de fil du moteur incorrects")
        if len(entry.get("layers", [])) != entry["n"] or any(
                layer["stitches"] <= 0 for layer in entry.get("layers", [])):
            quality_failures.append(entry["name"] + ": couche sans points brodes")
        stats = entry.get("dst_quality")
        if not stats:
            quality_failures.append(entry["name"] + ": DST absent/illisible")
            continue
        if stats["color_changes"] != entry["n"] - 1:
            quality_failures.append(
                entry["name"] + ": " + str(stats["color_changes"])
                + " changements pour " + str(entry["n"]) + " fils")
        if stats["stitches"] <= entry["n"]:
            quality_failures.append(entry["name"] + ": broderie vide ou presque vide")
        if stats["max_needle_step_mm"] > 7.0:
            quality_failures.append(
                entry["name"] + ": point cousu de "
                + str(stats["max_needle_step_mm"]) + " mm (limite 7 mm)")
        if stats["diagonals_over_5mm"] > 0:
            quality_failures.append(
                entry["name"] + ": " + str(stats["diagonals_over_5mm"])
                + " diagonales cousues > 5 mm (longueur totale "
                + str(stats["diagonal_length_mm"]) + " mm)")
        # A satin can pass the 7 mm stitch limit while producing far too many
        # non-sewing moves. Keep a per-size budget and a proportional guard:
        # the historical loose 60/90/140 limits would miss a large regression.
        satin_jump_limits = {"satin_3": 30, "satin_5": 45, "satin_8": 60}
        if entry["name"] in satin_jump_limits:
            limit = satin_jump_limits[entry["name"]]
            if stats["jumps"] > limit or stats["jumps"] * 3 > stats["stitches"]:
                quality_failures.append(
                    entry["name"] + ": " + str(stats["jumps"]) + " sauts pour "
                    + str(stats["stitches"]) + " points (limite "
                    + str(limit) + " et <= 1 saut / 3 points)")
    print("QA SUMMARY:", json.dumps({"scenarios": len(manifest), "expected_scenarios": len(cases), "failures": failures,
                                      "quality_failures": quality_failures},
                                     ensure_ascii=False), flush=True)
    if failures or quality_failures:
        sys.exit(2)

if __name__ == "__main__":
    try:
        build()
    except Exception as e:
        traceback.print_exc()
        (OUT/"fatal.txt").write_text(str(e),encoding="utf-8")
        sys.exit(1)
