# SPDX-License-Identifier: Apache-2.0
"""Tests du benchmark de segmentation (PR-01) : hors ligne, sans modele, sans OpenCV."""
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts" / "ai_bench"))

import backends as bk  # noqa: E402
import fixtures  # noqa: E402
import metrics  # noqa: E402
import openstitch_ai_bench as bench  # noqa: E402
import pngio  # noqa: E402

CORPUS = ROOT / "tests" / "fixtures" / "ai-benchmark"
FAKE_WORKER = Path(__file__).resolve().parent / "ai_bench_fake_worker.py"


def run_bench(out: Path, *args: str) -> dict:
    code = bench.main(["--out", str(out), *args])
    assert code == 0, code
    return json.loads((out / "report.json").read_text(encoding="utf-8"))


def result_of(report: dict, backend: str) -> dict:
    return next(r for r in report["cases"][0]["results"] if r["backend"] == backend)


class PngTests(unittest.TestCase):
    def test_roundtrip_and_determinism(self):
        px = bytes(range(12))
        data = pngio.encode(2, 2, "RGB", px)
        self.assertEqual(data, pngio.encode(2, 2, "RGB", px))
        self.assertEqual(pngio.decode(data), (2, 2, 3, px))

    def test_rejects_garbage(self):
        with self.assertRaises(pngio.PngError):
            pngio.decode(b"not a png")
        with self.assertRaises(pngio.PngError):
            pngio.decode(pngio.encode(2, 1, "L", b"\x00\x01")[:-20])


class FixtureTests(unittest.TestCase):
    def test_committed_corpus_matches_regeneration(self):
        self.assertEqual(fixtures.main(["--check", str(CORPUS)]), 0)

    def test_check_ignores_zlib_differences_but_not_pixels(self):
        import zlib as _zlib
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / "corpus"
            shutil.copytree(CORPUS, copy)
            target = copy / "marine-synth-01" / "gold" / "sky.png"
            w, h, ch, px = pngio.decode(target.read_bytes())
            # meme image, autre niveau de compression (autre implementation de zlib = octets differents)
            raw = b"".join(bytes([0]) + px[y * w:(y + 1) * w] for y in range(h))
            data = bytearray(pngio.encode(w, h, "L", px))
            self.assertNotEqual(_zlib.compress(raw, 1), _zlib.compress(raw, 9))
            target.write_bytes(bytes(data))
            self.assertEqual(fixtures.main(["--check", str(copy)]), 0)
            changed = bytearray(px)
            changed[0] ^= 255
            target.write_bytes(pngio.encode(w, h, "L", bytes(changed)))
            self.assertEqual(fixtures.main(["--check", str(copy)]), 2)

    def test_manifest_describes_the_required_scene(self):
        case = json.loads((CORPUS / "manifest.json").read_text(encoding="utf-8"))["cases"][0]
        ids = {o["semantic_id"] for o in case["objects"]}
        for needed in ("boat.hull", "boat.sail_main", "boat.sail_jib", "boat.mast", "boat.rigging",
                       "lighthouse.tower_white", "lighthouse.window", "sea.water", "sea.foam"):
            self.assertIn(needed, ids)
        self.assertEqual(case["rights"]["license"], "CC0-1.0")
        self.assertEqual(case["design"]["width_mm"], 160.0)
        # couleurs partagees entre objets distincts : le test ne se resout pas par la couleur
        colors = {}
        for o in case["objects"]:
            colors.setdefault(tuple(o["color_rgb"]), []).append(o["semantic_id"])
        self.assertTrue(any(len(v) > 1 for v in colors.values()))
        self.assertGreaterEqual(
            next(o for o in case["objects"] if o["semantic_id"] == "lighthouse.tower_white")["holes"], 1)


