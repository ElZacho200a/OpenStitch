# SPDX-License-Identifier: Apache-2.0
"""Backend Florence-2 (comprehension de scene) : rend des DETECTIONS (etiquette + boite), pas de masques.

Utilise la classe NATIVE de `transformers` (Florence2ForConditionalGeneration) avec des poids au
format safetensors : aucun code distant (`trust_remote_code`) n'est execute.
Invites : `automatic` (detection d'objets `<OD>`) et `text` (ancrage de phrases
`<CAPTION_TO_PHRASE_GROUNDING>`, phrases separees par « . »).
"""
from __future__ import annotations

import hashlib
import threading
from pathlib import Path

from backend import Backend, BackendError, Context, PredictRequest
from common import load_rgb, pick_device, sha256_file


class Florence2Backend(Backend):
    backend_id = "florence2"

    def __init__(self, model_dir: str, device: str = "auto") -> None:
        self.model_dir, self.device_pref = Path(model_dir), device
        self._model = self._processor = None
        self._hash: str | None = None
        self._lock = threading.Lock()

    def capabilities(self) -> dict:
        weights = self.model_dir / "model.safetensors"
        with self._lock:
            if self._hash is None and weights.is_file():
                self._hash = sha256_file(weights)
        return {"backend_id": self.backend_id, "backend_revision": self.model_dir.name,
                "display_name": "Florence-2 (comprehension de scene)", "model_id": self.model_dir.name,
                "model_hash": self._hash, "prompts": ["automatic", "text"], "devices": ["cpu", "cuda"],
                "license_code": "MIT", "license_weights": "MIT", "experimental": True}

    def preload(self) -> None:
        import torch  # noqa: F401
        import transformers  # noqa: F401
        from transformers import Florence2ForConditionalGeneration  # noqa: F401

    def health(self) -> dict:
        if not (self.model_dir / "model.safetensors").is_file():
            return {"state": "unavailable", "detail": "model.safetensors absent"}
        try:
            from transformers import Florence2ForConditionalGeneration  # noqa: F401
        except ImportError:
            return {"state": "unavailable", "detail": "transformers (Florence-2 natif) absent"}
        return {"state": "ready", "detail": ""}

    def _load(self, ctx: Context):
        if self._model is None:
            ctx.progress("loading_model")
            try:
                import torch
                from transformers import AutoProcessor, Florence2ForConditionalGeneration
            except ImportError as exc:
                raise BackendError("SAM2_NOT_INSTALLED", "transformers/torch absents", str(exc)) from exc
            device = pick_device(self.device_pref)
            dtype = torch.float16 if device == "cuda" else torch.float32
            self._model = Florence2ForConditionalGeneration.from_pretrained(str(self.model_dir), dtype=dtype).to(device).eval()
            self._processor = AutoProcessor.from_pretrained(str(self.model_dir))
            self._device, self._dtype = device, dtype
        return self._model, self._processor

    def predict(self, request: PredictRequest, ctx: Context) -> dict:
        import torch
        from PIL import Image

        model, processor = self._load(ctx)
        image_path = Path(request.job_dir) / request.image_file
        _array, source_size, data = load_rgb(image_path)
        image = Image.open(image_path).convert("RGB")
        detections = []
        for idx, prompt in enumerate(request.prompts):
            ctx.check_cancelled()
            ctx.progress("running_inference", idx / max(1, len(request.prompts)))
            if prompt["kind"] == "automatic":
                task, text = "<OD>", "<OD>"
            else:
                phrases = str(prompt.get("text", "")).strip()
                if not phrases:
                    raise BackendError("INVALID_REQUEST", "invite `text` sans phrase")
                task, text = "<CAPTION_TO_PHRASE_GROUNDING>", "<CAPTION_TO_PHRASE_GROUNDING>" + phrases
            inputs = processor(text=text, images=image, return_tensors="pt").to(self._device, self._dtype)
            with torch.inference_mode():
                out = model.generate(**inputs, max_new_tokens=256, num_beams=3, do_sample=False)
            decoded = processor.batch_decode(out, skip_special_tokens=False)[0]
            parsed = processor.post_process_generation(decoded, task=task, image_size=source_size)[task]
            for label, (x0, y0, x1, y1) in zip(parsed.get("labels", []), parsed.get("bboxes", [])):
                x0, y0 = max(0, int(round(x0))), max(0, int(round(y0)))
                x1, y1 = min(source_size[0], int(round(x1))), min(source_size[1], int(round(y1)))
                if x1 > x0 and y1 > y0:
                    detections.append({"id": f"d{len(detections)}", "label": label,
                                       "bbox_xywh": [x0, y0, x1 - x0, y1 - y0], "prompt_index": idx})
        return {"image_sha256": hashlib.sha256(data).hexdigest(),
                "transform": {"source_width": source_size[0], "source_height": source_size[1],
                              "width": source_size[0], "height": source_size[1], "scale": 1.0, "crop": None},
                "masks": [], "detections": detections, "warnings": []}
