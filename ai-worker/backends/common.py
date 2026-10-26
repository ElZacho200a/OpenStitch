# SPDX-License-Identifier: Apache-2.0
"""Aides communes aux backends a modeles (numpy / Pillow importes paresseusement)."""
from __future__ import annotations

import hashlib
from pathlib import Path

from backend import BackendError


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_rgb(image_path: Path, max_resolution: int | None = None):
    """(tableau RGB uint8 HxWx3, taille source (w,h), octets du fichier). Image eventuellement reduite."""
    try:
        import numpy as np
        from PIL import Image
    except ImportError as exc:
        raise BackendError("SAM2_NOT_INSTALLED", "numpy/Pillow absents", str(exc)) from exc
    try:
        data = image_path.read_bytes()
        with Image.open(image_path) as src:
            src = src.convert("RGB")
            source_size = src.size
            img = src
            if max_resolution and max(src.size) > int(max_resolution):
                scale = int(max_resolution) / float(max(src.size))
                img = src.resize((max(1, round(src.size[0] * scale)), max(1, round(src.size[1] * scale))), Image.LANCZOS)
            return np.ascontiguousarray(np.asarray(img)), source_size, data
    except (OSError, FileNotFoundError) as exc:
        raise BackendError("IMAGE_LOAD_FAILED", "image illisible", str(exc)) from exc


def write_masks(job: Path, items: list[dict], processed_size: tuple[int, int], source_size: tuple[int, int]) -> list[dict]:
    """`items` : {"mask": bool HxW (taille traitee), "confidence": float|None, "prompt_index": int}.
    Ecrit des PNG 8 bits a la resolution SOURCE et retourne les entrees `masks` du protocole."""
    import numpy as np
    from PIL import Image

    (job / "masks").mkdir(parents=True, exist_ok=True)
    pw, ph = processed_size
    sw, sh = source_size
    out = []
    for i, item in enumerate(items):
        img = Image.fromarray(item["mask"].astype(np.uint8) * 255, mode="L")
        if (pw, ph) != (sw, sh):
            img = img.resize((sw, sh), Image.NEAREST)
        arr = np.asarray(img) > 0
        if not arr.any():
            continue
        name = f"masks/mask_{i:04d}.png"
        img.save(job / name)
        ys, xs = arr.nonzero()
        entry = {"id": f"m{i}", "file": name, "area_px": int(arr.sum()),
                 "bbox_xywh": [int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1)],
                 "prompt_index": item.get("prompt_index", 0)}
        if item.get("confidence") is not None:
            entry["confidence"] = float(item["confidence"])
        out.append(entry)
    return out


def box_xyxy(prompt: dict, size: tuple[int, int], scale: float) -> list[float]:
    bx = prompt.get("box_xywh")
    if not (isinstance(bx, list) and len(bx) == 4 and all(isinstance(v, (int, float)) for v in bx)):
        raise BackendError("INVALID_REQUEST", "invite `box` sans `box_xywh` valide")
    x, y, w, h = bx
    return [x * scale, y * scale, (x + w) * scale, (y + h) * scale]


def pick_device(preference: str) -> str:
    if preference in ("cpu", "cuda"):
        return preference
    try:
        import torch
        return "cuda" if torch.cuda.is_available() else "cpu"
    except ImportError:
        return "cpu"
