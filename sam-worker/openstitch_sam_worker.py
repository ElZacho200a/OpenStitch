#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Point d'entree du worker de segmentation SAM 2 pour OpenStitch Studio.

Processus long-vivant, distinct du desktop C++, qui communique
EXCLUSIVEMENT en JSON Lines sur stdin/stdout : stdout ne recoit jamais autre
chose qu'une ligne JSON du protocole, les logs humains vont sur stderr.
Charge au plus un modele a la fois ; `segment_image` tourne dans un thread
dedie pour que le worker reste capable de repondre a `ping`/`cancel`/
`shutdown` pendant l'inference.
"""
from __future__ import annotations

import argparse
import logging
import os
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import model_registry  # noqa: E402
from mask_cleaner import count_components, fill_small_holes, remove_small_islands  # noqa: E402
from mask_deduplicator import deduplicate  # noqa: E402
from protocol import PROTOCOL_VERSION, StdoutWriter, error_payload, read_requests  # noqa: E402
from result_writer import write_job_results  # noqa: E402
from sam_service import DEFAULT_PROFILE, ModelError, SamService, load_image_array  # noqa: E402

log = logging.getLogger("openstitch_sam_worker")


class Worker:
    def __init__(self, models_dir: str, device_preference: str) -> None:
        self._writer = StdoutWriter()
        self._service = SamService(models_dir, device_preference)
        self._cancel_event = threading.Event()
        self._active_task_id: str | None = None
        self._task_lock = threading.Lock()

    def send(self, payload: dict) -> None:
        self._writer.send(payload)

    def progress(self, request_id: str | None, stage: str, **extra) -> None:
        self.send({"id": request_id, "type": "progress", "stage": stage, **extra})

    # -- gestionnaires de requetes ------------------------------------------------

    def handle_ping(self, req: dict) -> None:
        self.send({"id": req.get("id"), "type": "pong", "protocol_version": PROTOCOL_VERSION})

    def handle_load_model(self, req: dict) -> None:
        request_id = req.get("id")
        worker_id = req.get("model")
        self.progress(request_id, "loading_model", model=worker_id)
        started = time.monotonic()
        try:
            info = self._service.load_model(worker_id)
        except model_registry.UnknownModelError:
            self.send(error_payload(request_id, "MODEL_NOT_INSTALLED", f"Modele inconnu : {worker_id}"))
            return
        except ModelError as exc:
            self.send(error_payload(request_id, exc.code, exc.message, exc.details))
            return
        except Exception as exc:  # noqa: BLE001 -- une requete ne doit jamais tuer le worker
            log.exception("echec du chargement du modele")
            self.send(error_payload(request_id, "WORKER_START_FAILED", "Echec du chargement du modele", str(exc)))
            return
        elapsed = time.monotonic() - started
        self.send(
            {
                "id": request_id,
                "type": "model_ready",
                "model": info["model"],
                "device": info["device"],
                "load_seconds": round(elapsed, 3),
            }
        )

    def handle_segment_image(self, req: dict) -> None:
        request_id = req.get("id")
        with self._task_lock:
            self._cancel_event.clear()
            self._active_task_id = request_id
        thread = threading.Thread(target=self._run_segmentation, args=(req,), daemon=True)
        thread.start()

    def _run_segmentation(self, req: dict) -> None:
        request_id = req.get("id")
        job_dir = Path(req["job_dir"])
        image_file = req.get("image_file", "input.png")
        image_path = job_dir / image_file
        try:
            self.progress(request_id, "preparing_image")
            image = load_image_array(image_path, req.get("max_resolution"))
            if self._check_cancelled(request_id):
                return

            self.progress(request_id, "running_inference")
            overrides = {
                "points_per_side": req.get("points_per_side"),
                "pred_iou_thresh": req.get("pred_iou_thresh"),
                "stability_score_thresh": req.get("stability_score_thresh"),
                "min_mask_region_area": req.get("min_mask_region_area"),
            }
            raw_masks = self._service.generate_masks(image, req.get("profile", DEFAULT_PROFILE), overrides)
            if self._check_cancelled(request_id):
                return

            self.progress(request_id, "cleaning_masks")
            cleaned = self._clean_masks(raw_masks, req)
            final_masks = [cleaned[i] for i in deduplicate(cleaned)]
            if self._check_cancelled(request_id):
                return

            self.progress(request_id, "writing_results")
            height, width = image.shape[:2]
            masks_json = write_job_results(
                job_dir, width, height, image_file, self._service.loaded_model_id or "", final_masks
            )

            self.send(
                {
                    "id": request_id,
                    "type": "segment_result",
                    "job_dir": str(job_dir),
                    "masks_file": masks_json.name,
                    "mask_count": len(final_masks),
                }
            )
        except ModelError as exc:
            self.send(error_payload(request_id, exc.code, exc.message, exc.details))
        except FileNotFoundError as exc:
            self.send(error_payload(request_id, "IMAGE_LOAD_FAILED", "Image introuvable", str(exc)))
        except Exception as exc:  # noqa: BLE001
            log.exception("echec de la segmentation")
            code = "CUDA_OUT_OF_MEMORY" if "out of memory" in str(exc).lower() else "INFERENCE_FAILED"
            self.send(error_payload(request_id, code, "L'analyse par le modele IA a echoue", str(exc)))
        finally:
            with self._task_lock:
                if self._active_task_id == request_id:
                    self._active_task_id = None

    def _clean_masks(self, raw_masks: list[dict], req: dict) -> list[dict]:
        island_threshold = int(req.get("remove_island_area_threshold", 0) or 0)
        hole_threshold = int(req.get("fill_hole_area_threshold", 0) or 0)
        cleaned: list[dict] = []
        for raw in raw_masks:
            seg = raw["segmentation"].astype("uint8")
            before = count_components(seg)
            removed = filled = 0
            if island_threshold > 0:
                seg, removed = remove_small_islands(seg, island_threshold)
            if hole_threshold > 0:
                seg, filled = fill_small_holes(seg, hole_threshold)
            area = int(seg.sum())
            if area == 0:
                continue
            ys, xs = seg.nonzero()
            bbox = [int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1)]
            cleaned.append(
                {
                    "segmentation": seg.astype(bool),
                    "area_pixels": area,
                    "bbox_xywh": bbox,
                    "predicted_iou": float(raw.get("predicted_iou", 0.0)),
                    "stability_score": float(raw.get("stability_score", 0.0)),
                    "component_count_before": before,
                    "component_count_after_cleanup": count_components(seg),
                    "removed_island_count": removed,
                    "filled_hole_count": filled,
                }
            )
        return cleaned

    def _check_cancelled(self, request_id: str | None) -> bool:
        if not self._cancel_event.is_set():
            return False
        self.send({"id": request_id, "type": "cancelled", "target_id": request_id})
        return True

    def handle_cancel(self, req: dict) -> None:
        target_id = req.get("target_id")
        with self._task_lock:
            if self._active_task_id is not None and self._active_task_id == target_id:
                self._cancel_event.set()
        self.send({"id": req.get("id"), "type": "cancel_acknowledged", "target_id": target_id})

    def handle_shutdown(self, req: dict) -> None:
        self.send({"id": req.get("id"), "type": "shutting_down"})
        raise SystemExit(0)

    def dispatch(self, req: dict) -> None:
        if "__parse_error__" in req:
            self.send(
                error_payload(None, "INVALID_WORKER_RESPONSE", "Requete JSON invalide recue", req["__parse_error__"])
            )
            return
        handlers = {
            "ping": self.handle_ping,
            "load_model": self.handle_load_model,
            "segment_image": self.handle_segment_image,
            "cancel": self.handle_cancel,
            "shutdown": self.handle_shutdown,
        }
        handler = handlers.get(req.get("type"))
        if handler is None:
            self.send(
                error_payload(
                    req.get("id"), "INVALID_WORKER_RESPONSE", f"Type de requete inconnu : {req.get('type')}"
                )
            )
            return
        handler(req)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description="Worker de segmentation SAM 2 pour OpenStitch Studio")
    parser.add_argument("--models-dir", required=True)
    parser.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda"])
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=os.environ.get("OPENSTITCH_SAM_LOG_LEVEL", "INFO"),
        stream=sys.stderr,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    worker = Worker(args.models_dir, args.device)
    worker.send({"type": "worker_starting", "pid": os.getpid(), "protocol_version": PROTOCOL_VERSION})
    worker.send({"type": "worker_ready", "pid": os.getpid(), "protocol_version": PROTOCOL_VERSION})

    try:
        for request in read_requests(sys.stdin):
            try:
                worker.dispatch(request)
            except SystemExit:
                raise
            except Exception:  # noqa: BLE001 -- le worker ne doit jamais planter sur une requete
                log.exception("erreur non geree pendant le traitement d'une requete")
                worker.send(
                    error_payload(request.get("id"), "INVALID_WORKER_RESPONSE", "Erreur interne du worker")
                )
    except (SystemExit, KeyboardInterrupt):
        pass


if __name__ == "__main__":
    main()
