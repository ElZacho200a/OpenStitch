# SPDX-License-Identifier: Apache-2.0
"""Client Python minimal du protocole `openstitch.ai.backend/1` (benchmark uniquement).

Un worker = un processus ; une session = hello -> capabilities -> predict* -> shutdown.
"""
from __future__ import annotations

import json
import queue
import subprocess
import threading
import time

PROTOCOL = "openstitch.ai.backend/1"


class WorkerError(Exception):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code} : {message}")
        self.code, self.message = code, message


class WorkerSession:
    def __init__(self, command: list[str], timeout: float = 900.0) -> None:
        self.timeout = timeout
        self.proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                     text=True, encoding="utf-8", bufsize=1)
        self.q: "queue.Queue[dict | None]" = queue.Queue()
        threading.Thread(target=self._pump, daemon=True).start()
        self._n = 0
        hello = self._next(None)
        if hello.get("type") != "hello" or not str(hello.get("protocol", "")).startswith("openstitch.ai.backend/"):
            raise WorkerError("PROTOCOL_MISMATCH", f"hello inattendu : {hello}")
        if hello["protocol"] != PROTOCOL:
            raise WorkerError("PROTOCOL_MISMATCH", hello["protocol"])
        self.hello = hello

    def _pump(self):
        for line in self.proc.stdout:
            line = line.strip()
            if line:
                try:
                    self.q.put(json.loads(line))
                except ValueError:
                    self.q.put({"type": "error", "id": None, "code": "STDOUT_NOT_PROTOCOL", "message": line[:200]})
        self.q.put(None)

    def _next(self, rid):
        deadline = time.monotonic() + self.timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise WorkerError("BACKEND_TIMEOUT", "delai depasse")
            try:
                msg = self.q.get(timeout=left)
            except queue.Empty:
                raise WorkerError("BACKEND_TIMEOUT", "delai depasse")
            if msg is None:
                raise WorkerError("WORKER_CRASHED", "le worker s'est arrete")
            if rid is None or msg.get("id") in (rid, None):
                if msg.get("type") == "progress":
                    continue
                return msg

    def request(self, kind: str, **fields) -> dict:
        self._n += 1
        rid = f"{kind}-{self._n}"
        self.proc.stdin.write(json.dumps({"id": rid, "type": kind, "protocol": PROTOCOL, **fields}) + "\n")
        self.proc.stdin.flush()
        msg = self._next(rid)
        if msg.get("type") == "error":
            raise WorkerError(msg.get("code", "ERROR"), msg.get("message", ""))
        return msg

    def close(self) -> None:
        try:
            self.proc.stdin.write(json.dumps({"id": "bye", "type": "shutdown"}) + "\n")
            self.proc.stdin.flush()
        except OSError:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        for s in (self.proc.stdin, self.proc.stdout):
            try:
                s.close()
            except OSError:
                pass
