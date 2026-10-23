"""Local semantic replacement safety tests."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from openstitch_semantic_replace import semantic_objects


class ReplaceTest(unittest.TestCase):
    def test_exact_name_prefix(self):
        data = {"embroidery_objects": [
            {"id": 1, "name": "sem:morse12 1"},
            {"id": 2, "name": "sem:morse123 1"},
            {"id": 3, "name": "other"},
            {"id": 4, "name": "sem:morse12 2"},
        ]}
        self.assertEqual([x["id"] for x in semantic_objects(data, "morse12")], [1, 4])


if __name__ == "__main__":
    unittest.main()
