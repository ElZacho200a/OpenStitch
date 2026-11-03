import csv
from pathlib import Path
import tempfile
import unittest

from physical_matrix import FIELDS, cases, validate, write_template


class PhysicalMatrixTests(unittest.TestCase):
    def test_deterministic_grid(self):
        rows = list(cases())
        self.assertEqual(len(rows), 51)
        self.assertEqual(len({row[0] for row in rows}), len(rows))

    def test_blank_template_is_valid_but_not_measured(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trials.csv"
            write_template(path)
            before = path.read_bytes()
            errors, done, total = validate(path)
            self.assertEqual((errors, done, total), ([], 0, 51))
            with self.assertRaises(FileExistsError):
                write_template(path)
            self.assertEqual(before, path.read_bytes())

    def test_partial_measurement_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trials.csv"
            write_template(path)
            with path.open(newline="", encoding="utf-8") as file:
                rows = list(csv.DictReader(file))
            rows[0]["actual_mm"] = "2.5"
            with path.open("w", newline="", encoding="utf-8") as file:
                writer = csv.DictWriter(file, fieldnames=FIELDS)
                writer.writeheader()
                writer.writerows(rows)
            errors, _, _ = validate(path)
            self.assertTrue(any("partial observation" in e for e in errors))

    def test_complete_measured_case_counts_as_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trials.csv"
            write_template(path)
            with path.open(newline="", encoding="utf-8") as file:
                rows = list(csv.DictReader(file))
            rows[0].update(machine="Test machine", thread="40wt", needle="75/11",
                           stabilizer="cutaway", osp_commit="abc123",
                           dst_sha256="a" * 64, actual_mm="1.05", photo="front.jpg")
            with path.open("w", newline="", encoding="utf-8") as file:
                writer = csv.DictWriter(file, fieldnames=FIELDS)
                writer.writeheader()
                writer.writerows(rows)
            errors, done, total = validate(path)
            self.assertEqual((errors, done, total), ([], 1, 51))

    def test_rejects_missing_duplicate_and_nonfinite_measurement(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trials.csv"
            write_template(path)
            with path.open(newline="", encoding="utf-8") as file:
                rows = list(csv.DictReader(file))
            rows[0]["actual_mm"] = "nan"
            rows.pop()
            rows.append(rows[0].copy())
            with path.open("w", newline="", encoding="utf-8") as file:
                writer = csv.DictWriter(file, fieldnames=FIELDS)
                writer.writeheader()
                writer.writerows(rows)
            errors, _, _ = validate(path)
            self.assertTrue(any("duplicate" in e for e in errors))
            self.assertTrue(any("finite and positive" in e for e in errors))
            self.assertTrue(any("missing" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
