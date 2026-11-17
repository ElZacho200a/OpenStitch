"""Banc A/B RND-ZIGZAG-01 : openstitch-cli digitize vs zigzag convert, sur PNG identiques.

Etape mixte (image -> DST) : la segmentation/tracage differe entre les deux moteurs,
donc les resultats ne comparent PAS le seul moteur de points (voir docs/research/zigzag-benchmarks.md).
Les DST des deux outils sont decodes par le meme decodeur (`openstitch-cli stats --json`).

Usage :
  python run_bench.py --zigzag <zigzag.exe> --openstitch <openstitch-cli.exe> \
      --fixtures <dossier png> --work <dossier tmp> --out bench-results.json [--reps 5]
"""
import argparse, json, statistics, subprocess, sys, time
from pathlib import Path
import psutil

ZZ_MM_PER_PX = 0.2  # mesure : cercle 320 px -> 64,2 mm ; OpenStitch normalise via --dpi 127
OS_DPI = 25.4 / ZZ_MM_PER_PX


def run_timed(cmd, timeout=120):
    import tempfile
    t0 = time.perf_counter()
    with tempfile.TemporaryFile() as log:
        p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        ps, peak = psutil.Process(p.pid), 0
        while p.poll() is None:
            if time.perf_counter() - t0 > timeout:
                p.kill(); p.wait()
                return timeout, peak, -9, "timeout"
            try:
                peak = max(peak, ps.memory_info().rss)
            except psutil.Error:
                pass
            time.sleep(0.005)
        log.seek(0)
        return time.perf_counter() - t0, peak, p.returncode, log.read().decode("utf-8", "replace")


def pct(v, q):
    s = sorted(v)
    return s[min(len(s) - 1, round(q * (len(s) - 1)))]


def decode(osc, dst):
    r = subprocess.run([osc, "stats", "--json", str(dst)], capture_output=True, text=True)
    return json.loads(r.stdout) if r.returncode == 0 else {"error": r.stderr.strip()}


def main():
    a = argparse.ArgumentParser()
    for k in ("zigzag", "openstitch", "fixtures", "work", "out"):
        a.add_argument("--" + k, required=True)
    a.add_argument("--reps", type=int, default=5)
    a = a.parse_args()
    for k in ("zigzag", "openstitch", "fixtures", "work", "out"):
        setattr(a, k, str(Path(getattr(a, k)).resolve()))
    work = Path(a.work); work.mkdir(parents=True, exist_ok=True)
    res = {"zigzag_version": subprocess.run([a.zigzag, "-V"], capture_output=True, text=True).stdout.strip(),
           "openstitch_version": subprocess.run([a.openstitch, "--version"], capture_output=True, text=True).stdout.strip(),
           "reps": a.reps, "warmup": 1, "os_dpi": OS_DPI, "cases": {}}
    for png in sorted(Path(a.fixtures).glob("*.png")):
        case = {}
        for tool in ("zigzag", "openstitch"):
            dst = work / f"{tool}_{png.stem}.dst"
            cmd = ([a.zigzag, "convert", str(png), "-o", str(dst)] if tool == "zigzag"
                   else [a.openstitch, "digitize", str(png), str(dst), "--dpi", f"{OS_DPI:g}"])
            times, rss, rc, last = [], [], 0, ""
            for i in range(1 + a.reps):
                dst.unlink(missing_ok=True)
                t, m, rc, last = run_timed(cmd)
                if rc != 0:
                    break
                if i:
                    times.append(t); rss.append(m)
            entry = {"returncode": rc}
            if rc != 0:
                entry["error"] = last[-400:]
            else:
                entry.update(wall_s_p50=statistics.median(times), wall_s_p95=pct(times, .95),
                             wall_s_all=times, peak_rss_mb=max(rss) / 2**20,
                             stats=decode(a.openstitch, dst))
            case[tool] = entry
        res["cases"][png.stem] = case
        print(png.stem, {t: (round(c.get("wall_s_p50", -1), 3), c.get("stats", {}).get("stitches")) for t, c in case.items()})
    Path(a.out).write_text(json.dumps(res, indent=2), encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
