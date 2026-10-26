# SPDX-License-Identifier: Apache-2.0
"""Boucle de service d'un backend : une tache `predict`/`refine` a la fois, dans un thread,
pendant que le thread principal repond a `capabilities`/`health`/`cancel`/`unload`/`shutdown`."""
from __future__ import annotations

import logging
import os
import threading
import time

import protocol as proto
from backend import Backend, BackendError, Cancelled, Context, PredictRequest

log = logging.getLogger("openstitch_ai_worker")


class Server:
    def __init__(self, backend: Backend, writer: proto.StdoutWriter | None = None,
                 advertise_protocol: str = proto.PROTOCOL) -> None:
        self.backend = backend
        self.writer = writer or proto.StdoutWriter()
        self.advertise_protocol = advertise_protocol
        self._lock = threading.Lock()
        self._active_id: str | None = None
        self._cancel = threading.Event()

    # -- emission -------------------------------------------------------------
    def hello(self) -> None:
        self.writer.send({"type": "hello", "protocol": self.advertise_protocol,
                          "backend_id": self.backend.backend_id, "pid": os.getpid()})

    def _error(self, request_id, code: str, message: str, details: str = "") -> None:
        self.writer.send(proto.error_payload(request_id, code, message, details))

    # -- requetes ---------------------------------------------------------------
    def dispatch(self, req: dict) -> bool:
        """Traite une requete ; retourne False pour demander l'arret."""
        if "__parse_error__" in req:
            self._error(None, proto.E_INVALID_REQUEST, "ligne JSON invalide", req["__parse_error__"])
            return True
        rid, kind = req.get("id"), req.get("type")
        declared = req.get("protocol")
        if declared is not None and proto.protocol_major(declared) != proto.PROTOCOL_MAJOR:
            self._error(rid, proto.E_PROTOCOL_MISMATCH,
                        f"protocole demande {declared!r}, supporte {proto.PROTOCOL!r}")
            return True
        if kind == "capabilities":
            self._guarded(rid, lambda: self.writer.send(
                {"id": rid, "type": "capabilities", "protocol": proto.PROTOCOL,
                 "backend": self.backend.capabilities()}))
        elif kind == "health":
            self._guarded(rid, lambda: self.writer.send({"id": rid, "type": "health", **self.backend.health()}))
        elif kind in ("predict", "refine"):
            self._start_task(rid, kind, req)
        elif kind == "cancel":
            target = req.get("target_id")
            with self._lock:
                if self._active_id is not None and self._active_id == target:
                    self._cancel.set()
        elif kind == "unload":
            self._guarded(rid, lambda: (self.backend.unload(), self.writer.send({"id": rid, "type": "unloaded"})))
        elif kind == "shutdown":
            self.writer.send({"id": rid, "type": "bye"})
            return False
        else:
            self._error(rid, proto.E_INVALID_REQUEST, f"type de requete inconnu : {kind!r}")
        return True

    def _guarded(self, rid, fn) -> None:
        try:
            fn()
        except BackendError as exc:
            self._error(rid, exc.code, exc.message, exc.details)
        except Exception as exc:  # noqa: BLE001 -- une requete ne doit jamais tuer le worker
            log.exception("erreur interne")
            self._error(rid, proto.E_INTERNAL, "erreur interne du worker", str(exc))

    def _start_task(self, rid, kind: str, req: dict) -> None:
        if not isinstance(rid, str) or not rid:
            self._error(None, proto.E_INVALID_REQUEST, "`id` (texte non vide) requis")
            return
        with self._lock:
            if self._active_id is not None:
                self._error(rid, proto.E_BACKEND_BUSY, f"une tache est deja en cours ({self._active_id})")
                return
            self._active_id = rid
            self._cancel.clear()
        threading.Thread(target=self._run_task, args=(rid, kind, req), daemon=True).start()

    def _run_task(self, rid: str, kind: str, req: dict) -> None:
        started = time.monotonic()
        ctx = Context(rid, self._cancel, lambda stage, frac: self.writer.send(
            {"id": rid, "type": "progress", "stage": stage, **({} if frac is None else {"fraction": frac})}))
        try:
            if kind == "predict":
                if not isinstance(req.get("job_dir"), str):
                    raise BackendError(proto.E_INVALID_REQUEST, "`job_dir` requis")
                prompts = req.get("prompts") or [{"kind": "automatic"}]
                supported = set(self.backend.capabilities().get("prompts", []))
                for p in prompts:
                    if not isinstance(p, dict) or p.get("kind") not in supported:
                        raise BackendError(proto.E_UNSUPPORTED_PROMPT,
                                           f"invite non supportee : {p.get('kind') if isinstance(p, dict) else p!r}",
                                           f"supportees : {sorted(supported)}")
                result = self.backend.predict(PredictRequest(
                    req["job_dir"], req.get("image_file", "input.png"), prompts, req.get("options") or {}), ctx)
            else:
                result = self.backend.refine(req, ctx)
            ctx.check_cancelled()
            result = {**result, "elapsed_ms": int((time.monotonic() - started) * 1000)}
            self.writer.send({"id": rid, "type": "result", **result})
        except Cancelled:
            self.writer.send({"id": rid, "type": "cancelled", "target_id": rid})
        except BackendError as exc:
            self._error(rid, exc.code, exc.message, exc.details)
        except Exception as exc:  # noqa: BLE001
            log.exception("echec de la tache")
            code = "CUDA_OUT_OF_MEMORY" if "out of memory" in str(exc).lower() else proto.E_INFERENCE_FAILED
            self._error(rid, code, "l'analyse par le backend a echoue", str(exc))
        finally:
            with self._lock:
                self._active_id = None

    def serve(self, requests) -> None:
        self.hello()
        for req in requests:
            try:
                if not self.dispatch(req):
                    break
            except Exception:  # noqa: BLE001
                log.exception("erreur non geree")
                self._error(req.get("id") if isinstance(req, dict) else None, proto.E_INTERNAL, "erreur interne")
        self.backend.unload()