class MetricTests(unittest.TestCase):
    def setUp(self):
        self.grid = metrics.Grid(8, 6)

    def mask(self, cells):
        flat = bytearray(48)
        for x, y in cells:
            flat[y * 8 + x] = 1
        return self.grid.pack(flat)

    def test_pack_unpack_dilate_erode(self):
        m = self.mask([(3, 3)])
        self.assertEqual(metrics.count(m), 1)
        self.assertEqual(metrics.count(self.grid.dilate(m)), 9)
        self.assertEqual(metrics.count(self.grid.erode(self.grid.dilate(m))), 1)
        edge = self.mask([(7, 0)])
        self.assertEqual(metrics.count(self.grid.dilate(edge)), 4)  # pas de debordement de ligne

    def test_iou_dice(self):
        a, b = self.mask([(0, 0), (1, 0)]), self.mask([(1, 0), (2, 0)])
        self.assertAlmostEqual(metrics.iou(a, b), 1 / 3)
        self.assertAlmostEqual(metrics.dice(a, b), 0.5)
        self.assertIsNone(metrics.iou(0, 0))

    def test_holes_and_components(self):
        ring = bytearray(36)
        for x in range(1, 5):
            for y in range(1, 5):
                ring[y * 6 + x] = 1
        ring[2 * 6 + 2] = ring[2 * 6 + 3] = ring[3 * 6 + 2] = ring[3 * 6 + 3] = 0
        self.assertEqual(metrics.components_and_holes(ring, 6, 6), (1, 1))

    def test_gate_blocks_missing_required_object(self):
        def score(sid, fam, req, present):
            return metrics.ObjectScore(sid, fam, "area", req, True, present, 1, 1, 1, 1, "boundary_f",
                                       1, 1, 1, None, None, None)
        scores = [score("lighthouse.tower", "lighthouse", True, True), score("boat.hull", "boat", True, False)]
        gate = metrics.semantic_gate(scores, ["boat", "lighthouse"])
        self.assertFalse(gate["passed"])
        self.assertEqual(gate["required_missing"], ["boat.hull"])


