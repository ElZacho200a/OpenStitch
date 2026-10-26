# SPDX-License-Identifier: Apache-2.0
"""Tests du worker generique `openstitch.ai.backend/1` : bibliotheque standard, aucun modele."""
import json
import queue
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKER = ROOT / "ai-worker" / "openstitch_ai_worker.py"


PNG_SIGNATURE = bytes([137, 80, 78, 71, 13, 10, 26, 10])


class pngio:  # PNG minimal (filtre 0 seulement, 8 bits) : suffit pour les masques du faux backend
    @staticmethod
    def encode(w, h, mode, pixels):
        ch = 3 if mode == "RGB" else 1
        raw = b"".join(bytes([0]) + pixels[y * w * ch:(y + 1) * w * ch] for y in range(h))

        def chunk(n, p):
            return struct.pack(">I", len(p)) + n + p + struct.pack(">I", zlib.crc32(n + p))

        return (PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2 if ch == 3 else 0, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

    @staticmethod
    def decode(data):
        w, h, _, ctype = struct.unpack(">IIBB", data[16:26])
        pos, idat = 8, b""
        while pos < len(data):
            n, name = struct.unpack(">I4s", data[pos:pos + 8])
            if name == b"IDAT":
                idat += data[pos + 8:pos + 8 + n]
            pos += 12 + n
        ch = 3 if ctype == 2 else 1
        raw = zlib.decompress(idat)
        stride = w * ch
        return w, h, ch, b"".join(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)] for y in range(h))


class Session:
    """Un worker en sous-processus ; chaque ligne de stdout DOIT etre du JSON (sinon l'echec est enregistre)."""

    def __init__(self, *args: str) -> None:
        self.proc = subprocess.Popen([sys.executable, str(WORKER), *args], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True,
                                     encoding="utf-8", bufsize=1)
        self.q: "queue.Queue[dict | None]" = queue.Queue()
        self.non_json: list[str] = []
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        for line in self.proc.stdout:
            try:
                self.q.put(json.loads(line))
            except ValueError:
                self.non_json.append(line.strip())
        self.q.put(None)

    def send(self, payload) -> None:
        self.proc.stdin.write((payload if isinstance(payload, str) else json.dumps(payload)) + "\n")
        self.proc.stdin.flush()

    def recv(self, timeout: float = 20.0):
        return self.q.get(timeout=timeout)

    def until(self, wanted: str, timeout: float = 20.0) -> dict:
        while True:
            msg = self.recv(timeout)
            if msg is None or msg.get("type") == wanted:
                return msg

    def close(self) -> None:
        try:
            self.send({"id": "bye", "type": "shutdown"})
        except OSError:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        for stream in (self.proc.stdin, self.proc.stdout):
            try:
                stream.close()
            except OSError:
                pass  # worker deja mort (test de crash)


def make_job(tmp: Path, w=32, h=24) -> Path:
    job = tmp / "job"
    job.mkdir()
    (job / "input.png").write_bytes(pngio.encode(w, h, "RGB", bytes([200, 100, 50]) * (w * h)))
    return job


