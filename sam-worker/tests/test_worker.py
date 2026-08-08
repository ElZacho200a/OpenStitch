# SPDX-License-Identifier: Apache-2.0
import numpy as np
import pytest

import openstitch_sam_worker as worker_module


class RecordingWorker(worker_module.Worker):
    """Worker de test : capture les messages envoyes au lieu d'ecrire sur
    stdout, pour pouvoir les inspecter sans passer par un vrai processus."""

    def __init__(self):
        super().__init__(models_dir=".", device_preference="cpu")
        self.sent = []

    def send(self, payload):
        self.sent.append(payload)


def test_ping_replies_with_pong():
    w = RecordingWorker()
    w.dispatch({"id": "1", "type": "ping"})
    assert w.sent == [{"id": "1", "type": "pong", "protocol_version": worker_module.PROTOCOL_VERSION}]


def test_unknown_request_type_is_reported_as_invalid():
    w = RecordingWorker()
    w.dispatch({"id": "1", "type": "not_a_type"})
    assert w.sent[0]["type"] == "error"
    assert w.sent[0]["code"] == "INVALID_WORKER_RESPONSE"


def test_malformed_json_line_is_reported_without_crashing():
    w = RecordingWorker()
    w.dispatch({"__parse_error__": "boom", "__raw__": "{not json"})
    assert w.sent[0]["type"] == "error"
    assert w.sent[0]["code"] == "INVALID_WORKER_RESPONSE"


def test_shutdown_sends_ack_then_raises_system_exit():
    w = RecordingWorker()
    with pytest.raises(SystemExit):
        w.dispatch({"id": "9", "type": "shutdown"})
    assert w.sent[-1]["type"] == "shutting_down"


def test_cancel_without_matching_active_task_still_acknowledges():
    w = RecordingWorker()
    w.dispatch({"id": "1", "type": "cancel", "target_id": "no-such-task"})
    assert w.sent[0] == {"id": "1", "type": "cancel_acknowledged", "target_id": "no-such-task"}


def test_run_segmentation_reports_image_load_failure(tmp_path):
    w = RecordingWorker()
    w._run_segmentation({"id": "1", "job_dir": str(tmp_path), "image_file": "missing.png"})
    assert w.sent[-1]["type"] == "error"
    assert w.sent[-1]["code"] == "IMAGE_LOAD_FAILED"


def test_run_segmentation_honors_cancellation_before_inference(tmp_path):
    from PIL import Image

    Image.fromarray(np.zeros((10, 10, 3), dtype=np.uint8), mode="RGB").save(tmp_path / "input.png")
    w = RecordingWorker()
    w._cancel_event.set()
    w._run_segmentation({"id": "1", "job_dir": str(tmp_path), "image_file": "input.png"})
    assert w.sent[-1] == {"id": "1", "type": "cancelled", "target_id": "1"}
    assert not (tmp_path / "masks.json").exists()


def test_run_segmentation_full_success_path_writes_job_results(tmp_path):
    from PIL import Image

    Image.fromarray(np.zeros((20, 20, 3), dtype=np.uint8) + 128, mode="RGB").save(tmp_path / "input.png")

    w = RecordingWorker()
    fake_mask = np.zeros((20, 20), dtype=bool)
    fake_mask[2:10, 2:10] = True

    class FakeService:
        loaded_model_id = "tiny"

        def generate_masks(self, image, profile, overrides):
            return [{"segmentation": fake_mask, "predicted_iou": 0.9, "stability_score": 0.9}]

    w._service = FakeService()
    w._run_segmentation({"id": "1", "job_dir": str(tmp_path), "image_file": "input.png", "profile": "balanced"})

    results = [m for m in w.sent if m["type"] == "segment_result"]
    assert len(results) == 1
    assert results[0]["mask_count"] == 1
    assert (tmp_path / "masks.json").exists()
    assert (tmp_path / "masks" / "mask_0000.png").exists()


def test_run_segmentation_reports_error_from_fake_service(tmp_path):
    from PIL import Image

    Image.fromarray(np.zeros((10, 10, 3), dtype=np.uint8), mode="RGB").save(tmp_path / "input.png")

    w = RecordingWorker()

    class FailingService:
        loaded_model_id = "tiny"

        def generate_masks(self, image, profile, overrides):
            raise worker_module.ModelError("INFERENCE_FAILED", "boom")

    w._service = FailingService()
    w._run_segmentation({"id": "1", "job_dir": str(tmp_path), "image_file": "input.png"})
    assert w.sent[-1]["type"] == "error"
    assert w.sent[-1]["code"] == "INFERENCE_FAILED"
