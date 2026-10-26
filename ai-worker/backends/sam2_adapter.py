# SPDX-License-Identifier: Apache-2.0
"""Adaptateur SAM 2.1 : reutilise SamService / mask_cleaner / mask_deduplicator de `sam-worker/`
SANS les modifier (le worker v1 et son client desktop restent inchanges). Invite supportee :
`automatic` seulement ; box/point/refine arrivent avec PR-04.

numpy, Pillow, torch et sam2 sont importes paresseusement : le worker demarre et repond a
`capabilities`/`health` meme sans eux (etat `unavailable` explicite).
"""
from __future__ import annotations

import hashlib
import sys
import threading
from pathlib import Path

from backend import Backend, BackendError, Context, PredictRequest

SAM_WORKER_DIR = Path(__file__).resolve().parents[2] / "sam-worker"
CHECKPOINTS = {"tiny": "sam2.1_hiera_tiny.pt", "small": "sam2.1_hiera_small.pt",
               "base_plus": "sam2.1_hiera_base_plus.pt", "large": "sam2.1_hiera_large.pt"}


def _sam_modules():
    if str(SAM_WORKER_DIR) not in sys.path:
        sys.path.append(str(SAM_WORKER_DIR))
    try:
        import mask_cleaner, mask_deduplicator, model_registry, sam_service  # noqa: E401
    except ImportError as exc:
        raise BackendError("SAM2_NOT_INSTALLED", "dependances du worker SAM 2 absentes (numpy/Pillow)", str(exc)) from exc
    return mask_cleaner, mask_deduplicator, model_registry, sam_service


class Sam2Backend(Backend):
    backend_id = "sam2"

    def __init__(self, models_dir: str, model: str = "small", device: str = "auto") -> None:
        if model not in CHECKPOINTS:
            raise ValueError(f"taille de modele inconnue : {model}")
        self.models_dir, self.model, self.device = Path(models_dir), model, device
        self._service = None
        self._hash: str | None = None
        self._hash_lock = threading.Lock()

    def _checkpoint(self) -> Path:
        return self.models_dir / CHECKPOINTS[self.model]

    def _model_hash(self) -> str | None:
        ckpt = self._checkpoint()
        if not ckpt.is_file():
            return None
        with self._hash_lock:  # calcule une seule fois : le fichier peut peser jusqu'a ~900 Mo
            if self._hash is None:
                h = hashlib.sha256()
                with open(ckpt, "rb") as f:
                    for block in iter(lambda: f.read(1 << 20), b""):
                        h.update(block)
                self._hash = h.hexdigest()
            return self._hash

    def capabilities(self) -> dict:
        return {"backend_id": self.backend_id, "backend_revision": "sam2.1", "display_name": f"SAM 2.1 ({self.model})",
                "model_id": self.model, "model_hash": self._model_hash(), "prompts": ["automatic"],
                "devices": ["cpu", "cuda"], "license_code": "Apache-2.0", "license_weights": "Apache-2.0",
                "experimental": False}

    def health(self) -> dict:
        if not self._checkpoint().is_file():
            return {"state": "unavailable", "detail": f"checkpoint absent : {CHECKPOINTS[self.model]}"}
        try:
            _sam_modules()
            import sam2  # noqa: F401
        except BackendError as exc:
            return {"state": "unavailable", "detail": exc.message}
        except ImportError:
            return {"state": "unavailable", "detail": "paquet sam2 non installe"}
        return {"state": "ready", "detail": ""}

    def _ensure_loaded(self, ctx: Context):
        _cleaner, _dedupe, registry, svc = _sam_modules()
        if self._service is None:
            self._service = svc.SamService(self.models_dir, self.device)
        if self._service.loaded_model_id != self.model:
            ctx.progress("loading_model")
            try:
                self._service.load_model(self.model)
            except svc.ModelError as exc:
                raise BackendError(exc.code, exc.message, exc.details) from exc
            except registry.UnknownModelError as exc:
                raise BackendError("MODEL_NOT_INSTALLED", f"modele inconnu : {self.model}") from exc
        return self._service

    def predict(self, request: PredictRequest, ctx: Context) -> dict:
        cleaner, dedupe, _registry, svc = _sam_modules()
        import numpy as np
        from PIL import Image

        job, image_path = Path(request.job_dir), Path(request.job_dir) / request.image_file
        service = self._ensure_loaded(ctx)
        ctx.check_cancelled()
        ctx.progress("preparing_image")
        try:
            source_bytes = image_path.read_bytes()
            with Image.open(image_path) as src:
                source_w, source_h = src.size
        except (OSError, FileNotFoundError) as exc:
            raise BackendError("IMAGE_LOAD_FAILED", "image illisible", str(exc)) from exc
        max_res = request.options.get("max_resolution")
        array = svc.load_image_array(image_path, max_res)
        proc_h, proc_w = array.shape[:2]
        ctx.check_cancelled()
        ctx.progress("running_inference")
        overrides = {k: request.options.get(k) for k in
                     ("points_per_side", "pred_iou_thresh", "stability_score_thresh", "min_mask_region_area")}
        raw = service.generate_masks(array, request.options.get("profile", svc.DEFAULT_PROFILE), overrides)
        ctx.check_cancelled()
        ctx.progress("cleaning_masks")
        island, hole = int(request.options.get("remove_island_area_threshold", 0) or 0), \
            int(request.options.get("fill_hole_area_threshold", 0) or 0)
        cleaned = []
        for r in raw:
            seg = r["segmentation"].astype("uint8")
            if island > 0:
                seg, _ = cleaner.remove_small_islands(seg, island)
            if hole > 0:
                seg, _ = cleaner.fill_small_holes(seg, hole)
            if int(seg.sum()) == 0:
                continue
            ys, xs = seg.nonzero()
            cleaned.append({"segmentation": seg.astype(bool), "area_pixels": int(seg.sum()),
                            "bbox_xywh": [int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1),
                                          int(ys.max() - ys.min() + 1)],
                            "predicted_iou": float(r.get("predicted_iou", 0.0))})
        kept = [cleaned[i] for i in dedupe.deduplicate(cleaned)]
        ctx.check_cancelled()
        ctx.progress("writing_results")
        (job / "masks").mkdir(parents=True, exist_ok=True)
        scale_x, scale_y = source_w / proc_w, source_h / proc_h
        masks = []
        for i, m in enumerate(kept):
            img = Image.fromarray(m["segmentation"].astype(np.uint8) * 255, mode="L")
            if (proc_w, proc_h) != (source_w, source_h):  # masques normalises a la resolution SOURCE
                img = img.resize((source_w, source_h), Image.NEAREST)
            name = f"masks/mask_{i:04d}.png"
            img.save(job / name)
            x, y, w, h = m["bbox_xywh"]
            masks.append({"id": f"m{i}", "file": name, "area_px": int((np.asarray(img) > 0).sum()),
                          "bbox_xywh": [round(x * scale_x), round(y * scale_y), round(w * scale_x), round(h * scale_y)],
                          "confidence": m["predicted_iou"], "prompt_index": 0})
        return {"image_sha256": hashlib.sha256(source_bytes).hexdigest(),
                "transform": {"source_width": source_w, "source_height": source_h, "width": proc_w,
                              "height": proc_h, "scale": proc_w / source_w, "crop": None},
                "masks": masks, "warnings": []}

    def unload(self) -> None:
        if self._service is not None:
            self._service.unload_model()
