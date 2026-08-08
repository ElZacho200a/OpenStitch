#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Faux worker SAM 2 pour les tests d'intégration C++ de SamWorkerClient
(tests/unit/desktop/test_sam_worker_client.cpp) : STDLIB UNIQUEMENT (pas de
torch/sam2/numpy/PIL), réponses instantanées et déterministes. Ne fait
AUCUNE vraie segmentation -- sert uniquement à exercer le protocole JSON
Lines et le cycle de vie du processus côté client, sans dépendre d'un
environnement SAM 2 réel en CI.

Déclencheurs spéciaux :
- `--device crash-after-ready` : quitte brutalement juste après
  `worker_ready`, pour tester la détection de crash + le redémarrage auto.
- `segment_image` avec `profile: "hang"` : reste en "running_inference"
  jusqu'à un `cancel` (ou 5 s), pour tester l'annulation coopérative.
"""
from __future__ import annotations

import json
import os
import sys
import threading
import time

PROTOCOL_VERSION = 1


def send(payload: dict) -> None:
    sys.stdout.write(json.dumps(payload) + "\n")
    sys.stdout.flush()


def handle_segment_image(req: dict, cancel_event: threading.Event) -> None:
    request_id = req.get("id")
    send({"id": request_id, "type": "progress", "stage": "preparing_image"})
    send({"id": request_id, "type": "progress", "stage": "running_inference"})

    if req.get("profile") == "hang":
        for _ in range(50):
            if cancel_event.is_set():
                send({"id": request_id, "type": "cancelled", "target_id": request_id})
                return
            time.sleep(0.1)

    job_dir = req.get("job_dir", ".")
    os.makedirs(job_dir, exist_ok=True)
    payload = {
        "schema_version": 1,
        "source": req.get("image_file", "input.png"),
        "model": "fake",
        "width": 4,
        "height": 4,
        "masks": [],
    }
    with open(os.path.join(job_dir, "masks.json"), "w", encoding="utf-8") as f:
        json.dump(payload, f)
    send(
        {
            "id": request_id,
            "type": "segment_result",
            "job_dir": job_dir,
            "masks_file": "masks.json",
            "mask_count": 0,
        }
    )


def main() -> None:
    device = None
    if "--device" in sys.argv:
        idx = sys.argv.index("--device")
        if idx + 1 < len(sys.argv):
            device = sys.argv[idx + 1]

    send({"type": "worker_starting", "pid": os.getpid(), "protocol_version": PROTOCOL_VERSION})
    if device == "crash-after-ready":
        send({"type": "worker_ready", "pid": os.getpid(), "protocol_version": PROTOCOL_VERSION})
        sys.stdout.flush()
        os._exit(1)
    send({"type": "worker_ready", "pid": os.getpid(), "protocol_version": PROTOCOL_VERSION})

    cancel_event = threading.Event()
    active_id: dict[str, str | None] = {"value": None}
    lock = threading.Lock()

    for raw_line in sys.stdin:
        line = raw_line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            send({"id": None, "type": "error", "code": "INVALID_WORKER_RESPONSE", "message": "bad json",
                 "details": ""})
            continue

        req_type = req.get("type")
        if req_type == "ping":
            send({"id": req.get("id"), "type": "pong", "protocol_version": PROTOCOL_VERSION})
        elif req_type == "load_model":
            model = req.get("model")
            if model == "crash":
                sys.stdout.flush()
                os._exit(1)
            send({"id": req.get("id"), "type": "model_ready", "model": model, "device": "cpu",
                 "load_seconds": 0.01})
        elif req_type == "segment_image":
            with lock:
                cancel_event.clear()
                active_id["value"] = req.get("id")
            thread = threading.Thread(target=handle_segment_image, args=(req, cancel_event), daemon=True)
            thread.start()
        elif req_type == "cancel":
            target = req.get("target_id")
            with lock:
                if active_id["value"] == target:
                    cancel_event.set()
            send({"id": req.get("id"), "type": "cancel_acknowledged", "target_id": target})
        elif req_type == "shutdown":
            send({"id": req.get("id"), "type": "shutting_down"})
            return
        else:
            send({"id": req.get("id"), "type": "error", "code": "INVALID_WORKER_RESPONSE",
                 "message": f"type inconnu : {req_type}", "details": ""})


if __name__ == "__main__":
    main()
