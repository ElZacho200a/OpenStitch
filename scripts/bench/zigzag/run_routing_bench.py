"""L3 RND-ZIGZAG-01 : routage cache. Memes SVG a calques (mm) pour ZigZag et OpenStitch.

Scenarios (couleurs = calques, du dessous au dessus) :
  bridge_under   2 barres separees (calque 0) + barre transversale (calque 1) : le pont est cachable dessous
  bridge_row     5 carres alignes (calque 0) + fine barre (calque 1) qui recouvre les liaisons
  disjoint_same  6 cercles separes, une couleur : rien pour cacher une liaison
  stack_row      rectangle (0) + 3 cercles (1) : une liaison entre cercles est visible sur le rectangle
  frame_inner    anneau (0) + disque dans le trou (1)
Configurations : zigzag defaut / --over-sewn ; openstitch defaut / hidden_travel / hidden_travel + max 20/100 mm.
Mesures : coupes, sauts, longueur des sauts, points, et PIXELS VISIBLES ERRONES (couleur finale cousue
differente de la couleur attendue, y compris cousu hors forme), en mm2. Hypothese : bloc de couleur k = calque k.
Usage : python run_routing_bench.py --zigzag Z --openstitch O --work W --out R.json
"""
import argparse, json, re, subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter
from coverage import flatten, PX
from run_geometry_bench import op, FILL

COL = ["#2f4858", "#e8a33c", "#c9452e"]
TOK = "fill a=45 s=0.4 l=3 u=0 pc=0"


def rect(x0, y0, x1, y1):
    return f"M{x0},{y0} H{x1} V{y1} H{x0} Z"


def circ(cx, cy, r):
    return f"M{cx - r},{cy} a{r},{r} 0 1,1 {2 * r},0 a{r},{r} 0 1,1 {-2 * r},0 Z"


def ring(cx, cy, ro, ri):
    inner = f"M{cx - ri},{cy} a{ri},{ri} 0 1,0 {2 * ri},0 a{ri},{ri} 0 1,0 {-2 * ri},0 Z"
    return circ(cx, cy, ro) + " " + inner


SCEN = {
    "bridge_under": [[rect(20, 20, 30, 80), rect(70, 20, 80, 80)], [rect(10, 45, 90, 55)]],
    "bridge_row": [[rect(x, 46, x + 8, 54) for x in (6, 24, 42, 60, 78)], [rect(4, 48.5, 96, 51.5)]],
    "disjoint_same": [[circ(18, 25, 8), circ(50, 25, 8), circ(82, 25, 8),
                       circ(18, 70, 8), circ(50, 70, 8), circ(82, 70, 8)]],
    "stack_row": [[rect(10, 30, 90, 70)], [circ(25, 50, 8), circ(50, 50, 8), circ(75, 50, 8)]],
    "frame_inner": [[ring(50, 50, 40, 28)], [circ(50, 50, 20)]],
}


def svg_all(layers):
    body = ""
    for k, paths in enumerate(layers):
        for d in paths:
            body += f'<path id="{TOK}" fill="{COL[k]}" fill-rule="evenodd" d="{d}"/>'
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="100mm" viewBox="0 0 100 100">{body}</svg>\n'


