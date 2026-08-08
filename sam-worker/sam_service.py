# SPDX-License-Identifier: Apache-2.0
"""Chargement/dechargement du modele SAM 2 et generation des masques bruts.
Isole du reste du worker pour rester testable sans torch/sam2 installes
(les tests mockent `SamService._build_generator`)."""
from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np

import model_registry

# Trois profils centralises -- LA seule source de verite pour ces
# parametres, cote C++ comme cote worker : l'UI n'envoie qu'un nom de
# profil ("main_shapes" / "balanced" / "detail"), jamais des valeurs
# numeriques choisies independamment.
PROFILES: dict[str, dict[str, Any]] = {
    "main_shapes": {
        "points_per_side": 16,
        "pred_iou_thresh": 0.90,
        "stability_score_thresh": 0.95,
        "min_mask_region_area": 400,
    },
    "balanced": {
        "points_per_side": 32,
        "pred_iou_thresh": 0.86,
        "stability_score_thresh": 0.92,
        "min_mask_region_area": 100,
    },
    "detail": {
        "points_per_side": 64,
        "pred_iou_thresh": 0.80,
        "stability_score_thresh": 0.88,
        "min_mask_region_area": 25,
    },
}

DEFAULT_PROFILE = "balanced"


class ModelError(Exception):
    """Erreur structuree portant un code du protocole (cf. error.hpp cote C++)."""

    def __init__(self, code: str, message: str, details: str = "") -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.details = details


def load_image_array(path: Path, max_resolution: int | None = None) -> np.ndarray:
    """Charge une image en tableau uint8 HxWx3 RGB, contigu et modifiable
    (Pillow retourne parfois un tableau en lecture seule / non contigu :
    `.copy()` explicite pour eviter les avertissements PyTorch en aval)."""
    try:
        from PIL import Image
    except ImportError as exc:
        raise ModelError("SAM2_NOT_INSTALLED", "Pillow n'est pas installe", str(exc)) from exc

    try:
        with Image.open(path) as img:
            img = img.convert("RGB")
            if max_resolution:
                width, height = img.size
                longest = max(width, height)
                if longest > max_resolution:
                    scale = max_resolution / float(longest)
                    new_size = (max(1, round(width * scale)), max(1, round(height * scale)))
                    img = img.resize(new_size, Image.LANCZOS)
            array = np.asarray(img)
    except FileNotFoundError as exc:
        raise ModelError("IMAGE_LOAD_FAILED", f"Image introuvable : {path}", str(exc)) from exc
    except Exception as exc:  # noqa: BLE001 -- toute erreur de decodage devient IMAGE_LOAD_FAILED
        raise ModelError("IMAGE_LOAD_FAILED", f"Impossible de charger l'image : {path}", str(exc)) from exc

    if not array.flags["WRITEABLE"] or not array.flags["C_CONTIGUOUS"]:
        array = array.copy()
    return array


class SamService:
    def __init__(self, models_dir: str | Path, device_preference: str = "auto") -> None:
        self._models_dir = Path(models_dir)
        self._device_preference = device_preference
        self._loaded_model_id: str | None = None
        self._sam_model: Any = None
        self._device: str | None = None

    @property
    def loaded_model_id(self) -> str | None:
        return self._loaded_model_id

    def _resolve_device(self) -> str:
        if self._device_preference in ("cpu", "cuda"):
            return self._device_preference
        try:
            import torch

            return "cuda" if torch.cuda.is_available() else "cpu"
        except ImportError:
            return "cpu"

    def load_model(self, worker_id: str) -> dict[str, str]:
        descriptor = model_registry.resolve(worker_id)  # leve UnknownModelError si inconnu
        checkpoint = self._models_dir / descriptor.checkpoint_file
        if not checkpoint.is_file():
            raise ModelError(
                "CHECKPOINT_NOT_FOUND", f"Checkpoint introuvable pour {descriptor.display_name}",
                str(checkpoint),
            )

        self.unload_model()
        device = self._resolve_device()
        sam_model = self._build_sam2(descriptor, checkpoint, device)
        self._sam_model = sam_model
        self._loaded_model_id = worker_id
        self._device = device
        return {"model": worker_id, "device": device}

    def _build_sam2(self, descriptor: model_registry.ModelDescriptor, checkpoint: Path, device: str) -> Any:
        try:
            from sam2.build_sam import build_sam2
        except ImportError as exc:
            raise ModelError("SAM2_NOT_INSTALLED", "Le paquet sam2 n'est pas installe", str(exc)) from exc

        try:
            return build_sam2(descriptor.config_name, str(checkpoint), device=device)
        except FileNotFoundError as exc:
            raise ModelError(
                "CONFIG_NOT_FOUND", f"Configuration introuvable : {descriptor.config_name}", str(exc)
            ) from exc
        except RuntimeError as exc:
            text = str(exc).lower()
            if "out of memory" in text:
                raise ModelError("CUDA_OUT_OF_MEMORY", "Memoire GPU insuffisante", str(exc)) from exc
            if "size mismatch" in text or "missing key" in text or "unexpected key" in text:
                raise ModelError(
                    "MODEL_CHECKPOINT_MISMATCH",
                    "Le checkpoint ne correspond pas a la configuration du modele",
                    str(exc),
                ) from exc
            raise

    def unload_model(self) -> None:
        """Sequence explicite de dechargement (cf. exigence) : abandonner les
        references AVANT de forcer le ramasse-miettes, sinon `gc.collect()`
        ne peut pas liberer la memoire GPU du modele encore reference."""
        if self._sam_model is None:
            return
        self._sam_model = None
        self._loaded_model_id = None
        self._device = None
        import gc

        gc.collect()
        try:
            import torch

            if torch.cuda.is_available():
                torch.cuda.empty_cache()
        except ImportError:
            pass

    def generate_masks(
        self, image: np.ndarray, profile: str = DEFAULT_PROFILE, overrides: dict[str, Any] | None = None
    ) -> list[dict[str, Any]]:
        if self._sam_model is None:
            raise ModelError("MODEL_NOT_INSTALLED", "Aucun modele charge")
        generator = self._build_generator(profile, overrides)
        try:
            return generator.generate(image)
        except RuntimeError as exc:
            if "out of memory" in str(exc).lower():
                raise ModelError("CUDA_OUT_OF_MEMORY", "Memoire GPU insuffisante pendant l'analyse", str(exc)) from exc
            raise ModelError("INFERENCE_FAILED", "L'analyse par le modele IA a echoue", str(exc)) from exc

    def _build_generator(self, profile: str, overrides: dict[str, Any] | None) -> Any:
        try:
            from sam2.automatic_mask_generator import SAM2AutomaticMaskGenerator
        except ImportError as exc:
            raise ModelError("SAM2_NOT_INSTALLED", "Le paquet sam2 n'est pas installe", str(exc)) from exc

        params = dict(PROFILES.get(profile, PROFILES[DEFAULT_PROFILE]))
        if overrides:
            params.update({k: v for k, v in overrides.items() if v is not None})
        return SAM2AutomaticMaskGenerator(self._sam_model, **params)
