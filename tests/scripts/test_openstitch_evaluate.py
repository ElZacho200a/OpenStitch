"""Tests de la notation OpenStitch : aucun reseau et aucun moteur IA."""
from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "openstitch_evaluate.py"
spec = importlib.util.spec_from_file_location("openstitch_evaluate", SCRIPT)
grader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(grader)


class EvaluateTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        artifacts = {}
        for key in grader.ARTIFACTS:
            suffix = ".json" if key.endswith("_report") else ".bin"
            p = self.root / (key + suffix)
            p.write_text("{}" if suffix == ".json" else "test", encoding="utf-8")
            artifacts[key] = p.name
        self.data = {
            "schema": grader.SCHEMA,
            "source_id": "benchmark-feathers-01",
            "size_mm": [240, 240],
            "artifacts": artifacts,
            "checks": {key: {"status": "pass", "evidence": "Commande CLI reelle, journal conserve"}
                       for key in grader.CHECKS},
            "protected_details": [{"name": "oeil", "preserved": True, "critical": True}],
            "reviewer": "vision-judge-session-b",
            "digitizer": "digitizer-session-a",
            "machine_tested": False,
            "rubric": {key: {"score": maximum, "evidence": "Inspection source et rendu sur zones importantes"}
                       for key, maximum in grader.WEIGHTS.items()},
        }

    def test_perfect_score_is_software_only(self):
        r = grader.evaluate(self.data, self.root)
        self.assertEqual(r["status"], "accepted")
        self.assertEqual(r["score"], 20)
        self.assertEqual(r["validation_level"], "software_only")

    def test_improvement_below_threshold(self):
        self.data["rubric"]["fidelity"]["score"] = 1.5
        r = grader.evaluate(self.data, self.root)
        self.assertEqual(r["status"], "improve")
        self.assertEqual(r["score"], 16.5)

    def test_critical_identity_loss_caps_score(self):
        self.data["protected_details"][0]["preserved"] = False
        r = grader.evaluate(self.data, self.root)
        self.assertEqual(r["status"], "blocked")
        self.assertEqual(r["raw_score"], 20)
        self.assertEqual(r["score"], 9)
        self.assertIn("oeil", r["blockers"][0])

    def test_cli_failure_blocks_delivery(self):
        self.data["checks"]["project_check"]["status"] = "fail"
        r = grader.evaluate(self.data, self.root)
        self.assertEqual(r["status"], "blocked")
        self.assertEqual(r["next_action"], "fix_blockers")

    def test_no_grade_without_visual_review(self):
        del self.data["rubric"]["aesthetics"]
        r = grader.evaluate(self.data, self.root)
        self.assertEqual(r["status"], "incomplete")
        self.assertIsNone(r["score"])

    def test_missing_source_and_unrun_checks(self):
        (self.root / self.data["artifacts"]["source"]).unlink()
        self.data["checks"]["project_diagnose"]["status"] = "not_run"
        r = grader.evaluate(self.data, self.root)
        self.assertEqual(r["status"], "incomplete")
        self.assertTrue(any("source" in s for s in r["errors"]))

    def test_reject_score_out_of_range_and_same_reviewer(self):
        self.data["rubric"]["fidelity"]["score"] = 6
        self.data["reviewer"] = self.data["digitizer"]
        r = grader.evaluate(self.data, self.root)
        self.assertEqual(r["status"], "incomplete")
        self.assertIsNone(r["score"])

    def test_cli_json_exit_code(self):
        f = self.root / "review.json"
        f.write_text(json.dumps(self.data), encoding="utf-8")
        p = subprocess.run([sys.executable, str(SCRIPT), str(f)],
                           capture_output=True, text=True, check=False)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(json.loads(p.stdout)["score"], 20)


if __name__ == "__main__":
    unittest.main()
