"""Tests sans dépendances ni poids téléchargés pour IA-AUTO-01 L2."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "auto_digitize_ranker.py"
spec = importlib.util.spec_from_file_location("auto_digitize_ranker", SCRIPT)
ranker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ranker)


def candidate(name, technique, good):
    features = {
        "coverage_ratio": 0.96 if good else 0.63,
        "detail_retention_ratio": 0.95 if good else 0.58,
        "angle_coherence_ratio": 0.90 if good else 0.40,
        "short_stitch_ratio": 0.01 if good else 0.21,
        "overdensity_ratio": 0.03 if good else 0.37,
        "uncovered_ratio": 0.02 if good else 0.27,
        "travel_mm": 11.0 if good else 250.0,
        "trim_count": 1 if good else 40,
        "stitch_count": 350 if good else 1300,
    }
    return {"id": name, "technique": technique, "features": features}


def sample(index=0, kind="synthetic"):
    # Winner technique alternates: classifier cannot succeed by memorizing a family.
    technique = "tatami" if index % 2 else "auto_satin"
    other = "auto_satin" if index % 2 else "tatami"
    return {
        "schema": ranker.SAMPLE_SCHEMA,
        "sample_id": "sample-" + str(index),
        "motif_group": "motif-" + str(index),
        "semantic_id": "object-1",
        "candidates": [candidate("approved", technique, True),
                       candidate("rejected", other, False)],
        "preferred_id": "approved",
        "provenance": {"source_kind": kind, "rights": "own",
                       "opt_in": kind == "human_confirmed"},
    }


class RankerTest(unittest.TestCase):
    def test_valid_example(self):
        ranker.check_sample(sample())

    def test_duplicate_candidate_id_rejected(self):
        row = sample()
        row["candidates"][1]["id"] = "approved"
        with self.assertRaisesRegex(ValueError, "duplicate"):
            ranker.check_sample(row)

    def test_missing_feature_rejected_not_imputed_as_zero(self):
        row = sample()
        del row["candidates"][0]["features"]["coverage_ratio"]
        with self.assertRaisesRegex(ValueError, "exactly"):
            ranker.check_sample(row)

    def test_nan_and_negative_values_rejected(self):
        for value in (float("nan"), float("inf"), -1.0):
            row = sample()
            row["candidates"][0]["features"]["travel_mm"] = value
            with self.assertRaises(ValueError):
                ranker.check_sample(row)

    def test_unconsented_human_annotation_rejected(self):
        row = sample(kind="human_confirmed")
        row["provenance"]["opt_in"] = False
        with self.assertRaisesRegex(ValueError, "opt_in"):
            ranker.check_sample(row)

    def test_preferred_candidate_must_exist(self):
        row = sample()
        row["preferred_id"] = "ghost"
        with self.assertRaisesRegex(ValueError, "preferred_id"):
            ranker.check_sample(row)

    def test_split_has_no_motif_leakage(self):
        rows = [sample(i) for i in range(12)]
        a, b = ranker.split_by_motif(rows)
        self.assertTrue(a and b)
        self.assertFalse({r["motif_group"] for r in a} & {r["motif_group"] for r in b})
        self.assertEqual(len(a) + len(b), 12)

    def test_split_refuses_single_group(self):
        with self.assertRaisesRegex(ValueError, "two distinct"):
            ranker.split_by_motif([sample()])

    def test_training_deterministic_and_holdout(self):
        rows = [sample(i) for i in range(12)]
        one = ranker.train_model(rows, epochs=250)
        two = ranker.train_model(list(reversed(rows)), epochs=250)
        self.assertEqual(one, two)
        self.assertEqual(one["training"]["heldout_metrics"]["top1_accuracy"], 1.0)
        self.assertTrue(one["training"]["synthetic_only"])

    def test_model_is_versioned_and_strict(self):
        row = sample(1)
        model = ranker.train_model([sample(i) for i in range(6)], epochs=100)
        self.assertEqual(ranker.rank(model, row)[0]["id"], "approved")
        model["feature_names"] = ["unexpected"]
        with self.assertRaisesRegex(ValueError, "incompatible"):
            ranker.rank(model, row)

    def test_cli_validate_train_rank_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            samples = root / "samples.jsonl"
            model = root / "model.json"
            sample_path = root / "query.json"
            samples.write_text("\n".join(json.dumps(sample(i)) for i in range(8)),
                               encoding="utf-8")
            sample_path.write_text(json.dumps(sample()), encoding="utf-8")
            self.assertEqual(ranker.main(["validate", str(samples)]), 0)
            self.assertEqual(ranker.main(["train", str(samples), str(model), "--epochs", "50"]), 0)
            self.assertEqual(ranker.main(["rank", str(model), str(sample_path)]), 0)
            self.assertEqual(ranker.main(["train", str(samples), str(model)]), 2)

    def test_duplicate_sample_id_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / "data.jsonl"
            file.write_text(json.dumps(sample()) + "\n" + json.dumps(sample()), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "duplicate sample_id"):
                ranker.load_jsonl(file)

    def test_model_contains_only_aggregate_metadata(self):
        rows = [sample(i, kind="human_confirmed") for i in range(6)]
        model = ranker.train_model(rows, epochs=5)
        serialized = json.dumps(model)
        self.assertNotIn("object-1", serialized)
        self.assertNotIn("motif-1", serialized)
        self.assertNotIn("sample-1", serialized)
        self.assertFalse(model["training"]["synthetic_only"])


if __name__ == "__main__":
    unittest.main()
