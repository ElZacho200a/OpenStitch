"""Tests for the semantic import agent bridge."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import openstitch_semantic_import as tool


class SemanticImportTest(unittest.TestCase):
    def test_occlusion_topological_sort(self):
        scene = [{"semantic_id": "front", "over": ["back"]},
                 {"semantic_id": "back"}]
        self.assertEqual([x["semantic_id"] for x in tool.ordering(scene)],
                         ["back", "front"])

    def test_reject_cycle(self):
        with self.assertRaises(ValueError):
            tool.ordering([{"semantic_id": "a", "under": ["b"]},
                           {"semantic_id": "b", "under": ["a"]}])

    def test_import_real_cli(self):
        cli = os.environ.get("OPENSTITCH_CLI")
        if not cli:
            self.skipTest("OPENSTITCH_CLI not set")
        try:
            import cv2
            import numpy as np
        except ImportError:
            self.skipTest("opencv-python-headless unavailable")
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            mask = np.zeros((40, 50), dtype="uint8")
            mask[5:35, 10:40] = 255
            cv2.imwrite(str(folder / "mask.png"), mask)
            scene = {"schema": "openstitch.semantic/1",
                     "source_image": {"width_px": 50, "height_px": 40},
                     "design": {"width_mm": 100, "height_mm": 80},
                     "regions": [{"semantic_id": "walrus.body", "family": "walrus",
                                  "role": "body", "priority": "protected",
                                  "thread_rgb": "#E08040", "mask_path": "mask.png",
                                  "stitch_intent": "tatami"}]}
            (folder / "scene.json").write_text(json.dumps(scene))
            output = folder / "scene.osp"
            mapping = tool.import_manifest(folder / "scene.json", output, cli)
            self.assertIn("walrus.body", mapping["objects"])
            inspection = tool.call(cli, "project", "inspect", str(output))
            self.assertTrue(inspection["embroidery_objects"])
            self.assertTrue(all(obj["name"].startswith("sem:walrus.body")
                                for obj in inspection["embroidery_objects"]))
            with self.assertRaises(FileExistsError):
                tool.import_manifest(folder / "scene.json", output, cli)


if __name__ == "__main__":
    unittest.main()
