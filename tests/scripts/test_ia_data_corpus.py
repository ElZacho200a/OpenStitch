"""Tests sans moteur pour IA-DATA-01 L1 (générateur synthétique et corpus de candidats).

La CLI est simulée : on vérifie le contrat (schémas, déterminisme, statuts, sommes
de contrôle), pas la qualité des points. Un test d'intégration réel s'exécute si
OPENSTITCH_CLI désigne un openstitch-cli compilé.
"""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "ia_data_corpus.py"
spec = importlib.util.spec_from_file_location("ia_data_corpus", SCRIPT)
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)


def check_report(measured=True, objects=1):
    obj = {"id": 2, "measured": measured, "covered_ratio": 0.97 if measured else None,
           "overflow_ratio": 0.01 if measured else None}
    return {
        "schema": "openstitch.quality/1",
        "summary": {"errors": 0, "warnings": 1, "infos": 0, "passed": True},
        "sequence": {"stitches": 300, "jumps": 1, "trims": 1, "color_changes": 0,
                     "thread_m": 0.7, "short_stitches": 4, "long_moves_without_trim": 0,
                     "moves_between": {"count": 0, "travel_mm": 0.0},
                     "moves_within": {"count": 1, "travel_mm": 2.5}},
        "objects": [obj] * objects,
    }


DENSITY = {"schema": "openstitch.density/1", "layers_mean": 1.1, "layers_p99": 2.0,
           "layers_max": 2.4, "area_over_mm2": {"2_layers": 3.0, "3_layers": 0.0,
                                                "4_layers": 0.0}}
EMPTY = {"schema": "openstitch.error/1", "ok": False,
         "error": {"code": "E_REFUSED", "message": "aucun point cousu visible"}}


class FakeEngine(corpus.CliEngine):
    """Same measure() logic as the real engine; only run() is replaced."""

    def __init__(self, workdir, refuse=("auto_satin",), empty=()):
        self.refuse, self.empty, self.calls = refuse, empty, []
        super().__init__("fake-cli", workdir)

    def run(self, args):
        self.calls.append(args[:2])
        if args[:2] == ["project", "new"]:
            return 0, ""
        if args[:2] == ["project", "apply"]:
            ops = [o["op"] for o in json.loads(Path(args[3]).read_text())["operations"]]
            if any(op in self.refuse for op in ops):
                return 3, json.dumps({"ok": False, "outcomes": [
                    {"op": ops[-1], "ok": False, "error": {"code": "E_REFUSED"}}]})
            self.last_ops = ops
            return 0, json.dumps({"ok": True, "outcomes": []})
        if args[:2] == ["project", "check"]:
            return 0, json.dumps(check_report(measured="set_stitch_type" not in self.last_ops))
        if args[:2] == ["project", "density"]:
            if any(op in self.empty for op in self.last_ops):
                return 1, json.dumps(EMPTY)
            return 0, json.dumps(DENSITY)
        return 1, ""


class GeneratorTests(unittest.TestCase):
    def test_same_seed_same_shapes_and_prefix_stability(self):
        a = corpus.generate(7, 40)
        self.assertEqual(corpus.canonical(a), corpus.canonical(corpus.generate(7, 40)))
        # Shape i does not depend on the requested count.
        self.assertEqual(corpus.canonical(a[:10]), corpus.canonical(corpus.generate(7, 10)))
        self.assertNotEqual(corpus.canonical(a), corpus.canonical(corpus.generate(8, 40)))

    def test_shapes_are_valid_ccw_and_inside_hoop(self):
        for shape in corpus.generate(3, 300):
            corpus.check_shape(shape)
            self.assertGreater(corpus.area(shape["points_mm"]), 0)
            for x, y in shape["points_mm"]:
                self.assertLessEqual(abs(x), 100)
                self.assertLessEqual(abs(y), 100)

    def test_all_families_and_pathological_cases_appear(self):
        shapes = corpus.generate(42, 400)
        self.assertEqual({s["family"] for s in shapes}, set(corpus.FAMILIES))
        flags = {f for s in shapes for f in s["flags"]}
        self.assertTrue({"tiny", "thin", "very_concave"} <= flags)

    def test_shape_id_detects_tampering(self):
        shape = corpus.generate(1, 1)[0]
        shape["points_mm"][0][0] += 0.5
        with self.assertRaises(ValueError):
            corpus.check_shape(shape)

    def test_split_is_a_stable_hash_of_the_group(self):
        shapes = corpus.generate(42, 500)
        holdout = sum(corpus.split_of(s["split_group"]) == "holdout" for s in shapes)
        self.assertGreater(holdout, 60)
        self.assertLess(holdout, 140)
        self.assertEqual(corpus.split_of("abc"), corpus.split_of("abc"))

    def test_principal_axis_of_a_rotated_band(self):
        pts = corpus.transform([[-20, -1], [20, -1], [20, 1], [-20, 1]], 30.0)
        self.assertAlmostEqual(corpus.principal_axis_deg(pts), 30.0, delta=0.5)


