# SPDX-License-Identifier: Apache-2.0
"""Cadrage du protocole JSON Lines. stdout ne doit JAMAIS recevoir autre
chose qu'une ligne JSON du protocole (les logs humains vont sur stderr,
configures par le point d'entree) -- c'est ce contrat qui permet au client
C++ de lire stdout ligne par ligne sans jamais avoir a distinguer du bruit
d'une reponse.
"""
from __future__ import annotations

import json
import sys
import threading
from collections.abc import Generator
from typing import TextIO

PROTOCOL_VERSION = 1


class StdoutWriter:
    """Ecriture JSON Lines thread-safe : une requete `segment_image` tourne
    dans un thread dedie pendant que le thread principal continue de lire
    stdin (pour repondre a `ping`/`cancel`/`shutdown`) -- sans verrou, deux
    ecritures concurrentes pourraient entrelacer leurs lignes et corrompre
    le flux JSON Lines cote client.
    """

    def __init__(self, stream: TextIO = sys.stdout) -> None:
        self._stream = stream
        self._lock = threading.Lock()

    def send(self, payload: dict) -> None:
        line = json.dumps(payload, ensure_ascii=False)
        with self._lock:
            self._stream.write(line + "\n")
            self._stream.flush()


def error_payload(request_id: str | None, code: str, message: str, details: str = "") -> dict:
    return {"id": request_id, "type": "error", "code": code, "message": message, "details": details}


def read_requests(stream: TextIO = sys.stdin) -> Generator[dict, None, None]:
    """Lit une requete JSON par ligne non vide. Une ligne illisible produit
    un dict marqueur `__parse_error__` plutot que de lever -- le worker ne
    doit jamais s'arreter a cause d'une ligne malformee sur stdin."""
    for raw_line in stream:
        line = raw_line.strip()
        if not line:
            continue
        try:
            parsed = json.loads(line)
        except json.JSONDecodeError as exc:
            yield {"__parse_error__": str(exc), "__raw__": line}
            continue
        if not isinstance(parsed, dict):
            yield {"__parse_error__": "la requete doit etre un objet JSON", "__raw__": line}
            continue
        yield parsed
