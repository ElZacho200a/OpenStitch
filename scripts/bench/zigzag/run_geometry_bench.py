"""Voie « meme geometrie » de RND-ZIGZAG-01 : memes SVG vectoriels (mm) pour les deux moteurs.

ZigZag : `zigzag convert <svg> -o x.dst` (mode et reglages portes par l'id du chemin).
OpenStitch : project new + import_svg (+ auto_satin) + set_params + project export.
Reglages normalises : espacement 0,4 mm, sous-couche et compensation de tirage desactivees.
Non normalises : longueur de point au-dela de 3 mm en satin, coupes/finitions, verrous, ordre.
Decodeur commun : `openstitch-cli stats --json`. Usage :
  python run_geometry_bench.py --zigzag Z --openstitch O --fixtures <svg dir> --work W --out R.json
"""
import argparse, json, statistics, subprocess, sys
from pathlib import Path
from run_bench import run_timed, decode, pct
from coverage import coverage

FILL = {"angle_deg": 45, "row_spacing_mm": 0.4, "stitch_length_mm": 3, "pull_mode": "manual",
        "pull_compensation_mm": 0, "underlay_mode": "manual", "underlay_edge": False,
        "underlay_parallel": False, "underlay_zigzag": False, "hidden_underpath": False}
SATIN = {"pull_mode": "manual", "pull_compensation_mm": 0, "underlay_mode": "manual",
         "center_underlay": False, "underlay_edge": False, "underlay_zigzag": False,
         "short_stitch": "disabled"}


def op(osc, osp, name, **args):
    cmd = [osc, "project", "op", str(osp), name]
    for k, v in args.items():
        cmd += ["--arg", f"{k}={json.dumps(v) if not isinstance(v, str) else v}"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(f"{name} refuse : {(r.stdout + r.stderr)[-300:]}")


def build_os(osc, svg, mode, osp, dst):
    """Prepare le .osp (hors mesure) ; renvoie la commande d'export a chronometrer."""
    Path(osp).unlink(missing_ok=True)
    subprocess.run([osc, "project", "new", str(osp), "--width-mm", "100", "--height-mm", "100"],
                   capture_output=True, check=True)
    op(osc, osp, "import_svg", file=str(svg))
    if mode == "satin":
        op(osc, osp, "auto_satin", object=2, spacing_mm=0.4)
    op(osc, osp, "set_params", object=2, params=FILL if mode == "fill" else SATIN)
    return [osc, "project", "export", str(osp), "-o", str(dst)]


def main():
    a = argparse.ArgumentParser()
    for k in ("zigzag", "openstitch", "fixtures", "work", "out"):
        a.add_argument("--" + k, required=True)
    a.add_argument("--reps", type=int, default=5)
    a = a.parse_args()
    for k in ("zigzag", "openstitch", "fixtures", "work", "out"):
        setattr(a, k, str(Path(getattr(a, k)).resolve()))
    work = Path(a.work); work.mkdir(parents=True, exist_ok=True)
    res = {"reps": a.reps, "warmup": 1, "cases": {}}
    for svg in sorted(Path(a.fixtures).glob("*.svg")):
        shape, mode = svg.stem.split("__")
        case = {}
        for tool in ("zigzag", "openstitch"):
            dst = work / f"{tool}_{svg.stem}.dst"
            entry = {}
            try:
                if tool == "zigzag":
                    cmd = [a.zigzag, "convert", str(svg), "-o", str(dst)]
                else:
                    cmd = build_os(a.openstitch, svg, mode, work / f"{svg.stem}.osp", dst)
                times, rss = [], []
                for i in range(1 + a.reps):
                    dst.unlink(missing_ok=True)
                    t, m, rc, log = run_timed(cmd)
                    if rc:
                        raise RuntimeError(log[-300:])
                    if i:
                        times.append(t); rss.append(m)
                entry.update(wall_s_p50=statistics.median(times), wall_s_p95=pct(times, .95),
                             peak_rss_mb=max(rss) / 2**20, stats=decode(a.openstitch, dst),
                             quality=coverage(svg, dst, thread_mm=0.5))
            except Exception as e:  # refus consigne, jamais maquille
                entry["error"] = str(e)
            if tool == "zigzag" and "error" not in entry:
                info = subprocess.run([a.zigzag, "info", str(svg)], capture_output=True, text=True).stdout
                entry["notes"] = [l[6:] for l in info.splitlines() if l.startswith("note:")]
            case[tool] = entry
        res["cases"][svg.stem] = case
        print(svg.stem, {t: (c.get("stats", {}).get("stitches"), c.get("error", "")[:60]) for t, c in case.items()}, flush=True)
    Path(a.out).write_text(json.dumps(res, indent=2), encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
