# SPDX-License-Identifier: Apache-2.0
"""Deux backends factices (bibliotheque standard seulement) pour tester le contrat : decouverte,
annulation, erreurs typees, version incompatible, repli. Aucune inference, jamais en production.

- `fake-grid`  : sain et deterministe ; invites `automatic` (4 quadrants) et `box`.
- `fake-flaky` : pannes a la demande via `options.mode` = fail | slow | noise | crash.
"""
from __future__ import annotations

import hashlib
import os
import struct
import sys
import time
import zlib
from pathlib import Path

from backend import Backend, BackendError, Context, PredictRequest


def png_size(path: Path) -> tuple[int, int]:
    head = path.read_bytes()[:24]
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n":
        raise BackendError("IMAGE_LOAD_FAILED", f"PNG illisible : {path.name}")
    return struct.unpack(">II", head[16:24])


def write_mask_png(path: Path, width: int, height: int, box: tuple[int, int, int, int]) -> int:
    """Masque gris 8 bits (255 dans la boite x,y,w,h) ; retourne l'aire en pixels."""
    x0, y0, bw, bh = box
    x1, y1 = min(width, x0 + bw), min(height, y0 + bh)
    rows = []
    for y in range(height):
        row = bytearray(width)
        if y0 <= y < y1:
            row[x0:x1] = b"\xff" * max(0, x1 - x0)
        rows.append(b"\0" + bytes(row))

    def chunk(name: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + name + payload + struct.pack(">I", zlib.crc32(name + payload))

    data = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return max(0, x1 - x0) * max(0, y1 - y0)


def _wait(ctx: Context, delay_ms: int) -> None:
    end = time.monotonic() + delay_ms / 1000.0
    while time.monotonic() < end:
        ctx.check_cancelled()
        time.sleep(0.01)
    ctx.check_cancelled()


class FakeGrid(Backend):
    backend_id = "fake-grid"

    def capabilities(self) -> dict:
        return {"backend_id": self.backend_id, "backend_revision": "test-1", "display_name": "Fake grid (tests)",
                "model_id": "none", "model_hash": None, "prompts": ["automatic", "box"], "devices": ["cpu"],
                "license_code": "Apache-2.0", "license_weights": "n/a", "experimental": True}

    def predict(self, request: PredictRequest, ctx: Context) -> dict:
        job = Path(request.job_dir)
        image = job / request.image_file
        if not image.is_file():
            raise BackendError("IMAGE_LOAD_FAILED", "image introuvable", str(image))
        w, h = png_size(image)
        ctx.progress("preparing_image", 0.0)
        boxes: list[tuple[int, tuple[int, int, int, int]]] = []
        for idx, p in enumerate(request.prompts):
            if p["kind"] == "automatic":
                hw, hh = w // 2, h // 2
                boxes += [(idx, (0, 0, hw, hh)), (idx, (hw, 0, w - hw, hh)),
                          (idx, (0, hh, hw, h - hh)), (idx, (hw, hh, w - hw, h - hh))]
            else:
                bx = p.get("box_xywh")
                if not (isinstance(bx, list) and len(bx) == 4):
                    raise BackendError("INVALID_REQUEST", "invite `box` sans `box_xywh`")
                boxes.append((idx, tuple(int(v) for v in bx)))
        delay = int(request.options.get("delay_ms", 0))
        masks = []
        for i, (idx, box) in enumerate(boxes):
            _wait(ctx, delay)
            name = f"masks/mask_{i:04d}.png"
            area = write_mask_png(job / name, w, h, box)
            masks.append({"id": f"m{i}", "file": name, "bbox_xywh": list(box), "area_px": area,
                          "prompt_index": idx})
            ctx.progress("writing_masks", (i + 1) / len(boxes))
        return {"image_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
                "transform": {"source_width": w, "source_height": h, "width": w, "height": h,
                              "scale": 1.0, "crop": None},
                "masks": masks, "warnings": []}


class FakeFlaky(Backend):
    backend_id = "fake-flaky"

    def capabilities(self) -> dict:
        return {"backend_id": self.backend_id, "backend_revision": "test-1", "display_name": "Fake flaky (tests)",
                "model_id": "none", "model_hash": None, "prompts": ["automatic"], "devices": ["cpu"],
                "license_code": "Apache-2.0", "license_weights": "n/a", "experimental": True}

    def predict(self, request: PredictRequest, ctx: Context) -> dict:
        mode = request.options.get("mode", "fail")
        ctx.progress("running_inference", 0.0)
        if mode == "fail":
            raise BackendError("INFERENCE_FAILED", "echec simule", "mode=fail")
        if mode == "slow":
            _wait(ctx, int(request.options.get("delay_ms", 30000)))
            raise BackendError("INFERENCE_FAILED", "le mode slow n'etait pas annule")
        if mode == "noise":
            sys.stdout.write("this is not protocol\n")  # viole le contrat : le client doit le refuser
            sys.stdout.flush()
            return {"image_sha256": "", "transform": {}, "masks": [], "warnings": []}
        if mode == "crash":
            os._exit(3)
        raise BackendError("INVALID_REQUEST", f"mode inconnu : {mode!r}")

    def health(self) -> dict:
        return {"state": "ready", "detail": "fake-flaky"}