class MetricTests(unittest.TestCase):
    def test_metrics_cover_exactly_the_documented_keys(self):
        m = corpus.metrics_from(check_report(), DENSITY)
        self.assertEqual(set(m), set(corpus.METRIC_KEYS))
        self.assertEqual(m["travel_mm"], 2.5)
        self.assertEqual(m["area_over_2_layers_mm2"], 3.0)

    def test_unmeasured_coverage_stays_null_not_zero(self):
        m = corpus.metrics_from(check_report(measured=False), DENSITY)
        self.assertIsNone(m["covered_ratio"])
        self.assertFalse(m["coverage_measured"])

    def test_missing_engine_field_is_an_error(self):
        report = check_report()
        del report["sequence"]["trims"]
        with self.assertRaises(KeyError):
            corpus.metrics_from(report, DENSITY)
        with self.assertRaises(ValueError):
            corpus.metrics_from(check_report(objects=2), DENSITY)


class BuildTests(unittest.TestCase):
    def build(self, root, name, jobs=1, **kw):
        engine = FakeEngine(Path(root), **kw)
        out = Path(root) / name
        corpus.build(out, 5, 12, engine, {"cli_version": "fake", "engine_rev": "x"}, jobs)
        return out, engine

    def test_build_is_deterministic_and_validates(self):
        with tempfile.TemporaryDirectory() as root:
            a, _ = self.build(root, "a", jobs=1)
            b, _ = self.build(root, "b", jobs=4)
            for name in ("shapes.jsonl", "candidates.jsonl", "manifest.json"):
                self.assertEqual((a / name).read_bytes(), (b / name).read_bytes(), name)
            self.assertEqual(corpus.validate_dir(a), {"shapes": 12, "candidate_sets": 12})

    def test_statuses_refused_empty_measured(self):
        with tempfile.TemporaryDirectory() as root:
            out, _ = self.build(root, "c", empty=("spiral_fill",))
            row = corpus.read_jsonl(out / "candidates.jsonl")[0]
            by_id = {c["id"]: c for c in row["candidates"]}
            self.assertEqual(by_id["auto_satin"]["status"], "refused")
            self.assertEqual(by_id["auto_satin"]["error"]["code"], "E_REFUSED")
            self.assertEqual(by_id["tatami_a0"]["status"], "measured")
            self.assertIsNone(by_id["running"]["metrics"]["covered_ratio"])
            self.assertIsNone(row["label"])
            manifest = json.loads((out / "manifest.json").read_text())
            self.assertEqual(manifest["coverage"]["candidate_status"]["auto_satin"]["refused"], 12)

    def test_axis_candidate_uses_the_shape_axis(self):
        shape = corpus.generate(5, 1)[0]
        ops = corpus.candidate_ops(shape, dict((c[0], c[2]) for c in corpus.CANDIDATES)["tatami_axis"])
        self.assertEqual(ops[0]["params"]["angle_deg"], shape["geometry"]["principal_axis_deg"])
        self.assertEqual(ops[0]["object"], "$s")

    def test_refuses_to_overwrite_and_detects_corruption(self):
        with tempfile.TemporaryDirectory() as root:
            out, _ = self.build(root, "d")
            with self.assertRaises(ValueError):
                self.build(root, "d")
            data = (out / "candidates.jsonl").read_bytes()
            (out / "candidates.jsonl").write_bytes(data.replace(b'"stitches":300', b'"stitches":301', 1))
            with self.assertRaises(ValueError):
                corpus.validate_dir(out)

    def test_label_is_rejected_in_l1(self):
        with tempfile.TemporaryDirectory() as root:
            out, _ = self.build(root, "e")
            row = corpus.read_jsonl(out / "candidates.jsonl")[0]
            row["label"] = {"preferred_id": "tatami_a0"}
            with self.assertRaises(ValueError):
                corpus.check_set(row)

    def test_generate_command_needs_no_engine(self):
        with tempfile.TemporaryDirectory() as root:
            self.assertEqual(corpus.main(["generate", str(Path(root) / "g"), "--count", "5"]), 0)
            self.assertEqual(corpus.validate_dir(Path(root) / "g"), {"shapes": 5, "candidate_sets": 0})


@unittest.skipUnless(os.environ.get("OPENSTITCH_CLI"), "OPENSTITCH_CLI absent : intégration moteur sautée")
class EngineIntegrationTests(unittest.TestCase):
    def test_real_engine_measures_and_is_reproducible(self):
        with tempfile.TemporaryDirectory() as root:
            outs = []
            for name in ("r1", "r2"):
                out = Path(root) / name
                self.assertEqual(corpus.main(["build", str(out), "--count", "6", "--jobs", "2"]), 0)
                outs.append((out / "candidates.jsonl").read_bytes())
            self.assertEqual(outs[0], outs[1])
            rows = corpus.read_jsonl(Path(root) / "r1" / "candidates.jsonl")
            measured = [c for r in rows for c in r["candidates"] if c["status"] == "measured"]
            self.assertGreater(len(measured), 30)


if __name__ == "__main__":
    unittest.main()