class WorkerTests(unittest.TestCase):
    def open(self, *args: str) -> Session:
        s = Session(*args)
        self.addCleanup(s.close)
        return s

    def test_hello_capabilities_health(self):
        s = self.open("--backend", "fake-grid")
        hello = s.recv()
        self.assertEqual((hello["type"], hello["protocol"], hello["backend_id"]),
                         ("hello", "openstitch.ai.backend/1", "fake-grid"))
        s.send({"id": "c", "type": "capabilities"})
        caps = s.until("capabilities")["backend"]
        self.assertEqual(caps["prompts"], ["automatic", "box"])
        self.assertIn("license_weights", caps)
        s.send({"id": "h", "type": "health"})
        self.assertEqual(s.until("health")["state"], "ready")

    def test_predict_automatic_and_box_write_source_size_masks(self):
        with tempfile.TemporaryDirectory() as tmp:
            job = make_job(Path(tmp))
            s = self.open("--backend", "fake-grid")
            s.until("hello")
            s.send({"id": "p1", "type": "predict", "job_dir": str(job),
                    "prompts": [{"kind": "automatic"}, {"kind": "box", "box_xywh": [2, 3, 10, 8]}]})
            res = s.until("result")
            self.assertEqual(res["id"], "p1")
            self.assertEqual(len(res["masks"]), 5)
            self.assertEqual(res["transform"]["source_width"], 32)
            self.assertEqual(len(res["image_sha256"]), 64)
            self.assertGreaterEqual(res["elapsed_ms"], 0)
            for m in res["masks"]:
                w, h, ch, px = pngio.decode((job / m["file"]).read_bytes())
                self.assertEqual((w, h, ch), (32, 24, 1))
                self.assertEqual(sum(1 for v in px if v), m["area_px"])
            self.assertEqual(res["masks"][4]["prompt_index"], 1)
            self.assertEqual(res["masks"][4]["bbox_xywh"], [2, 3, 10, 8])

    def test_unsupported_prompt_is_a_typed_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.open("--backend", "fake-flaky")
            s.until("hello")
            s.send({"id": "p", "type": "predict", "job_dir": str(make_job(Path(tmp))),
                    "prompts": [{"kind": "box", "box_xywh": [0, 0, 1, 1]}]})
            err = s.until("error")
            self.assertEqual((err["id"], err["code"]), ("p", "UNSUPPORTED_PROMPT"))

    def test_cancellation_is_cooperative_and_worker_stays_alive(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.open("--backend", "fake-grid")
            s.until("hello")
            s.send({"id": "slow", "type": "predict", "job_dir": str(make_job(Path(tmp))),
                    "options": {"delay_ms": 5000}})
            s.until("progress")
            s.send({"id": "x", "type": "cancel", "target_id": "slow"})
            msg = s.until("cancelled")
            self.assertEqual(msg["target_id"], "slow")
            s.send({"id": "h", "type": "health"})
            self.assertEqual(s.until("health")["state"], "ready")

    def test_second_task_while_busy_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.open("--backend", "fake-grid")
            s.until("hello")
            job = str(make_job(Path(tmp)))
            s.send({"id": "a", "type": "predict", "job_dir": job, "options": {"delay_ms": 5000}})
            s.until("progress")
            s.send({"id": "b", "type": "predict", "job_dir": job})
            err = s.until("error")
            self.assertEqual((err["id"], err["code"]), ("b", "BACKEND_BUSY"))
            s.send({"id": "x", "type": "cancel", "target_id": "a"})
            s.until("cancelled")

    def test_backend_failure_and_bad_input_do_not_kill_the_worker(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.open("--backend", "fake-flaky")
            s.until("hello")
            s.send({"id": "f", "type": "predict", "job_dir": str(make_job(Path(tmp))), "options": {"mode": "fail"}})
            self.assertEqual(s.until("error")["code"], "INFERENCE_FAILED")
            s.send("{not json")
            self.assertEqual(s.until("error")["code"], "INVALID_REQUEST")
            s.send({"id": "u", "type": "frobnicate"})
            self.assertEqual(s.until("error")["code"], "INVALID_REQUEST")
            s.send({"id": "h", "type": "health"})
            self.assertEqual(s.until("health")["detail"], "fake-flaky")

    def test_protocol_mismatch_is_refused_explicitly(self):
        s = self.open("--backend", "fake-grid")
        s.until("hello")
        s.send({"id": "v", "protocol": "openstitch.ai.backend/2", "type": "capabilities"})
        self.assertEqual(s.until("error")["code"], "PROTOCOL_MISMATCH")
        s.send({"id": "v2", "protocol": "openstitch.ai.backend/1", "type": "capabilities"})
        self.assertEqual(s.until("capabilities")["id"], "v2")

    def test_incompatible_worker_advertises_its_version(self):
        s = self.open("--backend", "fake-grid", "--advertise-protocol", "openstitch.ai.backend/9")
        self.assertEqual(s.recv()["protocol"], "openstitch.ai.backend/9")

    def test_stdout_carries_protocol_lines_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.open("--backend", "fake-grid")
            s.until("hello")
            s.send({"id": "p", "type": "predict", "job_dir": str(make_job(Path(tmp)))})
            s.until("result")
            s.send({"id": "h", "type": "health"})
            s.until("health")
            self.assertEqual(s.non_json, [])

    def test_crash_ends_the_stream(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.open("--backend", "fake-flaky")
            s.until("hello")
            s.send({"id": "c", "type": "predict", "job_dir": str(make_job(Path(tmp))), "options": {"mode": "crash"}})
            while s.recv() is not None:
                pass
            self.assertEqual(s.proc.wait(timeout=10), 3)

    def test_sam2_backend_reports_unavailable_without_weights(self):
        with tempfile.TemporaryDirectory() as tmp:
            s = self.open("--backend", "sam2", "--models-dir", tmp)
            self.assertEqual(s.recv()["backend_id"], "sam2")
            s.send({"id": "c", "type": "capabilities"})
            caps = s.until("capabilities")["backend"]
            self.assertIsNone(caps["model_hash"])
            self.assertEqual(caps["prompts"], ["automatic"])
            self.assertEqual(caps["license_code"], "Apache-2.0")
            s.send({"id": "h", "type": "health"})
            health = s.until("health")
            self.assertEqual(health["state"], "unavailable")
            self.assertIn("checkpoint absent", health["detail"])
            s.send({"id": "p", "type": "predict", "job_dir": str(make_job(Path(tmp)))})
            self.assertIn(s.until("error")["code"], {"CHECKPOINT_NOT_FOUND", "SAM2_NOT_INSTALLED", "MODEL_NOT_INSTALLED"})


if __name__ == "__main__":
    unittest.main()
