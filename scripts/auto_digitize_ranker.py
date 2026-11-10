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


def dataset_digest(rows: list[dict]) -> str:
    canonical = "\n".join(json.dumps(r, sort_keys=True, separators=(",", ":"),
                                    ensure_ascii=False) for r in rows)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def train_model(rows: list[dict], epochs: int = 500) -> dict:
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
                          "synthetic_only": all(r["provenance"]["source_kind"] == "synthetic"
                                                for r in rows)}}
    model["training"]["train_metrics"] = metrics(train, model)
    model["training"]["heldout_metrics"] = metrics(validation, model)
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
    p = sub.add_parser("rank")
    p.add_argument("model", type=Path)
    p.add_argument("sample", type=Path)
    args = parser.parse_args(argv)
    try:
        if args.command == "rank":
            model = json.loads(args.model.read_text(encoding="utf-8"))
            sample = json.loads(args.sample.read_text(encoding="utf-8"))
            print(json.dumps({"ranking": rank(model, sample)}, indent=2))
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
                          "warning": "Evaluate on independent unseen motifs; this command does not split."}
                print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(f"IA-AUTO-01: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
