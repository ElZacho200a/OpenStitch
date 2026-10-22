"""Tests for per-object quality annotations."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from openstitch_semantic_review import issues_for_object, geometry_to_rect


class SemanticReviewTest(unittest.TestCase):
    def test_coverage_and_overflow(self):
        item = {"measured": True, "covered_ratio": 0.8,
                "overflow_ratio": 0.4, "findings_count": 2}
        self.assertEqual(issues_for_object(item),
                         ["coverage", "overflow", "quality"])

    def test_unmeasured_running_has_no_coverage_alert(self):
        self.assertEqual(issues_for_object({"measured": False, "findings_count": 0}), [])

    def test_geometry_mapping(self):
        bounds = {"min_mm": [-10, 0], "max_mm": [0, 10]}
        rect = geometry_to_rect(bounds, (-10, 0, 10, 10), (200, 100))
        self.assertEqual(rect, (0.0, 0.0, 100.0, 100.0))


if __name__ == "__main__":
    unittest.main()
