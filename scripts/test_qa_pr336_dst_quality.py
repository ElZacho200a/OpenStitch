#!/usr/bin/env python3
"""Regression tests for PR336 DST travel metrics (no CLI required)."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("qa_pr336_gradients.py")
spec = importlib.util.spec_from_file_location("qa_pr336_gradients", SCRIPT)
qa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qa)


class DstQualityTests(unittest.TestCase):
    def test_separates_jump_travel_from_needle_down(self):
        # Tajima DST: dx=+0.1 mm, dy=0; jump bit 0x80.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.dst"
            path.write_bytes(b" " * 512 + bytes([
                0x01, 0x00, 0x83,  # jump
                0x01, 0x00, 0x03,  # needle down
                0x00, 0x00, 0xF3,  # end
            ]))
            stats = qa.dst_quality(path)
        self.assertEqual(stats["jumps"], 1)
        self.assertEqual(stats["stitches"], 1)
        self.assertEqual(stats["jump_ratio"], 1.0)
        self.assertEqual(stats["jump_distance_mm"], 0.1)
        self.assertEqual(stats["max_jump_step_mm"], 0.1)
        self.assertEqual(stats["max_needle_step_mm"], 0.1)
        self.assertEqual(stats["diagonals_over_5mm"], 0)

    def test_no_needle_down_has_no_jump_ratio(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "only_jump.dst"
            path.write_bytes(b" " * 512 + bytes([0x01, 0x00, 0x83, 0, 0, 0xF3]))
            stats = qa.dst_quality(path)
        self.assertIsNone(stats["jump_ratio"])
        self.assertEqual(stats["jumps"], 1)
        self.assertEqual(stats["stitches"], 0)


if __name__ == "__main__":
    unittest.main()
