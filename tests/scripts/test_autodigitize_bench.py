"""Tests du banc auto-digitize (AUTO-DIGITIZE AD-1).

Sans moteur : corpus déterministe, aplatissement des rapports, verdicts de comparaison,
moteur simulé. Le test d'intégration réel s'exécute si OPENSTITCH_CLI désigne un
openstitch-cli compilé.
"""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "autodigitize_bench.py"
spec = importlib.util.spec_from_file_location("autodigitize_bench", SCRIPT)
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)

CHECK = {
    "schema": "openstitch.quality/1",
    "summary": {"errors": 0, "warnings": 2, "infos": 0, "passed": True},
    "sequence": {"stitches": 1200, "jumps": 3, "trims": 2, "color_changes": 1,
                 "short_stitches": 10, "long_moves_without_trim": 0,
                 "moves_between": {"count": 2, "travel_mm": 30.0},
                 "moves_within": {"count": 1, "travel_mm": 5.5}},
    "objects": [{}, {}],
}
FIDELITY = {"schema": "openstitch.fidelity/1", "motif_mm2": 1000.0,
            "missing": {"mm2": 20.0, "percent": 2.0},
            "interior_holes": {"count": 1, "mm2": 5.0, "percent": 0.5},
            "lost_detail": {"count": 0, "mm2": 0.0, "percent": 0.0},
            "wrong_colour": {"percent": 0.0}}
DENSITY = {"schema": "openstitch.density/1", "layers_p99": 2.0, "layers_max": 3.1,
           "hotspot_area_mm2": 0.0}
DIGITIZE = {"schema": "openstitch.digitize-report/1", "ok": True,
            "objects": {"ids_by_type": {"tatami": [2], "auto_satin": [4]}, "running": 0,
                        "satin": 1, "tatami": 1, "total": 2, "vector": 2}}


class FakeEngine:
    """Répond comme openstitch-cli ; `stitches` permet de simuler une régression."""

    def __init__(self, stitches=1200, fail_stage=None):
        self.stitches = stitches
        self.fail_stage = fail_stage
        self.calls = []

    def version(self):
        return "fake 0"

    def run(self, args):
        self.calls.append(args)
        stage = args[1]
        if stage == self.fail_stage:
            return 1, json.dumps({"schema": "openstitch.error/1", "ok": False,
                                  "error": {"code": "E_FAKE"}})
        if stage == "digitize":
            return 0, json.dumps(DIGITIZE)
        if stage == "check":
            doc = json.loads(json.dumps(CHECK))
            doc["sequence"]["stitches"] = self.stitches
            return 0, json.dumps(doc)
        return 0, json.dumps({"fidelity": FIDELITY, "density": DENSITY}[stage])


class CorpusTests(unittest.TestCase):
    def test_synthetic_pixels_are_identical_between_renders(self):
        for name in ("ring", "thin-lines"):
            fresh = bench.synthetic_pixels.__wrapped__(name)  # sans le cache
            self.assertEqual(fresh, bench.synthetic_pixels(name))

    def test_images_differ_and_png_holds_the_pixels(self):
        seen = set()
        for name in bench.SYNTHETIC:
            data = bench.render_synthetic(name)
            self.assertTrue(data.startswith(b"\x89PNG"))
            w, h, _channels, pixels = bench.pngio.decode(data)
            self.assertEqual((w, h), (bench.SIZE_PX, bench.SIZE_PX))
            self.assertEqual(pixels, bench.synthetic_pixels(name))
            seen.add(bench.sha256(pixels))
        self.assertEqual(len(seen), len(bench.SYNTHETIC))

    def test_repository_images_fit_the_default_frame(self):
        with tempfile.TemporaryDirectory() as tmp:
            cases = [c for c in bench.build_corpus(Path(tmp)) if c["kind"] == "repository"]
        self.assertEqual(len(cases), len(bench.REPO_IMAGES))
        for c in cases:
            w, h = bench.png_size(Path(c["image"]).read_bytes())
            longest_mm = max(w, h) * 25.4 / c["dpi"]
            self.assertLessEqual(longest_mm, 80.01, c["name"])

    def test_corpus_is_fifty_millimetres(self):
        self.assertAlmostEqual(bench.SIZE_PX * bench.MM_PER_PX, 50.0, places=6)


