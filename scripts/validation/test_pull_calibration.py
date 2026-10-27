"""Deterministic tests for the experimental pull calibration tool."""
import csv
import tempfile
import unittest
from pathlib import Path

from pull_calibration import FIELDS, fit, interpolate, template


class CalibrationTests(unittest.TestCase):
    def test_template_does_not_overwrite_measurements(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "measurements.csv"
            template(path)
            self.assertEqual(sum(1 for _ in path.open(encoding="utf-8")), 55)
            with self.assertRaises(FileExistsError):
                template(path)
            with self.assertRaisesRegex(ValueError, "no measured"):
                fit(path)

    def test_measurement_estimate_and_interpolation(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "measurements.csv"
            with path.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.DictWriter(stream, FIELDS)
                writer.writeheader()
                for width, actual in ((2, 1.8), (4, 3.6)):
                    writer.writerow(dict(fabric="cotton", machine="M", stabilizer="S",
                                         thread="T", needle="N", family="satin",
                                         nominal_mm=width, applied_pull_mm=0,
                                         actual_mm=actual))
            result = fit(path)
            self.assertFalse(result["validated"])
            points = result["profiles"][0]["points"]
            self.assertEqual([p["pull_per_side_mm"] for p in points], [0.1, 0.2])
            self.assertAlmostEqual(interpolate(points, 3.0), 0.15)
            self.assertAlmostEqual(interpolate(points, 10.0), 0.2)
            self.assertEqual(fit(path), result)


if __name__ == "__main__":
    unittest.main()