class RunnerTests(unittest.TestCase):
    def test_controls_validate_the_metric(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = run_bench(Path(tmp), "--backends", "", "--controls")
            oracle, lh = result_of(report, "oracle"), result_of(report, "lighthouse_only")
            self.assertTrue(oracle["gate"]["passed"])
            self.assertEqual(oracle["aggregate"]["mean_union_iou_protected"], 1.0)
            self.assertEqual(oracle["aggregate"]["holes_lost"], [])
            # critere produit principal : phare parfait mais voilier absent = echec bloquant
            self.assertFalse(lh["gate"]["passed"])
            self.assertIn("boat.hull", lh["gate"]["required_missing"])
            self.assertTrue(all(o["present"] for o in lh["objects"] if o["family"] == "lighthouse"))
            # niveau famille : le voilier est absent, le phare present
            self.assertTrue(oracle["family_gate"]["passed"])
            self.assertFalse(lh["family_gate"]["passed"])
            self.assertEqual(lh["family_gate"]["main_missing"], ["boat"])
            self.assertTrue((Path(tmp) / "summary.md").is_file())
            self.assertTrue((Path(tmp) / "compare_marine-synth-01_oracle.png").is_file())

    def test_missing_cli_and_weights_are_explicit_skips(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = run_bench(Path(tmp), "--backends", "cielab,sam2", "--cli", str(Path(tmp) / "absent"))
            for name in ("cielab", "sam2"):
                r = result_of(report, name)
                self.assertEqual(r["status"], "skipped")
                self.assertTrue(r["reason"])
                self.assertIsNone(r["aggregate"])  # jamais de metrique inventee

    def test_report_is_byte_reproducible(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            run_bench(Path(a), "--backends", "", "--controls")
            run_bench(Path(b), "--backends", "", "--controls")
            self.assertEqual((Path(a) / "report.json").read_bytes(), (Path(b) / "report.json").read_bytes())

    def test_tampered_corpus_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / "corpus"
            shutil.copytree(CORPUS, copy)
            victim = copy / "marine-synth-01" / "source.png"
            victim.write_bytes(victim.read_bytes()[:-1] + b"\x00")
            with self.assertRaises(bench.ManifestError):
                bench.load_manifest(copy / "manifest.json")

    def test_path_traversal_in_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / "corpus"
            shutil.copytree(CORPUS, copy)
            doc = json.loads((copy / "manifest.json").read_text(encoding="utf-8"))
            doc["cases"][0]["source"]["path"] = "../escape.png"
            (copy / "manifest.json").write_text(json.dumps(doc), encoding="utf-8")
            with self.assertRaises(bench.ManifestError):
                bench.load_manifest(copy / "manifest.json")

    def test_pipeline_without_commands_is_an_explicit_skip(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg = Path(tmp) / "pipelines.json"
            cfg.write_text(json.dumps({"plan+seg": {}}), encoding="utf-8")
            report = run_bench(Path(tmp) / "out", "--backends", "plan+seg", "--pipelines", str(cfg))
            r = result_of(report, "plan+seg")
            self.assertEqual(r["status"], "skipped")
            self.assertIn("non configuree", r["reason"])

    def test_unknown_backend_is_a_usage_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(bench.main(["--out", tmp, "--backends", "nope"]), 2)


class Sam2BackendTests(unittest.TestCase):
    """Chemin SAM 2 complet (protocole v1) contre un faux worker, sans poids ni torch."""

    def models(self, tmp: Path) -> str:
        d = tmp / "models"
        d.mkdir()
        (d / "sam2.1_hiera_small.pt").write_bytes(b"fake-weights")
        return str(d)

    def test_fake_worker_end_to_end(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            backend = bk.Sam2(self.models(tmp), worker=FAKE_WORKER, timeout=60)
            manifest = bench.load_manifest(CORPUS / "manifest.json")
            entry, res = bench.evaluate(manifest["cases"][0], CORPUS, backend, tmp / "w", 0.5)
            self.assertEqual(entry["status"], "ok", entry["reason"])
            self.assertEqual(entry["meta"]["worker_protocol_version"], 1)
            self.assertEqual(len(entry["meta"]["model_sha256"]), 64)
            # un masque par composante de couleur : le phare et le voilier sont retrouves
            self.assertGreater(entry["aggregate"]["regions"], 10)
            self.assertTrue(next(o for o in entry["objects"] if o["semantic_id"] == "lighthouse.lantern")["present"])

    def test_worker_error_becomes_skip_with_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            backend = bk.Sam2(self.models(tmp), worker=FAKE_WORKER, timeout=60)
            # le faux worker lit --fail-load dans argv : on le relaie par un script intermediaire
            shim = tmp / "shim.py"
            shim.write_text(
                "import sys, runpy\nsys.argv += ['--fail-load', 'SAM2_NOT_INSTALLED']\n"
                f"runpy.run_path({str(FAKE_WORKER)!r}, run_name='__main__')\n", encoding="utf-8")
            backend.worker = shim
            manifest = bench.load_manifest(CORPUS / "manifest.json")
            entry, _ = bench.evaluate(manifest["cases"][0], CORPUS, backend, tmp / "w", 0.5)
            self.assertEqual(entry["status"], "skipped")
            self.assertIn("SAM2_NOT_INSTALLED", entry["reason"])

    def test_missing_checkpoint_is_a_skip(self):
        with tempfile.TemporaryDirectory() as tmp:
            empty = Path(tmp) / "empty"
            empty.mkdir()
            manifest = bench.load_manifest(CORPUS / "manifest.json")
            entry, _ = bench.evaluate(manifest["cases"][0], CORPUS, bk.Sam2(str(empty)), Path(tmp) / "w", 0.5)
            self.assertEqual(entry["status"], "skipped")
            self.assertIn("checkpoint absent", entry["reason"])


class CielabBackendTests(unittest.TestCase):
    def test_regions_png_components_and_project_check(self):
        class FakeCli(bk.Cielab):
            def __init__(self):
                super().__init__("python")  # chemin resolu ; _run est remplace

            def _run(self, args):
                import subprocess
                if args[0:2] == ["project", "digitize"]:
                    out = Path(args[args.index("--debug-dir") + 1])
                    out.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(args[2], out / "regions.png")  # la source sert de "carte de regions"
                    return subprocess.CompletedProcess(args, 0, "", "")
                return subprocess.CompletedProcess(
                    args, 0, json.dumps({"summary": {"errors": 0, "warnings": 1}, "sequence": {"stitches": 5}}), "")

        with tempfile.TemporaryDirectory() as tmp:
            manifest = bench.load_manifest(CORPUS / "manifest.json")
            entry, _ = bench.evaluate(manifest["cases"][0], CORPUS, FakeCli(), Path(tmp), 0.5)
            self.assertEqual(entry["status"], "ok", entry["reason"])
            self.assertEqual(entry["project_check"]["summary"]["warnings"], 1)
            # composantes de couleur exacte de la source = quasi-oracle : les sujets principaux sont presents
            self.assertTrue(entry["gate"]["passed"])


if __name__ == "__main__":
    unittest.main()