class MetricTests(unittest.TestCase):
    def test_flatten_reports(self):
        m = bench.metrics_from(CHECK, FIDELITY, DENSITY, DIGITIZE)
        self.assertEqual(m["stitches"], 1200)
        self.assertEqual(m["travel_mm"], 35.5)
        self.assertEqual(m["missing_ratio"], 0.02)
        self.assertEqual(m["interior_hole_ratio"], 0.005)
        self.assertEqual(m["objects"], 2)
        self.assertEqual(m["quality_warnings"], 2)
        self.assertEqual(bench.object_types(DIGITIZE), {"auto_satin": 1, "tatami": 1})

    def test_absent_key_is_none_not_zero(self):
        m = bench.metrics_from({"sequence": {}}, {}, {}, {})
        self.assertIsNone(m["objects"])
        self.assertIsNone(m["stitches"])
        self.assertIsNone(m["missing_ratio"])

    def test_classify(self):
        self.assertEqual(bench.classify("stitches", 1000, 1000), "same")
        self.assertEqual(bench.classify("stitches", 1000, 900), "better")
        self.assertEqual(bench.classify("stitches", 1000, 1040), "worse")
        self.assertEqual(bench.classify("stitches", 1000, 1100), "regression")
        self.assertEqual(bench.classify("quality_errors", 0, 1), "regression")
        self.assertEqual(bench.classify("objects", 3, 4), "info")
        self.assertEqual(bench.classify("stitches", 1000, None), "missing")


class RunAndCompareTests(unittest.TestCase):
    def run_fake(self, engine, out):
        return bench.run_bench(engine, out, include_repo=False, only=["ring", "holes"])

    def test_run_and_compare_detects_regression(self):
        with tempfile.TemporaryDirectory() as tmp:
            runs = 2 * len(bench.PROFILES)  # 2 cas x profils
            base = self.run_fake(FakeEngine(), Path(tmp) / "a")
            self.assertEqual(base["summary"]["measured"], runs)
            same = bench.compare(self.run_fake(FakeEngine(), Path(tmp) / "b"), base)
            self.assertEqual(same["regressions"], 0)
            worse = bench.compare(self.run_fake(FakeEngine(stitches=2000), Path(tmp) / "c"),
                                  base)
            self.assertEqual(worse["regressions"], runs)
            better = bench.compare(self.run_fake(FakeEngine(stitches=900), Path(tmp) / "d"),
                                   base)
            self.assertEqual(better["improvements"], runs)

    def test_failed_stage_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            rep = self.run_fake(FakeEngine(fail_stage="fidelity"), Path(tmp))
            self.assertEqual(rep["summary"]["failed"], 2 * len(bench.PROFILES))
            self.assertEqual(rep["results"][0]["error"]["stage"], "fidelity")

    def test_synthetic_cases_pass_dpi_and_profile_options(self):
        with tempfile.TemporaryDirectory() as tmp:
            engine = FakeEngine()
            self.run_fake(engine, Path(tmp))
            digitize = [c for c in engine.calls if c[1] == "digitize"]
            self.assertEqual(len(digitize), 2 * len(bench.PROFILES))
            self.assertTrue(all("--dpi" in c for c in digitize))
            satin_profiles = sum("--satin-bands" in opts for opts in bench.PROFILES.values())
            self.assertEqual(sum("--satin-bands" in c for c in digitize), 2 * satin_profiles)

    def test_new_profile_is_not_a_regression(self):
        with tempfile.TemporaryDirectory() as tmp:
            current = self.run_fake(FakeEngine(), Path(tmp))
            old = json.loads(json.dumps(current))
            old["results"] = [r for r in old["results"] if r["profile"] == "default"]
            result = bench.compare(current, old)
            self.assertEqual(result["regressions"], 0)
            notes = [c.get("note") for c in result["cases"] if c.get("note")]
            self.assertTrue(notes and all("absent" in n for n in notes))

    def test_versioned_baseline_matches_corpus(self):
        path = ROOT / "tests/fixtures/autodigitize-bench/baseline.json"
        baseline = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(baseline["schema"], bench.REPORT_SCHEMA)
        hashes = {c["name"]: c["sha256"] for c in baseline["corpus"]["cases"]}
        for name in bench.SYNTHETIC:
            self.assertEqual(hashes.get(name), bench.sha256(bench.synthetic_pixels(name)), name)


@unittest.skipUnless(os.environ.get("OPENSTITCH_CLI"), "OPENSTITCH_CLI non défini")
class RealEngineTests(unittest.TestCase):
    def test_two_cases_measured(self):
        engine = bench.CliEngine(os.environ["OPENSTITCH_CLI"])
        with tempfile.TemporaryDirectory() as tmp:
            rep = bench.run_bench(engine, Path(tmp), include_repo=False,
                                  only=["ring", "logo-two-color"])
        self.assertEqual(rep["summary"]["measured"], 2 * len(bench.PROFILES), rep["results"])
        for r in rep["results"]:
            self.assertGreater(r["metrics"]["stitches"], 0)


if __name__ == "__main__":
    unittest.main()
