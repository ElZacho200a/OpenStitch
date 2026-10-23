import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "openstitch_evaluate.py"
spec = importlib.util.spec_from_file_location("evaluator", SCRIPT)
ev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ev)

class SelfReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        artifacts = {}
        for key in ev.ARTIFACTS:
            name = key + (".json" if key.endswith("_report") else ".dat")
            (self.base / name).write_text("{}" if key.endswith("_report") else "data")
            artifacts[key] = name
        self.review = {
            "schema": ev.SCHEMA, "evaluation_mode": "self_review",
            "source_id": "turner", "size_mm": [363.55, 240],
            "digitizer": "agent-1", "reviewer": "agent-1", "machine_tested": False,
            "artifacts": artifacts,
            "checks": {key: {"status": "pass", "evidence": "Actual CLI output was examined"} for key in ev.CHECKS},
            "protected_details": [{"name": "phare", "preserved": True, "critical": True}],
            "rubric": {key: {"score": value, "evidence": "Comparison of original and real OpenStitch render"} for key, value in ev.WEIGHTS.items()},
            "visual_review": "The lighthouse is visible but clouds are angular and the waves look like horizontal bands. The lighting has been lost.",
            "comparison_method": "Side by side inspection of source and real stitched preview"
        }
    def tearDown(self):
        self.temp.cleanup()
    def test_self_review_score_is_indicative(self):
        result = ev.evaluate(self.review, self.base)
        self.assertEqual(result["score"], 20)
        self.assertEqual(result["score_kind"], "indicative_self_review")
        self.assertEqual(result["next_action"], "request_independent_review")
    def test_missing_critique_blocks(self):
        self.review["visual_review"] = ""
        self.assertIsNone(ev.evaluate(self.review, self.base)["score"])
    def test_independent_requires_distinct_reviewer(self):
        self.review["evaluation_mode"] = "independent"
        self.assertIsNone(ev.evaluate(self.review, self.base)["score"])
    def test_critical_detail_caps_score(self):
        self.review["protected_details"][0]["preserved"] = False
        result = ev.evaluate(self.review, self.base)
        self.assertEqual(result["score"], 9)
        self.assertEqual(result["status"], "blocked")

if __name__ == "__main__":
    unittest.main()