def decode_full(path):
    d = open(path, "rb").read()[512:]
    x = y = blk = 0
    segs, jumps = [], []
    for i in range(0, len(d) - 2, 3):
        b0, b1, b2 = d[i], d[i + 1], d[i + 2]
        if b2 & 0xF3 == 0xF3:
            break
        dx = ((b0 & 1) - ((b0 >> 1) & 1) + 9 * ((b0 >> 2) & 1) - 9 * ((b0 >> 3) & 1) + 3 * (b1 & 1)
              - 3 * ((b1 >> 1) & 1) + 27 * ((b1 >> 2) & 1) - 27 * ((b1 >> 3) & 1)
              + 81 * ((b2 >> 2) & 1) - 81 * ((b2 >> 3) & 1))
        dy = (((b0 >> 7) & 1) - ((b0 >> 6) & 1) + 9 * ((b0 >> 5) & 1) - 9 * ((b0 >> 4) & 1)
              + 3 * ((b1 >> 7) & 1) - 3 * ((b1 >> 6) & 1) + 27 * ((b1 >> 5) & 1) - 27 * ((b1 >> 4) & 1)
              + 81 * ((b2 >> 5) & 1) - 81 * ((b2 >> 4) & 1))
        nx, ny = x + dx, y + dy
        if b2 & 0xC3 == 0xC3:
            blk += 1
        elif b2 & 0x83 == 0x83:
            jumps.append(((dx ** 2 + dy ** 2) ** 0.5) / 10)
        elif b2 & 0x83 == 0x03:
            segs.append((x / 10, y / 10, nx / 10, ny / 10, blk))
        x, y = nx, ny
    return segs, jumps, blk + 1


def visible_errors(layers, dst, thread_mm=0.5):
    subs_by_layer = [[flatten(d) for d in paths] for paths in layers]
    pts = [p for L in subs_by_layer for s in L for sub in s for p in sub]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    pad = 6
    off = (min(xs) - pad, min(ys) - pad)
    size = (int((max(xs) - min(xs) + 2 * pad) * PX) + 1, int((max(ys) - min(ys) + 2 * pad) * PX) + 1)
    expected = np.zeros(size[::-1], np.uint8)  # 0 = tissu nu, k+1 = calque k
    layer_mask = {}  # forme de chaque calque, sans occultation
    for k, L in enumerate(subs_by_layer):
        for subs in L:
            m = Image.new("1", size, 0)
            for s in subs:
                lay = Image.new("1", size, 0)
                ImageDraw.Draw(lay).polygon([((px - off[0]) * PX, (py - off[1]) * PX) for px, py in s], fill=1)
                m = ImageChops.logical_xor(m, lay)
            expected[np.array(m, bool)] = k + 1
            layer_mask[k] = layer_mask.get(k, np.zeros(size[::-1], bool)) | np.array(m, bool)
    segs, jumps, nblk = decode_full(dst)
    if not segs:
        return None
    sx = [p for s in segs for p in (s[0], s[2])]
    sy = [p for s in segs for p in (s[1], s[3])]
    cb = ((min(sx) + max(sx)) / 2, (min(sy) + max(sy)) / 2)
    sb = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
    obs = Image.new("L", size, 0)
    dr = ImageDraw.Draw(obs)
    w = max(1, round(thread_mm * PX))

    def p(x, y):
        return ((x - cb[0] + sb[0] - off[0]) * PX, (-(y - cb[1]) + sb[1] - off[1]) * PX)

    for x0, y0, x1, y1, b in segs:
        dr.line([p(x0, y0), p(x1, y1)], fill=b + 1, width=w)
    o = np.array(obs)
    px2 = 1 / (PX * PX)
    # fil « errant » : segment dont le milieu est encore visible (non recouvert) alors que ce n'est
    # pas sa couleur attendue et qu'il est a plus de 1 mm de toute zone de sa couleur (liaison visible).
    near = {}
    for b in range(nblk):
        reg = Image.fromarray((layer_mask[b] * 255).astype(np.uint8))
        near[b] = np.array(reg.filter(ImageFilter.MaxFilter(2 * PX + 1))) > 0
    stray = 0.0
    for x0, y0, x1, y1, b in segs:
        mx, my = p((x0 + x1) / 2, (y0 + y1) / 2)
        ix, iy = int(round(mx)), int(round(my))
        if 0 <= iy < o.shape[0] and 0 <= ix < o.shape[1]:
            if o[iy, ix] == b + 1 and expected[iy, ix] != b + 1 and not near[b][iy, ix]:
                stray += ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
    return {"stray_len_mm": stray, "wrong_mm2": float(((o != 0) & (o != expected)).sum() * px2),
            "missing_mm2": float(((o == 0) & (expected != 0)).sum() * px2),
            "expected_mm2": float((expected != 0).sum() * px2), "color_blocks": nblk,
            "jump_len_mm": float(sum(jumps)), "jumps": len(jumps)}


