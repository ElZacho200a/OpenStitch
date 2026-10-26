# SPDX-License-Identifier: Apache-2.0
"""Protocole `openstitch.ai.backend/1` : JSON Lines sur stdin/stdout, independant du modele.

stdout ne recoit JAMAIS autre chose qu'une ligne JSON du protocole ; les logs vont sur stderr.
Un backend ne mute jamais un projet : il produit des masques normalises (PNG binaires
a la resolution de l'image SOURCE) dans le dossier de tache.
"""
from __future__ import annotations

import json
import sys
import threading
from collections.abc import Generator
from typing import TextIO

PROTOCOL = "openstitch.ai.backend/1"
PROTOCOL_MAJOR = 1

# Codes d'erreur stables (partages avec libs/ai_segmentation/src/error.cpp).
E_PROTOCOL_MISMATCH = "PROTOCOL_MISMATCH"
E_INVALID_REQUEST = "INVALID_REQUEST"
E_UNSUPPORTED_PROMPT = "UNSUPPORTED_PROMPT"
E_BACKEND_BUSY = "BACKEND_BUSY"
E_INFERENCE_FAILED = "INFERENCE_FAILED"
E_IMAGE_LOAD_FAILED = "IMAGE_LOAD_FAILED"
E_INTERNAL = "INVALID_WORKER_RESPONSE"


def protocol_major(value: object) -> int | None:
    """`openstitch.ai.backend/N` -> N ; None si le texte n'est pas de ce protocole."""
    if not isinstance(value, str) or not value.startswith("openstitch.ai.backend/"):
        return None
    tail = value.rsplit("/", 1)[1]
    return int(tail) if tail.isdigit() else None


class StdoutWriter:
    """Ecriture thread-safe : un predict tourne dans un thread pendant que le principal
    reste a l'ecoute de `cancel`/`health`."""

    def __init__(self, stream: TextIO | None = None) -> None:
        self._stream = stream or sys.stdout
        self._lock = threading.Lock()

    def send(self, payload: dict) -> None:
        line = json.dumps(payload, ensure_ascii=True, separators=(",", ":"))
        with self._lock:
            self._stream.write(line + "\n")
            self._stream.flush()


def error_payload(request_id: str | None, code: str, message: str, details: str = "") -> dict:
    return {"id": request_id, "type": "error", "code": code, "message": message, "details": details}


def read_requests(stream: TextIO | None = None) -> Generator[dict, None, None]:
    """Une requete JSON (objet) par ligne non vide ; une ligne illisible donne un marqueur
    `__parse_error__` au lieu de lever : le worker ne s'arrete jamais sur une ligne malformee."""
    for raw in stream or sys.stdin:
        line = raw.strip()
        if not line:
            continue
        try:
            parsed = json.loads(line)
        except json.JSONDecodeError as exc:
            yield {"__parse_error__": str(exc)}
            continue
        yield parsed if isinstance(parsed, dict) else {"__parse_error__": "la requete doit etre un objet JSON"}
