import importlib.util
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "openstitch_independent_judge.py"
spec = importlib.util.spec_from_file_location("judge", SCRIPT)
judge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(judge)

class IndependentJudgeTests(unittest.TestCase):
    def test_rubric_total(self):
        self.assertEqual(sum(judge.RUBRIC.values()), 20)
    def test_reject_unknown_image_type(self):
        with self.assertRaises(ValueError):
            judge.data_url(Path("bad.svg"))
    def test_artifacts_contract(self):
        self.assertIn("preview", judge.REQUIRED)
        self.assertIn("osp", judge.REQUIRED)
        self.assertIn("source", judge.REQUIRED)

if __name__ == "__main__":
    unittest.main()
