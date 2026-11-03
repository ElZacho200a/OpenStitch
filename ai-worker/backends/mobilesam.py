# SPDX-License-Identifier: Apache-2.0
"""Backend MobileSAM (vit_t) guide par boites. Invite supportee : `box`."""
from __future__ import annotations

import hashlib
import threading
from pathlib import Path

from backend import Backend, BackendError, Context, PredictRequest
from common import box_xyxy, load_rgb, pick_device, sha256_file, write_masks


class MobileSamBackend(Backend):
    backend_id = "mobilesam"

    def __init__(self, checkpoint: str, device: str = "auto") -> None:
        self.checkpoint, self.device_pref = Path(checkpoint), device
        self._predictor = None
        self._hash: str | None = None
        self._lock = threading.Lock()

    def capabilities(self) -> dict:
        with self._lock:
            if self._hash is None and self.checkpoint.is_file():
                self._hash = sha256_file(self.checkpoint)
        return {"backend_id": self.backend_id, "backend_revision": "mobile_sam.pt", "display_name": "MobileSAM (vit_t)",
                "model_id": "vit_t", "model_hash": self._hash, "prompts": ["box"], "devices": ["cpu", "cuda"],
                "license_code": "Apache-2.0", "license_weights": "Apache-2.0 (a confirmer)", "experimental": True}

    def preload(self) -> None:
        import torch  # noqa: F401
        import mobile_sam  # noqa: F401

    def health(self) -> dict:
        if not self.checkpoint.is_file():
            return {"state": "unavailable", "detail": f"checkpoint absent : {self.checkpoint.name}"}
        try:
            import mobile_sam  # noqa: F401
        except ImportError:
            return {"state": "unavailable", "detail": "paquet mobile_sam non installe"}
        return {"state": "ready", "detail": ""}

    def _load(self, ctx: Context):
        if self._predictor is None:
            ctx.progress("loading_model")
            try:
                from mobile_sam import SamPredictor, sam_model_registry
            except ImportError as exc:
                raise BackendError("SAM2_NOT_INSTALLED", "paquet mobile_sam absent", str(exc)) from exc
            if not self.checkpoint.is_file():
                raise BackendError("CHECKPOINT_NOT_FOUND", "checkpoint introuvable", str(self.checkpoint))
            model = sam_model_registry["vit_t"](checkpoint=str(self.checkpoint))
            model.to(device=pick_device(self.device_pref)).eval()
            self._predictor = SamPredictor(model)
        return self._predictor

    def predict(self, request: PredictRequest, ctx: Context) -> dict:
        import numpy as np
        import torch

        job = Path(request.job_dir)
        predictor = self._load(ctx)
        array, source_size, data = load_rgb(job / request.image_file, request.options.get("max_resolution"))
        ph, pw = array.shape[:2]
        scale = pw / source_size[0]
        ctx.progress("running_inference")
        items = []
        with torch.inference_mode():
            predictor.set_image(array)
            for idx, prompt in enumerate(request.prompts):
                ctx.check_cancelled()
                box = np.array(box_xyxy(prompt, source_size, scale), dtype=np.float32)
                masks, scores, _ = predictor.predict(box=box, multimask_output=False)
                items.append({"mask": masks[0] > 0, "confidence": float(scores[0]), "prompt_index": idx})
        return {"image_sha256": hashlib.sha256(data).hexdigest(),
                "transform": {"source_width": source_size[0], "source_height": source_size[1], "width": pw,
                              "height": ph, "scale": scale, "crop": None},
                "masks": write_masks(job, items, (pw, ph), source_size), "warnings": []}
