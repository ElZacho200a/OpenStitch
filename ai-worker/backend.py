# SPDX-License-Identifier: Apache-2.0
"""Interface commune des backends de vision (SAM 2.1, fakes, puis HQ-SAM 2, Florence-2...)."""
from __future__ import annotations

import abc
import threading
from dataclasses import dataclass, field
from typing import Callable


class BackendError(Exception):
    """Erreur typee : `code` est un code stable du protocole."""

    def __init__(self, code: str, message: str, details: str = "") -> None:
        super().__init__(message)
        self.code, self.message, self.details = code, message, details


class Cancelled(Exception):
    """Levee par `Context.check_cancelled()` : la tache a ete annulee de facon cooperative."""


@dataclass
class Context:
    request_id: str
    cancel_event: threading.Event
    _progress: Callable[[str, float | None], None]

    def progress(self, stage: str, fraction: float | None = None) -> None:
        self._progress(stage, fraction)

    def check_cancelled(self) -> None:
        if self.cancel_event.is_set():
            raise Cancelled()


@dataclass
class PredictRequest:
    job_dir: str
    image_file: str = "input.png"
    prompts: list[dict] = field(default_factory=lambda: [{"kind": "automatic"}])
    options: dict = field(default_factory=dict)


class Backend(abc.ABC):
    """Contrat d'un backend. Les masques sont ecrits sous `job_dir/masks/` en PNG 8 bits
    (255 = dans le masque) a la resolution de l'image SOURCE, quelle que soit la
    resolution interne du modele (decrite par `transform`)."""

    backend_id: str = ""

    @abc.abstractmethod
    def capabilities(self) -> dict:
        """backend_id, backend_revision, display_name, model_id, model_hash (sha256 ou None),
        prompts [...], devices [...], license_code, license_weights, experimental."""

    @abc.abstractmethod
    def predict(self, request: PredictRequest, ctx: Context) -> dict:
        """Retourne {image_sha256, transform, masks:[{id,file,bbox_xywh,area_px,confidence?,prompt_index?}],
        warnings:[...]}. Doit appeler `ctx.check_cancelled()` aux points d'arret."""

    def refine(self, request: dict, ctx: Context) -> dict:
        raise BackendError("UNSUPPORTED_PROMPT", f"{self.backend_id} ne sait pas affiner un masque")

    def health(self) -> dict:
        return {"state": "ready", "detail": ""}

    def preload(self) -> None:
        """Importe les bibliotheques lourdes (torch, transformers...) dans le THREAD PRINCIPAL, avant
        la boucle de service : constate sous Windows, `import torch` lance depuis le thread d'une
        tache ne rend jamais la main tant que le thread principal attend sur stdin."""
        return None

    def unload(self) -> None:
        return None