def stats(osc, dst):
    r = subprocess.run([osc, "stats", "--json", str(dst)], capture_output=True, text=True)
    return json.loads(r.stdout)


def build_os(osc, layers, work, name, finishing):
    osp = work / f"{name}.osp"
    osp.unlink(missing_ok=True)
    subprocess.run([osc, "project", "new", str(osp), "--width-mm", "100", "--height-mm", "100"],
                   capture_output=True, check=True)
    for k, paths in enumerate(layers):
        f = work / f"{name}_L{k}.svg"
        f.write_text(svg_all([[]] * k + [paths]), encoding="utf-8")
        r = subprocess.run([osc, "project", "op", str(osp), "import_svg", "--arg", f"file={f}",
                            "--arg", f"rgb={COL[k]}"], capture_output=True, text=True)
        ids = [int(i) for i in re.search(r'"created":\[([\d,]+)\]', r.stdout).group(1).split(",")]
        for i in ids:
            op(osc, osp, "set_params", object=i, params=FILL)
    if finishing:
        op(osc, osp, "set_finishing", **finishing)
    return osp


def main():
    a = argparse.ArgumentParser()
    for k in ("zigzag", "openstitch", "work", "out"):
        a.add_argument("--" + k, required=True)
    a = a.parse_args()
    z, o, work = str(Path(a.zigzag).resolve()), str(Path(a.openstitch).resolve()), Path(a.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    cfgs = {"zigzag": [], "zigzag_oversewn": ["--over-sewn"], "openstitch": None,
            "openstitch_hidden": {"hidden_travel": True},
            "openstitch_hidden_max": {"hidden_travel": True, "hidden_travel_max_jump_mm": 20,
                                      "hidden_travel_max_bridge_mm": 100}}
    res = {}
    for name, layers in SCEN.items():
        res[name] = {}
        svg = work / f"{name}.svg"
        svg.write_text(svg_all(layers), encoding="utf-8")
        for cfg, v in cfgs.items():
            dst = work / f"{cfg}_{name}.dst"
            dst.unlink(missing_ok=True)
            try:
                if cfg.startswith("zigzag"):
                    r = subprocess.run([z, "convert", str(svg), "-o", str(dst)] + v, capture_output=True, text=True)
                    if r.returncode:
                        raise RuntimeError(r.stderr[-200:])
                else:
                    osp = build_os(o, layers, work, f"{cfg}_{name}", v)
                    r = subprocess.run([o, "project", "export", str(osp), "-o", str(dst)], capture_output=True, text=True)
                    if r.returncode:
                        raise RuntimeError((r.stdout + r.stderr)[-200:])
                s = stats(o, dst)
                if cfg.startswith("zigzag"):
                    ir = subprocess.run([z, "info", str(svg)], capture_output=True, text=True)
                    inf = ir.stdout + ir.stderr
                    m = re.search(r"(\d+) trims", inf)
                    s["trims_native"] = int(m.group(1)) if m else None
                    if "over-sewn" in "".join(v):
                        pass
                else:
                    s["trims_native"] = s["trims"]
                q = visible_errors(layers, dst)
                res[name][cfg] = {"stats": s, "visible": q}
                print(name, cfg, s["stitches"], "trims", s["trims_native"], "jumps", s["jumps"], "jumplen", round(q["jump_len_mm"]),
                      "blocks", q["color_blocks"], "stray_mm", round(q["stray_len_mm"], 1), flush=True)
            except Exception as e:
                res[name][cfg] = {"error": str(e)}
                print(name, cfg, "ERREUR", str(e)[:120], flush=True)
    Path(a.out).write_text(json.dumps(res, indent=2), encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
