"""Score detector masks on the marine fixture and marked photo samples."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def rate(part, total):
    return round(part / total, 6) if total else None


def expand(mask):
    image = Image.fromarray(mask.astype(np.uint8) * 255)
    return np.asarray(image.filter(ImageFilter.MaxFilter(3))) > 0


def mask_stats(predicted, truth):
    overlap = int(np.count_nonzero(predicted & truth))
    count = int(np.count_nonzero(predicted))
    positives = int(np.count_nonzero(truth))
    return {
        "precision": rate(overlap, count),
        "recall": rate(overlap, positives),
        "f1": rate(2 * overlap, count + positives),
        "iou": rate(overlap, count + positives - overlap),
        "precision_1px": rate(np.count_nonzero(predicted & expand(truth)), count),
        "recall_1px": rate(np.count_nonzero(truth & expand(predicted)), positives),
    }


def read_mask(path):
    return np.asarray(Image.open(path).convert("L")) > 127


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("photos", type=Path)
    parser.add_argument("masks", type=Path)
    parser.add_argument("--variants", nargs="+", default=["18", "24", "32"])
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    annotation = json.loads((here / "annotations.json").read_text(encoding="utf-8"))
    gold = here.parent / "marine-synth-01" / "gold"
    rigging = read_mask(gold / "boat.rigging.png")
    protected_objects = np.logical_or.reduce(
        [read_mask(gold / f"{item}.png") for item in ("boat.rigging", "boat.mast")]
    )
    sky_water = read_mask(gold / "family_sky.png") | read_mask(gold / "sea.water.png")
    negatives = sky_water & ~expand(protected_objects)
    report = {}
    for variant in args.variants:
        result = {}
        marine = read_mask(args.masks / f"marine-{variant}.png")
        scored = rigging | negatives
        result["marine"] = {
            **mask_stats(marine & scored, rigging),
            "fp_sky_water": rate(np.count_nonzero(marine & negatives), np.count_nonzero(negatives)),
        }
        for name, marks in annotation.items():
            photo = args.photos / f"{name}.png"
            if hashlib.sha256(photo.read_bytes()).hexdigest() != marks["sha256"]:
                raise ValueError(f"Image differs from annotated source: {photo}")
            candidate = read_mask(args.masks / f"{name}-{variant}.png")
            width, height = marks["size"]
            if candidate.shape != (height, width):
                raise ValueError(f"Mask dimensions do not match {name}")
            nearby = expand(candidate)
            result[name] = {"trace_recall_1px": {}, "false_positive_rate": {}}
            for label, points in marks["lines"].items():
                trace = Image.new("1", (width, height))
                ImageDraw.Draw(trace).line(list(map(tuple, points)), fill=1)
                selected = np.asarray(trace)
                result[name]["trace_recall_1px"][label] = rate(
                    np.count_nonzero(nearby & selected), np.count_nonzero(selected)
                )
            for label, (x0, y0, x1, y1) in marks["negative_boxes"].items():
                box = candidate[y0:y1, x0:x1]
                result[name]["false_positive_rate"][label] = rate(np.count_nonzero(box), box.size)
        report[variant] = result
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
