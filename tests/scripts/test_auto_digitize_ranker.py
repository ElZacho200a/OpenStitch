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

    def test_direct_training_rejects_duplicate_ids(self):
        rows = [sample(i) for i in range(8)]
        rows[1]["sample_id"] = rows[0]["sample_id"]
        with self.assertRaisesRegex(ValueError, "duplicate sample_id"):
            ranker.train_model(rows, epochs=1)

    def test_direct_training_rejects_unconsented_or_invalid_rows(self):
        rows = [sample(i) for i in range(8)]
        rows[0]["provenance"]["source_kind"] = "human_confirmed"
        rows[0]["provenance"]["opt_in"] = False
        with self.assertRaisesRegex(ValueError, "opt_in"):
            ranker.train_model(rows, epochs=1)
        rows[0]["provenance"]["opt_in"] = True
        rows[0]["candidates"][0]["features"]["travel_mm"] = float("nan")
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            ranker.train_model(rows, epochs=1)

    def test_direct_fit_rejects_invalid_rows(self):
        with self.assertRaisesRegex(ValueError, "no labeled samples"):
            ranker.fit([])
        row = sample()
        row["provenance"]["source_kind"] = "human_confirmed"
        row["provenance"]["opt_in"] = False
        with self.assertRaisesRegex(ValueError, "opt_in"):
            ranker.fit([row], epochs=1)
        row = sample()
        row["candidates"][0]["features"]["travel_mm"] = float("nan")
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            ranker.fit([row], epochs=1)

    def test_fit_rejects_nonfinite_and_boolean_hyperparameters(self):
        for kwargs in ({"epochs": True}, {"epochs": 1.5},
                       {"learning_rate": float("nan")},
                       {"learning_rate": float("inf")},
                       {"l2": float("nan")}, {"l2": True}):
            with self.subTest(kwargs=kwargs):
                with self.assertRaisesRegex(ValueError, "hyperparameters"):
                    ranker.fit([sample()], **kwargs)

    def test_metrics_empty_dataset_has_controlled_error(self):
        with self.assertRaisesRegex(ValueError, "no labeled samples"):
            ranker.metrics([], None)
        with self.assertRaisesRegex(ValueError, "no labeled samples"):
            ranker.metrics([], {"schema": ranker.MODEL_SCHEMA})

    def test_direct_training_rejects_empty_dataset(self):
        with self.assertRaisesRegex(ValueError, "no labeled samples"):
            ranker.train_model([], epochs=1)

    def test_paired_audit_groups_by_preferred_not_predicted_technique(self):
        rows = [sample(i) for i in range(4)]
        model = ranker.train_model([sample(i) for i in range(8)], epochs=30)
        audit = ranker.paired_evaluation(rows, model)
        self.assertEqual(audit["samples"], 4)
        self.assertEqual(audit["motif_groups"], 4)
        self.assertEqual(audit["model_only_correct"], 0)
        self.assertEqual(audit["heuristic_only_correct"], 0)
        self.assertEqual(audit["choice_disagreements"], 0)
        self.assertEqual(set(audit["by_preferred_technique"]),
                         {"auto_satin", "tatami"})
        self.assertEqual(audit["by_preferred_technique"]["tatami"]["samples"], 2)
        self.assertEqual(audit, ranker.paired_evaluation(list(reversed(rows)), model))

    def test_paired_audit_exposes_model_errors(self):
        rows = [sample(0), sample(1)]
        model = ranker.train_model([sample(i) for i in range(8)], epochs=30)
        model["weights"] = [-1.0] + [0.0] * (len(ranker.FEATURE_NAMES) - 1)
        audit = ranker.paired_evaluation(rows, model)
        self.assertEqual(audit["samples"], 2)
        self.assertEqual(audit["heuristic_only_correct"], 2)
        self.assertEqual(audit["model_only_correct"], 0)
        self.assertEqual(audit["choice_disagreements"], 2)
        with self.assertRaisesRegex(ValueError, "no labeled samples"):
            ranker.paired_evaluation([], model)

    def test_training_ignores_candidate_presentation_order(self):
        rows = [sample(i) for i in range(12)]
        for row in rows:
            row["candidates"].append(candidate("third", "running", False))
            row["candidates"][2]["features"]["travel_mm"] += 17.0
        reordered = json.loads(json.dumps(rows))
        for row in reordered:
            row["candidates"].reverse()
        original = ranker.train_model(rows, epochs=80)
        reversed_candidates = ranker.train_model(reordered, epochs=80)
        self.assertEqual(original, reversed_candidates)
        self.assertEqual(original["training"]["dataset_sha256"],
                         reversed_candidates["training"]["dataset_sha256"])

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

    def test_non_object_model_payload_is_rejected(self):
        row = sample()
        for malformed in (None, [], "model", 42):
            with self.subTest(malformed=malformed):
                with self.assertRaisesRegex(ValueError, "model must be a JSON object"):
                    ranker.rank(malformed, row)
                with self.assertRaisesRegex(ValueError, "model must be a JSON object"):
                    ranker.assess_candidates(malformed, row)

    def test_nonfinite_candidate_score_rejected(self):
        row = sample()
        model = ranker.train_model([sample(i) for i in range(6)], epochs=5)
        model["weights"] = [1e308] * len(ranker.FEATURE_NAMES)
        with self.assertRaisesRegex(ValueError, "nonfinite candidate score"):
            ranker.rank(model, row)

    def test_comparison_reports_both_choices_without_mutation(self):
        row = sample()
        before = json.dumps(row, sort_keys=True)
        model = ranker.train_model([sample(i) for i in range(8)], epochs=100)
        result = ranker.compare_candidates(model, row)
        self.assertEqual(result["schema"], "openstitch.ai.candidate-comparison/1")
        self.assertEqual(result["model_choice"], "approved")
        self.assertEqual(result["heuristic_choice"], "approved")
        self.assertTrue(result["agree"])
        self.assertFalse(result["applied"])
        self.assertEqual(json.dumps(row, sort_keys=True), before)
        self.assertEqual({c["id"] for c in result["ranking"]},
                         {"approved", "rejected"})

    def test_assessment_safety_gates_reject_unsafe_candidates(self):
        row = sample()
        row["candidates"][0]["features"]["coverage_ratio"] = 0.1
        row["candidates"][1]["features"] = dict(candidate("safe", "tatami", True)["features"])
        model = ranker.train_model([sample(i) for i in range(8)], epochs=50)
        result = ranker.assess_candidates(model, row)
        self.assertEqual(result["decision"], "fallback")
        self.assertEqual(result["reason"], "synthetic_or_unverified_model")
        self.assertEqual(result["rejected"][0]["id"], "approved")
        self.assertFalse(result["applied"])

    def test_assessment_abstains_when_all_candidates_unsafe(self):
        row = sample()
        for candidate in row["candidates"]:
            candidate["features"]["coverage_ratio"] = 0.1
        model = ranker.train_model([sample(i) for i in range(8)], epochs=50)
        result = ranker.assess_candidates(model, row)
        self.assertEqual(result["decision"], "abstain")
        self.assertIsNone(result["selected_id"])

    def test_assessment_low_margin_uses_baseline(self):
        row = sample()
        model = ranker.train_model([sample(i) for i in range(8)], epochs=50)
        row["candidates"][1]["features"] = dict(candidate("safe", "tatami", True)["features"])
        result = ranker.assess_candidates(model, row, min_margin=1.0)
        self.assertEqual(result["decision"], "fallback")
        self.assertEqual(result["selected_id"], result["heuristic_choice"])
        self.assertEqual(result["reason"], "synthetic_or_unverified_model")

    def test_synthetic_only_model_never_recommends_even_with_large_margin(self):
        model = ranker.train_model([sample(i) for i in range(8)], epochs=50)
        row = sample()
        row["candidates"][1]["features"] = dict(candidate("safe", "tatami", True)["features"])
        result = ranker.assess_candidates(model, row, min_margin=0)
        self.assertEqual(result["decision"], "fallback")
        self.assertEqual(result["reason"], "synthetic_or_unverified_model")
        self.assertEqual(result["selected_id"], result["heuristic_choice"])
        self.assertFalse(result["applied"])

    def test_mixed_training_cannot_promote_synthetic_preferences(self):
        rows = [sample(i, "human_confirmed" if i == 0 else "synthetic")
                for i in range(8)]
        model = ranker.train_model(rows, epochs=20)
        self.assertFalse(model["training"]["synthetic_only"])
        self.assertFalse(model["training"]["human_only"])
        row = sample()
        row["candidates"][1]["features"] = dict(row["candidates"][0]["features"])
        result = ranker.assess_candidates(model, row, min_margin=0)
        self.assertEqual(result["decision"], "fallback")
        self.assertEqual(result["reason"], "synthetic_or_unverified_model")
        self.assertFalse(result["applied"])

    def test_legacy_model_without_human_only_is_unverified(self):
        model = ranker.train_model([sample(i, "human_confirmed") for i in range(8)],
                                   epochs=20)
        self.assertTrue(model["training"]["human_only"])
        del model["training"]["human_only"]
        row = sample()
        row["candidates"][1]["features"] = dict(row["candidates"][0]["features"])
        result = ranker.assess_candidates(model, row, min_margin=0)
        self.assertEqual(result["decision"], "fallback")
        self.assertEqual(result["reason"], "synthetic_or_unverified_model")

    def test_quality_pareto_gate_blocks_dominated_model_choice(self):
        row = sample()
        # Make the inferior candidate pass the coarse safety thresholds.
        row["candidates"][1]["features"] = dict(candidate("good", "tatami", True)["features"])
        for key in ("coverage_ratio", "detail_retention_ratio", "angle_coherence_ratio"):
            row["candidates"][1]["features"][key] -= 0.01
        row["candidates"][1]["features"]["travel_mm"] += 1
        self.assertTrue(ranker.dominates_quality(row["candidates"][0], row["candidates"][1]))
        model = ranker.train_model([sample(i, "human_confirmed") for i in range(8)],
                                   epochs=10)
        model["weights"] = [0.0] * len(ranker.FEATURE_NAMES)
        # Artificially prefer the worse candidate by technique.
        technique = row["candidates"][1]["technique"]
        model["weights"][ranker.FEATURE_NAMES.index("technique:" + technique)] = 100.0
        result = ranker.assess_candidates(model, row, min_margin=0.1)
        self.assertEqual(result["model_choice"], "rejected")
        self.assertEqual(result["decision"], "fallback")
        self.assertEqual(result["reason"], "pareto_dominated")
        self.assertEqual(result["dominated_by"], ["approved"])
        self.assertEqual(result["selected_id"], "approved")
        self.assertFalse(result["applied"])

    def test_heuristic_fallback_cannot_choose_pareto_dominated_candidate(self):
        row = sample()
        winner, inferior = row["candidates"]
        # Both satisfy coarse safety thresholds; inferior is strictly worse
        # on one metric and equal on all the others.
        inferior["features"] = dict(winner["features"])
        inferior["features"]["stitch_count"] += 1
        self.assertTrue(ranker.dominates_quality(winner, inferior))
        # Deliberately make the heuristic score favor the inferior candidate
        # through a tiny difference ignored by the Pareto tolerance.
        inferior["features"]["coverage_ratio"] += 5e-10
        self.assertTrue(ranker.dominates_quality(winner, inferior))
        model = ranker.train_model([sample(i) for i in range(8)], epochs=5)
        result = ranker.assess_candidates(model, row)
        self.assertEqual(result["decision"], "fallback")
        self.assertEqual(result["selected_id"], winner["id"])
        self.assertFalse(result["applied"])

    def test_quality_pareto_does_not_reject_tradeoff(self):
        good = candidate("good", "tatami", True)
        tradeoff = candidate("tradeoff", "auto_satin", True)
        tradeoff["features"]["travel_mm"] = 5.0
        tradeoff["features"]["coverage_ratio"] = 0.90
        self.assertFalse(ranker.dominates_quality(good, tradeoff))
        self.assertFalse(ranker.dominates_quality(tradeoff, good))

    def test_assessment_rejects_invalid_threshold(self):
        model = ranker.train_model([sample(i) for i in range(8)], epochs=50)
        for value in (float("nan"), -1, 2, True):
            with self.assertRaisesRegex(ValueError, "threshold"):
                ranker.assess_candidates(model, sample(), min_margin=value)

    def test_ranking_metrics_are_bounded_and_deterministic(self):
        rows = [sample(i) for i in range(8)]
        model = ranker.train_model(rows, epochs=50)
        first = ranker.ranking_metrics(rows, model)
        self.assertEqual(first, ranker.ranking_metrics(list(reversed(rows)), model))
        self.assertEqual(first["top_k_accuracy"], 1.0)
        self.assertEqual(first["mean_reciprocal_rank"], 1.0)
        with self.assertRaisesRegex(ValueError, "positive"):
            ranker.ranking_metrics(rows, model, k=0)

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
