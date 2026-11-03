"""Tests for semantic bulk selection."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from openstitch_semantic_bulk import choose


class SemanticBulkTest(unittest.TestCase):
    def setUp(self):
        self.inspect = {"embroidery_objects": [
            {"id": 9, "name": "sem:morse12.body 1", "type": "tatami"},
            {"id": 10, "name": "sem:morse12.tusk 1", "type": "auto_satin"},
            {"id": 11, "name": "sem:morse123.body 1", "type": "tatami"},
            {"id": 12, "name": "other", "type": "running"},
        ]}
        self.mapping = {"objects": {
            "morse12.body": {"family": "morse"},
            "morse12.tusk": {"family": "morse"},
            "morse123.body": {"family": "rock"},
        }}

    def test_family_and_prefix(self):
        selected = choose(self.inspect, self.mapping, family="morse", prefix="morse12")
        self.assertEqual([o["id"] for o in selected], [9, 10])

    def test_type_and_prefix_do_not_overmatch(self):
        selected = choose(self.inspect, self.mapping, prefix="morse12", stitch_type="tatami")
        self.assertEqual([o["id"] for o in selected], [9])

    def test_selector_required(self):
        with self.assertRaises(ValueError):
            choose(self.inspect, self.mapping)


if __name__ == "__main__":
    unittest.main()
