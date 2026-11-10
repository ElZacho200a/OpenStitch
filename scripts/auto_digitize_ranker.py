#!/usr/bin/env python3
"""IA-AUTO-01 L2: small, deterministic candidate ranker. Python standard library only.

This is a research tool, NOT a stitch generator. It never edits OSP/DST, downloads
models, collects user data, or claims textile safety.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

SAMPLE_SCHEMA = "openstitch.ai.ranker-sample/1"
MODEL_SCHEMA = "openstitch.ai.ranker-model/1"
TECHNIQUES = ("tatami", "auto_satin", "satin", "running", "directional",
              "spiral", "radial", "contour")
# Every numeric input must be measured / annotated BEFORE selection.
# High bounds prevent nonsensical inputs, not claims of physical limits.
FEATURE_LIMITS = {
    "coverage_ratio": 1.0,
    "detail_retention_ratio": 1.0,
    "angle_coherence_ratio": 1.0,
    "short_stitch_ratio": 1.0,
    "overdensity_ratio": 1.0,
    "uncovered_ratio": 1.0,
    "travel_mm": 100000.0,
    "trim_count": 100000.0,
    "stitch_count": 10000000.0,
}
LOG_FEATURES = frozenset(("travel_mm", "trim_count", "stitch_count"))
FEATURE_NAMES = tuple(FEATURE_LIMITS) + tuple("technique:" + t for t in TECHNIQUES)


def fail(message: str) -> None:
    raise ValueError(message)


def check_candidate(candidate: object) -> None:
    if not isinstance(candidate, dict):
        fail("candidate must be an object")
    if not isinstance(candidate.get("id"), str) or not candidate["id"].strip():
        fail("candidate.id must be a nonempty string")
    if candidate.get("technique") not in TECHNIQUES:
        fail("unsupported candidate technique")
    features = candidate.get("features")
    if not isinstance(features, dict) or set(features) != set(FEATURE_LIMITS):
        fail("candidate.features must contain exactly: " + ", ".join(FEATURE_LIMITS))
    for name, maximum in FEATURE_LIMITS.items():
        value = features[name]
        if type(value) not in (float, int) or not math.isfinite(value):
            fail("nonfinite/non-numeric feature: " + name)
        if not (0 <= value <= maximum):
            fail("feature out of range: " + name)


def check_sample(row: object, require_label: bool = True) -> None:
    if not isinstance(row, dict) or row.get("schema") != SAMPLE_SCHEMA:
        fail("invalid ranker sample schema")
    for key in ("sample_id", "motif_group", "semantic_id"):
        if not isinstance(row.get(key), str) or not row[key].strip():
            fail("missing/nonempty identifier: " + key)
    candidates = row.get("candidates")
    if not isinstance(candidates, list) or len(candidates) < 2 or len(candidates) > 16:
        fail("each sample requires 2..16 candidates")
    ids = set()
    for c in candidates:
        check_candidate(c)
        if c["id"] in ids:
            fail("duplicate candidate.id")
        ids.add(c["id"])
    if require_label:
        if row.get("preferred_id") not in ids:
            fail("preferred_id must reference a candidate")
        prov = row.get("provenance")
        if not isinstance(prov, dict):
            fail("missing provenance")
        if prov.get("source_kind") not in ("human_confirmed", "synthetic"):
            fail("provenance.source_kind must be human_confirmed or synthetic")
        if not isinstance(prov.get("rights"), str) or not prov["rights"].strip():
            fail("missing provenance.rights")
        if prov["source_kind"] == "human_confirmed" and prov.get("opt_in") is not True:
            fail("human data requires explicit provenance.opt_in=true")


def load_jsonl(path: Path) -> list[dict]:
    rows = []
    seen = set()
    with path.open(encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
                check_sample(row)
                if row["sample_id"] in seen:
                    fail("duplicate sample_id")
                seen.add(row["sample_id"])
                rows.append(row)
            except (ValueError, TypeError) as exc:
                fail(f"{path}:{line_no}: {exc}")
    if not rows:
        fail("no labeled samples")
    return sorted(rows, key=lambda r: r["sample_id"])


def vector(candidate: dict) -> tuple[float, ...]:
    values = []
    for name, maximum in FEATURE_LIMITS.items():
        value = float(candidate["features"][name])
        values.append(math.log1p(value) / math.log1p(maximum)
                      if name in LOG_FEATURES else value / maximum)
    values.extend(1.0 if candidate["technique"] == t else 0.0 for t in TECHNIQUES)
    return tuple(values)


def score(model: dict, candidate: dict) -> float:
    check_candidate(candidate)
    if model.get("schema") != MODEL_SCHEMA or model.get("feature_names") != list(FEATURE_NAMES):
        fail("incompatible model schema/features")
    weights = model.get("weights")
    if not isinstance(weights, list) or len(weights) != len(FEATURE_NAMES) or any(
        type(w) not in (float, int) or not math.isfinite(w) for w in weights
    ):
        fail("invalid model weights")
    result = sum(a * b for a, b in zip(weights, vector(candidate)))
    if not math.isfinite(result):
        fail("nonfinite candidate score")
    return result


def rank(model: dict, row: dict) -> list[dict]:
    check_sample(row, require_label=False)
    return sorted(({"id": c["id"], "score": score(model, c)}
                   for c in row["candidates"]), key=lambda x: (-x["score"], x["id"]))


def split_by_motif(rows: list[dict], ratio: float = 0.8, salt: str = "openstitch-v1"):
    if not (0 < ratio < 1):
        fail("train ratio must be in (0,1)")
    groups = sorted({r["motif_group"] for r in rows},
                    key=lambda x: (hashlib.sha256((salt + ":" + x).encode()).hexdigest(), x))
    if len(groups) < 2:
        fail("at least two distinct motif groups required to avoid train/validation leakage")
    train_count = max(1, min(len(groups) - 1, int(len(groups) * ratio)))
    train_groups = set(groups[:train_count])
    train = [r for r in rows if r["motif_group"] in train_groups]
    heldout = [r for r in rows if r["motif_group"] not in train_groups]
    return train, heldout


def fit(rows: list[dict], *, epochs: int = 500, learning_rate: float = 0.3,
        l2: float = 0.002) -> list[float]:
    if not (1 <= epochs <= 10000 and 0 < learning_rate <= 1 and 0 <= l2 <= 1):
        fail("invalid training hyperparameters")
    pairs = []
    for row in rows:
        winning = next(c for c in row["candidates"] if c["id"] == row["preferred_id"])
        good = vector(winning)
        for other in row["candidates"]:
            if other["id"] != row["preferred_id"]:
                bad = vector(other)
                pairs.append(tuple(g - b for g, b in zip(good, bad)))
    if not pairs:
        fail("no preference pairs")
    weights = [0.0] * len(FEATURE_NAMES)
    # Full-batch pairwise logistic gradient: stable order, no random seeds needed.
    for _ in range(epochs):
        gradient = [l2 * weight for weight in weights]
        for diff in pairs:
            margin = max(-50.0, min(50.0, sum(w * d for w, d in zip(weights, diff))))
            # d/dw log(1 + exp(-margin))
            multiplier = -1.0 / (1.0 + math.exp(margin)) / len(pairs)
            for idx, d in enumerate(diff):
                gradient[idx] += multiplier * d
        weights = [w - learning_rate * g for w, g in zip(weights, gradient)]
    return weights


def heuristic(candidate: dict) -> float:
    f = candidate["features"]
    return (f["coverage_ratio"] + f["detail_retention_ratio"]
            + f["angle_coherence_ratio"] - f["short_stitch_ratio"]
            - f["overdensity_ratio"] - f["uncovered_ratio"]
            - math.log1p(f["trim_count"]) / math.log1p(FEATURE_LIMITS["trim_count"]))


def metrics(rows: list[dict], model: dict | None) -> dict:
    correct = 0
    for row in rows:
        if model is None:
            ordered = sorted(row["candidates"], key=lambda c: (-heuristic(c), c["id"]))
            best_id = ordered[0]["id"]
        else:
            best_id = rank(model, row)[0]["id"]
        correct += best_id == row["preferred_id"]
    return {"samples": len(rows), "top1_accuracy": round(correct / len(rows), 6)}


def compare_candidates(model: dict, row: dict) -> dict:
    """Compare measured alternatives; never apply a choice to OSP/DST."""
    check_sample(row, require_label=False)
    ranked = rank(model, row)
    baseline = sorted(row["candidates"], key=lambda c: (-heuristic(c), c["id"]))
    by_id = {c["id"]: c for c in row["candidates"]}
    return {
        "schema": "openstitch.ai.candidate-comparison/1",
        "sample_id": row["sample_id"],
        "semantic_id": row["semantic_id"],
        "model_choice": ranked[0]["id"],
        "heuristic_choice": baseline[0]["id"],
        "agree": ranked[0]["id"] == baseline[0]["id"],
        "ranking": [
            {"id": item["id"], "technique": by_id[item["id"]]["technique"],
             "score": item["score"],
             "heuristic_score": heuristic(by_id[item["id"]])}
            for item in ranked
        ],
        "applied": False,
    }


def dominates_quality(a: dict, b: dict, *, tolerance: float = 1e-9) -> bool:
    """Measured quality Pareto gate: all metrics no worse, one strictly better."""
    check_candidate(a)
    check_candidate(b)
    high = ("coverage_ratio", "detail_retention_ratio", "angle_coherence_ratio")
    low = ("short_stitch_ratio", "overdensity_ratio", "uncovered_ratio",
           "travel_mm", "trim_count", "stitch_count")
    af, bf = a["features"], b["features"]
    better = False
    for key in high:
        if af[key] + tolerance < bf[key]:
            return False
        better |= af[key] > bf[key] + tolerance
    for key in low:
        if af[key] > bf[key] + tolerance:
            return False
        better |= af[key] + tolerance < bf[key]
    return better


def assess_candidates(model: dict, row: dict, *, min_margin: float = 0.1,
                      min_coverage: float = 0.85,
                      max_uncovered: float = 0.15,
                      max_short_stitches: float = 0.2,
                      max_overdensity: float = 0.2) -> dict:
    """Conservative advisory decision: never writes embroidery or applies a model."""
    check_sample(row, require_label=False)
    for name, value in (("min_margin", min_margin), ("min_coverage", min_coverage),
                        ("max_uncovered", max_uncovered),
                        ("max_short_stitches", max_short_stitches),
                        ("max_overdensity", max_overdensity)):
        if type(value) not in (int, float) or not math.isfinite(value) or not 0 <= value <= 1:
            fail("invalid assessment threshold: " + name)
    accepted = []
    rejected = []
    for candidate in row["candidates"]:
        f = candidate["features"]
        reasons = []
        if f["coverage_ratio"] < min_coverage:
            reasons.append("insufficient_coverage")
        if f["uncovered_ratio"] > max_uncovered:
            reasons.append("excessive_uncovered_area")
        if f["short_stitch_ratio"] > max_short_stitches:
            reasons.append("excessive_short_stitches")
        if f["overdensity_ratio"] > max_overdensity:
            reasons.append("excessive_overdensity")
        if reasons:
            rejected.append({"id": candidate["id"], "reasons": reasons})
        else:
            accepted.append(candidate)
    rejected.sort(key=lambda item: item["id"])
    # Validate the model even if every candidate is rejected: no silent use
    # of incompatible weights after an apparent safety fallback.
    all_ranked = rank(model, row)
    if not accepted:
        return {"schema": "openstitch.ai.assessment/1", "decision": "abstain",
                "reason": "no_admissible_candidate", "selected_id": None,
                "margin": None, "rejected": rejected, "applied": False}
    accepted_ids = {c["id"] for c in accepted}
    ranked = [item for item in all_ranked if item["id"] in accepted_ids]
    # The fallback must obey the same Pareto gate as the learned proposal:
    # a heuristic winner can otherwise be strictly dominated on every
    # measured quality/cost axis by another admissible candidate.
    frontier = [c for c in accepted if not any(
        other["id"] != c["id"] and dominates_quality(other, c)
        for other in accepted)]
    baseline = sorted(frontier, key=lambda c: (-heuristic(c), c["id"]))[0]["id"]
    selected_candidate = next(c for c in accepted if c["id"] == ranked[0]["id"])
    dominators = sorted(c["id"] for c in accepted
                        if c["id"] != selected_candidate["id"]
                        and dominates_quality(c, selected_candidate))
    margin = ranked[0]["score"] - ranked[1]["score"] if len(ranked) > 1 else None
    if margin is not None and not math.isfinite(margin):
        fail("nonfinite ranking margin")
    # Synthetic-only training is useful for CI, never for autonomous decisions.
    # Mixed human/synthetic datasets and legacy models without human_only
    # are also unverified: one human annotation cannot promote synthetic data.
    training = model.get("training")
    human_validated = (isinstance(training, dict)
                       and training.get("human_only") is True)
    # A single admissible candidate needs no learned arbitration.
    confident = human_validated and len(ranked) > 1 and margin >= min_margin and not dominators
    return {"schema": "openstitch.ai.assessment/1",
            "decision": "recommend" if confident else "fallback",
            "reason": "sufficient_margin" if confident else
                      ("synthetic_or_unverified_model" if not human_validated else
                       "pareto_dominated" if dominators else
                       "single_admissible_candidate" if len(ranked) == 1 else "low_margin"),
            "selected_id": ranked[0]["id"] if confident else baseline,
            "model_choice": ranked[0]["id"], "heuristic_choice": baseline,
            "margin": margin, "dominated_by": dominators,
            "rejected": rejected, "applied": False}


def ranking_metrics(rows: list[dict], model: dict | None, k: int = 2) -> dict:
    """Top-k accuracy and mean reciprocal rank on labeled examples."""
    if type(k) is not int or k < 1:
        fail("k must be a positive integer")
    hits = 0
    reciprocal = 0.0
    for row in rows:
        check_sample(row)
        if model is None:
            ordered = sorted(row["candidates"], key=lambda c: (-heuristic(c), c["id"]))
            ids = [c["id"] for c in ordered]
        else:
            ids = [c["id"] for c in rank(model, row)]
        position = ids.index(row["preferred_id"]) + 1
        hits += position <= k
        reciprocal += 1.0 / position
    if not rows:
        fail("no labeled samples")
    return {"samples": len(rows), "k": k, "top_k_accuracy": round(hits / len(rows), 6),
            "mean_reciprocal_rank": round(reciprocal / len(rows), 6)}


def dataset_digest(rows: list[dict]) -> str:
    canonical = "\n".join(json.dumps(r, sort_keys=True, separators=(",", ":"),
                                    ensure_ascii=False) for r in rows)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def train_model(rows: list[dict], epochs: int = 500) -> dict:
    """Validate even in-process callers, not just the JSONL CLI boundary."""
    if not rows:
        fail("no labeled samples")
    seen = set()
    for row in rows:
        check_sample(row)
        if row["sample_id"] in seen:
            fail("duplicate sample_id")
        seen.add(row["sample_id"])
    rows = sorted(rows, key=lambda row: row["sample_id"])
    train, validation = split_by_motif(rows)
    model = {"schema": MODEL_SCHEMA, "feature_names": list(FEATURE_NAMES),
             "weights": fit(train, epochs=epochs),
             "training": {"dataset_sha256": dataset_digest(rows),
                          "train_samples": len(train), "heldout_samples": len(validation),
                          "train_motif_groups": len({r["motif_group"] for r in train}),
                          "heldout_motif_groups": len({r["motif_group"] for r in validation}),
                          "train_metrics": None, "heldout_metrics": None,
                          "baseline_heldout_metrics": metrics(validation, None),
                          "heldout_ranking_metrics": None,
                          "baseline_heldout_ranking_metrics": ranking_metrics(validation, None),
                          "synthetic_only": all(r["provenance"]["source_kind"] == "synthetic"
                                                for r in rows),
                          "human_only": all(r["provenance"]["source_kind"] == "human_confirmed"
                                            for r in rows)}}
    model["training"]["train_metrics"] = metrics(train, model)
    model["training"]["heldout_metrics"] = metrics(validation, model)
    model["training"]["heldout_ranking_metrics"] = ranking_metrics(validation, model)
    return model


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("validate", "train", "evaluate"):
        p = sub.add_parser(command)
        p.add_argument("samples", type=Path)
        if command != "validate":
            p.add_argument("model", type=Path, help="Output for train; input for evaluate")
        if command == "train":
            p.add_argument("--epochs", type=int, default=500)
    p = sub.add_parser("assess")
    p.add_argument("model", type=Path)
    p.add_argument("sample", type=Path)
    p.add_argument("--min-margin", type=float, default=0.1)
    p = sub.add_parser("compare")
    p.add_argument("model", type=Path)
    p.add_argument("sample", type=Path)
    p = sub.add_parser("rank")
    p.add_argument("model", type=Path)
    p.add_argument("sample", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command in ("rank", "compare", "assess"):
            model = json.loads(args.model.read_text(encoding="utf-8"))
            sample = json.loads(args.sample.read_text(encoding="utf-8"))
            output = (assess_candidates(model, sample, min_margin=args.min_margin)
                      if args.command == "assess" else
                      compare_candidates(model, sample) if args.command == "compare"
                      else {"ranking": rank(model, sample)})
            print(json.dumps(output, indent=2))
        else:
            rows = load_jsonl(args.samples)
            if args.command == "validate":
                print(json.dumps({"valid": True, "samples": len(rows),
                                  "motif_groups": len({r["motif_group"] for r in rows})}))
            elif args.command == "train":
                model = train_model(rows, args.epochs)
                # Exclusive creation: never silently overwrite a previously trained model.
                with args.model.open("x", encoding="utf-8") as handle:
                    json.dump(model, handle, indent=2, sort_keys=True)
                    handle.write("\n")
                print(json.dumps(model["training"], indent=2, sort_keys=True))
            else:
                model = json.loads(args.model.read_text(encoding="utf-8"))
                result = {"model": metrics(rows, model), "heuristic": metrics(rows, None),
                          "model_ranking": ranking_metrics(rows, model),
                          "heuristic_ranking": ranking_metrics(rows, None),
                          "warning": "Evaluate on independent unseen motifs; this command does not split."}
                print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(f"IA-AUTO-01: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
